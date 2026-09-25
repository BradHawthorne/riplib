#!/usr/bin/env python3
"""Execute port creation/switching, LOAD_ICON and PORT_COPY in the pinned DLL.

The pinned DLL's handler, coordinate conversion, show-bitmap rectangle path,
port/DC selection and clipboard copy execute in Unicorn. File lookup, string
services, drawing-state synchronization and GDI are explicit modeled boundaries.
This is a decoded-handler oracle, not the wire parser or a live RIPtel session.
"""
import argparse
import importlib.util
import json
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('raster', Path(__file__).with_name('dll-raster-fixtures.py'))
raster = importlib.util.module_from_spec(spec)
spec.loader.exec_module(raster)
IB, INST, PORTS, TABLE = raster.IB, raster.INST, raster.PORTS, raster.TABLE
ARGS, NAME, TEMP, STATE = 0x140000, 0x141000, 0x142000, 0x143000


class HandlerOracle(raster.Oracle):
    def __init__(self, dll, origin, offscreen=0):
        super().__init__(dll, 2, 7, [0, 0, 640, 400])
        self.rect_pointer = None
        self.allowed += [(0xCB38, 0xCEF8), (0x4A5C5, 0x4A5D0),
                         (0x4A890, 0x4A904), (0x342AD, 0x3448A),
                         (0x3326F, 0x3378A), (0x3393C, 0x339F2),
                         (0x46372, 0x46862), (0x468EB, 0x4699A),
                         (0x34231, 0x34269)]
        self.write(INST + 0x36, [STATE])
        self.vm.mem_write(NAME, b'fixture.bmp\0')
        self.write(PORTS + 0x22, [1000000])
        self.next_dc = 200
        # These services do not supply coordinates in this experiment.
        for rva in (0x45038, 0x18CF6, 0x18BF7, 0x153FE, 0x65CBF):
            self.stub(rva, lambda a: 0)
        self.stub(0x03FBF, lambda a: 1)  # apply clip to GDI
        self.stub(0x15396, lambda a: TEMP)
        self.stub(0x65431, self.strcpy)
        self.stub(0x3F71A, lambda a: a[0])
        self.stub(0x69E40, self.strrchr)
        self.stub(0x655E2, lambda a: int(self.string(a[0]).lower() != self.string(a[1]).lower()))
        self.stub(0x3F80E, lambda a: 0)  # local file is available
        self.import_stub(0x964EC, 1, lambda a: 0)  # GlobalFree
        # Execute real portInit, including shared-vs-offscreen geometry.
        del self.stubs[IB + 0x3326F]
        self.stub(0x33821, lambda a: 0)  # newly allocated slot is unprotected
        self.import_stub(0x964A0, 1, self.create_dc)
        for iat, argc in ((0x96490, 1), (0x963F0, 2), (0x9648C, 1),
                          (0x96400, 2), (0x96494, 1), (0x963F8, 2),
                          (0x96460, 3), (0x963C0, 2), (0x9640C, 1),
                          (0x9667C, 3), (0x963E0, 1)):
            self.import_stub(iat, argc, lambda a: 1)
        self.write(ARGS, [1, *origin, origin[0] + 100, origin[1] + 70, offscreen, 0])
        self.run(0x466EC, [INST, 0, ARGS, 0])
        self.write(ARGS, [1, 0])
        self.run(0x468EB, [INST, 0, ARGS, 0])
        port = TABLE + 0x78
        self.port = {'kind': self.vm.mem_read(port, 1)[0],
                     'clip': self.read(port + 0x3A, 4), 'dc': self.read(INST + 0x62, 1)[0]}
        if self.events: raise RuntimeError(f'port setup failed: {self.events}')

    def create_dc(self, a):
        self.next_dc += 1
        return self.next_dc

    def string(self, address):
        out = bytearray()
        for i in range(1024):
            b = self.vm.mem_read(address + i, 1)[0]
            if not b: return bytes(out)
            out.append(b)
        raise ValueError('unterminated modeled string')

    def strcpy(self, a):
        self.vm.mem_write(a[0], self.string(a[1]) + b'\0')
        return a[0]

    def strrchr(self, a):
        index = self.string(a[0]).rfind(bytes([a[1] & 255]))
        return a[0] + index if index >= 0 else 0

    def hook(self, vm, address, size, user):
        if address == IB + 0x3326F and self.rect_pointer is not None:
            args = self.read(vm.reg_read(self.esp) + 4, 8)
            self.events.append({'allocation': [args[4] & 0xFFFF, args[5] & 0xFFFF]})
        if address == IB + 0x4A410:
            args = self.read(vm.reg_read(self.esp) + 4, 16)
            self.rect_pointer = args[12]
        if address == IB + 0x2866:
            args = self.read(vm.reg_read(self.esp) + 4, 2)
            self.events.append({'capture_rectangle': self.read(args[1], 4)})
        super().hook(vm, address, size, user)

    def display(self, a):
        xywh = [struct.unpack('<h', struct.pack('<H', v & 0xFFFF))[0] for v in a[1:5]]
        self.events.append({'display': xywh, 'dc': a[0], 'rop': a[7],
                            'rectangle': self.read(self.rect_pointer, 4)})
        return 1  # unlike the helper oracle, resume through the real epilogue


def generate(dll):
    cases, copies = [], []
    for origin in ((0, 0), (10, 20)):
        for offscreen in (0, 1):
            for stretch in (0, 1):
                for capture in (0, 1):
                    o = HandlerOracle(dll, origin, offscreen)
                    o.write(ARGS, [3, 7, 0, 4, capture, stretch, 0])
                    o.run(0xCB38, [INST, 0, ARGS, NAME])
                    if any('error_code' in event for event in o.events):
                        raise RuntimeError(f'handler fixture failed: {o.events}')
                    if sum('display' in event for event in o.events) != 1:
                        raise RuntimeError('handler did not reach display exactly once')
                    if sum('StretchBlt' in event for event in o.events) != capture:
                        raise RuntimeError('handler did not honor the capture flag')
                    cases.append({'define_origin': list(origin), 'offscreen': offscreen, 'port': o.port,
                                  'args': [3, 7, 0, 4, capture, stretch, 0], 'events': o.events})
            for reverse in (False, True):
                for scaled in (False, True):
                    o = HandlerOracle(dll, origin, offscreen)
                    args = [0 if reverse else 1, 3, 7, 5, 14,
                            1 if reverse else 0, 30, 28, 34 if scaled else 32, 35, 0, 0]
                    o.write(ARGS, args)
                    o.run(0x46372, [INST, 0, ARGS, 0])
                    if any('error_code' in event for event in o.events) or len(o.events) != 1:
                        raise RuntimeError(f'port-copy fixture failed: {o.events}')
                    copies.append({'define_origin': list(origin), 'offscreen': offscreen,
                                   'port': o.port, 'args': args, 'events': o.events})
    result = {'driver_md5': raster.MD5, 'load_icon': cases, 'port_copy': copies}
    validate(result)
    return result


def validate(data):
    """Check the recorded contract independently of emulation or proprietary code."""
    def require(condition, message):
        if not condition: raise ValueError(message)

    require(data['driver_md5'] == raster.MD5, 'driver identity')
    for family in ('load_icon', 'port_copy'):
        cases = data[family]
        require(len(cases) == 16, 'case count')
        keys = set()
        for c in cases:
            ox, oy = c['define_origin']
            off = c['offscreen']
            require((ox, oy) in ((0, 0), (10, 20)) and off in (0, 1), 'input domain')
            px, py = (0, 0) if off else (ox, oy * 8 // 7)
            dc = 201 if off else 101
            require(c['port'] == {'kind': 4 if off else 1,
                                  'clip': [px, py, px + 100, py + 80], 'dc': dc}, 'port setup')
            a = c['args']
            if family == 'load_icon':
                capture, stretch = a[4:6]
                require(capture in (0, 1) and stretch in (0, 1), 'icon flag domain')
                require(a == [3, 7, 0, 4, capture, stretch, 0], 'icon arguments')
                keys.add((ox, oy, off, capture, stretch))
                height = 8 if stretch else 7
                rect = [px + 3, py + 8, px + 5, py + 8 + height]
                expected = [{'display': [px + 3, py + 8, 2, height], 'dc': dc,
                             'rop': 0x330008, 'rectangle': rect}]
                if capture:
                    expected += [{'capture_rectangle': rect}, {'allocation': [3, height + 1]},
                                 {'StretchBlt': [202 if off else 201, 0, 0, 3, height + 1,
                                                 dc, 2 * px + 3, 2 * py + 8, 2, height, 0xCC0020]}]
            else:
                reverse, scaled = a[0] == 0, a[8] == 34
                require(a == [0 if reverse else 1, 3, 7, 5, 14,
                              1 if reverse else 0, 30, 28, 34 if scaled else 32, 35, 0, 0], 'copy arguments')
                keys.add((ox, oy, off, reverse, scaled))
                sx, sy = (3, 8) if reverse else (px + 3, py + 8)
                dx, dy = (px + 30, py + 32) if reverse else (30, 32)
                call = [dc if reverse else 101, dx, dy, 4 if scaled else 2, 8,
                        101 if reverse else dc, sx, sy]
                if scaled: call += [2, 8]
                expected = [{'StretchBlt' if scaled else 'BitBlt': call + [0xCC0020]}]
            require(c['events'] == expected, family + ' coordinate/DC/capture contract')
        require(len(keys) == 16, 'missing or duplicate matrix case')


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('dll')
    ap.add_argument('--check', action='store_true')
    args = ap.parse_args()
    result = json.dumps(generate(args.dll), indent=2) + '\n'
    path = ROOT / 'tests/fixtures/port_calls.json'
    if args.check:
        if path.read_text(encoding='utf-8') != result:
            raise SystemExit('port fixtures differ from driver execution')
        print('16 LOAD_ICON and 16 PORT_COPY cases with native port setup match the driver')
    else:
        path.write_text(result, encoding='utf-8', newline='\n')


if __name__ == '__main__': main()
