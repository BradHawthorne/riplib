
=====================================================================
==       SEGMENT 6: v3.1 / v3.2 EXTENSIONS (§A2G)                  ==
=====================================================================

The A2GSPU §A2G extensions are additions to the RIPscrip protocol
that go beyond what TeleGrafix shipped in RIPSCRIP.DLL v3.0.7
(October 1997).  All extensions are backward-compatible — they use
existing command fields, previously unused parameter ranges, new
command letters not in the v3.0 table, or new $VARIABLE$ names.
A v3.0 client receiving v3.1 or v3.2 traffic sees the additions as
either no-ops or as literal text.

Protocol versioning:

     RIPSCRIP031001    v3.1 — §A2G.1 through §A2G.7
                              (AND/NOT, vertical text, font attrs,
                               native fills, FPU, palette offset, CCW)

     RIPSCRIP032001    v3.2 — adds §A2G.8 through §A2G.13
                              (state stack, layout vars, time vars,
                               color names, <<DEBUG>>, radial fill)

A client advertises its supported revision via $RIPVER$ and via the
ESC[! probe response (see §1.7).  The wire ID encodes the major,
minor, and sub-version each as a 2-digit field.

All extensions are documented with a §A2G section number for
cross-referencing.


---------------------------------------------------------------------
§A2G.1  EXTENDED WRITE MODES — AND and NOT
---------------------------------------------------------------------

The original RIPscrip v1.54 defined three write modes:

     Mode 0: COPY   (dst = src)
     Mode 1: OR     (dst = dst | src)
     Mode 3: XOR    (dst = dst ^ src)

     NOTE: The DLL internally used a different ordering
     (0=COPY, 1=XOR, 2=OR). The wire protocol values above
     are the RIPscrip specification values. RIPlib uses the
     wire protocol values.

v3.1 adds two modes:

     Mode 2: AND    (dst = dst & src)
     Mode 4: NOT    (dst = ~dst)

AND mode performs a bitwise AND between the source color and
the existing destination pixel. This is useful for masking
operations — AND with a color that has only certain channel
bits set will preserve only those channels.

     Example: White pixel (0xFF) AND Red (0xE0) = Red (0xE0)
              Keeps only the red channel bits.

NOT mode performs a bitwise inversion of the destination pixel,
ignoring the source color entirely. The draw color parameter
is accepted but not used — the destination is simply inverted.

     Example: NOT on white (0xFF) = black (0x00)
              NOT on red (0xE0) = cyan-ish (0x1F)
              NOT twice = original (self-inverse)

These modes apply to all drawing operations: lines, rectangles,
circles, fills, text, copy operations, etc.

     Wire format: !|W02|    (set AND mode)
                  !|W04|    (set NOT mode)


---------------------------------------------------------------------
§A2G.2  VERTICAL TEXT DIRECTION CORRECTION
---------------------------------------------------------------------

The Borland BGI v1.54 specification defines VERT_DIR (direction=1)
as "bottom to top" — the first character of the string is placed
at the origin (bottom), and subsequent characters advance upward
(decreasing Y).

This renders English text backwards on screen. Reading top-to-
bottom, "HELLO" appears as "OLLEH". No known BBS sends vertical
text commands, so this behavior was never observed in practice.

v3.1 CORRECTION: Direction 1 now renders top-to-bottom:

     Direction 0: Horizontal, left to right (unchanged)
     Direction 1: Vertical, top to bottom, CW glyph rotation
     Direction 2: Vertical, top to bottom, CCW glyph rotation

For direction 1 (CW), characters are readable when tilting
the head clockwise (right ear toward shoulder). This matches
the English book spine convention (US/UK).

For direction 2 (CCW), characters are readable when tilting
the head counter-clockwise (left ear toward shoulder). This
matches the French book spine convention.

Both vertical directions advance the cursor downward (Y += width).
The glyph rotation determines which way the reader tilts their
head to read the text.

Glyph coordinate transforms:

     Direction 0 (horizontal):
          screen_x = origin_x + stroke_dx
          screen_y = origin_y - stroke_dy    (BGI Y inverted)

     Direction 1 (CW, tilt head right):
          screen_x = origin_x + stroke_dy
          screen_y = origin_y + stroke_dx    (screen-CW rotation)

     Direction 2 (CCW, tilt head left):
          screen_x = origin_x - stroke_dy
          screen_y = origin_y - stroke_dx    (screen-CCW rotation)

     Wire format: !|Y010200|    (Triplex, dir=2 CCW, size 1)


---------------------------------------------------------------------
§A2G.3  FONT ATTRIBUTE RENDERING
---------------------------------------------------------------------

The RIP_FONT_ATTRIB command (|f) was present in v3.0 but the DLL
never applied the attribute bits to the rendered output. The bits
were parsed and stored but had no visual effect.

v3.1 implements all four attribute effects for BGI stroke fonts:

     Bit 0 (0x01) — BOLD:
          Each stroke segment is drawn twice: once at the normal
          position, once offset 1 pixel to the right. This
          thickens the glyph without changing its metrics.

     Bit 1 (0x02) — ITALIC:
          The X coordinate of each stroke point is sheared by
          a factor proportional to the Y position:

               shear = font.top * scale / 5

          The entire glyph origin is offset by this shear amount,
          creating a rightward lean at the top of the character.
          Uses FPU for the shear calculation.

     Bit 2 (0x04) — UNDERLINE:
          After the string is rendered, a horizontal line is drawn
          at baseline + 2 pixels, spanning the full string width.
          For vertical text, a vertical underline bar is drawn
          2 pixels left of the string origin.

     Bit 3 (0x08) — SHADOW:
          The entire string is rendered twice: first in a dimmed
          shadow color offset (1, 1) pixels from the origin, then
          in the normal color at the origin. The shadow color is
          computed by halving each RGB332 channel:

               shadow_color = (color >> 1) & 0x6D

Attributes are cumulative — bold+italic+underline is valid.
Attributes only affect BGI stroke fonts (font IDs 1-10). The
bitmap font (ID 0) ignores attributes.

     Wire format: !|f0300|    (bold + italic)
                  !|f0700|    (bold + italic + underline)


---------------------------------------------------------------------
§A2G.4  NATIVE FILL PATTERNS
---------------------------------------------------------------------

The original BGI defined 13 fill patterns (0-12), but most
implementations only provided 8 built-in patterns and
approximated patterns 9-11 using the closest available match.

v3.1 provides all 13 patterns natively:

     ID   Name              Pattern (8×8 hex)
     --   ---------------   ---------------------------------
     0    SOLID             FF FF FF FF FF FF FF FF
     1    CHECKER           AA 55 AA 55 AA 55 AA 55
     2    DIAGONAL_BACK     88 44 22 11 88 44 22 11
     3    DIAGONAL_FWD      11 22 44 88 11 22 44 88
     4    HORIZONTAL        FF 00 FF 00 FF 00 FF 00
     5    VERTICAL          AA AA AA AA AA AA AA AA
     6    CROSS_HATCH       FF AA FF AA FF AA FF AA
     7    LIGHT_DIAGONAL    80 40 20 10 08 04 02 01
     8    INTERLEAVE        CC 33 CC 33 CC 33 CC 33
     9    WIDE_DOT          80 00 08 00 80 00 08 00
     10   CLOSE_DOT         AA 00 AA 00 AA 00 AA 00
     11   USER_PATTERN      (set via |s command)

Previous implementations mapped:
     Pattern 9 → checker (approximate)
     Pattern 10 → light diagonal (approximate)
     Pattern 11 → checker (approximate)

v3.1 provides the correct patterns for all three, matching
the Borland BGI specification exactly.


---------------------------------------------------------------------
§A2G.5  FPU-ACCELERATED RENDERING
---------------------------------------------------------------------

v3.1 uses single-precision floating-point hardware (FPU) for
operations where integer arithmetic introduces visible errors:

BEZIER CURVES (draw_bezier):
     Replaced: Recursive integer De Casteljau subdivision with
               >>1 shift rounding at each of 8 recursion levels.
     With:     FPU parametric cubic evaluation:
                    B(t) = (1-t)³P₀ + 3(1-t)²tP₁ + 3(1-t)t²P₂ + t³P₃
               Adaptive step count based on control polygon length
               (4-64 steps). No rounding accumulation.

TRIGONOMETRY (arcs, pies, elliptical arcs):
     Replaced: 91-entry integer sine lookup table with
               linear interpolation (±1° accuracy).
     With:     sinf(), cosf(), atan2f(), sqrtf()
               (<0.01° accuracy, 1-2 cycle per call on Cortex-M33).

PIE FILL (draw_pie, draw_elliptical_pie):
     Replaced: Flood fill on arc+radii boundary (leaked through
               pixel gaps between arc and radial line endpoints).
     With:     Per-pixel angle+distance test using atan2f:
               For each pixel in the bounding box, compute angle
               from center and check if within the sector range
               AND within the radius. Eliminates all fill leaks.

COORDINATE SCALING:
     The scale_y function (y * 8 / 7) uses integer arithmetic
     which is exact for this specific ratio. FPU is not needed
     here — the integer result matches the float result exactly.

These improvements are transparent to the protocol — the same
wire commands produce smoother, more accurate output.


---------------------------------------------------------------------
§A2G.6  PALETTE INDEX CORRECTION
---------------------------------------------------------------------

The RIP_SET_PALETTE (|Q) and RIP_ONE_PALETTE (|a) commands in
early implementations wrote to hardware palette indices 0-15.
This conflicted with the xterm-256 color palette used by the
VT100/ANSI text renderer, which occupies indices 0-239.

v3.1 maps EGA colors to palette indices 240-255:

     EGA color 0  → framebuffer value 240
     EGA color 1  → framebuffer value 241
     ...
     EGA color 15 → framebuffer value 255

Drawing commands use s->palette[color] to convert EGA indices
to framebuffer values. The hardware palette at indices 240-255
stores the RGB565 color values.

This enables RIPscrip and VT100 to coexist on the same
framebuffer without palette conflicts:

     Indices 0-239:   xterm-256 colors (VT100/ANSI text)
     Indices 240-255: EGA 16-color palette (RIPscrip graphics)

Palette save/restore functions (rip_save_palette,
rip_apply_palette) snapshot and restore indices 240-255 when
switching between protocols.


---------------------------------------------------------------------
§A2G.7  EXTENDED TEXT DIRECTION (CCW)
---------------------------------------------------------------------

The RIP_FONT_STYLE (|Y) command's direction field is extended
from two values to three:

     dir=0: Horizontal, left to right          (v1.54)
     dir=1: Vertical CW, top to bottom         (v3.1 corrected)
     dir=2: Vertical CCW, top to bottom         (v3.1 new)

See §A2G.2 for the full description of the direction correction
and the glyph rotation coordinate transforms.

The direction field is a 2-digit MegaNum (range 0-1295), so
values 0-2 are a small subset of the available range. Values
3-35 are reserved for future use (e.g., arbitrary angle
rotation with FPU, if needed).

     Wire format: !|Y010200|    (Triplex, dir=2, size 1)

Direction validation:
     dir > 2 → command rejected (no state change)
     dir 0-2 → stored in s->font_dir


=====================================================================
==       §A2G.8+ — RIPscrip v3.2 QUALITY-OF-LIFE EXTENSIONS        ==
=====================================================================

The §A2G.8 through §A2G.13 extensions define RIPscrip v3.2: small
refinements that build on v3.1 without changing any existing
wire-format command.  Every addition is one of: a new command
letter not used in v3.0 / v3.1, a new $VARIABLE$ name, a new
preprocessor directive, or a new value for a previously-validated
parameter field.  v3.0 / v3.1 clients see the new content as either
no-op (unknown command letters are passed through the FSM accept
list) or as literal text ($XYZ$ falls through when unrecognized).


---------------------------------------------------------------------
§A2G.8  STATE PUSH/POP STACK
---------------------------------------------------------------------

Two new Level 0 commands wrap a bounded LIFO stack of "drawing
prelude" state:

     Function:     Push drawing state
     Command:      |^
     Arguments:    (none)
     Format:       !|^|

     Function:     Pop drawing state
     Command:      |~
     Arguments:    (none)
     Format:       !|~|

The stack is bounded to RIP_STATE_STACK_MAX (8) frames.  Each frame
captures the fields a typical scene most often re-sends as a prelude
before a styled draw:

     draw_color, back_color, fill_color, fill_pattern,
     line_style, line_pattern (16-bit), line_thick, write_mode,
     font_id, font_size, font_dir, font_attrib,
     font_hjust, font_vjust,
     font_ext_id, font_ext_attr, font_ext_size,
     filled_borders_enabled,
     draw_x, draw_y,
     vp_x0, vp_y0, vp_x1, vp_y1

Behavior:

     Push (|^):   If stack is full, the push is silently dropped.
                  Matches the "ignore unknown params" precedent for
                  graceful degradation.
     Pop  (|~):   If stack is empty, the pop is a no-op.  On a
                  successful pop, the full session drawing state is
                  re-applied immediately, so the next draw command
                  picks up restored color, write mode, line, fill,
                  cursor, and viewport state.

Stack lifetime:
     The stack is reset to depth=0 by:
          * RIP_RESET_WINDOWS (|*)
          * RIP_SESSION_RESET (host-driven)
          * rip_init_first / rip_session_reset C API calls

Example:

     !|c04|S0204|       sets red outline + green fill
     !|^|               push the current state
     !|c0F|S0104|       switch to white outline + blue fill
     !|R0A0A1414|       draws a blue-filled rect (white border)
     !|~|               pop — back to red outline + green fill
     !|R28281414|       another rect with the original colors


---------------------------------------------------------------------
§A2G.9  LAYOUT / INTROSPECTION VARIABLES
---------------------------------------------------------------------

These read-only variables expose current drawing state to text and
to <<IF>> expressions.  Each expands to a decimal string suitable
for direct use in IF comparison.

     Variable    Expands to                  Source
     ---------   -------------------------   ----------------------
     $CX$        current draw_x (decimal)    s->draw_x
     $CY$        current draw_y (decimal)    s->draw_y
     $VPW$       viewport width              vp_x1 - vp_x0 + 1
     $VPH$       viewport height             vp_y1 - vp_y0 + 1
     $VPCX$      viewport center X           (vp_x0 + vp_x1) / 2
     $VPCY$      viewport center Y           (vp_y0 + vp_y1) / 2
     $CCOL$      current draw color (0-15)   s->draw_color & 0x0F
     $CFCOL$     current fill color (0-15)   s->fill_color & 0x0F
     $CBCOL$     current back color (0-15)   s->back_color & 0x0F

Use case: a scene can compute its own centering without the BBS
hard-coding 320,200, surviving |v viewport changes and |2P port
definitions transparently:

     !|@$VPCX$$VPCY$Hello, world|     draws text at viewport center


---------------------------------------------------------------------
§A2G.10  TIME COMPONENT VARIABLES
---------------------------------------------------------------------

Extends the existing $DATE$ / $TIME$ / $YEAR$ / $WOYM$ family with
finer-grained accessors:

     Variable    Format    Source                  Range
     ---------   -------   ---------------------   --------
     $HOUR$      HH        host_time[0..1]          00-23
     $MIN$       MM        host_time[3..4]          00-59
     $SEC$       SS        RP2350 RTC               00-59
     $DOW$       D         day of week (Mon=0)      0-6
     $DOM$       DD        day of month             01-31
     $MONTH$     MM        month of year            01-12

All fall back to the local RTC (`time()` / `localtime()`) when the
host has not synced its date/time over CMD_SYNC_DATE/SYNC_TIME yet.

$DOW$ reuses the ISO-week date arithmetic already used by $WOYM$
(rip_weekday_monday0), so day-of-week and week-of-year stay
consistent even across leap years.

Use case: greeting variation by time of day, or by day of week:

     <<IF $HOUR$<12>>Good morning<<ENDIF>>
     <<IF $DOW$=4>>Happy Friday!<<ENDIF>>


---------------------------------------------------------------------
§A2G.11  EGA COLOR-NAME ALIASES
---------------------------------------------------------------------

Each EGA palette index has a readable variable alias.  Names are
all uppercase, no separators.  Each expands to its 2-digit MegaNum
value (suitable as a |c, |S, |k, |a argument if pre-expanded by a
text path, or as a comparison value in <<IF>> expressions).

     Index   Variable          Expands to
     -----   ---------------   ----------
     0       $BLACK$           "00"
     1       $BLUE$            "01"
     2       $GREEN$           "02"
     3       $CYAN$            "03"
     4       $RED$             "04"
     5       $MAGENTA$         "05"
     6       $BROWN$           "06"
     7       $LIGHTGRAY$       "07"
     8       $DARKGRAY$        "08"
     9       $LIGHTBLUE$       "09"
     10      $LIGHTGREEN$      "0A"
     11      $LIGHTCYAN$       "0B"
     12      $LIGHTRED$        "0C"
     13      $LIGHTMAGENTA$    "0D"
     14      $YELLOW$          "0E"
     15      $WHITE$           "0F"

These variables expand in any context where rip_expand_variables
runs: |T, |@, |t, |", |- text bodies, and <<IF>> expression bodies.
They do NOT expand inside numeric command-argument fields such as
|c<color>, because those fields are read by mega2() directly from
the wire buffer before variable expansion is reached.  Use them
in text bodies and IF comparisons:

     !|TYour color is $LIGHTRED$|
     <<IF $CCOL$=12>>The current color is light red<<ENDIF>>


---------------------------------------------------------------------
§A2G.12  <<DEBUG msg>> PREPROCESSOR DIRECTIVE
---------------------------------------------------------------------

A new preprocessor directive joins <<IF>> / <<ELSE>> / <<ENDIF>>:

     Directive:    <<DEBUG msg>>
     Effect:       Pushes "0x3E DEBUG: <msg>\r" to the TX FIFO.
                   Suppressed by an enclosing <<IF false>> branch.

The 0x3E (>) prefix marks the line as a host-side log, distinct
from the 0x3D (=) CMD_PLAY_SOUND marker and from regular text.  A
host that does not recognize the prefix simply drops the line —
making this safe to leave in production scene scripts.

Use case: instrumenting a scene script during development:

     <<DEBUG entering menu render>>
     !|@01010Menu|
     <<IF $APP0$=>><<DEBUG no user name yet>><<ENDIF>>

Output on TX:

     >DEBUG: entering menu render\r
     >DEBUG: no user name yet\r


---------------------------------------------------------------------
§A2G.13  RADIAL GRADIENT MODE
---------------------------------------------------------------------

The Level 2 gradient command |28 gains a third mode value:

     Mode 0 (v3.0):   Horizontal gradient — color varies with X.
     Mode 1 (v3.0):   Vertical gradient — color varies with Y.
     Mode 2 (§A2G):  Radial gradient — c1 at the box center,
                      c2 at the farthest box corner.  Per-pixel
                      linear interpolation by normalized squared
                      distance, using the FPU we already require
                      for §A2G.5 trig.

Backward compatibility:
     v3.0 stored mode as a bool (any non-zero = vertical), so
     existing clients sending mode=1 still get vertical output.
     Only mode=2 is new behavior.

     Wire format: !|28<x:2><y:2><w:2><h:2><c1:2><c2:2><mode:2>|

     Example:
          !|280A0A1414010202|
               x=10 y=10 w=20 h=20 c1=palette[1] c2=palette[2]
               mode=2 → radial gradient from center to corners


=====================================================================
==                    END OF SEGMENT 6                              ==
==           v3.1 / v3.2 Extensions (§A2G)                         ==
=====================================================================

Next: Segment 7 — Variable Expansion
