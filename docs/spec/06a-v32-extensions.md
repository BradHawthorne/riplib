
=====================================================================
==       SEGMENT 6A: v3.2 EXTENSIONS (§A2G.8 - §A2G.13)            ==
=====================================================================

The §A2G.8 through §A2G.13 extensions define RIPscrip v3.2: small
refinements that build on v3.1 without changing any existing wire-
format command.  Every addition is one of: a new command letter not
used in v3.0 / v3.1, a new $VARIABLE$ name, a new preprocessor
directive, or a new value for a previously-validated parameter
field.  v3.0 / v3.1 clients see the new content as either no-op
(unknown command letters are passed through the FSM accept list) or
as literal text ($XYZ$ falls through when unrecognized).

Protocol versioning:

     RIPSCRIP032001    v3.2 — adds §A2G.8 through §A2G.13
                              (state stack, layout vars, time vars,
                               color names, <<DEBUG>>, radial fill)

A client advertises its supported revision via $RIPVER$ and via the
ESC[! probe response (see §1.7).  v3.1 (§A2G.1 - §A2G.7) is
documented in `06-v31-extensions.md`; this segment covers only the
v3.2 additions on top of that baseline.


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
     $SEC$       SS        local RTC                00-59
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
     Mode 2 (§A2G):   Radial gradient — c1 at the box center,
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
==                    END OF SEGMENT 6A                             ==
==              v3.2 Extensions (§A2G.8 - §A2G.13)                  ==
=====================================================================

Next: Segment 7 — Variable Expansion
