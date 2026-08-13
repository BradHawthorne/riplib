# Syntax audit: RIPlib and bbs-land against the driver

**Date:** 2026-08-12 · **RIPlib:** v2.0.1 · **bbs-land:** `remote-imaging-protocol@main`
**Arbiter:** `RIPSCRIP.DLL`, 592,896 bytes, MD5 `bade8b1f4e467ac7ad4edb2639738d4c`, from a RIPtel 3.1 install

This audit answers one question: **where do the two projects disagree about the shape of a command, and who is right?** Not the names — those were reconciled in v2.0.0 — but the argument layouts: how many fields, how wide, in what order.

---

## Why a name comparison was not enough

The v2.0.0 work compared RIPlib's command *names* against bbs-land's reference and closed the gap from 14 disagreements to 2. That produced a comfortable number and a false sense of completeness.

A name comparison cannot see a command whose name is right and whose fields are wrong. `|k` was called `RIP_BACK_COLOR` by both projects and read one digit instead of two — correct name, wrong syntax, wrong colour on 132 uses across 22 shipped scenes.

The fix was to stop comparing the two projects to *each other* and compare each to the driver.

---

## The method, and why the driver is the arbiter

The driver dispatches every command through a table of 129 fixed-size records at RVA `0x080820`. Each record carries the command letter, an argument count, and a list of type bytes:

```
[+0]      index
[+1..4]   handler pointer
[+15]     command letter
[+16..19] argument count   (negative = variable length)
[+20..]   argument type codes
```

The type codes are the thing this audit turns on:

| code | meaning |
| --- | --- |
| `0x01` `0x02` `0x03` `0x04` … | a literal digit count |
| `0xFF` | a **coordinate** — width from `\|n` SET_COORDINATE_SIZE, 2 by default |
| `0xFE` | a **colour** — width from `\|M` SET_COLOR_MODE, 2 by default |

This is not an inference about the record's meaning. The driver resolves those codes at decode time in a routine at RVA `0x039DE0`:

```
t = argtype[i]
t >= 0    -> t                    literal digit count
t == 0xFF -> (state+2)->[0x39]    the byte SET_COORDINATE_SIZE writes
t == 0xFE -> (state+2)->[0x3a]    the byte SET_COLOR_MODE writes
```

Three state bytes sit adjacent — `+0x38` MegaNum radix, `+0x39` coordinate size, `+0x3a` colour mode — and the parser computes its dispatch entry as `index * 5 * 8 + 0x080820`, confirming both the table address and the 40-byte stride from the driver's own code rather than from anyone's notes.

So the record is machine-readable, complete for all 129 entries, and self-validating. It outranks both projects' documentation, and both projects already say so.

**One caveat, stated up front:** the record says what the driver *accepts*, not what any handler *does*. `|F` has a valid entry and a handler that is a bare `ret`. Where a handler contradicts its own record, the handler wins — that is how `|D`'s field order was settled below.

---

## Results

Each project's field lists were extracted and compared to the driver's, with three outcomes: exact match, **notation-only** (a literal `2` where the record says width-negotiated — identical at default settings, divergent the moment `|n` or `|M` changes a width), and genuinely different.

| | compared | exact | notation only | **different** |
| --- | --- | --- | --- | --- |
| **RIPlib** (after fixes) | 47 | 19 | 19 | **3** |
| **bbs-land** | 80 | 52 | 15 | **13** |

RIPlib's smaller comparable set is a limitation of the method, not a measure of coverage: field lists were read from handler comments, and not every handler spells one out.

---

## RIPlib's defects, and what the evidence was

Nine genuine differences were found. Six are fixed, one was a false alarm, three are recorded unresolved.

### Fixed in v2.0.1

**`|k` RIP_BACK_COLOR — read one digit instead of a colour-width field.**
Slot 43 types the argument `0xFE`. Reading a single digit made `|k04` set background **0** instead of 4, and `|k3K` set **3** instead of 128. **132 uses across 22 shipped scenes.** The only one of the nine with real rendering impact.

**`|=` RIP_LINE_STYLE — merged two fields into one.**
Slot 14 records `mega1, mega1, mega4, mega2` — four arguments. RIPlib read the leading two digits as a single `mega2` style. The handler validates `args[1] <= 4`, which is the BGI line-style range, identifying `args[1]` as the style and `args[0]` as a separate off/draw selector. Every shipped payload begins `00`, where the two readings coincide, so nothing rendered differently — but the field was silently discarded.

**`|D` RIP_SET_DRAWING_PALETTE — count and start swapped.** *Introduced by RIPlib in v2.0.0.*
Here the record alone was insufficient — it gives `mega2, mega2, mega1, mega4` without saying which `mega2` is which. The handler settles it. With `esp = E-0x420` at the checks:

```
args[0]  ->  count - argc == -3   "Invalid number of parameters"
             > 0x100              "More than 256 entries"
args[1]  ->  > 0xFF               "Start is out of range"
```

**Count comes first.** RIPlib had them reversed.

### Fixed in this audit

**`|3e` RIP_BAUD_EMULATION — read a `mega4` where the record says `mega2`.**
Slot 123 records one `mega2`. RIPlib preferred a `mega4` whenever four characters were available, reading two fields as one. *bbs-land documents `rate:4` as well* — that reading comes from the 2.0 draft, while the 3.0 driver's record says 2. **Both projects were wrong against the binary.**

**`|1I` RIP_LOAD_ICON — read a 2-digit mode over two 1-digit fields.**
Slot 97 records `FF FF 01 01 01 01 01`: two coordinates then **five single-digit fields**. RIPlib read `mega2(p+4)`, spanning the driver's `args[2]` and `args[3]`, which agrees only while `args[3]` is 0. The filename offset (9) was already correct, so only the mode decode changed.

### False alarm

**`|1i` RIP_ImageStyle.** The record says `n n n n 4 12` — 24 characters — and RIPlib reads 12. Every corpus payload is **exactly 24 characters**, and the 12-character tail is reserved. RIPlib reads the meaningful prefix and ignores the remainder. Correct as written.

### Recorded, not guessed (D-14)

Three disagreements are left in place because the correct reading cannot be established:

**`|1G` RIP_COPY_REGION.** Slot 95 records `FF FF FF FF 01 01 FF` — four coordinates, two single digits, then **one** further coordinate; twelve characters. RIPlib requires fourteen and reads a destination *pair* at offsets 10 and 12, citing the earlier reconstruction's "8 args". Only one trailing coordinate exists in the record, so a destination pair cannot be mapped onto it without inventing a field.

**`|:` RIP_MOUSE_REGION_EXT.** Slot 11 records argc 11 — ten coordinates and one digit, 21 characters. RIPlib requires 22 and reads six fields.

**`|1g` COPY_BLIT.** Slot 96 records argc 8 — six coordinates then two single digits. RIPlib reads seven fields and stops after the first trailing digit.

All three come from the original reconstruction rather than the dispatch record, all three disagree with it, and **none is exercised by any shipped scene** — which is precisely why they survived. A command no scene sends is a command no test can check. Replacing a coherent implementation with an uninterpretable one would be a downgrade, so they stay, recorded.

---

## bbs-land's divergences from the driver

Offered as evidence, not as a verdict on their record — several of these may be deliberate, sourced from the 1.54 specification or the 2.0 draft rather than from the 3.0 driver.

### The `Switch*` family — six commands, and the only ones that desync

| command | driver | their reference |
| --- | --- | --- |
| `\|2A` SwitchPalette | `1 2` | `2` |
| `\|2B` SwitchButtonStyle | `1 2` | `2` |
| `\|2E` SwitchEnvironment | `1 2` | `2` |
| `\|2T` SwitchTextWindow | `1 2` | `1 1` |
| `\|2Y` SwitchStyle | `1 2` | `1 1` |
| `\|2s` SwitchPort | `1 2` | `1 2 3` |

All six record `mega1 + mega2` — **three characters**. The corpus agrees: every `|2s` in it is three characters (`!|2s000`, `!|2s002`, `!|2s100`).

These matter more than the rest because the **totals** differ. A consumer following the six-character `|2s` layout over-consumes three bytes and desynchronises the remainder of the frame. The issue filed upstream covered `|2s` alone; it is six times wider than reported.

### Same total, different subdivision

`|1I`, `|1M`, `|1R`, `|1T`, `|1w`, `|2W`, `|3e` — the stream stays in sync, individual fields decode wrong. `|1R` is `2 6` against their `8`; `|1w` is `1 3` against their `4`.

### `|F` RIP_FILL

Their `x:XY y:XY border:CM` is the correct **wire** layout and RIPlib implements it. The record shows `argc=0` because the handler pointer `0x01B2FD` is a bare `ret` — the tail of the preceding function, with `0x01B2FE` (`|G`) being the real prologue. **The 3.0 driver stubs out flood fill.** This explains an anomaly rather than changing a signature.

### The notation class — 15 commands

`|"` `|&` `|+` `|-` `|;` `|U` `|[` `|]` `|_` `|g` `|u` `|w` `|1G` `|1e` `|1g` are documented with a literal `:2` where the record types the field as coordinate. Identical at default settings; wrong the moment `|n` selects another width.

**This is precisely the class of defect `|k` was on RIPlib's side** — a fixed width where the driver negotiates one. Same failure, opposite document.

---

## What this exercise says about method

**A comparison between two secondary sources measures agreement, not correctness.** Both projects can be wrong together, and on `|3e` both are.

**Names and syntax are independent.** `|k` had the right name in both projects and the wrong width in one.

**A test written from the implementation proves nothing.** The v2.0.0 `|D` test passed against swapped fields because its payload was authored to match the code rather than derived from the evidence. Every fix in this audit carries a regression test that fails against the old reading.

**Unexercised commands are where defects survive.** Eight of RIPlib's nine defects had zero corpus uses. The corpus is an excellent regression net for what it covers and silent about everything else.

**The record and the handler answer different questions.** The record says what is accepted; the handler says what happens. `|D`'s field order needed the handler; `|F`'s stub needed the handler; `|k`'s width needed only the record.

---

## Reproducing this

```sh
python scripts/dll-dispatch-table.py <path>/Ripscrip.dll   # the record, verbatim
python scripts/dll-argtypes.py       <path>/Ripscrip.dll   # width-negotiated commands
python scripts/dll-disasm.py         <path>/Ripscrip.dll 0x01f46a   # a handler, with imports resolved
python scripts/corpus-scan.py        <path-to-scenes>      # opcode census
```

Every script verifies the image fingerprint before reporting. Per-opcode adjudication is in [`docs/spec/12-dll-provenance.md`](../docs/spec/12-dll-provenance.md); the record itself is [`docs/spec/13-dll-command-table.md`](../docs/spec/13-dll-command-table.md). Findings sent upstream are [bbs-land issue #2](https://github.com/bbs-land/remote-imaging-protocol/issues/2).
