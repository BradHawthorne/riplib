#!/usr/bin/env python3
"""Execute bounded icon/clipboard rectangle and ROP paths in the pinned DLL.

Requires Unicorn. File loading, bitmap metadata, palette preparation, port
allocation, locking and invalidation are stubs. SetRect/OffsetRect/IsRectEmpty
are modeled. Driver dimension scaling, rectangle trimming, translation and
ROP selection execute unchanged. GDI calls are recorded, NOT rasterized.
No assertion about Windows pixels, palette inversion or resampling follows.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct

MD5 = "bade8b1f4e467ac7ad4edb2639738d4c"
IB = 0x10000000
INST, RES, PALETTE, PORTS, TABLE = 0x110000, 0x111000, 0x112000, 0x114000, 0x115000
RECT, DIB, STOP, STACK = 0x120000, 0x121000, 0x100000, 0x2FF000
CASES = [
    ("native", 10, 20, 2, 7, 0, [0, 0, 640, 400]),
    ("stretched", 10, 20, 2, 7, 1, [0, 0, 640, 400]),
    ("single_pixel", 0, 0, 1, 1, 0, [0, 0, 640, 400]),
    ("right_bottom_clip", 638, 398, 4, 7, 0, [0, 0, 640, 400]),
    ("stretched_clip", 638, 398, 4, 7, 1, [0, 0, 640, 400]),
    ("outside", 650, 410, 2, 7, 0, [0, 0, 640, 400]),
]


class Oracle:
    def __init__(self, dll, width, height, clip):
        from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
        from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EIP, UC_X86_REG_EAX
        self.esp, self.eip, self.eax = UC_X86_REG_ESP, UC_X86_REG_EIP, UC_X86_REG_EAX
        spec = importlib.util.spec_from_file_location("pe", Path(__file__).with_name("dll-disasm.py"))
        pe = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(pe)
        data, sections = pe.load(dll)
        if hashlib.md5(data).hexdigest() != MD5:
            raise ValueError("not the audited driver image")
        self.vm = Uc(UC_ARCH_X86, UC_MODE_32)
        self.vm.mem_map(IB, 0x100000)
        self.vm.mem_map(STOP, 0x200000)
        for s in sections:
            self.vm.mem_write(IB + s['rva'], data[s['raw']:s['raw'] + s['rs']])
        self.stubs = {}
        self.events = []
        self.allowed = [(0xCD5A, 0xCD9A), (0x4A410, 0x4A5C5), (0x49340, 0x493B7),
                        (0x31084, 0x310B8), (0x2866, 0x2A00), (0x13456, 0x13834),
                        (0x253C6, 0x253F5), (0x34269, 0x342AD), (0xE72A, 0xE749)]
        self.write(INST + 0xE, [RES])
        self.write(INST + 0x1A, [PALETTE])
        self.write(INST + 0x22, [PORTS])
        self.write(RES + 0x24, [640, 350, 640, 400])
        self.write(PORTS, [TABLE])
        self.vm.mem_write(PORTS + 8, struct.pack('<HH', 0, 0xFFFF))
        self.vm.mem_write(TABLE, b'\x01')
        self.write(TABLE + 0x3A, clip)
        self.write(TABLE + 0x6C, [101])
        for rva in (0x3988F, 0x398EE, 0x13C69):
            self.stub(rva, lambda args: 1)
        self.stub(0x38E47, self.error)
        self.stub(0x4EDC5, lambda args: DIB)
        self.stub(0x4E38E, lambda args: width)
        self.stub(0x4E348, lambda args: height)
        self.stub(0x48A20, lambda args: 1)
        self.stub(0x4EA60, lambda args: 1)
        self.stub(0x3326F, self.allocate)
        self.stub(0x4E42B, self.display)
        self.import_stub(0x96484, 2, lambda a: 8 if a[1] == 12 else 1)
        self.import_stub(0x96618, 5, self.set_rect)
        self.import_stub(0x96610, 3, self.offset_rect)
        self.import_stub(0x96690, 1, self.empty_rect)
        self.import_stub(0x96488, 11, lambda a: self.blit('StretchBlt', a[:11]))
        self.import_stub(0x963E4, 9, lambda a: self.blit('BitBlt', a[:9]))
        self.vm.hook_add(UC_HOOK_CODE, self.hook)

    def write(self, address, values):
        self.vm.mem_write(address, struct.pack('<' + 'I' * len(values),
                                             *(v & 0xFFFFFFFF for v in values)))

    def read(self, address, count):
        return list(struct.unpack('<' + 'i' * count, self.vm.mem_read(address, count * 4)))

    def stub(self, rva, fn):
        self.stubs[IB + rva] = (fn, 0)

    def import_stub(self, iat, argc, fn):
        address = 0x130000 + len(self.stubs) * 16
        self.write(IB + iat, [address])
        self.stubs[address] = (fn, argc * 4)

    def hook(self, vm, address, size, _):
        if address in self.stubs:
            fn, pop_bytes = self.stubs[address]
            sp = vm.reg_read(self.esp)
            result = fn(self.read(sp + 4, 16))
            vm.reg_write(self.eax, (result or 0) & 0xFFFFFFFF)
            vm.reg_write(self.eip, self.read(sp, 1)[0])
            vm.reg_write(self.esp, sp + 4 + pop_bytes)
        elif not any(lo <= address - IB < hi for lo, hi in self.allowed):
            raise RuntimeError(f"unexpected execution at RVA {address - IB:#x}")

    def run(self, rva, args):
        self.write(STACK, [STOP, *args])
        self.vm.reg_write(self.esp, STACK)
        self.vm.emu_start(IB + rva, STOP, count=100000)
        if self.vm.reg_read(self.eip) != STOP:
            raise RuntimeError("driver did not reach the bounded return")

    def error(self, args):
        self.events.append({'error_code': args[1]})
        return 0

    def set_rect(self, a):
        self.write(a[0], a[1:5])
        return 1

    def offset_rect(self, a):
        r = self.read(a[0], 4)
        self.write(a[0], [r[0]+a[1], r[1]+a[2], r[2]+a[1], r[3]+a[2]])
        return 1

    def empty_rect(self, a):
        l, t, r, b = self.read(a[0], 4)
        return int(l >= r or t >= b)

    def allocate(self, a):
        slot, width, height = a[1] & 0xFFFF, a[4] & 0xFFFF, a[5] & 0xFFFF
        p = TABLE + slot * 0x78
        self.vm.mem_write(p, b'\x02')
        for offset in (0x1A, 0x2A, 0x3A):
            self.write(p + offset, [0, 0, width, height])
        self.write(p + 0x6C, [102])
        self.events.append({'allocation': [width, height]})
        return 1

    def display(self, a):
        # This legacy ABI passes SHORT coordinates/dimensions in DWORD slots;
        # the high halves of EBX/EBP are not meaningful to the callee.
        xywh = [struct.unpack('<h', struct.pack('<H', v & 0xFFFF))[0] for v in a[1:5]]
        self.events.append({'display': xywh, 'rop': a[7], 'rectangle': self.read(RECT, 4)})
        # End the display stage at its GDI helper boundary, not its epilogue.
        self.vm.emu_stop()
        self.write(self.vm.reg_read(self.esp), [STOP])
        return 1

    def blit(self, name, a):
        self.events.append({name: a})
        return 1


def generate(dll):
    from unicorn.x86_const import UC_X86_REG_ESI, UC_X86_REG_EDX
    rops = []
    for mode in range(6):
        o = Oracle(dll, 2, 7, [0, 0, 640, 400])
        o.vm.reg_write(UC_X86_REG_ESI, mode)
        o.vm.emu_start(IB + 0xCD5A, IB + 0xCD9A, count=100)
        rops.append(o.vm.reg_read(UC_X86_REG_EDX))
    cases = []
    for name, x, y, w, h, stretch, clip in CASES:
        o = Oracle(dll, w, h, clip)
        o.run(0x4A410, [INST, 101, 0, x, y, 0, 0, 0, 2, stretch, -1, 0, RECT, rops[4], 0, 1])
        o.run(0x2866, [INST, RECT])
        cases.append({'name': name, 'input': [x, y, w, h, stretch], 'clip': clip, 'events': o.events})
    # Exercise native/scaled port ROP selection independently of icon capture.
    port_rops = []
    for mode in range(6):
        for scaled in (False, True):
            o = Oracle(dll, 2, 7, [0, 0, 640, 400])
            o.write(RECT, [10, 20, 12, 27])
            o.write(RECT + 16, [30, 40, 34 if scaled else 32, 47])
            o.run(0x134D0, [INST, TABLE, RECT, TABLE, RECT + 16, mode])
            port_rops.append({'mode': mode, 'scaled': scaled, 'events': o.events})
    return {'driver_md5': MD5, 'icon_rops': rops, 'captures': cases, 'port_rops': port_rops}


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('dll')
    ap.add_argument('--check', action='store_true')
    args = ap.parse_args()
    path = Path(__file__).resolve().parents[1] / 'tests/fixtures/raster_calls.json'
    generated = generate(args.dll)
    result = json.dumps(generated, indent=2) + '\n'
    header = path.with_name('image_rops.h')
    rops = ('/* Generated by dll-raster-fixtures.py; driver MD5 ' + MD5 + '. */\n'
            'static const uint32_t image_rop_fixtures[] = {\n    ' +
            ', '.join(f'0x{rop:08x}u' for rop in generated['icon_rops']) + '\n};\n')
    if args.check:
        if path.read_text(encoding='utf-8') != result or header.read_text(encoding='utf-8') != rops:
            raise SystemExit('raster call fixtures differ from driver execution')
        print('6 capture cases and 18 ROP selections match the bounded driver oracle')
    else:
        path.write_text(result, encoding='utf-8', newline='\n')
        header.write_text(rops, encoding='utf-8', newline='\n')


if __name__ == '__main__':
    main()
