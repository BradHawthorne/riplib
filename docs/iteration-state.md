# Iteration checkpoint — 2026-09-24, D-42

STATE: PROVEN by complete decoded-handler execution: 84 new PORT_COPY
boundary cases plus the 32 D-41 port/icon cases. Runtime pixel tests compare
50 shared-port fixtures across five supported ROPs and invalid mode 5.
The fixtures inject matching viewport geometry; they do not validate 2P
creation or independent offscreen storage. GDI sampling uses the separate
native memory-DIB evidence from D-40. This is not live-terminal pixel parity.

ADVANCE: 2C now uses relative coordinates, exclusive extents, floor-scaled
Y endpoints, full-viewport zero rectangles, position-only native copies,
driver rejection rules, paired native trimming and independent scaled
trimming. Scaled copies use the measured sampler. Native COPY obeys the
active clip while retaining its allocation-free memmove path. Two new
runtime regressions fail with 4d24ce6's ripscrip2.c and pass after correction.
The instrument rejects unexpected errors, missing events, duplicate cases
and wrong DCs. Wire syntax and public structure layout are unchanged.

FRONTIER: Port creation still clamps geometry to the display and treats
offscreen storage as informational. All 35 scenes replay cleanly, with
unchanged asset requests, regions and passive host silence. 33 retain their
render metrics. FONTS foreground changes 15,494 -> 2,092 (3 -> 2 colors);
SPECLEFX changes 82,788 -> 129,372 (7 colors). These are the two 2C scenes,
and both require independent, larger offscreen surfaces. The changes are
not evidence of complete rendering fidelity.

NEXT:

1. [/verify] Extend the native 2P oracle to reversed/empty/large rectangles,
   creation failure, replacement, switching and lifetime behavior.
2. [/debug] Correct bounded port-definition geometry with fail-before tests,
   retaining wire widths and public compatibility.
3. [/decide] Define optional surface storage and its memory budget before
   changing shared drawing paths. FONTS requests 1280x290 device pixels
   (371,200 bytes at 8 bpp); SPECLEFX requests 936x187 and 936x1097
   (1,201,824 bytes combined). Eager allocation of every port is unsuitable
   for small embedded hosts; fidelity requires an explicit storage contract.

COMPOUND: D-42 in spec/12-dll-provenance.md; divergence register 14.3.11;
scripts/dll-port-fixtures.py, port_calls.json and generated port_copy.h.
352 parser, 43 drawing and 19 audit-instrument tests; all six CTest groups
pass with Windows GCC/MSVC and Linux Clang ASan/UBSan. All 70 claims and
nine coverage floors hold. GCC static analysis, RP2350 archive build and
100,000 additional seeded sanitizer mutations pass.

PROMPTS:

- "Verify 2P geometry and port lifetime from the D-42 checkpoint."
- "Design bounded optional offscreen surfaces for the FONTS and SPECLEFX cases."

Reconcile: git status/log, fresh CTest, dll-conformance.py and
dll-validate-claims.py; then dll-port-fixtures.py <DLL> --check,
dll-raster-fixtures.py <DLL> --check and gdi-raster-fixtures.py --check.
The last requires Windows; portable unit tests require no proprietary DLL.
Driver: C:/RIPtel/RIPSCRIP.DLL, MD5 bade8b1f4e467ac7ad4edb2639738d4c.
PR #5 uses codex/remaining-protocol-compatibility. No merge/tag/release implied.
