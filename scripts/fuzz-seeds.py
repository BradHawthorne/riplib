#!/usr/bin/env python3
"""Export the CTest mutation fuzzer's directed seeds for libFuzzer.

Usage: python scripts/fuzz-seeds.py <output-directory>
No vendor content or DLL is required. Adjacent C string literals are joined.
"""
import argparse
import ast
from pathlib import Path
import re


def seeds(source):
    block = source.split('static const char *const seeds[] = {', 1)[1].split('};', 1)[0]
    block = re.sub(r'/\*.*?\*/', '', block, flags=re.S)
    literal = r'"(?:\\.|[^"\\])*"'
    for entry in re.findall(r'(?:' + literal + r'\s*)+', block):
        yield ''.join(ast.literal_eval(part) for part in re.findall(literal, entry)).encode('latin-1')


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('directory', type=Path)
    args = ap.parse_args()
    source = Path(__file__).resolve().parents[1] / 'tests/test_fuzz_seeded.c'
    corpus = list(seeds(source.read_text(encoding='utf-8')))
    if not corpus:
        raise SystemExit('no directed seeds extracted')
    args.directory.mkdir(parents=True, exist_ok=True)
    for i, seed in enumerate(corpus):
        (args.directory / f'seed-{i:03}').write_bytes(seed)
    print(f'Exported {len(corpus)} directed seeds')


if __name__ == '__main__':
    main()
