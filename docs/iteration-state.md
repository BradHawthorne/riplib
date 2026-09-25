# Iteration checkpoint — 2026-09-24, D-41

STATE: PROVEN by 32 decoded-handler executions with real driver port
creation/switching: shared and offscreen ports have different origins and
DCs; 1I capture applies an extra shared-port origin; explicit 2C coordinates
are relative, use exclusive endpoints and floor-scale both Y endpoints.
File/GDI services remain modeled. This is not live-terminal pixel parity.

ADVANCE: The D-40 port-origin question is resolved at the complete selected
handler path, including real portInit and the show-bitmap epilogue.
The new oracle and checked-in port_calls.json preserve 16 LOAD_ICON and
16 PORT_COPY cases. Two tests raise the audit-instrument suite to 18 and
reject origin, surface, capture and missing-case mutations. A removed native
X-add instruction is also detected. No runtime behavior changes in D-41.

FRONTIER: RIPlib's explicit port copies still use absolute coordinates,
inclusive extents and ceiling-scaled bottom Y. Offscreen selection remains
informational. A shared port defined at logical (10,20) displays icon (3,7)
at device (13,30), but the driver captures from (23,52). True offscreen
creation resets its origin to zero. One image-specific offset patch cannot
resolve these different contracts across the renderer.

NEXT:

1. [/verify] Extend the PORT_COPY oracle to all-zero source/destination,
   destination-position-only, reversed endpoints and clipping on every side.
2. [/debug] Correct bounded copy extents/rounding with fail-before pixel tests,
   preserving every wire field and documenting remaining storage differences.
3. [/decide] Design shared-port coordinate handling and optional offscreen
   storage together before changing all drawing paths. Avoid eager full-size
   allocation: 35 additional 640x400 8-bit surfaces need 8,960,000 bytes.

COMPOUND: D-41 in spec/12-dll-provenance.md; divergence register 14.3.11;
scripts/dll-port-fixtures.py and tests/fixtures/port_calls.json. Existing
350 parser/43 drawing tests and 70 driver/source claims remain green.
Existing tests do not establish runtime port parity for the new findings.

PROMPTS:

- "Verify PORT_COPY zero rectangles, reversed endpoints and clipping from D-41."
- "Fix port-copy extents using the driver fixtures, keeping wire syntax compatible."

Reconcile: git status/log, fresh CTest, dll-conformance.py and
dll-validate-claims.py; then dll-port-fixtures.py <DLL> --check,
dll-raster-fixtures.py <DLL> --check and gdi-raster-fixtures.py --check.
The last requires Windows; the portable unit tests require no proprietary DLL.
Driver: C:/RIPtel/RIPSCRIP.DLL, MD5 bade8b1f4e467ac7ad4edb2639738d4c.
PR #5 uses codex/remaining-protocol-compatibility. No merge/tag/release implied.
