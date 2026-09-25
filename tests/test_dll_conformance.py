"""Regression tests for the conformance instrument; no proprietary DLL needed."""
import contextlib
import copy
import importlib.util
import io
import json
from pathlib import Path
import sys
import struct
import tempfile
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[1]
# Keep instrument tests from generating cache files in the source tree.
sys.dont_write_bytecode = True
sys.path.insert(0, str(ROOT / "scripts"))
SPEC = importlib.util.spec_from_file_location(
    "dll_conformance", ROOT / "scripts" / "dll-conformance.py")
CHECK = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CHECK)
SOURCE = (ROOT / "src" / "ripscrip.c").read_text(encoding="latin-1")


def fixture(body):
    return ("if (s->is_level3) {\n"
            "switch (s->cmd_char) { case 'G': break; }\n}\n"
            "if (s->is_level2) {}\n"
            "if (s->is_level1) {\n"
            "switch (s->cmd_char) { case 'U': break; }\n}\n"
            "/* Level 0 commands */\n"
            "switch (s->cmd_char) {\n" + body + "\n}\n")


class HandlerCoverageTests(unittest.TestCase):
    def port_instrument(self):
        spec = importlib.util.spec_from_file_location('port_fixture', ROOT / 'scripts/dll-port-fixtures.py')
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        data = json.loads((ROOT / 'tests/fixtures/port_calls.json').read_text(encoding='utf-8'))
        return module, data

    def test_full_port_fixtures_preserve_coordinate_and_dc_contracts(self):
        module, data = self.port_instrument()
        module.validate(data)

    def test_port_copy_boundaries_preserve_rejection_and_scaling(self):
        module, data = self.port_instrument()
        module.validate(data)
        cases = data['copy_edges']
        self.assertEqual(len(cases), 84)
        self.assertEqual(len({(c['name'], tuple(c['define_origin']), c['offscreen']) for c in cases}), 84)
        for c in cases:
            if c['name'].startswith(('reverse_', 'empty_')) or c['name'].endswith('_outside'):
                self.assertTrue(c['events'])
                self.assertTrue(all('error_code' in e for e in c['events']))
        shared = {c['name']: c['events'][0] for c in cases
                  if c['define_origin'] == [10, 20] and not c['offscreen']}
        self.assertEqual(shared['zero_dest']['StretchBlt'][1:5], [0, 0, 640, 400])
        self.assertEqual(shared['position_only']['BitBlt'][1:5], [30, 32, 2, 8])
        self.assertEqual(shared['source_right_native']['BitBlt'][3:5], [2, 8])
        self.assertEqual(shared['source_right_scaled']['StretchBlt'][3:5], [8, 8])
        self.assertEqual(shared['source_right_scaled']['StretchBlt'][8:10], [2, 8])
        for mutation in ('error', 'empty', 'duplicate', 'surface'):
            broken = copy.deepcopy(data)
            case = broken['copy_edges'][0]
            if mutation == 'error': case['events'] = [{'error_code': 30}]
            elif mutation == 'empty': case['events'] = []
            elif mutation == 'duplicate': broken['copy_edges'][1] = case
            else: next(iter(case['events'][0].values()))[0] = 999
            with self.subTest(mutation=mutation), self.assertRaises(ValueError):
                module.validate(broken)

    def test_port_fixture_checker_rejects_origin_surface_and_matrix_mutations(self):
        module, original = self.port_instrument()
        for mutation in ('origin', 'surface', 'capture', 'duplicate'):
            data = copy.deepcopy(original)
            case = next(c for c in data['load_icon']
                        if c['define_origin'] == [10, 20] and not c['offscreen'] and c['args'][4])
            if mutation == 'origin': case['events'][0]['display'][0] -= 10
            elif mutation == 'surface': case['events'][-1]['StretchBlt'][5] = 201
            elif mutation == 'capture': case['events'][-1]['StretchBlt'][6] -= 10
            else: data['port_copy'][0] = data['port_copy'][1]
            with self.subTest(mutation=mutation), self.assertRaises(ValueError):
                module.validate(data)

    def test_raster_fixtures_keep_source_not_and_capture_extents(self):
        data = json.loads((ROOT / 'tests/fixtures/raster_calls.json').read_text(encoding='utf-8'))
        rops = [0xCC0020, 0x660046, 0xEE0086, 0x8800C6, 0x330008, 0xCC0020]
        self.assertEqual(data['icon_rops'], rops)
        self.assertEqual(len(data['port_rops']), 12)
        for case in data['port_rops']:
            call = 'StretchBlt' if case['scaled'] else 'BitBlt'
            self.assertEqual(case['events'][0][call][-1], rops[case['mode']])
        captures = {c['name']: c['events'] for c in data['captures']}
        self.assertEqual(captures['native'][1], {'allocation': [3, 8]})
        self.assertEqual(captures['stretched'][1], {'allocation': [3, 9]})
        self.assertEqual(captures['right_bottom_clip'][2]['StretchBlt'],
                         [102, 0, 0, 5, 8, 101, 638, 398, 2, 2, 0xCC0020])
        self.assertFalse(any('StretchBlt' in e for e in captures['outside']))

    def test_directed_fuzz_seeds_keep_adjacent_literals_and_escapes(self):
        spec = importlib.util.spec_from_file_location('fuzz_seeds', ROOT / 'scripts/fuzz-seeds.py')
        exporter = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(exporter)
        source = r'''static const char *const seeds[] = {
            /* ESC must remain separate from following hexadecimal digits. */
            "!|2\x1b" "0000CACHE|",
            "!|Tslash\\pipe\"quote|", "!|e\r\n",
        };'''
        self.assertEqual(list(exporter.seeds(source)),
                         [b'!|2\x1b0000CACHE|', b'!|Tslash\\pipe"quote|', b'!|e\r\n'])

    def run_check(self, function, *args):
        with contextlib.redirect_stdout(io.StringIO()) as output:
            result = function(*args)
        return result, output.getvalue()

    def test_button_tail_is_not_truncated(self):
        bodies = list(CHECK.handler_bodies(SOURCE.splitlines()))
        button = next(body for lvl, ch, _, body in bodies if (lvl, ch) == (1, "U"))
        self.assertIn("mega2(p + 8)", button)
        self.assertIn("mega_digit(p[10])", button)
        self.assertIn("s->num_mouse_regions++", button)
        self.assertNotIn("rip_clipboard_capture", button)

    def test_shifted_button_hotkey_is_caught_after_old_cutoff(self):
        before = "r->hotkey = (uint8_t)(mega2(p + 8) & 0xFF);"
        self.assertEqual(SOURCE.count(before), 1)
        broken = SOURCE.replace(before, before.replace("p + 8", "p + 9"))
        # DLL slot 107: XY XY XY XY mega2 mega1 mega1 (D-15).
        sigs = {(1, "U"): [[2, 2, 2, 2, 2, 1, 1]]}
        self.assertEqual(self.run_check(CHECK.check_offsets, sigs,
                                       SOURCE.splitlines(), False)[0], 0)
        result, output = self.run_check(CHECK.check_offsets, sigs,
                                       broken.splitlines(), False)
        self.assertEqual(result, 1)
        self.assertIn("9:2", output)

    def test_historical_image_style_gate_is_caught(self):
        bodies = list(CHECK.handler_bodies(SOURCE.splitlines()))
        _, _, line, _ = next(b for b in bodies if b[:2] == (1, "i"))
        lines = SOURCE.splitlines()
        gate = next(i for i in range(line, len(lines)) if "if (len >= 24)" in lines[i])
        sigs = {(1, "i"): [[2, 2, 2, 2, 4, 4, 4, 4]]}
        self.assertEqual(self.run_check(CHECK.check_gates, sigs, lines, False)[0], 0)
        lines[gate] = lines[gate].replace("len >= 24", "len >= 12")
        self.assertEqual(self.run_check(CHECK.check_gates, sigs, lines, False)[0], 1)

    def test_nested_labels_comments_and_quoted_braces(self):
        source = fixture("""case 'A':
        const char *name = "} case 'Z': /* quoted */";
        // } case 'Y':
        /* } case 'X': */
        switch (mode) { case 'B': break; default: break; }
        if (protected) {
        break;
        }
        mega2(p + 2);
        break;
        default: mega2(p + 90); break;""")
        bodies = [b for b in CHECK.handler_bodies(source.splitlines()) if b[0] == 0]
        self.assertEqual([b[1] for b in bodies], ["A"])
        self.assertIn("mega2(p + 2)", bodies[0][3])
        self.assertNotIn("mega2(p + 90)", bodies[0][3])

    def test_numeric_cases_and_switch_end(self):
        source = fixture("case 0x60: mega_digit(p[20]); break;\n"
                         "case 65: break;") + "switch (unrelated) { case 'Z': break; }"
        bodies = [b for b in CHECK.handler_bodies(source.splitlines()) if b[0] == 0]
        self.assertEqual([b[1] for b in bodies], ["`", "A"])
        sigs = {(0, "`"): [[2] * 10 + [1]]}
        self.assertEqual(self.run_check(CHECK.check_offsets, sigs,
                                       source.splitlines(), False)[0], 0)
        broken = source.replace("p[20]", "p[40]")
        self.assertEqual(self.run_check(CHECK.check_offsets, sigs,
                                       broken.splitlines(), False)[0], 1)

    def test_level2_defines_retain_numeric_esc(self):
        header = "#define RIP2_CMD_SWITCH_DIRECTORY 0x1B\n#define RIP2_CMD_SET_REFRESH 'R'\n"
        self.assertEqual(CHECK.level2_defines(header),
                         {'RIP2_CMD_SWITCH_DIRECTORY': '\x1b', 'RIP2_CMD_SET_REFRESH': 'R'})

    def test_padding_does_not_change_coverage(self):
        plain = fixture("case 'A': mega2(p); break;")
        padded = "\n" * 200 + plain.replace("mega2(p)", "\n" * 200 + "mega2(p)")
        for source in (plain, padded):
            bodies = list(CHECK.handler_bodies(source.splitlines()))
            self.assertEqual([(b[0], b[1]) for b in bodies], [(3, "G"), (1, "U"), (0, "A")])
            self.assertIn("mega2(p)", bodies[-1][3])

    def test_incomplete_or_unrecognised_structure_fails_closed(self):
        plain = fixture("case 'A': break;")
        for broken in (plain.rsplit("}", 1)[0],
                       plain.replace("case 'A'", "case UNKNOWN"),
                       plain.replace("case 'A':", "case 'A': case 65:"),
                       plain.replace("s->cmd_char", "other", 1)):
            with self.subTest(source=broken), self.assertRaises(SystemExit):
                list(CHECK.handler_bodies(broken.splitlines()))

    def test_main_exits_nonzero_for_a_finding(self):
        source = fixture("case 0x60: if (len >= 21) mega_digit(p[40]); break;")
        sigs = {(0, "`"): [[2] * 10 + [1]]}
        meta = {(0, "`"): (83, 11, 3)}
        with mock.patch.object(CHECK, "load", return_value=(b"", [])), \
             mock.patch.object(CHECK, "read_table", return_value=(sigs, meta)), \
             mock.patch.object(CHECK, "dispatch_rows", return_value=[
                 dict(slot=83, level=0, letter=96, handler=0x10001000,
                      argc=11, radix=3, types=[2] * 10 + [1])]), \
             mock.patch("builtins.open", mock.mock_open(read_data=source)), \
             mock.patch("sys.argv", ["dll-conformance.py", "unused.dll"]):
            result, output = self.run_check(CHECK.main)
        self.assertEqual(result, 1)
        self.assertIn("FAIL: 1 defect(s)", output)

    def test_dispatch_retains_esc_at_all_levels(self):
        image = bytearray(129 * 40)
        for slot in range(129):
            offset = slot * 40
            image[offset + 5] = 0 if slot<85 else ord("1") if slot<110 else ord("2") if slot<122 else ord("3") if slot<124 else ord("9")
            image[offset + 15] = 27 if slot in (85, 110, 124) else ord('A')
            struct.pack_into('<I', image, offset + 1, 0x10001000)
            struct.pack_into('<i', image, offset + 16, 1)
            image[offset + 20] = 2
        sections = [(0x80820, len(image), 0, len(image))]
        rows = CHECK.dispatch_rows(image, sections)
        _, meta = CHECK.read_table(image, sections)
        for level in (1, 2, 9):
            self.assertIn((level, '\x1b'), meta)
        self.assertEqual(self.run_check(CHECK.check_dispatch_accounting, rows, meta, False)[0], 0)
        # Re-inject the historical printable-only filter: exactly three losses.
        filtered = {k: v for k, v in meta.items() if k[1].isprintable()}
        result, output = self.run_check(CHECK.check_dispatch_accounting, rows, filtered, False)
        self.assertEqual(result, 3)
        self.assertIn('|2<0x1B>', output)

    def test_continuation_owner_and_duplicate_key_are_accounted(self):
        rows = [dict(slot=0, level=0, letter=65, handler=1),
                dict(slot=1, level=0, letter=66, handler=2),
                dict(slot=2, level=0, letter=0, handler=1),
                dict(slot=3, level=0, letter=65, handler=3)]
        meta = {(0, 'A'): (0, 1, 1), (0, 'B'): (1, 1, 1)}
        result, output = self.run_check(CHECK.check_dispatch_accounting, rows, meta, False)
        self.assertEqual(result, 0)
        self.assertIn('1 duplicate named row(s)', output)
        rows[2]['handler'] = 99
        self.assertEqual(self.run_check(CHECK.check_dispatch_accounting, rows, meta, False)[0], 1)

    def test_prefix_is_read_from_record_not_inferred_from_slot(self):
        image = bytearray(129 * 40)
        for slot in range(129):
            offset = slot * 40
            image[offset+15] = ord('D')
            struct.pack_into('<I',image,offset+1,0x10001000)
        sections = [(0x80820,len(image),0,len(image))]
        image[5:7] = b'9\0'
        image[125*40+5:125*40+7] = b'3\0'
        rows = CHECK.dispatch_rows(image,sections)
        self.assertEqual((rows[0]['level'],rows[125]['level']), (9,3))
        image[5] = ord('x')
        with self.assertRaises(ValueError):
            CHECK.dispatch_rows(image,sections)

    def test_short_dispatch_table_fails_closed(self):
        with self.assertRaises(SystemExit):
            CHECK.dispatch_rows(b'\0' * 40, [(0x80820, 40, 0, 40)])

    def test_reference_retains_esc_level9_unlinked_and_unassigned(self):
        spec = importlib.util.spec_from_file_location('ref_compare', ROOT / 'scripts/ref-compare.py')
        reference = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(reference)
        table = ("| Symbol | Level | Cmd | Arguments | Status | Purpose |\n"
                 "| [RIP_QUERY](query.md) | 1 | `<esc>` | `mode:1 flags:1 res:2 text` | | query |\n"
                 "| [RIP_STREAM](stream.md) | 9 | `U` | `type:2` | | stream |\n"
                 "| 1O (unidentified) | 1 | `O` | `x:2 y:2` | | unknown |\n"
                 "| RIP_SwitchDirectory | 2 (§) | - | - | HLP only | unknown |\n"
                 "| [RIP_BASE](base.md) | 0 | `J` (\\*) | `base:2` | | a\\|b |\n")
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'reference.md'
            path.write_text(table, encoding='utf-8')
            rows = reference.reference_rows(path)
            self.assertEqual(len(rows), 5)
            keyed = reference.load_reference(path)
            self.assertEqual(set(keyed), {(1, '\x1b'), (9, 'U'), (1, 'O'), (0, 'J')})
            self.assertEqual(sum(r['key'] is None for r in rows), 1)
            report = CHECK.crosswalk_markdown([], {}, b'', None, 'test', path, 'a' * 40)
            self.assertIn('All 5 inventory rows retained: 4 keyed opcodes and 1 names', report)
            self.assertIn('RIP_SwitchDirectory', report)
            self.assertIn('&#124;9U', report)


if __name__ == "__main__":
    unittest.main()
