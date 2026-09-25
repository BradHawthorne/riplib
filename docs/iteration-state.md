# Iteration checkpoint — 2026-09-24, D-40

STATE: PROVEN within the measured Windows memory-DIB profile: standard icon
stretch and clipboard capture match native samples, dimensions and ROP
results. There are 350 parser, 43 drawing and 16 audit-instrument tests;
all six CTest groups pass across Windows GCC/MSVC and Linux Clang sanitizers.
All 70 driver/source predicates hold. All 35 corpus scene metrics are
unchanged. Nine coverage floors, GCC static analysis and the RP2350 build pass.
An additional 100,000 seeded mutations pass under ASan/UBSan.

ADVANCE: 1I now captures the rendered framebuffer instead of caching the
source asset. The wire capture regression fails against 3780b18 and passes
after correction. Six added tests cover native fixtures, two-dimensional
sampling, negative origins, style bounds, capacity failure and full-frame
captures. A directed fuzz seed reaches the cached-icon capture path. Normal
captures use row copies plus repeated edges; full-frame clipboard capacity
increases by 1,041 bytes. Syntax and public structure layout are unchanged.

FRONTIER: Nonzero port origins and independent port surfaces. The bounded
helper chain adds the port origin again during capture; full handler/port
setup has not been executed. The portable renderer retains absolute screen
coordinates. GDI evidence covers matching 8-bit DIB palettes and modes 1/2/3;
unequal palettes, halftone, historical display drivers and fonts remain open.

NEXT:

1. [/verify] Trace and execute the complete 1I handler with nonzero port setup,
   checking whether its caller transforms coordinates before show/capture.
2. [/audit] Compare that result with Level 2 port creation/selection and screen
   coordinates; derive an explicit contract before changing port origins.
3. [/decide] If fidelity needs independent surfaces, present the measured
   behavior and memory cost before expanding the embedded renderer model.

COMPOUND: D-40 in spec/12-dll-provenance.md; nine driver call cases in
tests/fixtures/raster_calls.json; native generator gdi-raster-fixtures.py;
1,024 axis maps, 360 grids and 162 capture configurations, with checked-in
JSON/C results. Native execution uses Windows memory DIBs, never desktop DCs.

PROMPTS:

- "Verify the full LOAD_ICON handler with nonzero port origins from D-40."
- "Audit Level 2 port coordinates against the driver before changing the framebuffer model."

Reconcile before continuing: git status/log, fresh CTest, conformance and
claim validation, then dll-raster-fixtures.py <DLL> --check and (on Windows)
gdi-raster-fixtures.py --check. Driver: C:/RIPtel/RIPSCRIP.DLL, MD5
bade8b1f4e467ac7ad4edb2639738d4c. Upstream clone: build-crosswalk-reference,
revision 2fb17724b6122a5ad5cd1df38b69b8cce3a7079f. PR #5 carries this work on
codex/remaining-protocol-compatibility. No release/tag/merge is implied.
