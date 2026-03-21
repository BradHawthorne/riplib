
=====================================================================
==       SEGMENT 11: DLL DEVIATIONS, ERRATA & KNOWN BUGS           ==
=====================================================================

This segment documents deviations between the published v2.A4
specification and the production RIPSCRIP.DLL v3.0.7 (October
1997), specification errata discovered during binary analysis,
and known DLL bugs that implementers should avoid replicating.

This information was derived from systematic disassembly of
RIPSCRIP.DLL (592,896 bytes, 32-bit Windows PE, i386).


---------------------------------------------------------------------
11.1  DROPPED COMMANDS (v2.A4 → v3.0 DLL)
---------------------------------------------------------------------

The following commands were defined in the v2.A4 specification
but are NOT present in the production v3.0 DLL. Do not implement.

RIP_SCROLLER (v2.A1, §1.4.4):
     Intended as a standardized scrollbar widget. Completely absent
     from the DLL — no function string containing "scroller" found.
     The command "RIP_Scroll" (screen region scroll, |+) IS present
     but is a different command. Use RIP_BUTTON with graphics for
     scrollbar UI if needed.

RIP_FILLED_RECTANGLE (v2.A2, §3.4.1.20):
     Intended to add write mode support to filled rectangles (v1.54
     RIP_BAR did not support write modes). Not present as a named
     function in the DLL. The existing RIP_BAR command gained write
     mode support in practice, making this redundant.

RIP_WORLD_FRAME (v2.A0):
     World coordinate frame transformation system. References exist
     in the v2.A4 spec text but no implementation found in the DLL
     export table or function strings. The coordinate system remains
     the simple EGA 640×350 model with scale_y mapping.


---------------------------------------------------------------------
11.2  v2.A4 SPECIFICATION ERRATA
---------------------------------------------------------------------

Erratum 1 — Command letter 'b' collision:
     RIP_SET_BASE_MATH (§3.4.1.46, added v2.A0) and
     RIP_EXTENDED_TEXT_WINDOW (§3.4.1.12, added v2.A4) both use
     command letter 'b' at Level 0. The DLL disambiguates by
     argument length:
          RIP_SET_BASE_MATH: exactly 2 characters after 'b'
          RIP_EXTENDED_TEXT_WINDOW: 15+ characters after 'b'
     Implementations that parse by command letter alone will
     misinterpret one or both.

Erratum 2 — RIP_FILLED_POLY_BEZIER command letter:
     §3.4.1.19 header states "Command: x" but the Format line
     reads "!|z" (the unfilled RIP_POLY_BEZIER letter). Copy-paste
     error in the specification.
          Correct: RIP_POLY_BEZIER        = 'z' (unfilled)
                   RIP_FILLED_POLY_BEZIER = 'x' (filled)

Erratum 3 — RIP_DELETE_PORT command letter:
     §3.4.3.2 header states "Command: p" but the Format line reads
     "!|2s" (the RIP_SWITCH_PORT letter). The TOC lists these as
     separate commands.
          Correct: RIP_DELETE_PORT  = !|2p (Level 2, 'p')
                   RIP_SWITCH_PORT  = !|2s (Level 2, 's')


---------------------------------------------------------------------
11.3  v3.0 DLL KNOWN BUGS
---------------------------------------------------------------------

Implementers should be aware of these defects to avoid replicating
them. RIPlib v3.1 corrects all of these.

§BUG.1 — Memory allocator masks out-of-memory:
     ripHeapAllocPtr (86 call sites) unconditionally returns 1
     (success) even when allocation fails and the output pointer
     is NULL. Callers checking only the return value never detect
     OOM. The actual indicator is whether *ppOut is non-NULL.
     v3.1 FIX: Return NULL on failure, callers check pointer.

§BUG.2 — Buffer overflow discards entire input queue:
     When the RIP staging buffer exceeds RIP_BUF_MAX (5000 bytes),
     ALL pending input is discarded and the fill pointer reset to
     zero. Any partial command straddling the boundary is lost.
     v3.1 FIX: Discard only the current incomplete command.

§BUG.3 — Histogram counter overflow inversion:
     In RipDib_AccumHistogram (adaptive palette quantization), the
     16-bit histogram counter DECREMENTS on overflow instead of
     saturating at 0xFFFF. Extremely common colors wrap downward,
     potentially excluded from the quantized palette.
     v3.1 FIX: Saturate at max value, do not wrap.

§BUG.4 — Zmodem ZRPOS handler is a stub:
     During Zmodem file send, the ZRPOS response handler (receiver
     requesting retransmission from offset) is unimplemented:
          if (ret == ZRPOS) { /* TODO: handle re-seek */ }
     Bad blocks are not retransmitted, potentially delivering
     corrupted files.
     v3.1 FIX: Implement ZRPOS seek + retransmit.

§BUG.5 — VGA DAC precision loss:
     The palette pipeline converts all RGB values from 8-bit to
     6-bit (VGA DAC format) via right-shift by 2 before passing to
     GDI. This loses the bottom 2 bits of every color channel,
     even when the display supports 8-bit precision.
     v3.1 FIX: Use full 8-bit RGB → RGB565 conversion directly.

§BUG.6 — Pie fill leak through pixel gaps:
     draw_pie used flood fill on the arc+radii boundary. Sub-pixel
     gaps between the arc and radial line endpoints caused the fill
     to leak out and flood the entire screen.
     v3.1 FIX: Scanline-based per-pixel angle+distance test using
     FPU atan2f. Zero leak potential.

§BUG.7 — Write mode constants misordered:
     The DLL internal constants were 0=COPY, 1=XOR, 2=OR. The
     protocol wire values are 0=COPY, 1=OR, 3=XOR. Implementations
     that used the DLL constants instead of wire values would swap
     XOR and OR for every BBS connection.
     v3.1 FIX: Use wire protocol values consistently.

§BUG.8 — Bottom-to-top vertical text:
     BGI VERT_DIR rendered text bottom-to-top, producing backwards-
     reading text on screen. See §A2G.2 for the correction.

§BUG.9 — BGI font parser assumed '+' at byte 0:
     The CHR parser only checked byte 0 for the '+' marker. In
     bgi2c-generated headers, '+' is at ~byte 38. All 10 BGI
     stroke fonts silently failed to load, falling back to bitmap.
     v3.1 FIX: Scan for '+' with validation (see §8.4).


---------------------------------------------------------------------
11.4  REDESIGNED COMMANDS
---------------------------------------------------------------------

The following commands exist in both the v2.A4 spec and the v3.0
DLL but with different parameter formats or behavior:

RIP_BUTTON (Level 1, 'U'):
     v2.A4 spec describes separate RIP_MOUSE_REGION and RIP_BUTTON
     commands. In the DLL, the internal function ripCmd_MouseRegion
     handles both — the command letter 'U' creates the button
     visuals AND registers the mouse region in one call.

Drawing Port coordinate system:
     v2.A4 describes world-frame coordinates with origin offsets.
     The DLL uses simple viewport rectangles with EGA→card scaling.
     No world-frame transformation is implemented.

Palette slot switching:
     v2.A4 describes 36 palette slots with save/restore flags.
     The DLL implements this but with slightly different flag
     semantics (flag 0x01 = save primary, 0x02 = restore primary,
     0x04 = save alternate, 0x08 = restore alternate).


---------------------------------------------------------------------
11.5  RESURRECTED DEAD CODE (v3.1)
---------------------------------------------------------------------

The following features existed as code in the DLL (parsed, stored)
but never produced visible output. They were effectively dead code.
v3.1 activates them with working implementations.

§DEAD.1 — Font attribute rendering (|f command):
     The DLL parsed font_attrib bits (bold, italic, underline,
     shadow) from the RIP_FONT_ATTRIB command and stored them
     in the GFXSTYLE structure. However, the BGI stroke font
     renderer never read or applied these bits. All text rendered
     identically regardless of attribute settings.
     v3.1: All four attributes are now rendered. Bold uses
     double-stroke offset, italic uses FPU shear, underline
     draws at baseline, shadow draws dimmed offset copy.

§DEAD.2 — BGI stroke font loading:
     The DLL loaded CHR font files into memory and parsed their
     headers. However, the CHR binary parser had bugs in table
     ordering (width table and stroke offset table were often
     reversed in third-party implementations) and '+' marker
     detection (assumed byte 0, but bgi2c-generated data has
     '+' at ~byte 38). In practice, fonts silently failed to
     load and all text fell back to the bitmap font.
     v3.1: Complete parser rewrite — scans for '+' with
     validation, correct 16-byte header, offsets-before-widths
     table order. All 10 BGI fonts load and render correctly.

§DEAD.3 — AND and NOT write modes:
     The DLL's write mode handler accepted mode values 0-4 on
     the wire but only implemented COPY (0), XOR (1), and OR (2)
     internally. AND and NOT were parsed and stored but the
     pixel-write paths only had switch cases for three modes.
     v3.1: All five modes implemented in every pixel-write path
     (line, rect, fill, text, copy operations).

§DEAD.4 — Vertical text direction:
     The DLL accepted direction=1 in font style commands and
     stored the value, but the rendering produced backwards
     text (bottom-to-top) that was unreadable in English.
     The feature was documented but functionally broken — no
     BBS used it because the output was unusable.
     v3.1: Corrected to top-to-bottom with proper screen-CW
     glyph rotation. Added direction=2 (CCW) as a new option.

§DEAD.5 — Drawing Port alpha and compositing flags:
     The DLL's port structure had fields for opacity, compositing
     mode, and z-order, but these were never read by the rendering
     pipeline. Ports were always rendered at full opacity with
     simple overwrite compositing.
     v3.1: Port flags command (|2F) sets alpha, comp_mode, and
     zorder per-port. These feed into the windowed compositor
     for layered desktop rendering.

§DEAD.6 — Fill patterns 9-11 (INTERLEAVE, WIDE_DOT, CLOSE_DOT):
     The BGI specification defines 13 fill patterns (0-12), but
     most implementations only provided 8 built-in patterns.
     Patterns 9-11 were mapped to approximate alternatives
     (checker, light diagonal) rather than their correct bitmaps.
     v3.1: All three patterns implemented with their correct
     8×8 bitmaps per the Borland BGI specification.

§DEAD.7 — Patterned flood fill:
     The DLL's flood fill command accepted a fill pattern setting
     but the flood fill algorithm always filled with a solid color.
     The GDI brush for patterned fill was created but never applied
     to the ExtFloodFill call in the border-color codepath.
     v3.1: Two-pass algorithm — solid fill first (for boundary
     tracking), then pattern application over the filled region.

§DEAD.8 — Text justification rendering:
     The DLL parsed horizontal and vertical justification flags
     from the font style command and stored them in GFXSTYLE.
     However, the text rendering paths did not read these fields
     — all text rendered left-aligned at the draw cursor position.
     v3.1: Justification applied via string width measurement
     and cursor offset before rendering. Center, right, top,
     bottom, and baseline justification all functional.


=====================================================================
==                    END OF SEGMENT 11                             ==
==           DLL Deviations, Errata & Known Bugs                   ==
=====================================================================
