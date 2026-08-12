#!/usr/bin/env python3
"""corpus-scan.py -- command-frequency census over a directory of .RIP files.

Walks every !| frame, splits on '|', records the opcode (with level prefix
where present).  Reports a frequency table so implementation effort can be
aimed at what real TeleGrafix content actually uses.

Usage: python scripts/corpus-scan.py <dir-of-rip-files>
"""
import sys, os, glob, collections

def scan(path):
    hits = collections.Counter()
    data = open(path, "rb").read().decode("latin-1")
    for line in data.splitlines():
        if not line.startswith("!|"):
            continue
        for chunk in line[2:].split("|"):
            if not chunk:
                continue
            c = chunk[0]
            if c in "123" and len(chunk) > 1:      # level prefix
                hits[c + chunk[1]] += 1
            else:
                hits[c] += 1
    return hits

def main():
    root = sys.argv[1] if len(sys.argv) > 1 else "."
    seen, files = set(), []
    for pat in ("*.RIP", "*.rip"):          # case-insensitive filesystems match
        for f in glob.glob(os.path.join(root, "**", pat), recursive=True):
            k = os.path.normcase(os.path.abspath(f))
            if k not in seen:               # both patterns; dedupe by real path
                seen.add(k); files.append(f)
    files.sort()
    total, per_file = collections.Counter(), {}
    for f in files:
        h = scan(f)
        per_file[f] = h
        total.update(h)

    print(f"{len(files)} files, {sum(total.values())} command instances, "
          f"{len(total)} distinct opcodes\n")
    print(f"{'op':<5}{'count':>7}  {'files':>5}  bar")
    for op, n in total.most_common():
        nf = sum(1 for h in per_file.values() if op in h)
        print(f"{op:<5}{n:>7}  {nf:>5}  {'#' * min(40, n // 4 + 1)}")
    return total, per_file

if __name__ == "__main__":
    main()
