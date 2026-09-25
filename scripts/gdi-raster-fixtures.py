#!/usr/bin/env python3
"""Replay recorded driver calls on Windows memory DIBs, never screen DCs.

The DLL is not loaded. Its checked-in call arguments drive native GDI on the
current Windows host. Top-down 8-bit DIBs use explicit grayscale color tables;
the second table permutes indices. This measures these DIB configurations,
not every historical display driver, palette or RIPtel window configuration.
"""
import argparse
import ctypes as C
from ctypes import wintypes as W
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]


class Header(C.Structure):
    _fields_ = [('size', W.DWORD), ('width', W.LONG), ('height', W.LONG),
                ('planes', W.WORD), ('bits', W.WORD), ('compression', W.DWORD),
                ('image_size', W.DWORD), ('xppm', W.LONG), ('yppm', W.LONG),
                ('used', W.DWORD), ('important', W.DWORD)]


class Info(C.Structure):
    _fields_ = [('header', Header), ('colors', W.DWORD * 256)]


def gdi_api():
    if sys.platform != 'win32':
        raise SystemExit('Native memory-DIB fixture generation requires Windows')
    g = C.WinDLL('gdi32', use_last_error=True)
    signatures = {
        'CreateCompatibleDC': (W.HDC, [W.HDC]),
        'CreateDIBSection': (W.HBITMAP, [W.HDC, C.POINTER(Info), W.UINT, C.POINTER(C.c_void_p), W.HANDLE, W.DWORD]),
        'SelectObject': (W.HANDLE, [W.HDC, W.HANDLE]),
        'DeleteObject': (W.BOOL, [W.HANDLE]), 'DeleteDC': (W.BOOL, [W.HDC]),
        'GdiFlush': (W.BOOL, []), 'SetStretchBltMode': (C.c_int, [W.HDC, C.c_int]),
        'StretchBlt': (W.BOOL, [W.HDC, C.c_int, C.c_int, C.c_int, C.c_int,
                              W.HDC, C.c_int, C.c_int, C.c_int, C.c_int, W.DWORD]),
        'BitBlt': (W.BOOL, [W.HDC, C.c_int, C.c_int, C.c_int, C.c_int,
                          W.HDC, C.c_int, C.c_int, W.DWORD]),
    }
    for name, (result, args) in signatures.items():
        getattr(g, name).restype = result
        getattr(g, name).argtypes = args
    return g


def checked(result):
    if not result:
        raise C.WinError(C.get_last_error())
    return result


class DIB:
    def __init__(self, g, width, height, permuted=False):
        self.g, self.w, self.h = g, width, height
        self.stride = (width + 3) & ~3
        self.dc = self.bitmap = self.old = None
        info = Info()
        info.header = Header(C.sizeof(Header), width, -height, 1, 8, 0,
                             self.stride * height, 0, 0, 256, 0)
        for i in range(256):
            value = (i * 73 + 19) % 256 if permuted else i
            info.colors[i] = value * 0x010101
        pointer = C.c_void_p()
        try:
            self.dc = checked(g.CreateCompatibleDC(None))
            self.bitmap = checked(g.CreateDIBSection(self.dc, C.byref(info), 0, C.byref(pointer), None, 0))
            self.old = checked(g.SelectObject(self.dc, self.bitmap))
            self.data = (C.c_ubyte * (self.stride * height)).from_address(pointer.value)
            self.fill(0)
        except Exception:
            self.close()
            raise

    def fill(self, value):
        checked(self.g.GdiFlush())
        C.memset(self.data, value, len(self.data))

    def pixels(self):
        checked(self.g.GdiFlush())
        return [self.data[y * self.stride + x] for y in range(self.h) for x in range(self.w)]

    def close(self):
        if self.old: self.g.SelectObject(self.dc, self.old)
        if self.bitmap: self.g.DeleteObject(self.bitmap)
        if self.dc: self.g.DeleteDC(self.dc)
        self.old = self.bitmap = self.dc = None

    def __enter__(self): return self
    def __exit__(self, *args): self.close()


def generate():
    g = gdi_api()
    calls = json.loads((ROOT / 'tests/fixtures/raster_calls.json').read_text(encoding='utf-8'))
    cases, axes, grids = [], [], []
    # Distinct sample values expose both ordinary center sampling and GDI's
    # copy/edge-repeat shortcut when BOTH dimensions differ by at most one.
    for n in range(1, 33):
        for m in range(1, 33):
            with DIB(g, n, 1) as source, DIB(g, m, 1) as dest:
                for x in range(n): source.data[x] = x
                checked(g.SetStretchBltMode(dest.dc, 3))
                checked(g.StretchBlt(dest.dc, 0, 0, m, 1, source.dc, 0, 0, n, 1, 0xCC0020))
                value = 2166136261
                for pixel in dest.pixels(): value = ((value ^ pixel) * 16777619) & 0xFFFFFFFF
                axes.append([n, m, value])
    for w in (2, 3, 4, 7):
        for h in (2, 3, 7):
            for dw in (1, 3, 4, 5, 8, 10):
                for dh in (1, 3, 4, 8, 10):
                    with DIB(g, w, h) as source, DIB(g, dw, dh) as dest:
                        for y in range(h):
                            for x in range(w): source.data[y * source.stride + x] = y * w + x
                        checked(g.SetStretchBltMode(dest.dc, 3))
                        checked(g.StretchBlt(dest.dc, 0, 0, dw, dh, source.dc, 0, 0, w, h, 0xCC0020))
                        value = 2166136261
                        for pixel in dest.pixels(): value = ((value ^ pixel) * 16777619) & 0xFFFFFFFF
                        grids.append([w, h, dw, dh, value])
    for c in calls['captures']:
        x, y, width, height, stretch = c['input']
        events = c['events']
        display = events[0]['display']
        dw, dh = next(e['allocation'] for e in events if 'allocation' in e)
        for palette in ('identity', 'permuted'):
            for mode in (1, 2, 3):
                for rop in (0xCC0020, 0x660046, 0x330008):
                    permuted = palette == 'permuted'
                    with DIB(g, width, height, permuted) as icon, DIB(g, 640, 400, permuted) as screen, DIB(g, dw, dh, permuted) as clipboard:
                        screen.fill(0xA5)
                        for row in range(height):
                            for col in range(width):
                                icon.data[row * icon.stride + col] = ((row * width + col) * 11 + 7) % 256
                        checked(g.SetStretchBltMode(screen.dc, 3))
                        checked(g.StretchBlt(screen.dc, *display, icon.dc, 0, 0, width, height, rop))
                        checked(g.SetStretchBltMode(clipboard.dc, mode))
                        for event in events:
                            if 'StretchBlt' in event:
                                a = event['StretchBlt']
                                checked(g.StretchBlt(clipboard.dc, *a[1:5], screen.dc, *a[6:11]))
                        cases.append({'name': c['name'], 'palette': palette, 'stretch_mode': mode,
                                      'rop': rop, 'width': dw, 'height': dh, 'pixels': clipboard.pixels()})
    compact = [c for c in cases if c['palette'] == 'identity' and c['stretch_mode'] == 3]
    for c in cases:
        reference = next(r for r in compact if r['name'] == c['name'] and r['rop'] == c['rop'])
        if c['pixels'] != reference['pixels']:
            raise RuntimeError('palette/stretch-mode equivalence no longer holds: ' + str(c))
    return {'configuration': 'Windows GDI, top-down 8-bit memory DIBs, identical source/destination color tables',
            'driver_md5': calls['driver_md5'], 'axis_hashes': axes, 'grid_hashes': grids,
            'capture_configurations_verified': len(cases), 'captures': compact}


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--check', action='store_true')
    args = ap.parse_args()
    path = ROOT / 'tests/fixtures/gdi_raster.json'
    generated = generate()
    result = json.dumps(generated, separators=(',', ':')) + '\n'
    calls = json.loads((ROOT / 'tests/fixtures/raster_calls.json').read_text(encoding='utf-8'))
    inputs = {c['name']: c['input'] for c in calls['captures']}
    header = ['/* Generated by gdi-raster-fixtures.py on Windows memory DIBs. */',
              'static const struct { uint16_t source, dest; uint32_t hash; } gdi_axis_fixtures[] = {']
    header += [f'    {{{n}, {m}, 0x{value:08x}u}},' for n, m, value in generated['axis_hashes']]
    header += ['};', 'static const struct { uint16_t w, h, dw, dh; uint32_t hash; } gdi_grid_fixtures[] = {']
    header += [f'    {{{w}, {h}, {dw}, {dh}, 0x{value:08x}u}},' for w, h, dw, dh, value in generated['grid_hashes']]
    header += ['};', 'static const struct { int16_t x, y, w, h, stretch, mode, cw, ch; uint8_t pixels[64]; }',
               'gdi_capture_fixtures[] = {']
    for c in generated['captures']:
        if c['name'] == 'port_origin': continue  # separate port-coordinate boundary
        fields = inputs[c['name']] + [{0xCC0020: 0, 0x660046: 1, 0x330008: 4}[c['rop']], c['width'], c['height']]
        header.append('    {' + ','.join(map(str, fields)) + ', {' + ','.join(map(str, c['pixels'])) + '}},')
    header = '\n'.join(header + ['};', ''])
    header_path = path.with_suffix('.h')
    if args.check:
        if path.read_text(encoding='utf-8') != result or header_path.read_text(encoding='utf-8') != header:
            raise SystemExit('native GDI results differ from recorded fixtures')
        print(f"1024 axis maps, 360 grids and {generated['capture_configurations_verified']} capture cases match native memory-DIB results")
    else:
        path.write_text(result, encoding='utf-8', newline='\n')
        header_path.write_text(header, encoding='utf-8', newline='\n')


if __name__ == '__main__':
    main()
