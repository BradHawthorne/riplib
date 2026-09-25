# Iteration checkpoint — 2026-09-24, D-44

STATE: PROVEN by 16 decoded redefinition cases through the next native line
handler, clipping, ROP selection and graphics-style lock/unlock. Successful
definition resets the target position; protection preserves it. Selected
style slot 7 and all seeded style bytes survive port selection/redefinition.
GDI/memory handles, caret and invalidation remain modeled. Focus is inactive.
No live pixels or complete port/style parity are claimed.

ADVANCE: Active 2P redefinition now applies the new stored viewport,
preserves current style and resets drawing position, with or without the
activation flag. The portable mirror survives switch-away/back. Two runtime
regressions fail on f60270f and pass after correction. Wire widths, length
gates, aliases and public structure layout are unchanged.
Fixed an emulator import-address collision after stub removal; all existing
raster/port/lifetime fixtures remain unchanged. Two instrument regressions
cover that bug and five classes of redefinition-fixture mutation. Removing
the native next-line ROP call is detected.

FRONTIER: The driver selects graphics styles independently of ports
(RIPINST+0x0A versus +0x22); RIPlib still saves/loads per-port styles and
treats its explicit style selection as metadata. New/other-port activation
therefore needs a coherent style-slot correction. 2P geometry still uses
RIPlib's inclusive/clamped representation. Independent offscreen storage
remains absent, including the FONTS/SPECLEFX fidelity gap documented in D-42.
Active redefinition is fixed for stored geometry, not for those larger models.

NEXT:

1. [/verify] Trace explicit 2Y style selection and style lifetime alongside
   2P/2s; derive which attributes belong to style slots versus port cursors.
2. [/debug] Correct the proven style-selection semantics without changing
   wire syntax; retain source-level rebuild requirements explicitly if needed.
3. [/verify] Apply D-43's definition fixtures to a geometry/storage design,
   including empty/reversed/outside rectangles and bounded allocation failure.
   FONTS needs 371,200 bytes and SPECLEFX 1,201,824 bytes at 8 bpp; driver
   pixel accounting is not a portable byte budget.

COMPOUND: D-44 in spec/12-dll-provenance.md; register 14.3.13;
scripts/dll-port-redefine-fixtures.py and tests/fixtures/port_redefine.json;
updated active-definition and audit documentation.
356 parser, 43 drawing, 22 instrument tests; all six CTest groups pass
with Windows GCC/MSVC and Linux Clang ASan/UBSan. All 70 claims,
nine coverage floors, static analysis and RP2350 build pass. Another
100,000 seeded sanitizer mutations pass. All 35 corpus metrics, requests,
regions and passive host silence match D-43.

PROMPTS:

- "Verify explicit style selection and port switching from D-44, then fix the confirmed mismatch."
- "Use D-43 and D-44 to design compatible port geometry and bounded offscreen storage."

Reconcile: git status/log, fresh CTest, dll-conformance.py,
dll-validate-claims.py and dll-port-redefine-fixtures.py <DLL> --check.
The raster/port/lifetime oracles also verify the shared emulator change.
Driver: C:/RIPtel/RIPSCRIP.DLL, MD5 bade8b1f4e467ac7ad4edb2639738d4c.
PR #5 uses codex/remaining-protocol-compatibility. No merge/tag/release implied.
