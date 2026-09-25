# Iteration checkpoint — 2026-09-25, D-45

STATE: PROVEN by 128 native decoded style-selection/protection cases and
three sequences through native reset-style primitives. Runtime matches
colors, ROPs, flags and defaults; port changes leave style selection intact.
Brush realization, palette lookup and GDI remain modeled. Full reset,
font rasterization and scene pixel parity are not claimed.

ADVANCE: |2Y now restores 36 independent style slots; port switching
restores only cursor and viewport. Slot zero cannot be protected. Unused
styles take defaults, contrary to the historical reference's copy-current
prose but matching DLL execution. Soft reset preserves protected styles,
selects zero and clears unprotected styles; disconnect clears the table.
Custom fill rows and character spacing are now restored to the renderer.
Wire syntax is unchanged. Consumers must rebuild for the expanded session
structure (GCC: 1,296-byte style table plus 8 active pattern bytes).

FRONTIER: 2P still clamps/sorts geometry to the shared framebuffer and uses
inclusive/ceiling-scaled stored rectangles. D-43 native fixtures prove
exclusive, floor-scaled, potentially off-display rectangles. Independent
offscreen storage remains absent: FONTS needs 371,200 bytes and SPECLEFX
1,201,824 bytes at 8 bpp. Driver pixel accounting is not a portable byte
budget. Other resource-table backing stores and complete reset side effects
remain separate fidelity boundaries.

NEXT:

1. [/debug] Apply D-43 definition fixtures to exact shared-port geometry,
   preserving existing wire widths and established short-form tolerances.
2. [/verify] Carry rectangles through clipping/copy paths with empty,
   reversed and off-display cases; keep native geometry distinct from
   framebuffer safety and full-scene pixel claims.
3. [/decide] Derive bounded optional offscreen allocation from native
   lifetime/failure evidence and the embedded RAM budget, then implement
   and validate the FONTS/SPECLEFX path.

COMPOUND: D-45 in spec/12-dll-provenance.md and register 14.3.13;
scripts/dll-style-fixtures.py, style_slots.json and generated C cases.
359 parser, 43 drawing, 23 instrument tests; all six CTest groups,
Windows GCC/MSVC and Linux Clang ASan/UBSan, 100,000 seeded mutations,
nine coverage floors, static analysis and RP2350 build pass. All 70
claims hold; conformance reports zero defects. All 35 corpus summaries,
requests, regions and passive host silence match D-44. Two new runtime
regressions fail before correction and pass afterward.

PROMPTS:

- "Use D-43 through D-45 to fix shared-port geometry without changing syntax."
- "Derive and validate bounded offscreen storage for FONTS and SPECLEFX."

Reconcile git status/log, fresh CTest, dll-conformance.py,
dll-validate-claims.py and dll-style-fixtures.py <DLL> --check.
Driver: C:/RIPtel/RIPSCRIP.DLL, MD5 bade8b1f4e467ac7ad4edb2639738d4c.
Reference revision: 2fb17724b6122a5ad5cd1df38b69b8cce3a7079f.
PR #5: codex/remaining-protocol-compatibility. No merge/tag/release implied.
