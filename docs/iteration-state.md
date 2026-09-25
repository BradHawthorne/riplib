# Iteration checkpoint — 2026-09-24, D-39

STATE: PROVEN by runtime tests and bounded driver execution: image ROP 4
inverts the source. The suite has 344 parser tests, 43 drawing tests and 16
audit-instrument tests. All 70 driver/source predicates hold; 35 corpus scene
metrics are unchanged. The driver oracle reproduces six capture cases and
eighteen ROP selections. Exact GDI pixels remain unproven.

ADVANCE: Icon, clipboard and port images now use source inversion, including
native/scaled/tiled paths. Level 2 shares the image blitter. Three regressions
fail against bc9e7e1 and pass after correction. A directed fuzz seed exercises
capture followed by NOT pastes and port copies. Generic drawing NOT, wire
syntax and public layout are unchanged.

FRONTIER: 1I still caches source pixels instead of capturing the rendered
screen. The bounded executable oracle proves that a 2x7 icon allocates 3x8
and copies through StretchBlt; stretched 2x8 allocates 3x9. Right/bottom
clipping shrinks the source but retains the larger destination allocation.
File/palette/allocation operations are stubbed, rectangle APIs are modeled,
and GDI calls are recorded without pixel execution. This is stronger than
the previous static trace, but not a complete capture implementation oracle.

NEXT:

1. [/verify] Run the recorded BitBlt/StretchBlt calls against controlled Windows
   memory DIBs with explicit palettes and stretch modes. Compare sample values,
   not just rectangles; include clipping, XOR and source inversion.
2. [/debug] Use that evidence to implement icon clipboard capture, including
   dimensions and clipping. Retain explicit tolerance for GDI color mapping
   that cannot be represented by the portable indexed renderer.
3. [/audit] Check port-origin and left/top clipping independently: the current
   oracle's capture cases use a zero-origin source port and right/bottom clips.

COMPOUND: D-39 in spec/12-dll-provenance.md; executable generator
scripts/dll-raster-fixtures.py; results in tests/fixtures/raster_calls.json
and image_rops.h; three driver/source predicates and three value regressions.

PROMPTS:

- "Iterate the GDI memory-DIB capture experiment in docs/iteration-state.md."
- "Audit port origins and left/top capture clipping with the driver oracle."

Reconcile before continuing: git status/log, fresh CTest, conformance and
claim validation, then run dll-raster-fixtures.py with --check.
Driver: C:/RIPtel/RIPSCRIP.DLL, MD5 bade8b1f4e467ac7ad4edb2639738d4c.
Upstream reference clone: build-crosswalk-reference, revision
2fb17724b6122a5ad5cd1df38b69b8cce3a7079f. PR #5 carries this work on
codex/remaining-protocol-compatibility. No release/tag/merge is implied.
