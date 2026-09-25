# RIPlib / RIPtel / bbs-land crosswalk audit

Audited 2026-09-24. The [complete generated crosswalk](riptel-crosswalk.md)
contains every DLL dispatch row, RIPlib source handler, and upstream command
inventory row. This is a command inventory and static syntax audit, not a
claim of terminal-feature or pixel parity.

## Inputs and result

| Input | Exact scope |
|---|---|
| RIPlib | Working tree based on `024032a263649cbd842bcf855ac1fe9238606e6f`; source SHA-256 values are in the generated crosswalk |
| RIPtel | Local RIPtel 3.1 installation; RIPSCRIP.DLL is 592,896 bytes, MD5 `bade8b1f4e467ac7ad4edb2639738d4c`, self-reported version 3.00.04 |
| bbs-land | [Commit 2fb17724b6122a5ad5cd1df38b69b8cce3a7079f](https://github.com/bbs-land/remote-imaging-protocol/tree/2fb17724b6122a5ad5cd1df38b69b8cce3a7079f), principally `version/3.0/ripscrip/9.0-command-reference.md` and `version/3.0-riplib/CONFLICTS.md` |
| Corpus | The local installation's 35 `.RIP` scenes, 12,328 lexical command instances, 70 command keys; not upstream's larger 116-script census |

The DLL has **129 rows, 118 distinct command keys, 11 continuation rows,
and no duplicate keys**. RIPlib has handlers for all 118, plus 28 source-only
keys (including four preserved level-3 service aliases). The earlier
117-key count and duplicate `3D` finding were wrong: the last five records
carry a literal `9` prefix. D-34 documents the correction.

The upstream inventory has **116 rows: 112 keyed opcodes and four names
without assigned opcodes**. Among 84 comparable numeric layouts, **69
agree and 15 differ** from the DLL. Twelve elided/variable lists remain
uncompared. The generated table also preserves nonnumeric cases, reference
opcodes absent from the DLL, and the four unassigned names.

## Fixes from this audit

| Finding | Driver evidence | Implemented correction |
|---|---|---|
| Five misidentified punctuation commands | Slots 8, 10, 11, 83, 84 all call the pure geometry helper at RVA `0x00FA70` | Comma draws an affine elliptical arc; period outlines the whole ellipse; colon draws a pie; backtick draws a chord; brace fills the whole ellipse. They no longer copy screen regions, stamp icons, create mouse regions, or draw triangles |
| Backtick field overread | Slot 83 has ten coordinate fields and a final single digit | Require 21 characters; read the fill digit at offset 20. Geometry uses center, conjugate radii, and endpoint rays |
| Missing Level 2 ESC | Slot 110, RVA `0x046F66`, self-named `RIP_SwitchDirectory` | Validate and store the logical host directory, including `$OFF$`; no process-directory mutation |
| Missing Level 9 ESC | Slot 124, RVA `0x024B4E`, self-named `RIP_EnterBlockMode` | Decode the eight-character prefix and filename, validate driver bounds, store a pending request, and optionally call the host transfer handler |
| Wrong refresh behavior | Slot 117 calls `refreshAssignCommand` at RVA `0x03E43C` | Store the command string; host calls `rip_request_refresh()` to transmit it. Parsing itself neither transmits nor forces a framebuffer redraw |
| Empty fills skipped; filled Bezier used drawing color | Zero brush rows at RVA `0x07AFD8`; filled handlers select a brush | Apply background fill consistently across filled shapes; Bezier uses current fill ink and pattern |
| Compound polygon fill inherited pen style | Its interior spans called `draw_line`, bypassing the fill brush | Use filled spans so both brush colors apply independently of line dash/thickness |
| CopyBlit mode treated as raster operation; scroll effects ignored | Both handlers use `SRCCOPY`, then fill exposed source with a mode-selected brush | Modes 0–5 implemented for copy and scroll; scroll mode 6 samples source before moving. Overlap survives; drawing write mode does not alter the move |
| Checker coverage holes | Numeric cases, early breaks, long handlers, ESC macro values, and continuation rows | Complete handler extraction and explicit accounting; numeric Level 2 defines retained; adversarial instrument tests |

The five geometry names above are descriptive RIPlib names. The driver's
helper supplies geometry evidence; its command handlers do not supply those
names. [D-31 through D-33](spec/12-dll-provenance.md) record the addresses,
corrections to earlier claims, fixtures, and portable host boundaries.

**Compatibility change:** RIPlib-specific stamp-slot content must move
from `|.` to the extension `|3.`. `|3J` still saves slots. This avoids
occupying the driver's ellipse opcode. New host fields extend the public
`rip_state_t`; rebuild consumers with the matching headers and library.

## Remaining boundaries

- File transfers, logical directory selection, URL navigation, audio,
  host expressions and playback timing require host implementation.
  `9D` stores its expression and optionally invokes the registered handler.
- Exact Windows GDI text rasterization and font substitution remain outside
  the portable BGI/CP437 renderer. Exact built-in brush masks are now verified.
- The existing single-framebuffer and active-text-window model remains:
  automatic mode-4 query hit-testing uses the active text window. Hosts managing
  additional text windows can dispatch their registered queries through
  `rip_trigger_query()`. This does not establish 36 independent text surfaces.
- General host macros and decimal-compressed query prefixes are not fully
  emulated. Unknown query macros remain silent; malformed templates send no
  partial response. Negotiated-width normalization still covers its generated
  fixed-arity table, not every variable-length signature or world transform.
- No hardware run or complete RIPtel framebuffer differential was performed.
  These are explicit integration/rendering boundaries, not disputed opcode syntax.

## Fifteen upstream numeric-layout differences

`n` means configurable coordinate/color width, normalized to two digits
for this comparison. These are differences in the numeric argument array;
the DLL passes strings separately. The links in the full crosswalk lead
to the exact upstream lines at the pinned revision.

| Commands | DLL layout | bbs-land layout | Effect |
|---|---|---|---|
| `\|1I` | `n n 1 1 1 1 1` | `n n 2 1 1 1` | Same default total, different subdivision |
| `\|1M` | `2 n n n n 1 1 2 3` | `2 n n n n 1 1 5` | Same total, merged reserved fields |
| `\|1R` | `2 6` | `8` | Same prefix total, different subdivision |
| `\|1T` | `n n n n 1 1` | `n n n n 2` | Same total, merged fields |
| `\|1w` | `1 3` | `4` | Same total, missing separate mode field |
| `\|2W` | `1 n n n n 2 2` | `1 n n n n 4` | Same total, merged fields |
| `\|2A`, `\|2B`, `\|2E` | `1 2` | `2` | Three characters versus two |
| `\|2T`, `\|2Y` | `1 2` | `1 1` | Three characters versus two |
| `\|2s` | `1 2` | `1 2 3` | Three characters versus six |
| `\|3e` | `2` | `4` | Two characters versus four |
| `\|9ESC` | `1 1 2 2 2` | `1 1 2 4` | Same total, different subdivision |
| `\|9U` | `2 4` | `2 8` | Driver consumes six numeric characters |

RIPlib's comment-signature comparison reports 76 comparable commands,
with zero layout differences. The broader actual-read conformance check also
reports zero defects. These are static syntax checks, separate from the
behavioral regressions and geometry oracle below.

## Upstream conflict register: what is stale

The pinned [upstream conflict register](https://github.com/bbs-land/remote-imaging-protocol/blob/2fb17724b6122a5ad5cd1df38b69b8cce3a7079f/version/3.0-riplib/CONFLICTS.md)
describes an older RIPlib snapshot. The following are verified against the
current source; they are not claims that every concern in that file is closed.

| Upstream entries | Current evidence | Assessment |
|---|---|---|
| B1, write modes | [drawing.h](../include/drawing.h): XOR=1, OR=2, AND=3 | The alleged numbering defect is fixed |
| B2/B3/B5/B6, opcode identities | [ripscrip.c](../src/ripscrip.c): `J` base math, `f` world frame, `K` filled rectangle, `D/d` drawing palette, `y` extended font style | The listed old opcode assignments are stale; identity agreement alone does not prove full rendering parity |
| B4, punctuation block | [ripscrip.c](../src/ripscrip.c): skewed-oval family, markers and poly-polygon | Old ICON_STYLE/TEXT_XY_EXT/etc. assignments are gone |
| B8, swapped commands | [ripscrip.c](../src/ripscrip.c): `1i` image style, `1w` audio, `1A` article selection, `1G/1g` scroll/copy, Level 1 ESC query, `t` poly-Bezier line | The listed old assignments are stale; host-owned behavior remains separately limited |
| B12 and X5, stream introducers | [ripscrip.c](../src/ripscrip.c): SOH/STX accepted; ordinary `!` requires a line boundary | Missing-control-introducer and relaxed-CSI-trigger descriptions are stale |
| B9, empty fill | [ripscrip.c](../src/ripscrip.c): all tested filled primitives paint the background at pattern 0 | Empty-fill regression fixed across twelve shape families; all ten patterned brush masks now match the driver table |
| B7, refresh | [ripscrip2.c](../src/ripscrip2.c): consumes four digits and stores the trailing command | Refresh-string behavior is implemented; transmission requires an explicit host call |
| X7, DEBUG transmission | [CMakeLists.txt](../CMakeLists.txt): `RIPLIB_ENABLE_DEBUG_DIRECTIVE` defaults OFF | Unsolicited debug output is disabled by default; this does not settle macro-name ambiguity |
| N1, backtick called a genuine addition | DLL slot 83 is present | The opcode itself is driver-backed, although the former composite-icon interpretation was wrong; D-31 replaces it with an affine chord |

Other conflict entries, text-variable semantics,
font rendering, host-command behavior, and historical-version differences
were not exhaustively re-adjudicated here. No upstream repository changes
or messages were made.

## Reproduction and validation

```powershell
python scripts/dll-conformance.py C:/RIPtel/RIPSCRIP.DLL -v --corpus C:/RIPtel --crosswalk docs/riptel-crosswalk.md --reference build-crosswalk-reference/version/3.0/ripscrip/9.0-command-reference.md --reference-revision 2fb17724b6122a5ad5cd1df38b69b8cce3a7079f
python scripts/ref-compare.py C:/RIPtel/RIPSCRIP.DLL build-crosswalk-reference/version/3.0/ripscrip/9.0-command-reference.md
python tests/test_dll_conformance.py
```

The reference path is an ignored local clone of the pinned commit; the
DLL and vendor corpus are not vendored. Re-injecting the printable-only
filter produces exactly three dispatch-accounting findings. Tests also
cover truncated records, orphan continuations, duplicate keys, upstream
ESC/level-9/unlinked/unassigned rows, numeric Level 2 macro values, and the earlier handler-boundary defects.

The initial runtime regressions passed **326/326**. The isolated pre-fix runtime failed
twelve of those tests (new public helper definitions supplied only to link
the test harness); the current runtime passes them. The geometry fixture
contains **52 point runs** generated by executing the pinned DLL helper in
an x86 emulator, including coincident rays, skewed axes, truncation, and
degeneracies. A further 136 deterministic point-run comparisons exposed and
verified fixes to same-segment direction and missing-ray fallback handling.
This is geometry evidence for sampled inputs, not universal pixel parity.

All **35 corpus scenes** replay cleanly. Asset requests and region counts are
unchanged. Fill corrections change these four scene summaries:

| Scene | Foreground pixels before → after | Distinct colors before → after |
|---|---|---|
| CURVES | 67,814 → 67,814 | 2 → 7 |
| LANDSCPE | 111,595 → 200,890 | 6 → 7 |
| SEABYME1 | 110,681 → 173,865 | 6 → 10 |
| SEANITE | 121,222 → 180,882 | 6 → 12 |

The other 31 scene summaries are unchanged. Filled Bezier now uses the
fill brush, and empty fills paint the background; those are intentional
rendering changes, not silently refreshed golden images. The six existing
compatibility frame-hash fixtures remain unchanged.

The standing validator re-derives **58 claims** from the image, source and
corpus. The 129-row binary-table and command/spec document checks pass.
Field-name review remains advisory: it reports 16 unmatched diagnostic
concepts across nine commands, including known shared-name diagnostics;
those are not counted as resolved defects. See the final verification notes below for
sanitizer and fuzz results.

Additional reproduction:

```powershell
python scripts/dll-affine-fixtures.py C:/RIPtel/RIPSCRIP.DLL --check
python scripts/dll-validate-claims.py C:/RIPtel/RIPSCRIP.DLL
python scripts/check-dll-table.py C:/RIPtel/RIPSCRIP.DLL
python scripts/check-field-names.py C:/RIPtel/RIPSCRIP.DLL
python scripts/check-command-docs.py
python scripts/check-spec-examples.py
```

Oracle regeneration requires Python Unicorn; normal tests use the checked-in
numeric fixtures and need neither Unicorn nor the unvendored DLL.

## Initial audit verification (PR #4)

- All six CTest groups pass on Windows GCC and Linux Clang with ASan/UBSan;
  the final parser suite is 328/328. Fourteen audit instrument tests pass.
- Linux coverage-guided fuzzing, instrumenting the library as well as its
  harness, completed 60,717 runs in 61 seconds with no finding. It began
  with 86 directed seeds. The earlier sanitizer sweep also completed one
  million seeded mutations; the final geometry/fill changes received a
  further 100,000 seeded mutations.
- The Cortex-M33/RP2350 static archive builds with `arm-none-eabi-gcc`.
  This is a cross-compile check, not an on-device execution result.
- Predicate reinjection detects a wrong DLL closure mode, a nonzero EMPTY
  brush row, and replacing the arc handler with screen copying. The actual
  driver remains unmodified; these checks mutate bytes/source in memory.
- `git diff --check` passes. The version stays at 2.0.1 pending a separate
  release decision. No bbs-land repository changes were made.

For the fuzz build, configure with Clang and enable `RIPLIB_BUILD_TESTS`
and `RIPLIB_BUILD_FUZZ`; CMake instruments both library and harness. Run
`scripts/fuzz-seeds.py <corpus-directory>` to export the 86 directed seeds,
then run CTest plus both fuzz executables. The test runner disables inlining so hundreds of large
session fixtures do not combine into one oversized optimized stack frame.

### Publication verification findings

- A C++ consumer failed to link C API functions because six public headers
  lacked C linkage guards. The guards now cover all six; installed-package
  and source-vendoring consumers pass on Linux GCC and Windows MSVC. CI
  exercises both integration paths across its OS/build-type matrix.
- A synthetic nearly parallel affine segment produced an out-of-range
  double-to-int conversion under UBSan. Rejecting impossible intersections
  before conversion fixes it while all 52 driver-derived fixtures still pass.
  A deterministic 500,000-case geometry stress sweep passes with ASan,
  UBSan and float-cast-overflow instrumentation.
- Tiled icon iteration used signed 16-bit counters and could wrap forever
  at 32767. A regression times out against the old loop and passes with
  32-bit counters, checking both axes, rendered pixels, and restored clipping.
- The corrected affine command no longer needs the internal scaled-screen-copy
  helper. Removing its unused implementation/declaration removes dead code;
  all nine existing coverage floors pass without lowering any threshold.
- The CI fuzz option formerly instrumented only its entry point. It now
  instruments RIPlib as well, rejects unsupported configurations, and seeds
  coverage-guided runs from the mutation harness. An option-only Clang build
  completed 182,340 runs in 61 seconds without a sanitizer finding.
- Generated Python caches are excluded from version control. Source hashes
  in the crosswalk normalize CRLF so checkout conventions do not change them.

## Remaining-issue pass (D-34 through D-37)

| Issue | Result |
|---|---|
| Global radix recorded but ignored | Session base reaches all command levels and width normalization; fixed-radix exceptions remain; disconnect resets it |
| Apparent duplicate `3D` and missing service dispatch | Literal prefix recovery proves `3D` is delay and `9D` a host expression; all five level-9 services dispatch, four historical level-3 aliases remain |
| Encoded-stream uncertainty | The bounded `9U` handler validates type but contains no payload decoder; RIPlib records metadata without inventing a codec |
| Icon ROP and stretch | ROP reads the actual args[3] column; stretch uses device/logical dimension ratios |
| Approximate fill masks | All ten patterned masks match the DLL's 80 row values; generic drawing API pattern IDs stay compatible |
| Query protection and timing | Definitions defer output, check target protection, clear on `$OFF$`, evaluate templates at events, and honor mouse-field precedence |
| Shared audit blind spot | All table readers use prefix bytes; mutation tests vary prefix independently of slot; table checker rejects the five old wrong prefixes |

There are now 337 parser tests and 15 audit-instrument tests. All six CTest
groups pass under Windows GCC/MSVC and Linux Clang ASan/UBSan; 35 shipped
scenes replay without failure. All 67 driver/source predicates hold. All
nine coverage floors pass, GCC `-fanalyzer` is clean, and the RP2350 archive
builds. Fuzzing with 93 directed seeds completed 183,890 runs in 61 seconds
without a finding. All nine new regression functions fail against the
pre-fix implementation and pass after correction.

Comparing all 35 scene metrics with baseline changes only BUTTONS.RIP's
foreground count (90,788 to 93,716, with the corrected fill masks). Its
10 colors, two asset requests and 20 regions are unchanged; all other
scene metrics and host-silence assertions remain unchanged. Reinjection
of the old prefix, icon ROP column and a single wrong brush bit makes
the strengthened claim validator fail in each case.

This pass changes no wire field widths and preserves legacy service aliases.
The public session structure grows, so consumers must rebuild. Exact fonts,
GDI edges, clipboard screen capture, independent window surfaces and full
host-macro execution remain explicit integration/rendering boundaries.

## Raster value iteration (D-38)

Native-size image restores now obey the active viewport for every existing
drawing raster operation. Tile boxes intersect that viewport, retain the
original tile phase and skip invisible tiles. Clipboard captures initialize
offscreen padding, Level 2 shares that implementation, and scaled port-copy
scratch images are initialized before capture. Unrepresentable clipboard
dimensions are rejected without damaging the previous clipboard.

Six regressions cover pixels, source stride, empty clips, extreme origins,
dirty rows, tile phase, reused capture bytes, metadata and wire-level scaled
port copies. The first five fail against 7f773bd. There are now 341 parser
tests and 43 drawing tests. Windows GCC/MSVC and Linux Clang ASan/UBSan pass
all six CTest groups. All 35 corpus scene metrics are identical to 7f773bd;
67 driver/source predicates and all nine coverage floors still pass. GCC
static analysis and the RP2350 archive build pass. A further 100,000 seeded
mutations pass under sanitizers.

No syntax or public session layout changes in this iteration. Exact icon
clipboard screen capture remains open: static driver tracing finds an
exclusive source rectangle passed to a helper that allocates inclusive
dimensions, then selects a scaling path. That needs an executable rectangle
and copy-call oracle before changing capture dimensions. See D-38 and
[iteration-state.md](iteration-state.md) for the evidence and next experiment.

## Image operands and executable rectangle oracle (D-39)

Image mode 4 now complements source samples for icon drawing, clipboard
pasting and port copies, including scaling and tiling. It previously inverted
the destination. The generic drawing API keeps its destination-inversion
contract. Level 2 now shares the image blitter. Syntax and public structures
are unchanged.

The new `scripts/dll-raster-fixtures.py` executes six capture cases and eighteen
ROP selections in the pinned driver, with explicit file/palette/allocation
stubs and modeled rectangle APIs. It records actual driver arguments to GDI
without rasterizing them. Checked-in call fixtures and ROP truth tables make
the results reviewable; ordinary CI does not require the DLL or Unicorn.
Regenerate with `python scripts/dll-raster-fixtures.py C:/RIPtel/RIPSCRIP.DLL`
and verify with the same command plus `--check`.

Three new runtime tests fail against bc9e7e1 and pass with the fix. The suite
now has 344 parser tests, 43 drawing tests and 16 audit-instrument tests.
All 70 driver/source predicates hold. All 35 corpus scene metrics are
unchanged. Native/scaled/tiled output and state restoration are checked
against the driver's ROP truth tables; a new directed seed reaches image NOT.

The capture discrepancy is now demonstrated at the call boundary: 2x7 is
captured into 3x8, and right/bottom clipping shrinks the source without
shrinking the destination allocation. Exact capture pixels, GDI palette
remapping and resampling remain unverified. RIPlib still caches source icons.

## Native memory-DIB capture iteration (D-40)

This supersedes D-39's source-icon caching limitation. Standard LOAD_ICON
stretch now follows measured native GDI sampling, and its clipboard flag
captures the displayed pixels after the raster operation. Capture uses the
actual rendered rectangle, expands both dimensions by one, and retains that
allocation when right/bottom clipping shrinks its source. Ordinary captures
use row copies with repeated edges. Full-frame capacity grows by 1,041 bytes
to 257,041; oversized captures preserve the previous clipboard. Wire syntax,
public structure layout and other extension sampling remain unchanged.

The evidence consists of nine bounded DLL call cases, 1,024 native size-pair
maps, 360 two-dimensional grids and 162 native capture configurations. All
capture configurations use top-down 8-bit memory DIBs, matching palettes
(identity or permuted grayscale), stretch modes 1/2/3, and COPY/XOR/NOT source.
The near-size sampling shortcut requires BOTH dimensions to differ by at
most one; mixed-ratio fixtures caught an initial per-axis hypothesis.

Six new runtime tests bring the parser suite to 350, alongside 43 drawing
and 16 audit-instrument tests. The wire capture regression fails against
3780b18 and passes with this correction. The remaining tests exercise new
helpers, style bounds, negative origins, capacity failure and full-frame
capture. All six CTest groups pass under Windows GCC/MSVC and Linux Clang
sanitizers. All 35 scene metrics remain unchanged, 70 driver/source claims
hold, all nine coverage floors pass, GCC static analysis is clean, and the
RP2350 archive builds. A directed seed now reaches cached-icon screen capture;
100,000 additional seeded mutations pass under ASan/UBSan.

Nonzero port origins remain a separate boundary: the bounded driver helper
chain adds the origin again during capture. Full port setup has not been
executed; RIPlib retains its absolute framebuffer model. This evidence also
does not establish arbitrary palette remapping, independent port surfaces,
halftone, historical display-driver or font parity. D-40 records the exact
scope and reproduction commands; [iteration-state.md](iteration-state.md)
identifies the next evidence needed.

## Port coordinate and surface evidence (D-41)

The new `dll-port-fixtures.py` executes real port creation and switching,
followed by complete decoded LOAD_ICON or PORT_COPY handlers through return.
Its 32 cases distinguish shared-screen ports from offscreen ports, which
reset their origin to zero and select a separate DC. File/GDI services remain
modeled; this is call-level evidence, not a live-terminal pixel comparison.

The extra LOAD_ICON capture offset is confirmed for shared ports: logical
port origin (10,20), icon (3,7), becomes device display (13,30) and capture
source (23,52). PORT_COPY instead adds the source/destination origin once,
uses exclusive extents and floor-scales both endpoints. A logical source
(3,7)-(5,14) produces 2x8 pixels. RIPlib's absolute coordinates, inclusive
copy extents and informational offscreen flag remain explicit differences.

No runtime behavior changes in this iteration. Two new instrument tests
(18 total) preserve the measured contracts and reject four fixture mutations.
A mutation removing the driver's X-coordinate addition is also rejected.
All 350 parser tests, 43 drawing tests and 70 existing claims remain green.
The next input class is PORT_COPY's all-zero rectangles, reversed endpoints
and clipping, before making a coherent correction to its copy semantics.

## Port-copy correction (D-42)

PORT_COPY now uses relative coordinates, exclusive extents and floor-scaled
Y endpoints. All-zero destinations scale to their entire viewport;
position-only destinations retain native size. Empty/reversed rectangles
and invalid ROPs are rejected. Trimming follows the driver's native/scaled
distinction, and scaled copies use measured GDI sampling. The native COPY
shortcut now obeys the active clip; overlapping samples and draw state are
preserved. Syntax and public structure layout are unchanged.

The driver oracle adds 84 boundary cases. Fifty shared-port fixtures check
runtime pixels across all five ROPs and invalid mode 5, using matching stored
viewport geometry. Both new runtime tests fail with the previous ripscrip2.c
and pass after correction. There are 352 parser, 43 drawing and 19 instrument
tests; all six CTest groups, 70 claims, nine coverage floors, static analysis,
RP2350 build and 100,000 additional sanitizer mutations pass.

All 35 scenes replay, but two change: FONTS foreground 15,494 -> 2,092
(3 -> 2 colors), SPECLEFX foreground 82,788 -> 129,372 (7 colors retained).
The other 33 scene metrics and all asset-request/region/host-silence results
are unchanged. Both changed scenes use larger independent offscreen surfaces
that RIPlib still maps onto its one display. This pass proves copy behavior
for equivalent viewports; it does not establish 2P geometry, independent
storage or full visual parity. Those remain the next implementation boundary.

## Port lifetime correction (D-43)

The complete native deletion path disproves the earlier reserved-destination
and source-zero explanations. `2p` now deletes all unprotected secondary
ports when source is zero and always selects its destination after a valid
request, including when deletion is refused or its source is absent. An
empty destination is recreated. `2s` cannot protect the permanent master
port. Deleted queries are cleared and protected destination state survives.
Wire widths, gates and public structure layout are unchanged.

The new oracle records 18 lifetime sequences, 18 definition cases and eight
allocation failures. Thirty-two wire steps compare allocated slots,
protection and selection against the driver; two new runtime tests fail
before the fix. Instrument predicates reject altered states, geometry,
resource cleanup, accounting, diagnostics and missing/duplicate cases.
Removing the actual native destination-selection call is also detected.
All 354 parser, 43 drawing and 20 instrument tests pass, with sanitizers,
100,000 mutations, coverage floors, static analysis and the RP2350 build.
All 35 corpus metrics match D-42; all 70 claims hold.

Geometry/failure evidence is conditional on modeled allocation services:
shared rectangles are not clamped, reversed dimensions wrap, and failed
replacement can lose the old port and recreate a default shared port. That
evidence guides future storage work; no large-surface implementation or
Windows pixel parity is claimed. See D-43 and the iteration checkpoint.

## Active redefinition correction (D-44)

Active `2P` redefinition now applies the new stored viewport, resets drawing
position and preserves the current style for both activation choices. Two
new runtime regressions fail on f60270f and pass after correction, including
protected refusal, switch persistence and drawing inside the new clip.

The driver oracle adds 16 cases through the next native line handler, its
clipping and ROP setup, and real graphics-style lock/unlock. Styles retain
an independent selected index: RIPlib's broader per-port style model remains
an explicit difference. Port-definition geometry and offscreen storage also
remain open. Fixing an import-address collision in the emulator leaves all
older driver fixtures unchanged; adversarial tests cover that bug and the
new fixture predicates.

All 356 parser, 43 drawing and 22 instrument tests pass, along with sanitizer
tests, 100,000 mutations, coverage floors, static analysis, the RP2350 build,
70 claims and conformance. All 35 corpus metrics remain unchanged from D-43.
