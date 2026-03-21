
=====================================================================
==       SEGMENT 3: LEVEL 1 INTERACTIVE COMMANDS                   ==
=====================================================================

Level 1 commands handle interactive elements: mouse regions,
buttons, clipboard operations, icons, text blocks, file queries,
and variable definitions. They are prefixed with '1' after the
'|' delimiter.

     Format: !|1<cmd><parameters>|


---------------------------------------------------------------------
3.1  RIP_KILL_MOUSE — Clear All Mouse Regions
---------------------------------------------------------------------

     Function:     Kill All Mouse Fields
     Command:      |1K
     Arguments:    (none)
     Format:       !|1K|

Clears all registered mouse regions and buttons. Typically sent
before drawing a new screen with fresh interactive elements.


---------------------------------------------------------------------
3.2  RIP_MOUSE — Define Mouse Region
---------------------------------------------------------------------

     Function:     Define Mouse Region
     Command:      |1M
     Arguments:    x0:2 y0:2 x1:2 y1:2 hotkey:2 flags:1 text
     Format:       !|1M<x0><y0><x1><y1><hotkey><flags><text>|
     Example:      !|1M0A0F1E2A0D0SELECT 1\r|

Defines a rectangular mouse-clickable region on screen.

     Parameter   Width   Range     Description
     ---------   -----   -------   -----------
     x0,y0       2,2     coords    Top-left corner
     x1,y1       2,2     coords    Bottom-right corner
     hotkey      2       0-255     ASCII key equivalent (0=none)
     flags       1       0-15      Behavior flags
     text        var     ASCII     Host command string

Flags:

     Bit   Value   Name            Description
     ---   -----   -----------     ---------------------------
     0     0x01    MF_SEND_CHAR    Send hotkey char, not text
     2     0x04    MF_ACTIVE       Region is active (always set)
     4     0x10    (reserved)
     5     0x20    MF_RADIO        Radio-button group behavior
     6     0x40    MF_TOGGLE       Toggle state on each click

Click behavior:
     1. If MF_TOGGLE: XOR-invert region visually, toggle active
     2. If MF_SEND_CHAR and hotkey != 0: send hotkey + CR
     3. Default: send text string + CR to BBS
     4. First matching region wins (top-to-bottom scan)
     5. One-shot: region deactivates after click (unless toggle)


---------------------------------------------------------------------
3.3  RIP_BUTTON_STYLE — Define Button Style
---------------------------------------------------------------------

     Function:     Define Button Style
     Command:      |1B
     Arguments:    wid:2 hgt:2 orient:2 flags:4 bevsize:2
                   dfore:2 dback:2 bright:2 dark:2 surface:2
                   grp_no:2 flags2:2 uline:2 corner:2 res:6
     Format:       !|1B<30 chars of params>|

Defines the visual style for subsequent RIP_BUTTON commands.

     Parameter   Width   Range     Description
     ---------   -----   -------   -----------
     wid         2       1-639     Button width
     hgt         2       1-349     Button height
     orient      2       0-4       Orientation (0=horizontal)
     flags       4       0-65535   Style flags (16-bit)
     bevsize     2       0-10      Bevel depth in pixels
     dfore       2       0-15      Label foreground color
     dback       2       0-15      Label background color
     bright      2       0-15      Highlight color (top/left)
     dark        2       0-15      Shadow color (bottom/right)
     surface     2       0-15      Surface fill color
     grp_no      2       0-35      Radio button group number
     flags2      2       0-1295    Extended flags
     uline       2       0-15      Underline color
     corner      2       0-15      Corner color
     res         6       0         Reserved (6 chars, ignored)

Style flags (in flags parameter):

     Bit    Value    Name                 Description
     ---    ------   ----------------     ---------------------------
     0      0x0001   BSF_CLIPBOARD_PORT   Uses clipboard port
     7      0x0080   BSF_ICON_BUTTON      Button displays icon
     8      0x0100   BSF_PLAIN_BUTTON     Plain rectangular style
     10     0x0400   BSF_OFFSCREEN_OK     Allowed in offscreen ports
     14     0x4000   BSF_PROTECTED        Style slot is locked


---------------------------------------------------------------------
3.4  RIP_BUTTON — Create Button Instance
---------------------------------------------------------------------

     Function:     Create Button (draw + register mouse region)
     Command:      |1U
     Arguments:    x0:2 y0:2 x1:2 y1:2 hotkey:2 flags:1 res:1 text
     Format:       !|1U<x0><y0><x1><y1><hotkey><flags><res><text>|

Creates a visual button at the specified position using the
current button style, and registers a mouse region for click
handling.

     Parameter   Width   Range     Description
     ---------   -----   -------   -----------
     x0,y0       2,2     coords    Top-left corner (y0 scale_y)
     x1,y1       2,2     coords    Bottom-right (y1 scale_y1)
     hotkey      2       0-255     ASCII key equivalent
     flags       1       0-15      Region flags (see §3.2)
     res         1       0         Reserved
     text        var     ASCII     icon<>label<>host_command

Button rendering:
     1. Fill surface with button style surface color
     2. Draw highlight bevel (top + left edges)
     3. Draw shadow bevel (bottom + right edges)
     4. Parse text for '<>' separators:
          - Segment 1: icon filename (looked up in flash/cache)
          - Segment 2: display label (centered on button face)
          - Segment 3: host command (sent on click)
     5. Draw icon if found (centered, or top-half if label present)
     6. Draw label centered on button face
     7. Register mouse region with host command

Text format (3 segments separated by '<>'):

     icon_name<>display_label<>host_command

     If 2 segments: icon<>label (no host command)
     If 1 segment:  label only (also used as host command)


---------------------------------------------------------------------
3.5  RIP_GET_IMAGE — Copy Screen Region to Clipboard
---------------------------------------------------------------------

     Function:     Copy Screen Region to Clipboard
     Command:      |1C
     Arguments:    x0:2 y0:2 x1:2 y1:2 res:1
     Format:       !|1C<x0><y0><x1><y1><res>|

Copies pixels from the screen rectangle to the internal clipboard
buffer. The clipboard stores raw 8bpp pixel data in PSRAM.

     Parameter   Width   Range     Description
     ---------   -----   -------   -----------
     x0,y0       2,2     coords    Top-left (y0 scale_y)
     x1,y1       2,2     coords    Bottom-right (y1 scale_y1)
     res         1       0         Reserved


---------------------------------------------------------------------
3.6  RIP_PUT_IMAGE — Paste Clipboard to Screen
---------------------------------------------------------------------

     Function:     Paste Clipboard to Screen
     Command:      |1P
     Arguments:    x:2 y:2 mode:2
     Format:       !|1P<x><y><mode>|

Pastes the clipboard buffer to screen position (x,y).

     Parameter   Width   Range     Description
     ---------   -----   -------   -----------
     x,y         2,2     coords    Destination position
     mode        2       0-4       Write mode for paste


---------------------------------------------------------------------
3.7  RIP_BEGIN_TEXT — Begin Text Block Region
---------------------------------------------------------------------

     Function:     Begin Text Block
     Command:      |1T
     Arguments:    x0:2 y0:2 x1:2 y1:2 res:2
     Format:       !|1T<x0><y0><x1><y1><res>|

Defines a rectangular region for flowing text. Subsequent
RIP_REGION_TEXT commands ('t') render text lines within this
region, advancing the cursor downward after each line.

     Parameter   Width   Range     Description
     ---------   -----   -------   -----------
     x0,y0       2,2     coords    Top-left of text region
     x1,y1       2,2     coords    Bottom-right
     res         2       0         Reserved


---------------------------------------------------------------------
3.8  RIP_REGION_TEXT — Text Line in Block
---------------------------------------------------------------------

     Function:     Render One Line of Text in Block
     Command:      |1t     (lowercase, Level 1 context)
     Arguments:    justify:1 text
     Format:       !|1t<justify><text>|

Renders a line of text within the active text block region.
Advances the Y cursor by one line height (16px for bitmap font).

     Parameter   Width   Range     Description
     ---------   -----   -------   -----------
     justify     1       0-1       0=left, 1=justified
     text        var     ASCII     Text to render

Also appears as Level 0 't' in some protocol versions.


---------------------------------------------------------------------
3.9  RIP_END_TEXT — End Text Block
---------------------------------------------------------------------

     Function:     End Text Block
     Command:      |1E
     Arguments:    (none)
     Format:       !|1E|

Deactivates the current text block region.


---------------------------------------------------------------------
3.10  RIP_COPY_REGION — Copy Screen Region
---------------------------------------------------------------------

     Function:     Copy Screen Region to Another Position
     Command:      |1G
     Arguments:    x0:2 y0:2 x1:2 y1:2 res:2 dest_x:2 dest_y:2
     Format:       !|1G<x0><y0><x1><y1><res><dx><dy>|

Copies a rectangular region of the screen to a new position.
Handles overlapping source and destination correctly (uses
memmove-order row copying).

     Parameter   Width   Range     Description
     ---------   -----   -------   -----------
     x0,y0       2,2     coords    Source top-left
     x1,y1       2,2     coords    Source bottom-right
     res         2       0         Reserved
     dest_x      2       0-639     Destination X
     dest_y      2       0-349     Destination Y (scale_y)


---------------------------------------------------------------------
3.11  RIP_LOAD_ICON — Load and Display Icon
---------------------------------------------------------------------

     Function:     Load Icon from Cache/Flash
     Command:      |1I
     Arguments:    x:2 y:2 mode:2 clipboard:1 res:2 filename
     Format:       !|1I<x><y><mode><clip><res><filename>|
     Example:      !|1I0A0F000000MYICON|

Looks up an icon by filename and displays it at (x,y).

     Parameter   Width   Range     Description
     ---------   -----   -------   -----------
     x,y         2,2     coords    Display position
     mode        2       0-4       Write mode for blit
     clipboard   1       0-1       Copy to clipboard first
     res         2       0         Reserved
     filename    var     ASCII     Icon name (no extension)

Lookup order:
     1. Flash-embedded BMP table (95 icons)
     2. Flash-embedded ICN table (3 icons)
     3. PSRAM runtime cache (64 entries)
     4. Not found → queue file request for BBS transfer


---------------------------------------------------------------------
3.12  RIP_WRITE_ICON — Write Icon to Storage
---------------------------------------------------------------------

     Function:     Write Icon (stub)
     Command:      |1W
     Arguments:    (implementation-defined)
     Format:       !|1W...|

No-op on embedded systems (no writable filesystem in v1.54).
With flash filesystem (v3.1), could write to /icons/.


---------------------------------------------------------------------
3.13  RIP_PLAY_AUDIO — Play Audio File
---------------------------------------------------------------------

     Function:     Play Audio
     Command:      |1A
     Arguments:    filename (free-form text)
     Format:       !|1A<filename>|

Requests playback of an audio file. On A2GSPU, the filename
is forwarded to the IIgs via the TX queue for native sound.


---------------------------------------------------------------------
3.14  RIP_PLAY_MIDI — Play MIDI File
---------------------------------------------------------------------

     Function:     Play MIDI
     Command:      |1Z
     Arguments:    filename (free-form text)
     Format:       !|1Z<filename>|

Requests MIDI playback. Forwarded to IIgs Ensoniq DOC chip.


---------------------------------------------------------------------
3.15  RIP_IMAGE_STYLE — Set Icon Display Mode
---------------------------------------------------------------------

     Function:     Set Icon/Image Display Mode
     Command:      |1S
     Arguments:    mode:2
     Format:       !|1S<mode>|

Sets the display mode for subsequent icon blits (normal,
stretched, tiled, etc.).


---------------------------------------------------------------------
3.16  RIP_SET_ICON_DIR — Set Icon Search Directory
---------------------------------------------------------------------

     Function:     Set Icon Directory
     Command:      |1N
     Arguments:    path (free-form text)
     Format:       !|1N<path>|

Sets the search directory for icon file lookups. Stored as a
prefix for filename resolution.

     Parameter   Width   Range     Description
     ---------   -----   -------   -----------
     path        var     ASCII     Directory path (max 63 chars)


---------------------------------------------------------------------
3.17  RIP_FILE_QUERY — Query File Existence
---------------------------------------------------------------------

     Function:     Query File on Client
     Command:      |1F
     Arguments:    mode:2 res:4 filename
     Format:       !|1F<mode><res><filename>|
     Example:      !|1F000000MYFONT.CHR|

Queries whether a file exists on the client. The client responds
via the TX queue with the result.

     Parameter   Width   Range     Description
     ---------   -----   -------   -----------
     mode        2       0-3       Query type
     res         4       0         Reserved
     filename    var     ASCII     File to check

Mode values:
     0: Check if file exists
     1: Check if file exists and return size
     2: Request file transfer (initiates Zmodem)


---------------------------------------------------------------------
3.18  RIP_DEFINE — Define Text Variable
---------------------------------------------------------------------

     Function:     Define Application Variable
     Command:      |1D
     Arguments:    name (format: name=value)
     Format:       !|1D<name>=<value>|
     Example:      !|1DMYVAR=Hello World|

Defines a text variable that can be expanded in subsequent
text commands via $MYVAR$ syntax. Stored in the application
variable table (APP0-APP9 for indexed access).


---------------------------------------------------------------------
3.19  RIP_FONT_LOAD — Load Font File
---------------------------------------------------------------------

     Function:     Load Font
     Command:      |1O
     Arguments:    filename (free-form text)
     Format:       !|1O<filename>|
     Example:      !|1OMYFONT.CHR|

Requests loading of a BGI CHR or RFF font file. On the card,
all 10 standard fonts are pre-compiled in flash — this command
is a no-op for standard fonts. Custom fonts would be loaded
from the flash filesystem (/fonts/) if available.


---------------------------------------------------------------------
3.20  RIP_QUERY_EXT — Extended Query
---------------------------------------------------------------------

     Function:     Extended Query Command
     Command:      |1Q
     Arguments:    flags:3 res:2 varname
     Format:       !|1Q<flags><res><varname>|

Extended version of the query system. Routes to the same
handler as the ESC-based QUERY command but with additional
flags for response formatting.

     Parameter   Width   Range     Description
     ---------   -----   -------   -----------
     flags       3       0-46655   Query flags
     res         2       0         Reserved
     varname     var     ASCII     Variable name to query


=====================================================================
==                    END OF SEGMENT 3                              ==
==           Level 1 Interactive Commands                           ==
=====================================================================

Next: Segment 4 — Extended Commands (v2.0+)
