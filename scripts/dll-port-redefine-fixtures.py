#!/usr/bin/env python3
"""Trace port redefinition into the next native line handler (decoded arguments).

Executes focus-off handling, port lifetime, position reset, clipping and ROP
selection. GDI calls and pen handles are modeled; no live pixel parity follows.
Styles are separately selected through RIPINST+0x0A, not the port index.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('lifecycle', Path(__file__).with_name('dll-port-lifecycle-fixtures.py'))
life = importlib.util.module_from_spec(spec)
spec.loader.exec_module(life)
IB, INST, PORTS, TABLE, ARGS = life.IB, life.INST, life.PORTS, life.TABLE, life.ARGS
STYLE, STYLES, FOCUS, PEN = 0x160000, 0x161000, 0x162000, 0x163000


def style_seed():
    table = bytearray(36 * 0x61)
    for i in range(36):
        p = i * 0x61
        table[p:p + 2] = (i + 1).to_bytes(2, 'little')
    p = 7 * 0x61
    table[p:p + 2] = b'\x05\x00'  # color
    table[p + 2:p + 4] = b'\x03\x00'  # background
    table[p + 4] = 1  # actual SetROP2 helper reads the write mode here
    table[p + 0x1A:p + 0x1C] = b'\x09\x00'  # fill color
    table[p + 0x1D] = 1  # pen width, no inflated invalidation rectangle
    return bytes(table)


class RedefineOracle(life.LifecycleOracle):
    def __init__(self, dll):
        super().__init__(dll)
        self.write(TABLE + 3, [0, 0, 640, 400])  # master backing bounds for post-line clip restore
        self.allowed += [(0x45038, 0x45056), (0x3445B, 0x34488),
                         (0x1CB79, 0x1CD15), (0xE6B3, 0xE72A),
                         (0x13CFC, 0x13D10), (0x2530B, 0x25337),
                         (0x3988F, 0x39935)]
        del self.stubs[IB + 0x45038]
        del self.stubs[IB + 0x3988F]
        del self.stubs[IB + 0x398EE]
        self.write(INST + 0x1E, [FOCUS])  # focus rectangle is inactive
        self.write(INST + 0x0A, [STYLE])
        self.write(STYLE, [STYLES])
        self.write(STYLE + 4, [777])
        self.vm.mem_write(STYLE + 8, (7).to_bytes(2, 'little'))
        self.vm.mem_write(STYLES, style_seed())
        self.write(INST + 0x3A, [PEN])
        self.write(PEN + 8, [501])
        self.drawing = []
        self.stub(0x03FBF, self.clip)
        self.import_stub(0x964E4, 1, lambda a: STYLES if a[0] == 777 else 0)
        self.import_stub(0x964F4, 1, lambda a: 1)
        self.import_stub(0x96408, 2, lambda a: self.record('SetROP2', a[:2]))
        self.import_stub(0x963DC, 4, lambda a: self.record('MoveToEx', a[:3]))
        self.import_stub(0x963CC, 3, lambda a: self.record('LineTo', a[:3]))

    def record(self, name, args):
        self.drawing.append({name: args})
        return 1

    def clip(self, a):
        return self.record('clip', [self.read(INST + 0x62, 1)[0], *self.read(a[1], 4)])

    def style_state(self):
        return {'selected': int.from_bytes(self.vm.mem_read(STYLE + 8, 2), 'little'),
                'sha256': hashlib.sha256(self.vm.mem_read(STYLES, 36 * 0x61)).hexdigest()}


def generate(dll):
    cases = []
    for active in (0, 1):
        for flags in range(4):
            for protected in (False, True):
                o = RedefineOracle(dll)
                if protected: o.command('s', [1, 1])
                o.command('s', [active, 0])
                o.write(TABLE + 0x5C, [11, 13])
                o.write(TABLE + 0x78 + 0x5C, [37, 49])
                args = [1, 20, 30, 50, 60, flags, 0]
                step = o.command('P', args)
                cursor = o.read(TABLE + 0x78 + 0x5C, 2)
                style = o.style_state()
                o.write(ARGS, [0, 0, 3, 7])
                o.drawing.clear()
                o.events.clear()
                o.run(0x1CB79, [INST, 0, ARGS, 0])
                if o.events: raise ValueError(f'next line failed: {o.events}')
                if o.vm.mem_read(STYLE + 0xA, 2) != b'\0\0':
                    raise ValueError('unbalanced native graphics-style locks')
                if o.style_state() != style: raise ValueError('next line changed seeded styles')
                cases.append({'initial_active': active, 'flags': flags, 'protected': protected,
                              'define': step, 'target_cursor': cursor, 'style': style,
                              'next_line': o.drawing})
    data = {'driver_md5': life.ports.raster.MD5, 'cases': cases}
    validate(data)
    return data


def validate(data):
    def require(condition, message):
        if not condition: raise ValueError(message)
    require(data['driver_md5'] == life.ports.raster.MD5, 'driver identity')
    domain = [(a, f, p) for a in (0, 1) for f in range(4) for p in (False, True)]
    require([(c['initial_active'], c['flags'], c['protected']) for c in data['cases']] == domain,
            'redefinition matrix')
    style = {'selected': 7, 'sha256': hashlib.sha256(style_seed()).hexdigest()}
    for c in data['cases']:
        a, f, p = c['initial_active'], c['flags'], c['protected']
        step = c['define']
        require(step['command'] == 'P' and step['args'] == [1, 20, 30, 50, 60, f, 0], 'define input')
        selected = a if p or not f & 2 else 1
        require(step['state']['active'] == selected, 'active selection')
        require(step['errors'] == ([{'error_code': 30}] if p else []), 'define errors')
        require(c['target_cursor'] == ([37, 49] if p else [0, 0]), 'drawing position reset')
        require(c['style'] == style, 'independent style preserved')
        clip = [0, 0, 100, 80] if p else [0, 0, 30, 34] if f & 1 else [20, 34, 50, 68]
        target = next(port for port in step['state']['ports'] if port['slot'] == 1)
        require(target['clip'] == clip, 'redefined viewport')
        if not selected: clip = [0, 0, 640, 400]
        dc = 201 if selected and f & 1 and not p else 101
        expected = [{'clip': [dc, *clip]}, {'SetROP2': [dc, 7]},
                    {'MoveToEx': [dc, clip[0], clip[1]]},
                    {'LineTo': [dc, clip[0] + 3, clip[1] + 8]}, {'clip': [dc, *clip]}]
        require(c['next_line'] == expected, 'next-line clip/style/origin contract')


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('dll')
    ap.add_argument('--check', action='store_true')
    args = ap.parse_args()
    data = generate(args.dll)
    result = json.dumps(data, indent=2) + '\n'
    path = ROOT / 'tests/fixtures/port_redefine.json'
    if args.check:
        if path.read_text(encoding='utf-8') != result:
            raise SystemExit('redefinition fixtures differ from driver execution')
        print('16 redefinition cases preserve styles, reset positions and apply the next-line viewport')
    else: path.write_text(result, encoding='utf-8', newline='\n')


if __name__ == '__main__': main()
