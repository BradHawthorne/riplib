#!/usr/bin/env python3
"""Execute decoded 2Y selection/protection and subsequent port switches.

Native style creation, selection, protection, ROP and pen arguments execute.
Brush realization, palette lookup and GDI are modeled; this is not pixel parity.
"""
import argparse
import importlib.util
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('redefine', Path(__file__).with_name('dll-port-redefine-fixtures.py'))
base = importlib.util.module_from_spec(spec)
spec.loader.exec_module(base)
IB, INST, ARGS = base.IB, base.INST, base.ARGS
STYLE, STYLES = base.STYLE, base.STYLES
SEEDS = {0: [2, 3, 1, 4], 7: [5, 6, 2, 9], 35: [10, 11, 3, 12]}
DEFAULT = [15, 0, 0, 15]


class StyleOracle(base.RedefineOracle):
    def __init__(self, dll, active):
        super().__init__(dll)
        self.allowed += [(0x46E41, 0x46ECE), (0x390E4, 0x3935B), (0x3943E, 0x39629)]
        self.stub(0x69E60, self.copy_default)
        self.stub(0x11210, lambda a: 1)  # brush realization, not slot selection
        self.stub(0x3DCE0, lambda a: a[1])  # identity palette for pen arguments
        self.import_stub(0x963D4, 3, lambda a: self.record('CreatePen', a[:3]) or 501)
        table = bytearray(36 * 0x61)
        for slot, values in SEEDS.items():
            p = slot * 0x61
            table[p:p+2] = values[0].to_bytes(2, 'little')
            table[p+2:p+4] = values[1].to_bytes(2, 'little')
            table[p+4] = values[2]
            table[p+0x1A:p+0x1C] = values[3].to_bytes(2, 'little')
            table[p+0x1D] = 1
            table[p+0x21] = 2
        self.write(base.TABLE + 0x5C, [11, 13])
        self.write(base.TABLE + 0x78 + 0x5C, [37, 49])
        self.vm.mem_write(STYLES, bytes(table))
        self.vm.mem_write(STYLE + 8, active.to_bytes(2, 'little'))

    def copy_default(self, a):
        if a[1:3] != [IB + 0x762E8, 0x61] or not STYLES <= a[0] < STYLES + 36 * 0x61:
            raise ValueError('unexpected style default copy')
        self.vm.mem_write(a[0], bytes(self.vm.mem_read(a[1], a[2])))
        return a[0]

    def capture(self, dest, flags):
        self.events.clear()
        self.drawing.clear()
        port_before = bytes(self.vm.mem_read(base.TABLE, 36 * 0x78))
        self.write(ARGS, [dest, flags])
        self.run(0x46E41, [INST, 0, ARGS, 0])
        if bytes(self.vm.mem_read(base.TABLE, 36 * 0x78)) != port_before:
            raise ValueError('style selection changed port state/cursor')
        selected = self.style_state()['selected']
        p = STYLES + selected * 0x61
        word = lambda offset: int.from_bytes(self.vm.mem_read(p + offset, 2), 'little')
        values = [word(0), word(2), self.vm.mem_read(p + 4, 1)[0], word(0x1A)]
        mask = sum(1 << i for i in range(36) if self.vm.mem_read(STYLES + i * 0x61 + 0x21, 1)[0] & 1)
        errors, drawing = list(self.events), list(self.drawing)
        before = self.style_state()
        self.command('s', [0, 0])
        self.command('s', [1, 0])
        if self.style_state() != before: raise ValueError('port switch changed styles')
        if self.vm.mem_read(STYLE + 0xA, 2) != b'\0\0': raise ValueError('unbalanced style locks')
        return {'selected': selected, 'values': values, 'protected': mask,
                'errors': errors, 'drawing': drawing}


def reset_cases(dll):
    cases = []
    for active in (0, 7, 35):
        o = StyleOracle(dll, active)
        o.capture(7, 1)
        o.capture(active, 0)
        o.run(0x391FD, [INST, -2 & 0xFFFFFFFF])
        o.run(0x394FE, [INST, 0])
        cases.append({'active': active, 'selected_after_reset': o.style_state()['selected'], 'zero': o.capture(0, 0),
                      'protected': o.capture(7, 0), 'unprotected': o.capture(35, 0)})
    return cases


def validate(data):
    def require(ok, what):
        if not ok: raise ValueError(what)
    require(data['driver_md5'] == base.life.ports.raster.MD5, 'driver identity')
    domain = [(a, d, f) for a in (0, 7) for d in (0, 7, 8, 35) for f in range(16)]
    require([(c['active'], c['dest'], c['flags']) for c in data['cases']] == domain, 'style matrix')
    require([c['active'] for c in data['resets']] == [0, 7, 35], 'reset matrix')
    for c in data['resets']:
        require(c['selected_after_reset'] == 0, 'reset selects style zero')
        require(c['zero']['values'] == DEFAULT and c['zero']['selected'] == 0, 'reset selects defaults')
        require(c['protected']['values'] == SEEDS[7] and c['protected']['protected'] == 128, 'protected style survives reset')
        require(c['unprotected']['values'] == DEFAULT, 'unprotected style resets')
    for c in data['cases']:
        a, d, f = c['active'], c['dest'], c['flags']
        mask = 0
        if a and f & 4: mask |= 1 << a
        if f & 8: mask &= ~(1 << a)
        if d and f & 1: mask |= 1 << d
        if f & 2: mask &= ~(1 << d)
        v = SEEDS.get(d, DEFAULT)
        r = c['result']
        require(r['selected'] == d and r['values'] == v, 'selected style values/defaults')
        require(r['protected'] == mask, 'protection ordering/master exception')
        errors = int(a == 0 and bool(f & 4)) + int(d == 0 and bool(f & 1))
        require(r['errors'] == [{'error_code': 30}] * errors, 'master protection diagnostic')
        require(r['drawing'] == [{'SetROP2': [101, [13, 7, 15, 9, 6][v[2]]]},
                                 {'CreatePen': [0, 1, v[0]]}], 'selected ROP and pen')


def generate(dll):
    data = {'driver_md5': base.life.ports.raster.MD5, 'cases': []}
    for a in (0, 7):
        for d in (0, 7, 8, 35):
            for f in range(16):
                o = StyleOracle(dll, a)
                data['cases'].append({'active': a, 'dest': d, 'flags': f, 'result': o.capture(d, f)})
    data['resets'] = reset_cases(dll)
    validate(data)
    return data


def c_header(data):
    rows = ['/* Generated by dll-style-fixtures.py; decoded driver style contract. */',
            'static const struct { unsigned active, dest, flags, color, back, mode, fill; uint64_t protected_mask; } style_cases[] = {']
    for c in data['cases']:
        r = c['result']
        values = [c['active'], c['dest'], c['flags'], *r['values']]
        rows.append('    {' + ', '.join(map(str, values)) + ', UINT64_C(' + str(r['protected']) + ')},')
    return '\n'.join(rows + ['};', ''])


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('dll')
    ap.add_argument('--check', action='store_true')
    args = ap.parse_args()
    data = generate(args.dll)
    for name, content in [('style_slots.json', json.dumps(data, indent=2) + '\n'), ('style_slots.h', c_header(data))]:
        path = ROOT / 'tests/fixtures' / name
        if args.check:
            if path.read_text(encoding='utf-8') != content: raise SystemExit(name + ' differs from native execution')
        else: path.write_text(content, encoding='utf-8', newline='\n')
    print('128 native style cases and 3 reset sequences; port switches preserve styles')


if __name__ == '__main__': main()
