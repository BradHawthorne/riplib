# Changelog

All notable changes to RIPlib are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [2.0.3] — unreleased

Patch release. Resolves the three argument layouts 2.0.2 recorded as
unresolved, by disassembling their handlers rather than reasoning from the
dispatch record alone — and, in doing so, finds three further defects in the
mouse-region and button path. All nine of the audit's genuine disagreements
are now settled; see [design/syntax-audit.md](design/syntax-audit.md) and
D-14/D-15 in [docs/spec/12-dll-provenance.md](docs/spec/12-dll-provenance.md).

### Fixed

- **`|1G` is `RIP_Scroll`, not `RIP_COPY_REGION`.** The handler names itself
  in its own diagnostics, and `RIP_COPY_REGION` is `|,` — the name had been
  on two commands at once. The move is `OffsetRect(&r, 0, dest_y - y0)` with
  `dx` a hardcoded zero: vertical only, with no destination X field. RIPlib
  read 14 characters against the record's 12 and invented a destination pair.
  Now `x0 y0 x1 y1 mode:1 excl:1 dest_y`.
- **`|:` `RIP_MOUSE_REGION_EXT` is five vertices**, not a rectangle with a
  hotkey and flags. RIPlib required 22 characters against the record's 21, so
  **every valid command was dropped in full**. Now registers the bounding box
  of the five vertices.
- **`|1g` `RIP_CopyBlit`** gated on 12 characters instead of 14 and treated
  the mode digit as optional, so a truncated command still blitted; and it
  discarded inverted source rects that the driver orders. Both corrected.
- **`|1M` `RIP_Mouse` read two 1-digit flags as one 2-digit hotkey.** The
  record, the handler and the 1.54 specification agree the fields are `clk`
  and `clr` (`invertable`/`resetafter`); RIPlib glued them into a hotkey and
  then took its flag bits from a *reserved* column. Across the 36 commands in
  22 shipped scenes that column is uniformly `'0'`, so the hotkey was always
  the constant 36 and the flags were always 0. `RIP_MOUSE` has no hotkey
  field. Host command text was unaffected — the offset was and remains 17.
- **`|1U` `RIP_Button` parsed its hotkey and flags and discarded both**,
  leaving the `SEND_CHAR`/`RADIO`/`TOGGLE` dispatch unreachable from any
  command. Now wired to the fields the record, the spec and bbs-land all
  agree carry them.
- **`|1U` buttons never became clickable.** Region registration was gated on
  a non-empty host command, and all 39 buttons in the shipped corpus carry an
  empty one (`<>Clear<>`). The region is now registered regardless; dispatch
  already guards on `text_len` before sending.
- **`|3G` `RIP_GotoURL` folded 8 reserved digits onto the front of every
  URL.** The record's fixed prefix is the offset a trailing string starts at,
  and this one is 8. RIPlib read from 0. It launches nothing — the `SV-2/S2`
  neutering stands — but it handed the embedder a URL pointing somewhere
  other than the one sent, and quietly: a reserved field of *digits* passes
  the character validation, so a **wrong** URL was stored rather than none.
- **`|3R` prefixed every registered variable name with 8 stray digits**
  (record `mega4 + mega2 + 8` = 14; RIPlib read at 6), so no name a scene
  registered could ever be matched.
- **`|1i` `RIP_ImageStyle` gated on 12 characters against a 24-character
  record.** Ignoring the 12-digit reserved tail is correct; acting on a
  command carrying only the prefix is not — the same defect class as `|1g`.
- **`|3e` `RIP_BAUD_EMULATION` preferred a `mega4`** where the record says
  `mega2` and the handler loads exactly one argument. This was previously
  documented as fixed when it was in fact an accept-both compromise; it now
  matches the arbiter. No corpus scene sends `|3e`.

With these, RIPlib's field lists stand at **zero disagreements** against the
driver's dispatch record — 26 exact, 21 notation-only, and 4 that match the
record exactly and add a documented string tail (which the record never
expresses, since strings are passed out-of-band). See D-16.

Measuring the *coverage* of that comparison then showed it had never included
Level 2 at all — `ripscrip.c` delegates the whole level to `ripscrip2.c`,
whose handlers are keyed on `RIP2_CMD_*` constants rather than character
literals, so eleven commands were invisible to every pass. Auditing them:

- **`|2P` `RIP_PortDefine` read the wrong half of its flags field.** Slot 115
  types it `mega4` spanning offsets 9–12; RIPlib read `mega2l(raw + 9)`, the
  **high** two digits. MegaNum is big-endian, so a flags word small enough to
  be a bit-set lives entirely in the trailing digits. All three `|2P`
  commands in the corpus carry `"0001"`, and RIPlib decoded every one as
  **0** — no `|2P` could ever set a port flag, including "make active
  immediately".
- **Loose length gates on seven Level 2 commands**, the same class as `|1g`
  and `|1i`: the whole `Switch*` family plus `|2s` record 3 characters and
  gated on 1; `|2p` records 4 and gated on 1.
- Verified correct and recorded as checked: `|2C` `RIP_PortCopy` matches slot
  113 field for field, and `|2R` reads its `mega4` at the right width.

Three of the tests that had to change were carrying payloads authored against
the defective readings — `|2P...0200` encodes 2 only if the flags field is
two digits at offset 9. See D-17.

Finally, a second audit instrument was added that checks **what the code
reads** rather than what its comment claims — rebuilding each handler's real
offset/width layout from its accessor calls and testing that against the
record's field boundaries. It covers 68 commands and 269 individual reads,
where the comment-based comparison could only reach 51. It found:

- **`|3D` `RIP_DELAY` fell back to a `mega2`** below four characters, though
  slot 122 records a single `mega4` — the same leniency removed from `|3e`.
  Removed; no corpus scene sends `|3D`.
- **`|k`'s single-digit fallback was removed and then restored.** The corpus
  contradicted the assumption: of 133 `|k` commands in shipped scenes, 132
  are two characters and one (`N2_BUSI.RIP`, `"|k0"`) is one. Matching the
  record exactly would drop a command real content sends, for no gain. It is
  now a documented tolerance the audit names rather than an unexamined
  fallback.

**A comment that was wrong twice over — and the check that should have caught
it.** A note above the Level 0 Line case claimed `'@' = RIP_PIXEL` and that
`'X'` "is not in the DLL command table". Both are false: slot 16's handler
names itself `RIP_TextXY()`, and `'X'` is slot 70 with a handler calling
`GDI32!SetPixel`. It also described a `case '@'` that wasn't beneath it. The
**code was right throughout** — `'@'` has always been `RIP_TEXT_XY` and `'X'`
`RIP_PIXEL`; only the note was wrong.

That is the *fourth* documentation defect of one shape in a single day (`|1I`,
`|y`, `|3e`, `|@`), every one found by accident while looking for something
else — and the `|3e` paragraph was quoted back as an open item in a status
report hours after the code had stopped matching it. Prose about code doesn't
notice when the code changes, and a stale conclusion reads as *more*
authoritative than the behaviour it misdescribes.

New: [`scripts/dll-validate-claims.py`](scripts/dll-validate-claims.py) —
adversarial by construction. It states each load-bearing claim as a predicate
and tries to **refute** it from the image, the corpus and the source: handler
self-naming, the fixed-radix sets, every string-tail prefix width, the corpus
population figures, and what the code now does, including negatives. A claim it
cannot re-derive is reported UNVERIFIED rather than passed. 29 claims, all
holding; verified to fail by re-injecting the `|3e` compromise. See D-27.

The `|1R`/`|1b` fixes confirmed against shipped content rather than authored
payloads. Replaying DRAGON.RIP through v2.0.2 and through the current tree and
printing the asset names it asks the host for:

| v2.0.2 | now |
| --- | --- |
| `0000STRIP6` | `STRIP6` |
| `0000GODRAG3` | `GODRAG3` |
| `0000TORCH` | `TORCH` |
| `00000000DRAG` | `DRAGON` |
| `0000BACK` | `BACK` |

Five requests, every one wrong before and right after — `00000000DRAG` is what
`dragon.txt` became once eight reserved digits were prepended and the result
hit the name limit, losing the real name entirely.

Across all 35 scenes, v2.0.2 vs now: **zero** differences in foreground pixels
or colour counts, and the same 61 asset requests. Everything landed in
non-rendering paths, exactly where the pixel metrics could not see it.

One further defect found, diagnosed in full, and deliberately **not** fixed
yet: BUTTONS.RIP and CURVES.RIP send a `|1R` whose filename is a
`<<IF $COLORS$…>>` conditional, and RIPlib requests it literally.

The scope question — which payloads expand, and when — is answered by a case
distinction the corpus makes cleanly. **Uppercase is a directive; lowercase is
literal text.** All 14 `<<IF>>` uses sit in `|1R` payloads selecting a file by
colour depth; all 19 `<<if>>` uses sit in `|1M` host-command text the *host*
evaluates on click. No uppercase directive appears in host text and no
lowercase one in a filename. The driver's own diagnostics are uppercase, and
RIPlib already matches case-sensitively — so its recognition is right.

Two things stop it working: the preprocessor runs only in `RIP_ST_IDLE`, so it
never sees bytes inside a command; and an unrecognised directive is
*swallowed* rather than emitted, so naively extending it would eat `|1M`'s
lowercase host commands — a regression in the same two scenes.

**Both parts now applied.** `rip_process()` is a filter that runs the `<< >>`
scanner for every byte and hands what survives to `rip_dispatch_byte()` — the
old `rip_process` with the scanner lifted out of its IDLE case. Three things
had to be right together: unrecognised runs are emitted **verbatim** (without
which the rework would have deleted all 19 lowercase `<<if>>` host commands);
the lone-`<` false alarm re-dispatches instead of writing to the terminal
directly (correct only at IDLE, would have dropped the character inside a
command); and suppression moved with the scanner, so a false branch now
suppresses command bytes too.

Against shipped content: BUTTONS.RIP and CURVES.RIP now request **`BLUEFADE`**
instead of a file named `<<IF $COLORS`, while `|1M`'s host text
(`<<if $RETURN$!="">>$<<RETURN>>$<<else>>…`) survives byte-for-byte with all 20
regions intact.

Across all 35 scenes: **zero** differences in pixels, colours, request counts
or region counts against the pre-rework commit — the only change is which file
the two conditional scenes ask for. Clean under UBSan+ASan and 400k fuzz
iterations; `execute_rip_command`'s stack is unchanged at 656 bytes, the byte
path costs 24 more for the extra frame. See D-26.

### Fixed (build)

- **`|1U` passed a NULL source to `memcpy`.** Removing the `host_len > 0`
  registration gate so hostless buttons become clickable made the copy below
  reachable with `host_text` still NULL, and memcpy's source must be valid
  even at zero length (C11 7.24.1p2). CI's sanitizer job caught it; nothing
  local could. ASan sees nothing (no memory is touched) and UBSan on Windows
  sees nothing either — the check is `nonnull-attribute` and fires only
  because *glibc* annotates `memcpy` that way. Verified by running CI's exact
  sanitizer flags under clang 22 locally: the suites pass with **and** without
  the guard. Only the Linux job can catch this class.
- **libm was linked on the wrong condition.** `if(NOT MSVC)` asks about the
  compiler; whether libm is a separate library is a property of the C library.
  clang targeting the MSVC ABI on Windows is not `MSVC` to CMake, so it took
  the `-lm` branch and failed with "cannot open m.lib" — there is no libm on
  Windows under any toolchain. Replaced with a `check_library_exists(m sinf)`
  probe. Verified on MSVC, clang-on-Windows (previously broken) and the ARM
  cross-build; CI covers Linux and macOS.

**An assertion about silence found three more string-tail defects.** The
corpus harness stubbed `riplib_host_tx` away entirely, so anything a scene
sent to the host vanished unmeasured. Turning that into an assertion —
*passively rendering a scene must send nothing to the host* — failed on 1 of
35 scenes.

That is a security property, not a style preference: RIPlib's posture is that
untrusted content cannot make the terminal act on its own, and host traffic is
always a *response* (to a click, to a query the host began), none of which
happens during replay.

- **`|1A` `RIP_PLAY_AUDIO` read its filename at 4, not 6.** NEWS.RIP sends
  `|1A010000` — the six fixed characters and no filename — so RIPlib took
  `"00"` as a name and pushed a sound request for it.
- **`|1b` `RIP_LoadBitmap` read its filename at 14, not 18.** This is the one
  that matters: slot 88's fixed prefix is 18 and the corpus confirms it
  (`"VU0QYY1S0000000000back.bmp"`). RIPlib asked for `"0000back.bmp"` — **36
  commands across the corpus, every one requesting a name no host could
  match**, in the command that loads the artwork.
- **`|1W` `RIP_WRITE_ICON` used a heuristic instead of the record**, stripping
  a leading `"00"` where the record says one reserved character.

None was reachable by the offset audit — that check looks at `mega*()`
decodes, and a string tail is a `p + N` pointer. `dll-conformance.py` gained a
string-tail class, which is what should have caught all three. See D-25.

**The corpus replay now counts mouse regions.** It measured foreground
pixels, colours, asset requests, FSM state and guard bands — so of the three
interaction defects fixed above (`|1U` buttons never registering, `|1M` flags
from a reserved column, `|1R` requesting the wrong filename), only the last
was visible, and only because asset requests happened to be counted already.

Replaying all 35 scenes with and without the `|1U` fix: **101 regions vs 55**.
46 buttons across the shipped corpus became clickable and not one pixel moved.
A metric that cannot move cannot regress — before this, reverting that fix
left every scene reporting PASS with identical numbers.

The shape has now cost this project three separate defects: a renderer's test
harness measures what it renders, so everything that is *not* rendering —
interaction, host requests, state a consumer reads — is invisible by
construction unless deliberately counted. See D-24.

**The audit is now a script in the repo, not a one-off.**
[`scripts/dll-conformance.py`](scripts/dll-conformance.py) checks RIPlib's
parser against the driver's dispatch record across four classes — read
offsets, length gates, radix selection and coverage. Every one of those was
first hit as a single bug and only afterwards turned into a check, at which
point each found more of the same; making them re-runnable is what stops that
happening a third time. It exits non-zero on a defect so it can gate a build,
and it names the deliberate tolerances rather than passing them silently.

Verified it can actually fail: re-injecting three real historical defects —
`|1i`'s 12-character gate, `|h` decoded with the base-36 helper, and a
one-character shift in `|1G`'s field offsets — is caught as three distinct
findings with exit 1. A check that cannot fail is worth nothing.

Not run in CI: `RIPSCRIP.DLL` is not vendored and will not be. Run it by hand
against a RIPtel install when the parser changes.

**Radix selection and Level 2 offsets, both now checked mechanically.** No
defects — recorded because "no defects" only means something when it is
reproducible.

The per-command radix (D-12) re-derives exactly from the binary: flag `1` =
always base 36 (`|J`, `|N`), `2` = always base 64 (`|D`, `|d`, `|h`, `|y`),
`3` = follow the global base (95), `0` = unset on the 13 argc-0 commands.
RIPlib calls the right decoder for all six fixed-radix commands. This class
deserves a standing check because getting it wrong is *silent and total* —
`rip_mega_digit()` is case-insensitive, so a base-64 field decoded with it
folds `a`–`z` onto 10–35 and returns 0 for `#`/`&`, which is what corrupted 61
of TUNNEL.RIP's 65 palette entries before `|d` was fixed.

The offset audit now covers `ripscrip2.c` too — D-17 checked Level 2 *by
hand*, which is not repeatable and is exactly what let `|2P`'s invented flag
bits survive in the very handler being inspected. Seven distinct handler
bodies (11 commands), zero flagged. Two corrections to that instrument first:
stopping the body at the first `break;` truncates every Level 2 handler at its
length gate, and `|2R` composes its `mega4` by hand from four 1-digit reads,
which read as four defects until consecutive runs were collapsed.

The radix check also caught a **stale comment**: `|d` claimed `|y`
`RIP_ExtendedFontStyle` "is not implemented yet". It was implemented on
2026-08-12 under D-5 and decodes base 64 as its flag requires. Same class as
the stale field lists on `|1I`, `|1w`, `|1M` and `|1T`. See D-23.

**Non-numeric guards audited, and one bug found hiding behind another.**
Classifying the driver's remaining guards — protection, zero-value, viewport,
vertex-count, parameter-count, allocation — put most classes to rest at once.
`|<` already matches (rejects contours under two vertices); the memory class
does not apply. **Protection turns out to be unreachable from the stream**: 41
commands read the protection word and *none* writes it, so it is host-side
state no RIP stream can set, and RIPlib's lack of style/palette/environment
protection is inert rather than divergent. Port protection is the exception
and RIPlib does implement it, matching `|2s` bits 0–3.

- **`|2P` carried `|2s`'s flag meanings.** RIPlib set `FULLSCREEN` from wire
  bit 2 and `PROTECTED` from bit 3 in `RIP_PortDefine`, but that handler reads
  only bits 0 (passed into port initialisation) and 1 (make active). Bits 2
  and 3 are never read there.

  Worth recording *why* it never surfaced: those bits could not fire, because
  the flags field was being decoded from the wrong half of a `mega4` and
  always came out zero. **One defect was masking another** — fixing the field
  decode is what armed the invented bits. A latent defect behind a live one is
  invisible to every test that exercises the live one.

Wire bit 0 is consumed by the driver but what it selects is not recovered, so
RIPlib does not act on it; every `|2P` in the corpus sets exactly that bit and
those scenes render correctly without it. Recorded rather than guessed. See
D-22.

**Value ranges audited as a class.** The driver validates its fields
explicitly and names each failure (`cmp edi,6` → `"Invalid mode parameter"`),
and those bounds had been matched only where a handler happened to be read for
another reason. Extracting all of them — anchoring on the error reporter and
walking back to the guarding compare — gives the driver's whole validation
table. `|;`, `|r`, `|d` and `|q` already matched exactly. Two did not:

- **`|a` `RIP_ONE_PALETTE` masked where the driver rejects.** The handler
  validates `cmp ebx,0x3F / jbe` → `"Invalid Color Parameter"`; RIPlib applied
  `& 0x3F`, folding 64 onto 0 and painting a wrong colour where the driver
  paints nothing. RIPlib had already made the opposite choice for `|d` and
  `|q`; this was the last place still masking.
- **`|Y` `RIP_FontStyle` never checked the font number.** The handler enforces
  font 0–10, direction 0–1 and size 1–10; RIPlib enforced only the size, so a
  font the driver rejects was accepted and fell through to the bitmap fallback
  where the driver keeps the previous font.

Directions 2 and 3 stay: they are RIPlib's own vertical-glyph extensions, now
recorded in the register §14.3.5, and the corpus uses only 0 and 1. See D-21.

Worth noting how the first attempt at this failed: disassembling a fixed byte
count from each handler entry ran into the *next* function, so `|!` — a
zero-argument handler — came back carrying font and palette diagnostics. That
is exactly how a neighbouring handler's strings were once misattributed to
`|3e`. Bounding each handler at the next entry fixed it.

**Length gates audited as a class.** Six defects of one shape had been found
one at a time (`|1g`, `|1i`, the `Switch*` family, `|2p`, `|2W`, `|1R`), each
caught by looking at that command for another reason. Checking the whole table
at once found **fifteen more**: a gate looser than its record acts on a
truncated command with fields read past its end.

Tightened, each checked against shipped scenes first — `|1M` 13→17, `|1B`
30→36, `|1P` 5→7, `|1b` 14→18, `|1e` 8→24, `|1A` 4→6, `|Y` 6→8, `|Z` 16→18,
`|1c` 2→6, `|1D` >0→5, `|,` 12→20, `|.` 6→12, `|b` 18→20, `|r` 2→6, `|=` 2→4.

**Not** tightened where content contradicts the record: `|k` (132 of 133 uses
are 2 chars, one is 1) and `|=` (107 of 116 are 8 chars, 2 are 7, 7 are 4 —
the handler reads progressively because all three widths are real content).

No unbounded reads: nine handlers have no numeric gate and all nine are
bounded by other means. Final state — 72 gates match their record, 1
dispatches on multiple lengths, 2 are corpus-backed tolerances, 9 are bounded
without a numeric gate; **zero admit truncation, zero drop valid input**.
See D-20.

Applying the string-offset rule to the *rest* of the table — while building
the divergence register that "RIPtel is the measure" requires — found a fifth
command, and the one with teeth:

- **`|1R` `RIP_READ_SCENE` took its filename from offset 0.** Slot 104 records
  an 8-character fixed prefix, and all 25 `|1R` commands in the corpus begin
  with exactly eight zeros (`"00000000dragon.txt"`). RIPlib requested
  `"00000000dragon.txt"` — a name no host could match, so **scene loading was
  inert wherever it was used**.
- **`|2W` `RIP_PortWrite` gated on 9 characters** where slot 120 records 13.
- **`|!` `RIP_COMMENT` is now implemented rather than merely survived** — the
  most frequent command in shipped content (709 occurrences). It was consumed
  correctly only because the Level 0 switch has no default, which is accident
  rather than intent.

New: [`docs/spec/14-divergence-register.md`](docs/spec/14-divergence-register.md)
— the standing record of every place RIPlib diverges from bbs-land (13, all
resolved in the driver's favour, 7 affecting total width) and every place it
deliberately diverges from the driver itself (the `|3G` no-launch policy, no
file I/O, the `|k` tolerance, accepted-but-unperformed modes, approximated hit
areas, and the two commands left unimplemented).

Both audit instruments were themselves corrected: overloaded letters store
their extra signatures as **continuation rows with a `0x00` letter byte**,
identified only by sharing the named entry's handler pointer, so filtering on
a printable letter made `|h` present as one signature instead of six. Grouping
by handler fixes it — and independently confirms D-2 (slots 32–37, all on
`0x1001CAE1`, totals 8/4/6/8/3/3). See D-18.

### Added

- `RIP_MF_INVERT` and `RIP_MF_RESET` for `|1M`'s `clk` and `clr` flags.
- Regression tests for each fix above, each verified to fail against the
  previous reading. The `MF_RADIO` test now asserts its fixture registered
  two regions — it had been passing vacuously against zero.

### Changed

- Documentation corrected for `|1G` (§3.10), `|:` (§4.14) and the appendix
  command table; `|1g` added to the appendix, where it had been missing
  despite being implemented. Stale field lists on `|1I` and `|1w` corrected.

## [2.0.2] — unreleased

Patch release. Completes the syntax audit begun in 2.0.1 by comparing every
handler against the driver's own dispatch record rather than against
bbs-land's reference — see [design/syntax-audit.md](design/syntax-audit.md).

### Fixed

- **`|3e` RIP_BAUD_EMULATION read a `mega4` where the record says `mega2`.**
  Slot 123 records one `mega2`; RIPlib preferred a `mega4` whenever four
  characters were available, reading two fields as one. bbs-land documents
  `rate:4` as well — that comes from the 2.0 draft, while the 3.0 driver
  says 2, so **both projects were wrong against the binary**.
- **`|1I` RIP_LOAD_ICON read a 2-digit mode over two 1-digit fields.**
  Slot 97 records `FF FF 01 01 01 01 01` — two coordinates then five
  single-digit fields. The old reading spanned the driver's `args[2]` and
  `args[3]`, agreeing only while `args[3]` was 0. The filename offset was
  already correct.

### Notes

- `|1i` RIP_ImageStyle was investigated and is **correct as written**: its
  payloads are 24 characters, of which the trailing 12 are reserved, and
  RIPlib reads the meaningful prefix.
- New defect **D-14** records three field lists that disagree with the
  dispatch record and are deliberately left unchanged — `|1G`, `|:`, `|1g`.
  All three come from the original reconstruction, none is exercised by any
  shipped scene, and for `|1G` the record's single trailing coordinate
  cannot be mapped onto a destination pair without inventing a field.
  Replacing a coherent implementation with an uninterpretable one would be
  a downgrade.

## [2.0.1] — 2026-08-12

Patch release. Three argument-layout bugs, all found by diffing RIPlib's
handlers against bbs-land's [3.0 command reference](https://github.com/bbs-land/remote-imaging-protocol)
field by field rather than comparing command names alone. **Their reference
was right on all three.**

### Fixed

- **`|k` RIP_BACK_COLOR read one digit instead of a colour-width field.**
  The dispatch record types this argument `0xFE` — colour, whose width comes
  from `|M` SET_COLOR_MODE and is 2 by default. Reading a single digit made
  `|k04` set background **0** instead of 4, and `|k3K` set **3** instead of
  128. This was wrong on 132 uses across 22 of the shipped scenes, so it is
  the one with real rendering impact.
- **`|=` RIP_LINE_STYLE merged two fields into one.** The dispatch record is
  `mega1, mega1, mega4, mega2` — four arguments, the first two a separate
  `off_draw` selector and `style`. RIPlib read the leading two digits as a
  single `mega2` style. That coincides with the correct reading whenever
  `off_draw` is 0, which is every payload in the shipped corpus, so nothing
  rendered differently — but the field was lost and a non-zero `off_draw`
  would have mis-read the style. The handler validates `args[1] <= 4`, the
  BGI line-style range, which is what identifies which field is which.
- **`|D` RIP_SET_DRAWING_PALETTE had count and start swapped.** The handler
  checks `args[0]` against `0x100` ("More than 256 entries") and against the
  argument count, and `args[1]` against `0xFF` ("Start is out of range"), so
  **count comes first**. No shipped scene uses `|D`, so nothing rendered
  wrong; the layout was simply inverted.

### Changed

- `rip_state_t` gains `line_off_draw`, recording `|=`'s first field. It is
  recorded rather than applied: the dash pattern RIPlib builds already
  carries the on/off bits, and what the driver does with this field
  separately is not established.
- README corrected against the code — four claims were wrong (stale test
  counts, a self-contradictory fill-pattern count, a vertical-text claim
  describing behaviour that had been reverted, and an unsupported driver
  version label) and the v2.0.0 realignment was not mentioned at all.

### Notes

Command-name agreement with bbs-land's 3.0 reference went from 35/49 to
49/51 in v2.0.0; the two remaining differences are naming style, not
meaning. This release closes the argument-layout gaps that a name-only
comparison could not see. Findings flowing the other way are filed as
[bbs-land issue #2](https://github.com/bbs-land/remote-imaging-protocol/issues/2).

## [2.0.0] — 2026-08-12

Major release. RIPlib's command set is realigned to the shipping
RIPSCRIP.DLL, and **thirteen Level-0 commands change meaning**. Content
authored against RIPlib 1.x that used any of them will render differently.
Every reassignment below is backed by the driver's own dispatch record,
its handlers' self-naming error paths, or TeleGrafix's own commented demo
files — the evidence for each is recorded in
`docs/spec/12-dll-provenance.md`.

### Changed — commands that now mean something else (breaking)

The skewed-oval family. Six letters were bound to unrelated commands and
rendered as rectangles and line segments. TeleGrafix's own commented demo
`ICONS/NEWCMDS.RIP` names each one, and every arity matches the dispatch
record exactly:

| Cmd | was | is |
| --- | --- | --- |
| `\|&` | ICON_STYLE | **RIP_SKEWED_OVAL** (5 args) |
| `\|-` | TEXT_XY_EXT | **RIP_FILLED_SKEWED_OVAL** (5) |
| `\|]` | POLYLINE_EXT | **RIP_SKEWED_OVAL_ARC** (7) |
| `\|[` | FILLED_POLYGON_EXT | **RIP_SKEWED_OVAL_PIE_SLICE** (7) |
| `\|+` | SCROLL | **RIP_SKEWED_OVAL_CHORD** (7) |
| `\|_` | DRAW_TO | **RIP_FILLED_OVAL_CHORD** (6) |

The demo strokes a coordinate grid before drawing and places each shape on
an intersection, which independently confirms the field layout. `skew` is a
**rotation angle in whole degrees**, not a shear factor: the driver's
generator walks the outline one point per degree applying a 2-D rotation
from Q14 sine/cosine tables, then hands the run to `GDI32!Polygon`.

Seven more:

| Cmd | was | is |
| --- | --- | --- |
| `\|K` | KILL_MOUSE_EXT | **RIP_FILLED_RECTANGLE** — reaches `GDI32!Rectangle`, same as `\|B` and `\|R`. The mouse-field kill is `\|1k`, which is separate and real. |
| `\|<` | GET_IMAGE_EXT | **RIP_POLY_POLYGON** — the only handler reaching `GDI32!PolyPolygon`. Interior is even-odd across all contours, so overlaps cut holes. Clipboard capture is `\|1C`. |
| `\|;` | BUTTON_EXT | **RIP_POLY_MARKER** — the handler names itself and validates marker < 36, rotation < 360, flags <= 3. All 36 glyph outlines are carried; marker 0 is a circle drawn by the ellipse generator, 1-35 come from the driver's descriptor table at RVA 0x07ca48. |
| `\|J` | SAVE_ICON | **RIP_SET_BASE_MATH** — selects the MegaNum radix. |
| `\|D` | FILL_PATTERN_EXT | **RIP_SET_DRAWING_PALETTE** — block form of `\|d`. The 8x8 pattern is `\|s`. |
| `\|d` | EXT_FONT_STYLE | **RIP_ONE_DRAWING_PALETTE** — extended font style is `\|y`. |
| `\|1S` | IMAGE_STYLE | **removed** — no `S` or `s` exists in the driver's Level 1 band. Image style is `\|1i`. |

`\|;` was the most damaging: RIPlib added a **mouse region on every call**,
and the shipped corpus issues 361 of them, so a scene of markers
manufactured hundreds of phantom clickable areas.

Capabilities displaced by the above are not lost — they move to Level-3
letters the driver does not define: `\|3&` icon display style, `\|3-`
bounded text box, `\|3J` icon-slot save.

### Added

- **`\|3D` RIP_DELAY**, in sixtieths of a second. Recovered by following
  call targets rather than strings: its callee busy-waits on
  `WINMM!timeGetTime`, and the chunking arithmetic (3900 ticks, 65000 ms per
  chunk) fixes the unit. **RIPlib never blocks** — a rendering library that
  stalls its caller for up to 65 seconds per chunk is unusable on the
  cooperative hosts RIPlib targets. The request is recorded and handed over
  by the new `rip_take_delay()`; ignoring it is safe.
- **Base-64 MegaNum** (`docs/spec/01-wire-format.md` §1.5.1). RIPscrip has a
  second radix — `0-9 A-Z a-z # &`, case-sensitive — and four commands
  (`\|D`, `\|d`, `\|h`, `\|y`) always use it regardless of any global
  setting. The alphabet was recovered from TeleGrafix's own content, and the
  binary corroborates: a 4-digit base-64 field spans exactly `0..0xFFFFFF`,
  which is the bound the palette handler enforces. Previously RIPlib decoded
  these as case-insensitive base 36, which folded `a-z` onto `10-35` and
  turned `#`/`&` into 0 — **61 of TUNNEL.RIP's 65 palette entries decoded
  wrong**, and every `\|y` in 25 files misread its scale fields.
- `corpus_tests` — replays authentic `.RIP` scenes byte-for-byte, asserting
  no crash, no wedged FSM and no drawing outside the framebuffer, and
  reporting foreground pixels, distinct colours and pending asset requests.
  Scenes are third-party content and are not vendored; `-DRIPLIB_CORPUS_DIR`
  enables the test and it reports SKIP without one.
- `fuzz_seeded_tests` — plain-C99 mutation fuzzer with a fixed seed, wired
  into ctest. Builds long commands with `\` continuations, which is what
  the previous fuzzer could not reach.
- `scripts/dll-disasm.py`, `scripts/dll-handler-imports.py`,
  `scripts/corpus-scan.py` — handler disassembly with import resolution,
  classification of handlers by the Win32 APIs they reach, and opcode census
  over a corpus.

### Fixed

- **Long commands were silently dropped.** `cmd_buf` was 256 bytes, but
  real scenes exceed it — `HAWK.RIP` issues a `\|p` declaring 153 vertices,
  needing 614 characters across 11 `\` continuations. The accumulator
  stopped storing at 255, the command then failed its own length check, and
  the whole thing was discarded. Widened to 1024 (corpus maximum is 674).
  `HAWK.RIP` went from 9,896 to 78,562 painted pixels; `LGF1.RIP` from
  67,607 to 107,930; `CURVES.RIP` from 15 to 41 bezier segments.
- **Stack overflow in the text path (latent).** `rip_render_text()` passed
  an unbounded length to `unescape_text()` writing into a 256-byte buffer,
  safe only because `cmd_buf` happened to be 256 as well — the sizes matched
  by accident, not by design. `unescape_text()` now takes and enforces the
  destination capacity, and all six callers pass `sizeof(dst)`.
- **Wrapping arcs drew nothing.** A sweep whose end angle is below its start
  wraps through 0; the generator returned early instead. TeleGrafix's demo
  issues `\|_` with start=324 end=216, a 252-degree sweep that rendered as
  nothing at all.
- **`\|P`/`\|p`/`\|l` rejected polygons above 64 vertices** outright rather
  than drawing them. Cap raised to 192 (corpus maximum is 153).
- `\|1k` now honours its flags field (1/2/4 = contained/intersecting/
  outside); previously it always deleted the contained set.
- `\|2R` consumes its `res:4` argument; it was read as zero-argument.
- `\|n` records a coordinate width it cannot honour in
  `rip_state_t.coord_size_unsupported` rather than accepting it and
  mis-parsing everything after (D-11).

### Security

- `\|3G` RIP_GotoURL is **opt-in**. With no handler registered the URL is
  validated and stored, and nothing else happens; RIPlib never opens a URL
  or spawns a process. Schemes are restricted to `http://` and `https://` —
  `javascript:`, `data:`, `file:` and friends are refused outright rather
  than left to host policy.


### Fixed — earlier alignment work in this release

- **Write modes were misnumbered (breaking rendering fix).** `|W` wire values
  are `0=COPY, 1=XOR, 2=OR, 3=AND, 4=NOT`. RIPlib had OR=1, AND=2, XOR=3 and
  passes the wire byte straight through, so it rendered XOR where content
  meant OR and vice versa — `|W01` draw-twice-to-erase smeared instead of
  erasing. Established by disassembling RIPSCRIP.DLL 3.0.7: the handler
  (RVA 0x02102C) stores the wire byte unmodified and the apply path
  translates it at RVA 0x00E6B3 into GDI raster ops (`R2_XORPEN` for 1,
  `R2_MERGEPEN` for 2, `R2_MASKPEN` for 3). Spec §11 `§BUG.7`, the sole
  basis for the old numbering, is withdrawn — it was never a DLL bug.
- **SOH/STX command introducers now work.** RIPscrip syntax rule 12 allows
  `SOH` (0x01) and `STX` (0x02) to replace `!` anywhere in a line. RIPlib
  discarded SOH outright, so scenes opening with the SOH form — which the
  shipped 2.x corpus does — never started.

### Changed — earlier alignment work in this release

- `§A2G.1` (AND/NOT write modes) withdrawn as a protocol extension. Both
  modes have been documented since v2.00 Alpha 1 and the shipping driver
  rendered them, so this was a completeness fix to RIPlib's renderer, not a
  language addition. Spec `§DEAD.3` corrected accordingly.
- `§A2G.4` qualified: RIPlib provides 11 built-in fill bitmaps plus a
  user slot, not 13, and four BGI styles still resolve to approximations
  with styles 5 and 6 collapsing onto one bitmap.
- `|28` gradient re-attributed as RIPlib-original rather than inherited
  from RIPSCRIP.DLL 3.0.7; no such command exists in that driver.
- `§DEV.4` corrected: `|1R`, `|!`, `|(`, `|)` and the backtick composite-icon
  command are all present in the shipping driver and are not RIPlib
  extensions. Only `|1V` and `|1X` are RIPlib-original. Closes open
  question U-026.

### Added — earlier alignment work in this release

- `docs/spec/12-dll-provenance.md` — evidence classes, opcode adjudication,
  the `|W` disassembly, and a register of open defects.
- `docs/spec/13-dll-command-table.md` — the driver's command dispatch table
  verbatim (129 entries: letter, handler, arity, argument types, name).
- `docs/historical/ripscrip-v3-RE-notes.md` — restored; it is the substrate
  segments 11-13 cite.
- `scripts/dll-provenance.py`, `scripts/dll-dispatch-table.py`,
  `scripts/dll-name-handlers.py` — regenerate the binary-derived data.
- `scripts/check-branding.sh` + CI job enforcing platform independence.

### Notes

- The `[1.3.0]` section below also contains alignment-era entries that were
  appended there in error; that release is dated 2026-05-31 and predates
  this work. They are interleaved with genuine 1.3.0 content and have been
  left in place rather than risk mis-attributing entries by splitting them.
  Where the two disagree, this section is authoritative.


## [1.3.0] — 2026-05-31

Minor release that completes the reentrant multi-session API surface.
Purely additive — no behaviour change to any existing entrypoint, fully
backward compatible with 1.2.x. (Candidate C-004 variant A′; see
`design/adr/0004-multi-session-state-family-completion.md`.)

### Added

- **Four driver commands that RIPlib never implemented** (D-5). Each was
  recovered from the binary rather than guessed — argument widths come from
  the dispatch entry's own type bytes, ranges from each handler's validation
  branches:
  - `|j` **RIP_POINT** (`x:XY y:XY`) — identified by disassembly: the handler
    at RVA 0x01E2F8 transforms both coordinates then fills a 1x1 rect with a
    brush, and the `RIP_Point` name string is referenced from inside its body.
  - `|r` **RIP_TEXT_METRIC** (`mode:1 domain:1 res:4`) — `mode < 4`,
    `domain < 2`, per the handler's own diagnostics. The channel the driver
    delivers the computed metric on has not been recovered, so RIPlib records
    the request rather than inventing one.
  - `|x` **RIP_FILLED_POLY_BEZIER** — the filled counterpart of `|z`, which
    §11.2 Erratum 2 already had the letters right for. Segments are flattened
    into a single outline so the fill spans the whole curve.
  - `|y` **RIP_EXTENDED_FONT_STYLE** — the dispatch entry's width bytes
    (`01 01 04 02×7 06`) sum to **26 characters**, independently matching the
    "26-digit layout" the bbs-land reconstruction recovered from FONTS.RIP.
    Rotation and character-spacing fields are applied; the fields whose
    meaning has not been recovered are parsed at their correct widths and
    left uninterpreted rather than guessed.

- `scripts/dll-handler-semantics.py` — recovers per-command field semantics
  from the driver's own validation diagnostics (66 of 129 handlers). Feeds
  `docs/spec/13-dll-command-table.md` §13.5.
- Reentrant `*_state()` counterparts for the four host-event entrypoints
  that previously existed **only** as `g_rip_state`-bound globals:
  `rip_sync_date_byte_state()`, `rip_sync_time_byte_state()`,
  `rip_query_response_byte_state()`, and `rip_apply_palette_state()`.
  The `SESSION SAFETY` block in `include/ripscrip.h` documents a
  two-flavour contract — *"every public entrypoint comes in two
  flavours"* — but before this release that contract was unfulfilled for
  these four functions, so a multi-session embedder could not feed host
  time-sync, query responses, or apply a saved palette to a specific
  non-global session. The complete reentrant surface now lives in one
  block in the header.
- `tests/test_ripscrip.c`: `test_state_api_sync_query_palette_isolation`
  drives two sessions and asserts the new `_state()` forms operate on the
  passed `rip_state_t` independent of `g_rip_state` (`test_ripscrip` is
  now 240 checks; 287 total across the three suites).

### Changed

- **X1 closed: `$COMPAT$` / `$COPY$` / `$PROT$` renamed** to `$RLCOMPAT$` /
  `$RLCOPY$` / `$RLPROT$` (breaking). The driver uses those bare names for
  *parameterized actions* — `$COMPAT(env)$` drops an environment to 1.54
  settings and has 21 uses in the shipped corpus — so occupying them meant a
  host could not tell a compliant terminal from RIPlib. The `RL` prefix cannot
  collide with a driver name.
- **`§A2G` is a documented opaque revision tag** (ADR-0007). It derives from a
  consumer name but expands to nothing for any reader, and is cited ~20 times
  in an external repository; renaming would cost real interoperability for a
  cosmetic gain. The platform-independence constraint is amended **with** an
  ADR and a chronological log row rather than in place — the exact defect
  code-review finding 6 flagged on `3e05ecb`.

- **Argument-count overloads are now dispatched (D-2).** The driver accepts
  several signatures per letter, selected by argument length; RIPlib bound one
  layout per letter, so any other accepted form was read with the wrong field
  offsets and drew the wrong picture. `|t`, `|x` and `|z` share a
  poly-bezier pattern (4-char header, 5-char move-to, 13-char curve-to) whose
  lengths are distinct, so dispatch is exact. `|h`'s 4- and 6-character forms
  now read their own layouts instead of being parsed with the 8-character one,
  which pulled id and flags from past the end of the parameters.
- **Level-0 `|t` is `RIP_POLY_BEZIER_LINE`, not `RIP_REGION_TEXT` (B8).**
  Its handler (RVA 0x01E4A4) sits beside `|z` with a structurally identical
  body plus a write-mode apply — a drawing command. Region text is `|1t`,
  which RIPlib already implemented, so the correction loses nothing.

### Security

- **`|3G` RIP_GotoURL is opt-in, with a scheme allow-list.** RIPlib still never
  opens a URL or spawns a process. By default the URL is validated and stored
  only. An embedder that wants click-through registers a handler with
  `rip_set_url_handler()`; `javascript:`, `data:`, `file:`, `vbscript:` and
  everything else outside `http`/`https` are refused outright, even with a
  handler registered. Over-long URLs are rejected rather than truncated, since
  truncation can change which host a URL points at.
- **`$GOTOURL$` routed through the same path.** It was hard-neutered while
  `|3G` was not, so the same request behaved differently depending on which
  syntax the BBS used. Both now share the allow-list, validation and opt-in
  gate. The stream still receives a zero-length response either way, so a host
  cannot probe whether a handler exists.

- **`card_tx_push` renamed to `riplib_host_tx` (breaking).** One of exactly
  three functions every port must implement, and its old name carried a
  specific consumer's terminology into the public API of a library whose
  headline claim is platform independence.
- **`<<DEBUG>>` is off by default (X7).** Enable with
  `-DRIPLIB_ENABLE_DEBUG_DIRECTIVE=ON`. Unsolicited terminal-to-host traffic
  has no precedent in RIPscrip — everything else a terminal sends is a
  *response* — and a BBS at a prompt reads those bytes as keystrokes. The
  directive is still parsed and consumed when disabled, so rendering is
  unchanged. The README previously called it "safe to leave in production";
  that was wrong.
- **EGA palette base is now a port decision, not a baked-in host policy.**
  `RIPLIB_PALETTE_BASE` (default 240, preserving v1.x behaviour) selects the
  framebuffer slot of EGA colour 0. RIPlib hardcoded 240 because its first
  consumer shared the framebuffer with an xterm-256 text renderer — an
  assumption about the *host*, not the protocol. Ports that own the whole
  framebuffer can now use `-DRIPLIB_PALETTE_BASE=0`.
- The existing single-session globals (`rip_sync_date_byte`,
  `rip_sync_time_byte`, `rip_query_response_byte`, `rip_apply_palette`)
  are now thin wrappers over their `_state()` forms — byte-for-byte
  identical single-session behaviour, mirroring the existing
  `rip_mouse_event_ext` / `rip_file_upload_*` pattern. The documented
  `rip_save_palette(s)` ↔ `rip_apply_palette()` "API asymmetry" is
  resolved: `rip_apply_palette_state(s)` is the matching explicit-state
  form.

### Notes
- The `g_rip_state` singleton and the convenience globals remain for the
  common single-session embedded case. Full removal of the singleton (a
  breaking change) stays parked as candidate **C-004-A** pending a
  concrete multi-session consumer — see
  `design/adr/0004-multi-session-state-family-completion.md` and
  `design/decisions.md`.

## [1.2.2] — 2026-05-30

Audit-and-portability patch release. Fixes the host (gcc/clang) build and
static-library linking, corrects a scalable-text scale bug, hardens scene
state, and reconciles documentation with the source. No public API or
protocol-surface changes — fully backward compatible with 1.2.1.

### Documentation
- Documentation accuracy pass against the current source tree:
  - README file-structure now lists the modules extracted from
    `ripscrip.c` (`rip_preproc.c`, `rip_variables.c`, `rip_clipboard.c`,
    `riplib_version.c`) and the `riplib_version.h` header, and drops the
    stale "(4900+ lines)" annotation.
  - README test counts corrected to **285 total** checks
    (`test_drawing` 41, `test_ripscrip` 238, `test_compat` 6). The
    [1.2.1] note below quoted the earlier 275/228 figures, which the
    test-suite growth has since superseded.
  - README "platform-independent" wording corrected: the library uses
    `string.h`/`stdlib.h`/`stdio.h` (`snprintf`) and `math.h`
    (`sinf`/`cosf`/`atan2f`/`sqrtf`) and links `libm` — not "no libc
    beyond memcpy/memset, no floating point."
  - `docs/spec/01-wire-format.md` banner version corrected v1.2.0 → v1.2.1.
- `docs/spec/11-dll-deviations.md` rewritten so it records only
  deliberate, decided deviations (§DEV.1-5); spec text was corrected to
  match the code (icon lookup order §9.2, scalable-text range §5.9,
  the RIPlib-extension commands in §A.1) and unresolved items moved to
  `design/knowledge.md`.

### Fixed

- **`|d` was parsed as extended font style; it is `RIP_OneDrawingPalette`
  (breaking).** Settled by disassembling RVA 0x01CF95, whose three validation
  branches carry their own error strings: index must be <= 0xFF ("Color
  palette index out of range"), bits must be exactly 8 ("Bits value out of
  range"), rgb must be <= 0xFFFFFF ("RGB Color value is out of range!").
  `|d` now writes a palette entry and no longer corrupts font state.
  Extended font style is `|y` (`RIP_ExtendedFontStyle`, 11 arguments), which
  is **not yet implemented** — its full field layout has not been recovered,
  and guessing it would be worse than the gap. See docs/spec §12.8 and D-5.

- **`!` now requires a line boundary again (X5).** RIPlib fired on any `!`, so
  ordinary prose containing an ANSI sequence followed by an exclamation mark
  parsed as a command. `!` again introduces a command only at start-of-stream
  or after CR/LF/FF. The portable way to start a scene mid-line is the SOH/STX
  introducer, which now works.
- **`|Y` direction `01` restored to its documented meaning (X3, breaking).**
  v3.1 had redefined direction 1 from the specified bottom-to-top (BGI
  VERT_DIR) to top-to-bottom, so content authored against either side read
  upside-down on the other. Direction 1 is bottom-to-top again; the corrected
  top-to-bottom CW rendering moved to a new **direction 3**. Direction 2 (CCW,
  top-to-bottom) is unchanged. `|26` SCALABLE_TEXT's 90-degree rotation now
  maps to direction 3.
- **Fill pattern `00` paints the background colour on a bar (B9).** The 1.54
  spec is explicit — "Fill pattern 00 will set the entire fill area to the
  background color" — but RIPlib skipped the fill, so `!|S0000|` plus a bar,
  the idiom for blanking a region, did nothing. Corrected for `RIP_BAR`; the
  polygon case is deliberately left alone, as implementations genuinely
  disagree there.

- **`|f` was parsed as FONT_ATTRIB but is `RIP_SetWorldFrame` (breaking).**
  The driver's slot-28 handler names itself `RIP_SetWorldFrame` and takes two
  coordinate pairs; RIPlib read two MegaNums as `attrib:2 res:2`. The corpus
  standard `|fZKQO` (base-36 1280x960) opens most shipping 3.x scenes, so
  RIPlib mis-parsed ordinary content and silently corrupted font state. Font
  attributes moved to `|q` (`RIP_FontAttrib`, slot 55), which range-checks
  the value `<= 0x0F` exactly as RIPlib's 4-bit field already did. `|f` now
  stores the world frame; the world->device transform is not yet applied.
- **Time variables were the right values under the wrong names (breaking).**
  RIPSCRIP.DLL 3.0.7 carries both names of each pair as distinct strings, and
  has no `DOM` at all. Corrected: `$HOUR$` is now 12-hour and `$MHOUR$` is
  24-hour; `$DOW$` spells the day out and `$WDAY$` is the digit with
  **Sunday = 0**; `$MONTH$` spells the month out and `$MONTHNUM$` is numeric;
  `$DOM$` is replaced by `$DAY$`. Previously `<<IF $DOW$=4>>Happy Friday!` --
  RIPlib's own documented example -- evaluated false on every conforming
  terminal, and Friday is 5 with Sunday=0 in any case.
- `26` SCALABLE_TEXT scale was bit-masked `& 0x07` (silently corrupting
  valid scales — e.g. 10 became 2); now clamped to the BGI renderer's
  real 1-10 range, matching the `|Y` RIP_FONT_STYLE size field.
- `src/bgi_font.c` now includes `<stddef.h>` for `size_t` — fixes the
  host gcc/clang build (newlib/MSVC pulled it in transitively).
- CMake links `libm` `PUBLIC` so static-library consumers (tests,
  examples, downstream apps) resolve the math symbols.
- The libFuzzer `-fsanitize=fuzzer` flag is scoped to the `fuzz_parser`
  target instead of global flags, so the CMake compiler-probe no longer
  fails to configure.

### Changed
- The `|#` (RIP_NO_MORE) scene terminator now defensively closes any open
  text block, so a malformed stream that omits `|1E` before `|#` cannot
  bleed stale text into the next scene's REGION_TEXT. Well-formed streams
  (which send `|1E`) are unaffected; full state reset remains `|*`.
  Covered by a new regression test (`test_ripscrip` is now 239 checks).
- Audit-driven comment/contract accuracy fixes (no behaviour change):
  documented `draw_text`'s font-buffer size contract (≥ 256×font_height
  bytes) in `drawing.h`; the `$RAND$` comment now states the real
  guarantee (deterministic Knuth/POSIX LCG) instead of unverifiable
  bit-for-bit DLL compatibility; corrected the `RIP_TEXT_WINDOW` routing
  comment and documented the full-screen-window heuristic as deviation
  §DEV.6.

## [1.2.1] — 2026-05-11

Patch release for the RIPscrip v3.2 surface.

### Fixed
- `ESC[!` now advertises the same v3.2 wire ID as `$RIPVER$`:
  **`RIPSCRIP032001`**.
- `|^` / `|~` now preserve and re-apply the full drawing prelude:
  colors, write mode, fill style/background, 16-bit line pattern,
  line thickness, font and extended font fields, draw cursor, viewport,
  and filled-border state.
- Level 2 Drawing Port save/restore now preserves custom line patterns.
- The drawing backend now accepts the full 16-bit RIPscrip user line
  pattern instead of truncating to an 8-bit approximation.
- The `$DOW$` Friday example in the v3.2 spec now matches the Monday=0
  convention.

### Documentation
- README coverage wording now describes RIPlib as a portable
  rendering/parser core, with host-owned operations called out explicitly.
- README test counts updated to 275 total checks:
  `test_drawing` 41, `test_ripscrip` 228, `test_compat` 6.

## [1.2.0] — 2026-05-11

**Bumps the supported protocol from RIPscrip v3.1 → v3.2** by
defining six quality-of-life extensions as §A2G.8 through §A2G.13.
All additions are backward compatible — they use new command
letters, new `$VARIABLE$` names, or new values for previously-
validated parameter fields.  A v3.0 / v3.1 client sees the
additions as either no-ops or as literal text that falls through
`$XYZ$` unrecognized-variable handling.

Protocol ID advertised by `$RIPVER$` and the ESC[! probe response
is now **`RIPSCRIP032001`**.

### Added (v3.2 protocol — §A2G.8 through §A2G.13)
- **State push/pop stack** — `|^` and `|~` save/restore the drawing
  prelude (colors, fill/line/write state, font fields, draw cursor,
  viewport).  Bounded to 8 frames; overflow drops silently, pop on
  empty is a no-op.  Cleared by `|*` and session reset.  See §A2G.8.
- **Layout / introspection variables** — `$CX$` `$CY$` `$VPW$` `$VPH$`
  `$VPCX$` `$VPCY$` `$CCOL$` `$CFCOL$` `$CBCOL$` expose current
  drawing state.  Use case: "center this text" without hardcoding
  320,200.  See §A2G.9.
- **Time component variables** — `$HOUR$` `$MIN$` `$SEC$` `$DOW$`
  `$DOM$` `$MONTH$` extend the existing `$DATE$` / `$TIME$` family.
  All fall back to local RTC when host hasn't synced.  See §A2G.10.
- **EGA color-name aliases** — `$BLACK$` through `$WHITE$` each
  expand to the 2-digit MegaNum of the EGA palette index.  Useful
  in `<<IF>>` comparisons and in text bodies.  See §A2G.11.
- **`<<DEBUG msg>>` preprocessor directive** — pushes
  `0x3E DEBUG: <msg>\r` to TX, suppressed by enclosing
  `<<IF false>>` branches.  Safe to leave in production scripts
  (hosts that don't recognize the prefix drop the line).  See §A2G.12.
- **Radial gradient** — `|28` gains mode 2 (radial), in addition to
  the existing horizontal (0) and vertical (1) modes.  Per-pixel
  interpolation by normalized squared distance, using the FPU we
  already require for §A2G.5 trig.  Existing clients sending
  mode 0/1 are unaffected.  See §A2G.13.

### Changed
- `$RIPVER$` now reports `"RIPSCRIP032001"` (was `"RIPSCRIP031001"`).

### Tests added (+14)
- `test_state_stack_*` — push/pop roundtrip, pop-on-empty, overflow
- `test_var_*` — CX/CY/VPW/VPH/VPCX/VPCY, CCOL/CFCOL, $RED$, $LIGHTMAGENTA$,
  HOUR/MIN, DOW/DOM/MONTH
- `test_preproc_debug_*` — DEBUG emits to TX, suppressed inside false IF
- `test_l2_gradient_radial_mode` — radial gradient renders pixels

Test count: 254 → 268 total (drawing 41, ripscrip 222, compat 5).

## [1.1.0] — 2026-05-11

The protocol-complete + sanitizer-clean milestone.  Builds on the
initial v1.0.0 release with a systematic spec/code/test audit that
surfaced 18 protocol bugs, drove every documented command through
wire-level tests, and locked in coverage + sanitizer + analyzer
guards in CI.

### Added
- Full RIPscrip protocol coverage: every one of the **99 documented commands**
  across Level 0 (38), Level 1 (20), Extended (27), and Level 2 (14) is parsed
  and dispatched, with at least one wire-level test per command.
- **251 regression tests** across three suites:
  - `test_drawing` (41) — drawing primitives
  - `test_ripscrip` (205) — protocol parser, state, FSM, text variables,
    mouse regions, icon caching, BMP/ICN ingestion
  - `test_compat` (5) — golden-frame FNV-1a hash fixtures
- CI matrix: Linux/macOS/Windows × Debug/Release, plus three guard jobs:
  - **sanitizers** — UBSan + ASan + `detect_leaks=1` on Linux
  - **static-analysis** — `gcc -fanalyzer` warnings-as-errors
  - **coverage** — per-file floor thresholds (gcov) prevent regression
- `examples/rip2ppm` — a CLI that reads a `.rip` wire stream and writes a PPM
  image, demonstrating end-to-end rendering without any platform glue.
- v3.1 (§A2G) extensions: AND/NOT write modes, vertical text direction
  correction, font attribute rendering, native fill patterns, palette index
  correction, extended text directions.

### Reverse-engineered (documented bugs found and fixed during audit)
- **L1**–**L18**: 18 distinct protocol-layer bugs identified by spec/code/test
  cross-reading. Fixes are in `git log` between the initial implementation and
  v1.0.  Notable examples:
  - L13: BMP parser rejected truncated and zero-height images correctly only
    after the audit added explicit guards.
  - L14, L16a/b, L17a/b: silent NULL-font draw_text calls in text paths.
  - L18: 1D handler double-wrote `app_vars[0]` via a legacy fallback even
    when the modern `=` syntax already stored the value.

### Coverage (line %, gcov, Linux gcc -O0)
| File | Coverage |
|---|---|
| `src/rip_icn.c` | 97% |
| `src/drawing.c` | 88% |
| `src/ripscrip2.c` | 88% |
| `src/bgi_font.c` | 85% |
| `src/rip_icons.c` | 84% |
| `src/ripscrip.c` | 84% |

### Notes for consumers
- After a `!|cmd|` sequence the FSM is in `RIP_ST_COMMAND` (state 2), waiting
  for either another command letter or `CR`/`LF`.  Raw text bytes fed without
  a trailing newline will be interpreted as command letters, not text.  Feed
  `\n` or call `rip_session_reset()` between scenes if you intend the next
  bytes to be text.
- Tests that create more than one `rip_state_t` should call
  `psram_arena_destroy(&s.psram_arena)` between init calls or track every
  arena base for cleanup; ASan with `detect_leaks=1` will catch leaks.
