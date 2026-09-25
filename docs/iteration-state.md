# Iteration checkpoint — 2026-09-24, D-43

STATE: PROVEN by 18 complete decoded port-lifetime sequences (34 steps),
18 shared/offscreen definitions and eight allocation failures. Two runtime
regressions compare 32 base-36 wire steps plus query cleanup, destination
state restoration, truncation and invalid base-64 indices. Both fail on
a2f0eb3 and pass after the correction. GDI resources and synchronization
remain modeled; metadata evidence does not establish pixel parity.

ADVANCE: 2p now honors its destination, validates both indices, deletes all
unprotected secondary ports for source zero, and selects destination after
refused or missing-source deletion. Empty destinations are recreated.
2s cannot protect port zero. Wire fields, length gates, aliases and public
structure layout are unchanged. Instrument mutation tests reject altered
state, geometry, cleanup, budget, diagnostics and duplicated cases; removing
the native post-delete selection call also fails validation.

FRONTIER: 2P shared definitions use exclusive, floor-scaled, unclamped
geometry. Empty dimensions reach initialization; reversed differences wrap
through unsigned 16-bit dimensions. Offscreen allocation has a pixel budget.
Replacement deletes old storage before attempting new allocation; failure
can recreate the former active slot as a default shared port. RIPlib still
clamps 2P, lacks independent offscreen storage, and does not reload the
active viewport when redefining that active slot without the activation bit.
Drawing-state synchronization is modeled, so driver style behavior needs
further verification. Do not infer it from metadata tests.

NEXT:

1. [/verify] Trace active 2P redefinition through drawing-state synchronization
   and observe which style/viewport fields survive or reset.
2. [/debug] Correct port-definition geometry and active viewport application
   with fail-before tests, including empty/off-display rectangles.
3. [/decide] Define optional host-backed offscreen storage and a bounded
   allocation policy, using D-43's resource accounting/failure fixtures.
   FONTS needs 371,200 bytes and SPECLEFX 1,201,824 bytes at 8 bpp. Keep
   embedded memory limits explicit; DLL pixel budgets are not byte budgets.

COMPOUND: D-43 in spec/12-dll-provenance.md; register 14.3.12;
scripts/dll-port-lifecycle-fixtures.py and port_lifecycle.json/.h;
corrected Delete Port syntax/semantics in spec/05 and the appendix.
354 parser, 43 drawing, 20 instrument tests; all six CTest groups pass
on Windows GCC/MSVC and Linux Clang ASan/UBSan. All 70 existing claims,
nine coverage floors, GCC static analysis and RP2350 build pass; another
100,000 seeded sanitizer mutations pass. All 35 corpus metrics, asset
requests, region counts and passive host silence match D-42. FONTS and
SPECLEFX's existing offscreen visual-fidelity limitation remains unresolved.

PROMPTS:

- "Verify active port redefinition and fix its viewport/state synchronization from D-43."
- "Use the D-43 geometry and failure fixtures to design bounded offscreen storage."

Reconcile: git status/log, fresh CTest, dll-conformance.py,
dll-validate-claims.py and dll-port-lifecycle-fixtures.py <DLL> --check.
Existing port/raster/GDI fixture checks remain useful when those paths change.
Driver: C:/RIPtel/RIPSCRIP.DLL, MD5 bade8b1f4e467ac7ad4edb2639738d4c.
PR #5 uses codex/remaining-protocol-compatibility. No merge/tag/release implied.
