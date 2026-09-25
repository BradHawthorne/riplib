#!/usr/bin/env python3
"""Execute decoded port lifetime/definition paths in the pinned RIPtel DLL.

Extends the D-41 oracle with real protection, deletion and replacement paths.
Allocation and GDI handles are modeled, not operating-system resources. Records
resource accounting, port metadata and active selection; not drawing-state or
pixel parity. No proprietary executable bytes are included in the fixtures.
"""
import argparse
import importlib.util
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('ports', Path(__file__).with_name('dll-port-fixtures.py'))
ports = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ports)
IB, INST, PORTS, TABLE, ARGS = ports.IB, ports.INST, ports.PORTS, ports.TABLE, ports.ARGS
HANDLERS = {'P': 0x466EC, 'p': 0x46862, 's': 0x468EB}
DEFINE_2 = ['P', [2, 10, 20, 110, 90, 0, 0]]
LIFETIMES = [
    ('delete_current', [['p', [1, 0, 0]]]),
    ('delete_current_missing_dest', [['p', [1, 2, 0]]]),
    ('delete_same_dest', [['p', [1, 1, 0]]]),
    ('delete_other', [DEFINE_2, ['p', [2, 0, 0]]]),
    ('delete_missing', [['p', [3, 2, 0]]]),
    ('protected_current', [['s', [1, 1]], ['p', [1, 2, 0]]]),
    ('protected_other', [DEFINE_2, ['s', [2, 1]], ['s', [1, 0]], ['p', [2, 0, 0]]]),
    ('delete_all', [DEFINE_2, ['p', [0, 0, 0]]]),
    ('delete_all_protected', [DEFINE_2, ['s', [2, 1]], ['s', [1, 0]], ['p', [0, 0, 0]]]),
    ('delete_all_recreate_dest', [DEFINE_2, ['p', [0, 2, 0]]]),
    ('unprotect_then_delete', [['s', [1, 1]], ['s', [1, 2]], ['p', [1, 0, 0]]]),
    ('protect_master_dest', [['s', [0, 1]], ['s', [1, 0]]]),
    ('protect_master_source', [['s', [0, 0]], ['s', [1, 4]]]),
    ('replace_shared', [['P', [1, 20, 30, 50, 60, 0, 0]]]),
    ('replace_protected', [['s', [1, 1]], ['P', [1, 20, 30, 50, 60, 2, 0]]]),
    ('replace_offscreen_delete', [['P', [1, 10, 20, 110, 90, 1, 0]], ['p', [1, 0, 0]]]),
    ('invalid_delete_source', [['p', [36, 0, 0]]]),
    ('invalid_delete_dest', [['p', [1, 36, 0]]]),
]
RECTANGLES = [
    ('normal', [10, 20, 110, 90]), ('unit', [0, 0, 1, 1]),
    ('empty_x', [10, 20, 10, 90]), ('empty_y', [10, 20, 110, 20]),
    ('reverse_x', [110, 20, 10, 90]), ('reverse_y', [10, 90, 110, 20]),
    ('outside', [650, 360, 700, 400]), ('fonts', [0, 0, 1280, 254]),
    ('effects', [0, 0, 936, 960]),
]


class LifecycleOracle(ports.HandlerOracle):
    def __init__(self, dll, budget=2000000, failure=None):
        super().__init__(dll, (0, 0), 0)
        self.allowed += [(0x3302C, 0x3326F), (0x3378A, 0x3393C), (0x46862, 0x468EB)]
        del self.stubs[IB + 0x33821]
        self.stub(0x68D70, self.memset)
        self.write(PORTS + 0x22, [budget])
        self.write(PORTS + 0x32, [0, 0, 640, 400])  # master device bounds for lazy ports
        self.failure = failure
        self.resources = []
        self.next_bitmap = 300
        self.import_stub(0x96460, 3, self.bitmap)
        self.import_stub(0x963C0, 2, lambda a: 0 if self.failure == 'select' else 1)
        self.import_stub(0x963D0, 1, lambda a: self.release('bitmap', a[0]))
        self.import_stub(0x964AC, 1, lambda a: self.release('dc', a[0]))

    def memset(self, a):
        if a[1:3] != [0, 0x78] or not TABLE <= a[0] < TABLE + 36 * 0x78 or (a[0] - TABLE) % 0x78:
            raise ValueError('unexpected port-entry memory clear')
        self.vm.mem_write(a[0], bytes([a[1] & 255]) * a[2])
        return a[0]

    def create_dc(self, a):
        # The superclass creates a shared initial port, which needs no new DC.
        if getattr(self, 'failure', None) == 'dc': return 0
        dc = super().create_dc(a)
        self.resources.append({'create_dc': dc})
        return dc

    def bitmap(self, a):
        if self.failure == 'bitmap': return 0
        self.next_bitmap += 1
        self.resources.append({'create_bitmap': [self.next_bitmap, a[1], a[2]]})
        return self.next_bitmap

    def release(self, kind, handle):
        self.resources.append({'delete_' + kind: handle})
        return 1

    def snapshot(self):
        live = []
        for i in range(36):
            p = TABLE + i * 0x78
            kind = self.vm.mem_read(p, 1)[0]
            if kind:
                live.append({'slot': i, 'kind': kind, 'clip': self.read(p + 0x3A, 4),
                             'protected': bool(self.vm.mem_read(p + 0x17, 1)[0] & 1),
                             'dc': self.read(p + 0x6C, 1)[0]})
        active = int.from_bytes(self.vm.mem_read(PORTS + 8, 2), 'little')
        return {'active': active, 'ports': live, 'available_pixels': self.read(PORTS + 0x22, 1)[0]}

    def command(self, command, args):
        self.events.clear()
        self.resources.clear()
        self.write(ARGS, args)
        self.run(HANDLERS[command], [INST, 0, ARGS, 0])
        return {'command': command, 'args': args, 'state': self.snapshot(),
                'errors': list(self.events), 'resources': list(self.resources)}


def generate(dll):
    lifetimes = []
    for name, sequence in LIFETIMES:
        oracle = LifecycleOracle(dll)
        lifetimes.append({'name': name, 'steps': [oracle.command(c, a) for c, a in sequence]})
    definitions = []
    for name, rect in RECTANGLES:
        for offscreen in (0, 1):
            oracle = LifecycleOracle(dll)
            definitions.append({'name': name, 'offscreen': offscreen,
                                'step': oracle.command('P', [2, *rect, offscreen | 2, 0])})
    failures = []
    for failure in ('budget', 'dc', 'bitmap', 'select'):
        for replacement in (False, True):
            oracle = LifecycleOracle(dll, 1 if failure == 'budget' else 2000000, failure)
            failures.append({'failure': failure, 'replacement': replacement,
                             'step': oracle.command('P', [1 if replacement else 2, 0, 0, 100, 70, 3, 0])})
    data = {'driver_md5': ports.raster.MD5, 'lifetimes': lifetimes,
            'definitions': definitions, 'failures': failures}
    validate(data)
    return data


def validate(data):
    """Independent state predicates keep unexpected failures out of goldens."""
    def require(condition, message):
        if not condition: raise ValueError(message)

    require(data['driver_md5'] == ports.raster.MD5, 'driver identity')
    require([c['name'] for c in data['lifetimes']] == [n for n, _ in LIFETIMES], 'lifetime matrix')
    for case, (_, sequence) in zip(data['lifetimes'], LIFETIMES):
        active, live, protected = 1, {0, 1}, set()
        require(len(case['steps']) == len(sequence), 'lifetime step count')
        for step, (command, args) in zip(case['steps'], sequence):
            require((step['command'], step['args']) == (command, args), 'lifetime input')
            expected_errors = []
            slot = args[0]
            if command == 'p':
                if slot >= 36 or args[1] >= 36: expected_errors = [{'error_code': 30}]
                else:
                    if slot == 0: live &= protected | {0}
                    elif slot in protected: expected_errors = [{'error_code': 30}]
                    else: live.discard(slot)
                    active = args[1]
                    live.add(active)
            elif command == 's':
                flags = args[1]
                if active:
                    if flags & 4: protected.add(active)
                    if flags & 8: protected.discard(active)
                active = slot
                live.add(active)
                if active:
                    if flags & 1: protected.add(active)
                    if flags & 2: protected.discard(active)
            elif slot in protected: expected_errors = [{'error_code': 30}]
            else:
                live.add(slot)
                if args[5] & 2: active = slot
            state = step['state']
            require(state['active'] == active and {p['slot'] for p in state['ports']} == live,
                    'lifetime selection/allocation contract')
            require({p['slot'] for p in state['ports'] if p['protected']} == protected,
                    'lifetime protection contract')
            require(step['errors'] == expected_errors, 'lifetime diagnostic contract')
        if case['name'] == 'replace_offscreen_delete':
            created, deleted = case['steps']
            require(created['state']['available_pixels'] == 1992000 and
                    deleted['state']['available_pixels'] == 2000000, 'lifetime pixel budget restoration')
            require(created['resources'] == [{'create_dc': 201}, {'create_bitmap': [301, 100, 80]}] and
                    deleted['resources'] == [{'delete_bitmap': 301}, {'delete_dc': 201}],
                    'lifetime resource ownership')

    require(len(data['definitions']) == 18, 'definition matrix size')
    for case, (name, rect, off) in zip(data['definitions'],
                                      [(n, r, o) for n, r in RECTANGLES for o in (0, 1)]):
        require((case['name'], case['offscreen']) == (name, off), 'definition matrix')
        step = case['step']
        require(step['command'] == 'P' and step['args'] == [2, *rect, off | 2, 0], 'definition input')
        x, y, right, bottom = rect
        y, bottom = y * 8 // 7, bottom * 8 // 7
        w, h = (right - x) & 65535, (bottom - y) & 65535
        rejected = off and w * h > 2000000
        require(step['errors'] == ([{'error_code': 255}, {'error_code': 40}] if rejected else []),
                'definition diagnostic contract')
        require(step['state']['active'] == (1 if rejected else 2), 'definition selection')
        require(step['state']['available_pixels'] == 2000000 - (w * h if off and not rejected else 0),
                'definition pixel accounting')
        target = [p for p in step['state']['ports'] if p['slot'] == 2]
        expected = [] if rejected else [{'slot': 2, 'kind': 4 if off else 1,
                    'clip': [0, 0, w, h] if off else [x, y, x + w, y + h],
                    'protected': False, 'dc': 201 if off else 101}]
        require(target == expected, 'definition rectangle/DC contract')

    require([(c['failure'], c['replacement']) for c in data['failures']] ==
            [(f, r) for f in ('budget', 'dc', 'bitmap', 'select') for r in (False, True)], 'failure matrix')
    for case in data['failures']:
        step = case['step']
        require(step['errors'] == [{'error_code': 255}, {'error_code': 40}], 'failure diagnostics')
        require(step['state']['active'] == 1 and [p['slot'] for p in step['state']['ports']] == [0, 1],
                'failure selection/allocation')
        require(step['state']['ports'][1]['clip'] == [0, 0, 640 if case['replacement'] else 100,
                                                    400 if case['replacement'] else 80], 'failure replacement')
        require(step['state']['available_pixels'] == (1 if case['failure'] == 'budget' else 2000000),
                'failed allocation changed budget')
        resources = []
        if case['failure'] == 'bitmap': resources = [{'create_dc': 201}, {'delete_dc': 201}]
        elif case['failure'] == 'select':
            resources = [{'create_dc': 201}, {'create_bitmap': [301, 100, 80]},
                         {'delete_bitmap': 301}, {'delete_dc': 201}]
        require(step['resources'] == resources, 'failure resource cleanup')


def c_header(data):
    lines = ['/* Generated driver lifetime states. Geometry/storage are not compared here. */',
             'static const struct { bool reset; const char *wire; uint8_t active;',
             '    uint64_t allocated, protected_ports; } port_lifecycle_fixtures[] = {']
    for case in data['lifetimes']:
        if case['name'].startswith('invalid_'): continue  # decoded 36 is outside base-36 wire digits
        for i, step in enumerate(case['steps']):
            widths = {'P': [1, 2, 2, 2, 2, 4, 4], 'p': [1, 1, 2], 's': [1, 2]}[step['command']]
            raw = ''
            for value, width in zip(step['args'], widths):
                digits = ''
                for _ in range(width):
                    digits = '0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ'[value % 36] + digits
                    value //= 36
                if value: raise ValueError('unrepresentable wire fixture')
                raw += digits
            state = step['state']
            live = sum(1 << p['slot'] for p in state['ports'])
            protected = sum(1 << p['slot'] for p in state['ports'] if p['protected'])
            lines.append('    {%s, "!|2%s%s|", %d, UINT64_C(%d), UINT64_C(%d)},' %
                         ('true' if i == 0 else 'false', step['command'], raw, state['active'], live, protected))
    return '\n'.join(lines + ['};', ''])


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('dll')
    ap.add_argument('--check', action='store_true')
    args = ap.parse_args()
    data = generate(args.dll)
    result = json.dumps(data, indent=2) + '\n'
    path = ROOT / 'tests/fixtures/port_lifecycle.json'
    header = c_header(data)
    header_path = path.with_suffix('.h')
    if args.check:
        if path.read_text(encoding='utf-8') != result or header_path.read_text(encoding='utf-8') != header:
            raise SystemExit('port lifecycle fixtures differ from driver execution')
        print('18 lifetime sequences, 18 definitions and 8 allocation failures match driver execution')
    else:
        path.write_text(result, encoding='utf-8', newline='\n')
        header_path.write_text(header, encoding='utf-8', newline='\n')


if __name__ == '__main__': main()
