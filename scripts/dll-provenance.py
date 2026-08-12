#!/usr/bin/env python3
"""Reproduce the RIPSCRIP.DLL provenance analysis behind docs/spec/.

RIPlib's specification segments — especially 11-dll-deviations.md — rest on a
binary analysis of TeleGrafix's RIPSCRIP.DLL 3.0.7. This script re-derives that
evidence from the binary so every claim in the spec can be re-checked instead of
taken on trust, and so a "not found" result names which table was searched.

Usage:
    python scripts/dll-provenance.py <path-to-Ripscrip.dll> [-o OUTDIR]

The reference artifact ships in the RIPtel Visual Telnet 3.1 install. The
expected fingerprint is asserted below; a mismatch is reported, not silently
tolerated, because the addresses recorded in the spec are only valid for it.

Method mirrors the original, documented in docs/historical/ripscrip-v3-RE-notes.md:
  1. Export table enumeration
  2. String table extraction
  3. Error/assertion message cross-referencing
  4. Switch jump-table location (for dispatch analysis)

No third-party dependencies; stdlib struct/re only.
"""
import argparse
import json
import hashlib
import re
import struct
import sys
from pathlib import Path

EXPECTED = {
    "size": 592896,
    "md5": "bade8b1f4e467ac7ad4edb2639738d4c",
    "image_base": 0x10000000,
    "exports": 153,
    "build_path_fragments": ["rip3", "dll32"],
}

# Landmarks recorded by the original analysis. Re-validated on every run so a
# drifting or wrong binary is caught immediately.
LANDMARKS = {
    "ripParseStateMachine": 0x10039E90,
    "parse_state_jump_table": 0x1003AB9C,
    "ripCmd_MouseRegion": 0x1000A964,
}


class PE:
    def __init__(self, data):
        self.d = data
        e = struct.unpack_from("<I", data, 0x3C)[0]
        if data[e:e + 4] != b"PE\0\0":
            raise ValueError("not a PE image")
        coff = e + 4
        self.machine, nsec = struct.unpack_from("<HH", data, coff)
        self.timestamp = struct.unpack_from("<I", data, coff + 4)[0]
        optsz = struct.unpack_from("<H", data, coff + 16)[0]
        opt = coff + 20
        self.magic = struct.unpack_from("<H", data, opt)[0]
        self.image_base = struct.unpack_from("<I", data, opt + 0x1C)[0]
        self.export_rva, self.export_size = struct.unpack_from("<II", data, opt + 0x60)
        self.sections = []
        so = opt + optsz
        for i in range(nsec):
            o = so + i * 40
            name = data[o:o + 8].rstrip(b"\0").decode("ascii", "replace")
            vsize, vaddr, rawsize, rawptr = struct.unpack_from("<IIII", data, o + 8)
            self.sections.append(
                dict(name=name, rva=vaddr, vsize=vsize, raw=rawptr, rawsize=rawsize)
            )
        self.text = next(s for s in self.sections if s["name"] == ".text")

    def sec_of_rva(self, rva):
        for s in self.sections:
            if s["rva"] <= rva < s["rva"] + max(s["vsize"], s["rawsize"]):
                return s
        return None

    def va_to_off(self, va):
        s = self.sec_of_rva(va - self.image_base)
        if not s:
            return None
        return (va - self.image_base) - s["rva"] + s["raw"]

    def off_to_va(self, off):
        for s in self.sections:
            if s["raw"] <= off < s["raw"] + s["rawsize"]:
                return self.image_base + s["rva"] + (off - s["raw"])
        return None

    def sec_name(self, va):
        s = self.sec_of_rva(va - self.image_base)
        return s["name"] if s else "?"

    def in_text(self, va):
        t = self.text
        return self.image_base + t["rva"] <= va < self.image_base + t["rva"] + t["vsize"]


def verify(pe, data, report):
    md5 = hashlib.md5(data).hexdigest()
    frags = [f for f in EXPECTED["build_path_fragments"]
             if data.find(f.encode()) >= 0]
    ok = (len(data) == EXPECTED["size"] and md5 == EXPECTED["md5"]
          and pe.image_base == EXPECTED["image_base"])
    report["fingerprint"] = dict(
        size=len(data), size_ok=len(data) == EXPECTED["size"],
        md5=md5, md5_ok=md5 == EXPECTED["md5"],
        image_base=f"0x{pe.image_base:08x}",
        machine=f"0x{pe.machine:04x}", pe32=pe.magic == 0x10B,
        timestamp=f"0x{pe.timestamp:08x}",
        build_path_fragments_found=frags,
        verified=ok,
    )
    return ok


def exports(pe, data):
    eo = pe.va_to_off(pe.image_base + pe.export_rva)
    nfunc, nname = struct.unpack_from("<II", data, eo + 0x14)
    addr_rva, names_rva, ord_rva = struct.unpack_from("<III", data, eo + 0x1C)
    ao, no, oo = (pe.va_to_off(pe.image_base + x) for x in (addr_rva, names_rva, ord_rva))
    out, name_string_vas = {}, set()
    for i in range(nname):
        nr = struct.unpack_from("<I", data, no + i * 4)[0]
        s = pe.va_to_off(pe.image_base + nr)
        nm = data[s:data.index(b"\0", s)].decode("ascii", "replace")
        ordi = struct.unpack_from("<H", data, oo + i * 2)[0]
        fr = struct.unpack_from("<I", data, ao + ordi * 4)[0]
        out[nm] = f"0x{pe.image_base + fr:08x}"
        name_string_vas.add(pe.image_base + nr)
    return out, name_string_vas, nfunc


def rip_names(pe, data, export_string_vas):
    """Classify every RIP_* string: export-table name vs internal (command) name."""
    t = pe.text
    tlo, thi = t["raw"], t["raw"] + t["rawsize"]
    rows = []
    for m in re.finditer(rb"RIP_[A-Za-z0-9_]{2,40}\x00", data):
        va = pe.off_to_va(m.start())
        if va is None:
            continue
        needle = struct.pack("<I", va)
        xrefs, st = [], tlo
        while True:
            i = data.find(needle, st, thi)
            if i < 0:
                break
            if i >= 1 and data[i - 1] == 0x68:      # push imm32
                xrefs.append(pe.off_to_va(i - 1))
            st = i + 1
        rows.append(dict(
            name=m.group()[:-1].decode(), va=f"0x{va:08x}", section=pe.sec_name(va),
            is_export_name=va in export_string_vas,
            code_xrefs=[f"0x{x:08x}" for x in xrefs],
        ))
    rows.sort(key=lambda r: r["name"])
    return rows


def assertion_strings(pe, data):
    """Recover `module.cpp - Func()` assertion strings and their code references."""
    out = []
    for m in re.finditer(rb"[A-Za-z0-9_]{2,24}\.(?:cpp|c) - ([A-Za-z_][A-Za-z0-9_]{2,40})\(\)", data):
        s = m.start()
        while s > 0 and 0x20 <= data[s - 1] < 0x7F:
            s -= 1
        va = pe.off_to_va(s)
        if va is None:
            continue
        needle = b"\x68" + struct.pack("<I", va)
        t = pe.text
        xrefs, st = [], t["raw"]
        while True:
            i = data.find(needle, st, t["raw"] + t["rawsize"])
            if i < 0:
                break
            xrefs.append(f"0x{pe.off_to_va(i):08x}")
            st = i + 1
        out.append(dict(text=data[s:m.end()].decode("ascii", "replace"),
                        func=m.group(1).decode(), va=f"0x{va:08x}", code_xrefs=xrefs))
    return out


MODRM = {0x85: "eax", 0x8D: "ecx", 0x95: "edx", 0x9D: "ebx", 0xB5: "esi", 0xBD: "edi"}


def jump_tables(pe, data):
    """Locate `jmp dword ptr [reg*4 + disp32]` switch tables.

    `entries` is an UPPER BOUND: the walk stops at the first value that is not a
    .text address, so adjacent tables inflate it. The `cmp` immediately preceding
    the jmp gives the true case count when present.
    """
    t = pe.text
    out, i, end = [], t["raw"], t["raw"] + t["rawsize"]
    while i < end - 7:
        if data[i] == 0xFF and data[i + 1] == 0x24 and data[i + 2] in MODRM:
            disp = struct.unpack_from("<I", data, i + 3)[0]
            toff = pe.va_to_off(disp)
            if toff is not None:
                n = 0
                while toff + n * 4 + 4 <= len(data) and n <= 400:
                    if not pe.in_text(struct.unpack_from("<I", data, toff + n * 4)[0]):
                        break
                    n += 1
                if n >= 3:
                    pre = data[max(0, i - 40):i]
                    cmp_imm = None
                    for k in range(len(pre) - 2):
                        if pre[k] == 0x83 and 0xF8 <= pre[k + 1] <= 0xFF:
                            cmp_imm = pre[k + 2]
                    out.append(dict(
                        jmp_va=f"0x{pe.off_to_va(i):08x}", table_va=f"0x{disp:08x}",
                        index_reg=MODRM[data[i + 2]], entries_upper_bound=n,
                        preceding_cmp_imm=cmp_imm,
                        cases=(cmp_imm + 1) if cmp_imm is not None else None,
                    ))
            i += 7
            continue
        i += 1
    out.sort(key=lambda x: -x["entries_upper_bound"])
    return out


def check_landmarks(pe, tables):
    res = {}
    tvas = {t["table_va"] for t in tables}
    for name, va in LANDMARKS.items():
        s = pe.sec_of_rva(va - pe.image_base)
        entry = dict(va=f"0x{va:08x}", section=s["name"] if s else "UNMAPPED",
                     file_offset=(f"0x{pe.va_to_off(va):06x}" if s else None))
        if f"0x{va:08x}" in tvas:
            t = next(x for x in tables if x["table_va"] == f"0x{va:08x}")
            entry.update(found_as_jump_table=True, jmp_site=t["jmp_va"], cases=t["cases"])
        res[name] = entry
    return res


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("dll", help="path to Ripscrip.dll (RIPtel 3.1 install)")
    ap.add_argument("-o", "--outdir", default="dll-provenance",
                    help="directory for the generated JSON dataset")
    args = ap.parse_args()

    data = Path(args.dll).read_bytes()
    pe = PE(data)
    report = {"source": str(args.dll)}

    ok = verify(pe, data, report)
    exp, exp_vas, nfunc = exports(pe, data)
    report["exports"] = dict(count=len(exp), count_matches_record=len(exp) == EXPECTED["exports"])
    names = rip_names(pe, data, exp_vas)
    internal = [r for r in names if not r["is_export_name"]]
    report["rip_names"] = dict(total=len(names), export_table=len(names) - len(internal),
                               internal_candidates=len(internal))
    asserts = assertion_strings(pe, data)
    report["assertion_strings"] = len(asserts)
    tables = jump_tables(pe, data)
    report["switch_tables"] = len(tables)
    report["landmarks"] = check_landmarks(pe, tables)
    report["sections"] = [
        dict(name=s["name"], va=f"0x{pe.image_base + s['rva']:08x}",
             vsize=f"0x{s['vsize']:06x}") for s in pe.sections
    ]

    out = Path(args.outdir)
    out.mkdir(parents=True, exist_ok=True)
    for fn, obj in (("report.json", report), ("exports.json", exp),
                    ("rip-names.json", names), ("internal-names.json", internal),
                    ("assertion-strings.json", asserts), ("jump-tables.json", tables)):
        (out / fn).write_text(json.dumps(obj, indent=1))

    print(f"fingerprint verified : {ok}")
    print(f"  size {len(data)} md5 {report['fingerprint']['md5']}")
    print(f"exports              : {len(exp)} (record says {EXPECTED['exports']})")
    print(f"RIP_* strings        : {len(names)}  export-names {len(names)-len(internal)}"
          f"  internal {len(internal)}")
    print(f"assertion strings    : {len(asserts)}")
    print(f"switch jump tables   : {len(tables)}")
    for k, v in report["landmarks"].items():
        extra = f" jmp={v['jmp_site']} cases={v['cases']}" if v.get("found_as_jump_table") else ""
        print(f"landmark {k:<24}: {v['section']}{extra}")
    print(f"\ndataset written to {out}/")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
