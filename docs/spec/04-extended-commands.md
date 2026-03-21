
=====================================================================
==       SEGMENT 4: EXTENDED COMMANDS (v2.0+)                      ==
=====================================================================

Extended commands were added in RIPscrip v2.0 and later versions.
They use Level 0 routing (no prefix) with command letters not
used in the original v1.54 specification.

These commands provide: rounded rectangles, scroll operations,
poly-Bezier curves, bounded text, extended polygon/polyline
operations, draw-to, animation frames, extended mouse regions,
extended buttons, extended text windows, extended font styles,
font attributes, icon operations, and coordinate/color modes.


---------------------------------------------------------------------
4.1  RIP_FILLED_CIRCLE — Filled Circle
---------------------------------------------------------------------

     Function:     Draw Filled Circle
     Command:      |G
     Arguments:    cx:2 cy:2 radius:2
     Format:       !|G<cx><cy><r>|

Draws a filled circle. If fill pattern is non-empty, fills with
fill color first, then draws outline in draw color. Radius is
Y-scaled (EGA 350→400).

     Attributes: [DC] [FC] [FP] [WM] [CL]


---------------------------------------------------------------------
4.2  RIP_ROUNDED_RECT — Rounded Rectangle Outline
---------------------------------------------------------------------

     Function:     Draw Rounded Rectangle (outline)
     Command:      |U
     Arguments:    x0:2 y0:2 x1:2 y1:2 radius:2
     Format:       !|U<x0><y0><x1><y1><r>|
     Example:      !|U0A0F3K2A08|   (10,15)-(128,42) r=8

Draws a rectangle with rounded corners. Corner arcs are rendered
using the midpoint circle algorithm. Radius is clamped to half
the smaller dimension.

     Parameter   Width   Range     Description
     ---------   -----   -------   -----------
     x0,y0       2,2     coords    Top-left corner
     x1,y1       2,2     coords    Bottom-right corner
     radius      2       1-100     Corner radius (scale_y)

     v3.1: Previously stubbed as plain rectangle. Now renders
     native rounded corners using quarter-arc + straight edge
     algorithm. Essential for polished button/dialog rendering
     and QuickDraw II FrameRRect parity.

     Attributes: [DC] [WM] [LP] [CL]


---------------------------------------------------------------------
4.3  RIP_FILLED_ROUNDED_RECT — Filled Rounded Rectangle
---------------------------------------------------------------------

     Function:     Draw Filled Rounded Rectangle
     Command:      |u     (lowercase)
     Arguments:    x0:2 y0:2 x1:2 y1:2 radius:2
     Format:       !|u<x0><y0><x1><y1><r>|

Draws a filled rounded rectangle. If fill pattern is non-empty,
fills with fill color, then draws outline in draw color.

     Attributes: [DC] [FC] [FP] [WM] [CL]


---------------------------------------------------------------------
4.4  RIP_SCROLL — Scroll Screen Region
---------------------------------------------------------------------

     Function:     Scroll Screen Region
     Command:      |+
     Arguments:    x0:2 y0:2 x1:2 y1:2 dx:2 dy:2 fill_color:2
     Format:       !|+<x0><y0><x1><y1><dx><dy><fill>|

Scrolls a rectangular region by (dx, dy) pixels and fills the
vacated area with fill_color.

     Parameter   Width   Range     Description
     ---------   -----   -------   -----------
     x0,y0       2,2     coords    Region top-left
     x1,y1       2,2     coords    Region bottom-right
     dx          2       signed    Horizontal scroll offset
     dy          2       signed    Vertical scroll offset
     fill_color  2       0-15      Color for vacated area

Implementation: uses draw_copy_rect for the shift, then
draw_rect to fill the vacated strip.


---------------------------------------------------------------------
4.5  RIP_COPY_REGION_EXT — Extended Copy Region
---------------------------------------------------------------------

     Function:     Extended Copy Region
     Command:      |,
     Arguments:    sx0:2 sy0:2 sx1:2 sy1:2 dx:2 dy:2
                   dx1:2 dy1:2 res:2 res:2
     Format:       !|,<sx0><sy0><sx1><sy1><dx><dy><dx1><dy1><r><r>|

Extended version of copy region with separate source and
destination rectangles. Supports scaling if source and
destination sizes differ (not implemented in v3.1 — sizes
must match).


---------------------------------------------------------------------
4.6  RIP_TEXT_XY_EXT — Extended Text at Position
---------------------------------------------------------------------

     Function:     Extended Text at Position
     Command:      |-
     Arguments:    x0:2 y0:2 x1:2 y1:2 flags:2 text
     Format:       !|-<x0><y0><x1><y1><flags><text>|

Draws text at (x0,y0) with a bounding box (x0,y0)-(x1,y1).
Flags control justification and clipping behavior.

     Parameter   Width   Range     Description
     ---------   -----   -------   -----------
     x0,y0       2,2     coords    Text position / box top-left
     x1,y1       2,2     coords    Bounding box bottom-right
     flags       2       0-63      Justification + clip flags
     text        var     ASCII     Text to render


---------------------------------------------------------------------
4.7  RIP_POLY_BEZIER — Multi-Segment Bezier
---------------------------------------------------------------------

     Function:     Draw Multi-Segment Bezier Curve
     Command:      |z     (lowercase)
     Arguments:    nsegs:2 nsteps:2 [x0:2 y0:2 x1:2 y1:2
                   x2:2 y2:2 x3:2 y3:2] × nsegs
     Format:       !|z<nsegs><nsteps><points...>|

Draws multiple connected cubic Bezier curve segments.
Each segment uses 4 control points (8 coordinates).

     Parameter   Width   Range     Description
     ---------   -----   -------   -----------
     nsegs       2       1-16      Number of Bezier segments
     nsteps      2       4-64      Steps per segment (ignored*)
     points      8/seg   coords    4 control points per segment

     * v3.1: nsteps is accepted for compatibility but RIPlib
       uses FPU adaptive step count per segment.


---------------------------------------------------------------------
4.8  RIP_BOUNDED_TEXT — Text with Bounding Box
---------------------------------------------------------------------

     Function:     Bounded Text
     Command:      |"
     Arguments:    x0:2 y0:2 x1:2 y1:2 flags:2 text
     Format:       !|"<x0><y0><x1><y1><flags><text>|

Draws text within a bounding rectangle with justification
and optional clipping. Similar to TEXT_XY_EXT but with
additional formatting control.


---------------------------------------------------------------------
4.9  RIP_FILLED_POLYGON_EXT — Extended Filled Polygon
---------------------------------------------------------------------

     Function:     Extended Filled Polygon
     Command:      |[
     Arguments:    x0:2 y0:2 x1:2 y1:2 mode:2 p1:2 p2:2
     Format:       !|[<x0><y0><x1><y1><mode><p1><p2>|

Extended polygon fill with additional parameters for fill
mode and pattern control.


---------------------------------------------------------------------
4.10  RIP_POLYLINE_EXT — Extended Polyline
---------------------------------------------------------------------

     Function:     Extended Polyline
     Command:      |]
     Arguments:    x0:2 y0:2 x1:2 y1:2 mode:2 p1:2 p2:2
     Format:       !|]<x0><y0><x1><y1><mode><p1><p2>|

Extended polyline with line mode and pattern parameters.


---------------------------------------------------------------------
4.11  RIP_DRAW_TO — Draw Line To Position
---------------------------------------------------------------------

     Function:     Draw To (line from current position)
     Command:      |_
     Arguments:    x0:2 y0:2 mode:2 param:2 x1:2 y1:2
     Format:       !|_<x0><y0><mode><param><x1><y1>|

Draws a line from (x0,y0) to (x1,y1) with extended mode
and parameter control. Updates the drawing cursor.


---------------------------------------------------------------------
4.12  RIP_ANIMATION_FRAME — Animation Frame
---------------------------------------------------------------------

     Function:     Animation Frame
     Command:      |{
     Arguments:    x0:2 y0:2 x1:2 y1:2 x2:2 y2:2
     Format:       !|{<x0><y0><x1><y1><x2><y2>|

Defines an animation frame with source, destination, and
timing coordinates. Used for simple sprite-like animations.

     v3.1 STATUS: Stubbed. Full implementation requires a
     sprite/frame manager with timer-based playback.


---------------------------------------------------------------------
4.13  RIP_KILL_MOUSE_EXT — Extended Kill Mouse Fields
---------------------------------------------------------------------

     Function:     Kill Mouse Fields in Region
     Command:      |K     (Level 0, extended context)
     Arguments:    x0:2 y0:2 x1:2 y1:2
     Format:       !|K<x0><y0><x1><y1>|

Clears mouse regions that overlap the specified rectangle.
Unlike RIP_KILL_MOUSE (Level 1), this only removes regions
within the given bounds.


---------------------------------------------------------------------
4.14  RIP_MOUSE_REGION_EXT — Extended Mouse Region
---------------------------------------------------------------------

     Function:     Extended Mouse Region
     Command:      |:
     Arguments:    x0:2 y0:2 x1:2 y1:2 hotkey:2 flags:2 res×5
     Format:       !|:<x0><y0><x1><y1><hotkey><flags><res...>|

Extended version of RIP_MOUSE with additional flag bits
and reserved fields for future use.


---------------------------------------------------------------------
4.15  RIP_BUTTON_EXT — Extended Button
---------------------------------------------------------------------

     Function:     Extended Button
     Command:      |;
     Arguments:    x0:2 y0:2 x1:2 y1:2 style:2 lx:2 ly:2
                   rx:2 ry:2 flags:2 tidx:2
     Format:       !|;<x0><y0><x1><y1><style><lx><ly><rx><ry><flags><tidx>|

Extended button with separate label and icon positioning
coordinates and a style table index.


---------------------------------------------------------------------
4.16  RIP_EXT_TEXT_WINDOW — Extended Text Window
---------------------------------------------------------------------

     Function:     Extended Text Window
     Command:      |b
     Arguments:    x0:2 y0:2 x1:2 y1:2 fore:2 back:2
                   font:1 size:4 flags:3
     Format:       !|b<x0><y0><x1><y1><fore><back><font><size><flags>|

Extended text window with color, font, and formatting control.

     Parameter   Width   Range     Description
     ---------   -----   -------   -----------
     x0,y0       2,2     coords    Window top-left
     x1,y1       2,2     coords    Window bottom-right
     fore        2       0-15      Text foreground color
     back        2       0-15      Text background color
     font        1       0-10      Font ID
     size        4       0-9999    Font size (extended range)
     flags       3       0-46655   Formatting flags


---------------------------------------------------------------------
4.17  RIP_EXT_FONT_STYLE — Extended Font Style
---------------------------------------------------------------------

     Function:     Extended Font Style
     Command:      |d
     Arguments:    font_id:2 attr:1 size:4
     Format:       !|d<font_id><attr><size>|

Sets font with extended size range (4-digit MegaNum allows
sizes beyond the standard 1-10 range).

     Parameter   Width   Range     Description
     ---------   -----   -------   -----------
     font_id     2       0-10      Font ID
     attr        1       0-15      Attribute flags
     size        4       1-9999    Extended size


---------------------------------------------------------------------
4.18  RIP_FONT_ATTRIB — Font Attribute Flags
---------------------------------------------------------------------

     Function:     Set Font Attributes
     Command:      |f
     Arguments:    attrib:2 reserved:2
     Format:       !|f<attrib><res>|
     Example:      !|f0100|        bold only

Sets font rendering attributes for subsequent text commands.

     Parameter   Width   Range     Description
     ---------   -----   -------   -----------
     attrib      2       0-15      Attribute bitmask
     reserved    2       0         Reserved

Attribute bits:

     Bit   Value   Name        Rendering Effect
     ---   -----   --------    --------------------------------
     0     0x01    BOLD        Draw strokes twice, +1px offset
     1     0x02    ITALIC      Shear X by top*scale/5 (FPU)
     2     0x04    UNDERLINE   Baseline line after string
     3     0x08    SHADOW      Dark offset copy behind string

     v3.1: All four attributes are now rendered. Previously
     parsed but not applied in any known implementation.
     Bold uses double-stroke, italic uses FPU shear, underline
     draws at baseline+2, shadow uses dimmed RGB332 color.


---------------------------------------------------------------------
4.19  RIP_HEADER — Scene Header
---------------------------------------------------------------------

     Function:     Scene Header
     Command:      |h
     Arguments:    type:2 id:4 flags:2
     Format:       !|h<type><id><flags>|

Defines metadata for the current RIPscrip scene (type,
identifier, and behavioral flags).


---------------------------------------------------------------------
4.20  RIP_SET_COORDINATE_SIZE — Set Coordinate Mode
---------------------------------------------------------------------

     Function:     Set Coordinate Size
     Command:      |n
     Arguments:    mode:1 size:3
     Format:       !|n<mode><size>|

Sets the coordinate resolution mode (EGA 640×350, VGA 640×480,
etc.). Affects how protocol coordinates map to the framebuffer.


---------------------------------------------------------------------
4.21  RIP_SET_COLOR_MODE — Set Color Mode
---------------------------------------------------------------------

     Function:     Set Color Depth Mode
     Command:      |M
     Arguments:    mode:1 depth:1
     Format:       !|M<mode><depth>|

Sets the color mode (EGA 16-color, VGA 256-color, etc.).


---------------------------------------------------------------------
4.22  RIP_SET_BORDER — Set Border Color
---------------------------------------------------------------------

     Function:     Set Border Color
     Command:      |N
     Arguments:    color:2
     Format:       !|N<color>|

Sets the screen border color.


---------------------------------------------------------------------
4.23  RIP_ICON_STYLE — Set Icon Display Style
---------------------------------------------------------------------

     Function:     Set Icon Style
     Command:      |&
     Arguments:    x0:2 y0:2 x1:2 y1:2 style:2 align:2 scale:2
     Format:       !|&<x0><y0><x1><y1><style><align><scale>|

Sets display parameters for subsequent icon rendering.


---------------------------------------------------------------------
4.24  RIP_STAMP_ICON — Stamp Icon at Position
---------------------------------------------------------------------

     Function:     Stamp Icon
     Command:      |.
     Arguments:    slot:2 x:2 y:2 w:2 h:2 flags:2
     Format:       !|.<slot><x><y><w><h><flags>|

Stamps a previously saved icon slot at the given position
with optional scaling and flags.


---------------------------------------------------------------------
4.25  RIP_SAVE_ICON — Save Screen Region to Icon Slot
---------------------------------------------------------------------

     Function:     Save Icon
     Command:      |J
     Arguments:    slot:2
     Format:       !|J<slot>|

Saves the current clipboard contents to a numbered icon slot
for later stamping with RIP_STAMP_ICON.


---------------------------------------------------------------------
4.26  RIP_FILL_PATTERN_EXT — Extended Fill Pattern
---------------------------------------------------------------------

     Function:     Extended Fill Pattern (Level 0 context)
     Command:      |D
     Arguments:    pat[0]:2 ... pat[7]:2 color:2
     Format:       !|D<p0><p1><p2><p3><p4><p5><p6><p7><color>|

Same as RIP_FILL_PATTERN (|s) but appears in extended command
context. Sets an 8×8 user-defined fill pattern.


---------------------------------------------------------------------
4.27  RIP_GET_IMAGE_EXT — Extended Get Image
---------------------------------------------------------------------

     Function:     Extended Get Image
     Command:      |<
     Arguments:    x0:2 y0:2 x1:2 y1:2
     Format:       !|<<x0><y0><x1><y1>|

Extended clipboard copy with simplified parameters.


=====================================================================
==                    END OF SEGMENT 4                              ==
==           Extended Commands (v2.0+)                              ==
=====================================================================

Next: Segment 5 — Level 2 Drawing Port System
