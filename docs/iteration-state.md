# Iteration checkpoint — 2026-09-24

STATE: PROVEN by six CTest groups, Windows GCC/MSVC, Linux Clang ASan/UBSan,
35 corpus replays with identical scene metrics, 67 driver/source predicates,
nine coverage floors, GCC static analysis and an RP2350 archive build.
100,000 seeded sanitizer mutations pass. These establish the tested portable
contracts; exact Windows GDI output remains unproven.

ADVANCE: D-38 fixes native blits escaping the viewport, tiles widening it,
stale/uninitialized offscreen capture cells and signed clipboard metadata
overflow. Six regressions added; the first five fail on 7f773bd. Level 2 now
shares clipboard capture. Tile iteration skips invisible work without moving
its phase. Wire syntax and public layout are unchanged by this iteration.

FRONTIER: `1I` still caches source pixels instead of capturing the rendered
screen. Static tracing shows a one-pixel extent expansion and a scaled copy
in the driver's capture helper; an exact portable model needs execution
evidence. Full GDI pixels, fonts, independent window surfaces and host macros
remain distinct boundaries, not evidence of complete compatibility.

NEXT:

1. [/verify] Execute a bounded driver oracle for `show_bmp_file`'s returned
   rectangle and `clipBoardGetImage`'s allocation/copy arguments. Use native,
   stretched and clipped assets. Stub file/GDI services explicitly and record
   which pixel semantics the oracle cannot prove.
2. [/audit] Compare image ROP 4 with the drawing engine's destination inversion:
   inspect `1I`'s ROP mapping at CD5A and the downstream bitmap call before
   selecting source inversion or adding a separate image operation. Preserve
   generic `DRAW_MODE_NOT`'s existing destination-inversion contract.
3. [/debug] Derive capture dimensions/content from that evidence, then add
   fail-before/pass-after value tests and update the crosswalk/provenance.

COMPOUND: D-38 in `spec/12-dll-provenance.md`; six executable regressions in
`tests/test_drawing.c` and `tests/test_ripscrip.c`; current crosswalk source
hashes and the raster-value result in `crosswalk-audit.md`.

PROMPTS:

- "Iterate the icon clipboard rectangle oracle described in docs/iteration-state.md."
- "Audit image NOT raster operations against the driver, preserving the public drawing API."

Reconcile before continuing: `git status --short`, `git log -3 --oneline`,
fresh CTest, `scripts/dll-conformance.py` and `scripts/dll-validate-claims.py`.
The driver is `C:/RIPtel/RIPSCRIP.DLL` (MD5
`bade8b1f4e467ac7ad4edb2639738d4c`). The upstream reference clone is
`build-crosswalk-reference`, pinned to
`2fb17724b6122a5ad5cd1df38b69b8cce3a7079f`. PR #5 carries this work on
`codex/remaining-protocol-compatibility`. No release/tag/merge is implied.
