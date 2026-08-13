
=====================================================================
==       SEGMENT 12: BINARY PROVENANCE & EVIDENCE CLASSES          ==
=====================================================================

Segment 11 records conclusions drawn from a binary analysis of
TeleGrafix's RIPSCRIP.DLL.  This segment records the *evidence* those
conclusions rest on: which artifact, how it is identified, how the
analysis is reproduced, and — critically — what each class of
evidence can and cannot establish.

The rule this segment exists to enforce:

     A claim in these specification segments about what the DLL
     does, contains, or omits MUST cite the evidence class it comes
     from.  A bare "not present in the DLL" is not a citation.

This was written after an external standardization effort
(bbs-land/remote-imaging-protocol) raised conflicts against segment
11 that could not be checked, because the substrate had been
summarized away.  See design/bbs-land-alignment.md.


---------------------------------------------------------------------
12.1  THE ARTIFACT
---------------------------------------------------------------------

     File:         Ripscrip.dll
     Size:         592,896 bytes
     MD5:          bade8b1f4e467ac7ad4edb2639738d4c
     Format:       32-bit Windows PE (i386), PE32
     Image base:   0x10000000
     Build date:   October 16, 1997
     Build path:   C:\src\rip3\dll32\   (recoverable from .rdata)
     Driver:       see "Version labelling" below
     Ships in:     RIPtel Visual Telnet 3.1

Version labelling (CORRECTED 2026-08-12).  This document previously
recorded the driver as "RIPscrip 3.0.7".  The binary does not support
that string: it contains "3.0.7" ZERO times, and the value returned by
its own ripProductVersion() entry point is the literal

     3.00.04

which appears exactly once, in the .rdata block alongside the other
ripProductName()/ripVendorName()/ripProductPlatform() constants
("RIPscrip", "TeleGrafix Communications, Inc.", "Win32").  RIPTEL.EXE
from the same install carries "3.1" and no 3.0.x string at all.

"3.0.7" is an EXTERNAL label, not a self-report: bbs-land's artifact
catalogue records a RIPSCRIP.DLL of that name as extracted from
rtel3100.exe, and RIPlib adopted the label from there.  Whether that
build is byte-identical to this one is unverified — the catalogue does
not publish a hash.  Other RIPlib documents still use "3.0.7" as a
shorthand for this driver; where they do, it means "the image with the
MD5 above", which is the only identifier that is actually checkable.
Treat the size+MD5 pair as authoritative and the version string as
provenance metadata.

Every address in segments 11 and 12 is an absolute virtual address
valid ONLY for this exact image.  A different build invalidates them
all.  Verify the MD5 before relying on any recorded address.

Section layout:

     .text     0x10001000   vsize 0x074AE0
     .rdata    0x10076000   vsize 0x002470
     .data     0x10079000   vsize 0x01C9F0
     .idata    0x10096000   vsize 0x00147C
     .rsrc     0x10098000   vsize 0x00309C
     .reloc    0x1009C000   vsize 0x004676


---------------------------------------------------------------------
12.2  REPRODUCING THE ANALYSIS
---------------------------------------------------------------------

     python scripts/dll-provenance.py <path>/Ripscrip.dll

The script re-derives the whole dataset from the binary with no
third-party dependencies, and fails loudly if the fingerprint does
not match.  Expected output:

     fingerprint verified : True
     exports              : 153
     RIP_* strings        : 90   export-names 69   internal 21
     assertion strings    : 8
     switch jump tables   : 94
     landmark parse_state_jump_table : .text jmp=0x10039eb1 cases=13

The method mirrors the original reconstruction (see
docs/historical/ripscrip-v3-RE-notes.md):

     1. Export table enumeration
     2. String table extraction
     3. Error/assertion message cross-referencing
     4. Switch jump-table location


---------------------------------------------------------------------
12.3  EVIDENCE CLASSES — WHAT EACH ONE PROVES
---------------------------------------------------------------------

This is the section segment 11 needed and did not have.  The four
classes are NOT interchangeable, and conflating them produced at
least one false claim in the historical record.

CLASS A — Export table (153 entries).
     What it contains: the DLL's HOST-FACING API only — engine and
     instance lifecycle (RIP_EngineCreate, RIP_InstanceInit),
     stream handling (RIP_StreamWrite), buffer processing
     (RIP_ProcessBuffer), palette getters, block-mode and temp-file
     helpers.  One entry retains its MSVC decoration:
     ?RIP_SetDefaultSettings@@YAHPAURIPINST@@@Z.

     PROVES:     a host-callable entry point exists.
     PROVES NOT: anything whatsoever about RIPscrip commands.
                 NOT ONE command handler is exported.

     ==> "Absent from the export table" is worthless as evidence
         about a command.  Every command is absent from it.

CLASS B — Internal name strings (21).
     RIP_* strings that are NOT export-table names, each referenced
     by a `push imm32` inside .text.  These are handler names used
     in the DLL's own diagnostics, so a hit is strong positive
     evidence that the named handler exists.

     PROVES:     the named handler exists in this build.
     PROVES NOT: which command letter reaches it.  Absence proves
                 nothing either — only handlers that emit a
                 diagnostic carry a name string at all.

CLASS C — Assertion strings (8).
     Strings of the form "<module>.cpp - <Func>()", giving the
     original source module name.  Recovered:

          r_ports.cpp   - portDelete()
          riprocmd.cpp  - RIP_BackColor()
          riprocmd.cpp  - RIP_OneDrawingPalette()
          riprocmd.cpp  - RIP_PortCopy()
          riprocmd.cpp  - RIP_PortDelete()
          riprocmd.cpp  - RIP_SwitchEnvironment()
          riprocmd.cpp  - RIP_SwitchPalette()
          riprocmd.cpp  - rip_query()

     PROVES:     the function existed under that name, in that
                 source module, at build time.  Strongest class.
     PROVES NOT: command-letter binding.

CLASS D — Switch jump tables (94).
     Sites matching `jmp dword ptr [reg*4 + disp32]`.  The entry
     count reported by the tool is an UPPER BOUND — the walk stops
     at the first non-.text value, so adjacent tables inflate it.
     The `cmp` immediately preceding the jmp gives the true case
     count where present.

     PROVES:     a dispatch exists and how many cases it has.
     PROVES NOT: the semantic key, without further disassembly.


---------------------------------------------------------------------
12.4  INTERNAL COMMAND-HANDLER NAMES (CLASS B)
---------------------------------------------------------------------

21 RIP_* names present in .data, absent from the export table, each
with at least one code cross-reference.  These are the citable
provenance for handler existence.  "String VA" is where the name
lives; "xref" is a `push` of that address from inside .text.

     NAME                      STRING VA     CODE XREF(S)
     RIP_BeginExtendedText     0x1007A458    0x1000A6A8
     RIP_Button                0x1007A514    0x1000AC04
     RIP_ButtonStyle           0x1007A6F0    0x1000B4A0
     RIP_Color                 0x1007D3B4    0x1001AC36 +2
     RIP_Define                0x1007A814    0x1000BD77
     RIP_EnterBlockMode        0x1007DF1C    0x10024C3B +2
     RIP_ExtendedFontStyle     0x1007AD44    0x1000DDAD +2
     RIP_FileQuery             0x1007A914    0x1000BE51
     RIP_Image                 0x1007A94C    0x1000C2FF +1
     RIP_LineStyle             0x1007D73C    0x1001CE94
     RIP_LoadIcon              0x1007A9FC    0x1000CBBF
     RIP_MOUSE                 0x1007AAAC    0x1000CF93
     RIP_Mouse                 0x1007AADC    0x1000CF72 +1
     RIP_OneDrawingPalette     0x1007D7E8    0x1001D115
     RIP_PlayAudio             0x1007AB10    0x1000D2B6 +1
     RIP_Point                 0x1007D928    0x1001E3D9
     RIP_PolyPolygon           0x1007DA10    0x1001E9CE +2
     RIP_Query                 0x1007AC28    0x1000D431
     RIP_ReadScene             0x1007AC40    0x1000D671 +1
     RIP_RegionText            0x1007AC54    0x1000D736
     RIP_Scroll                0x1007AC84    0x1000D8AC +2

Note the xrefs fall in two contiguous bands — roughly
0x1000A000-0x1000E000 and 0x1001A000-0x1001F000 — and run in
alphabetical order with ascending address, consistent with handlers
compiled from one or two translation units.

Full machine-readable form: run the script; see internal-names.json.


---------------------------------------------------------------------
12.5  VALIDATED LANDMARKS
---------------------------------------------------------------------

Addresses previously recorded outside the repository, re-validated
against the binary by the script on every run:

     ripParseStateMachine      0x10039E90   .text
     parse-state jump table    0x1003AB9C   .text
                               reached from jmp at 0x10039EB1,
                               preceding `cmp` gives 13 cases
     ripCmd_MouseRegion        0x1000A964   .text

The 13-case count independently confirms the "13 states (0..12)"
figure recorded for the parse state machine.  RIPlib's own state 13
(LEVEL3_LETTER) is a RIPlib addition and is not present in the DLL.


---------------------------------------------------------------------
12.6  WHAT THIS SEGMENT SETTLES
---------------------------------------------------------------------

Handler existence confirmed by Class B/C evidence, correcting
segment 11 where it claimed otherwise:

     RIP_SetWorldFrame       — PRESENT in the string table.
                               Segment 11 §11.1 states there is "no
                               implementation found in the DLL export
                               table or function strings."  The export
                               half is a category error (see 12.3
                               Class A); the string half is FACTUALLY
                               WRONG.  §11.1 must be corrected.

     RIP_ReadScene           — PRESENT (0x1007AC40).  §DEV.4 lists
                               '1R' READ_SCENE as a RIPlib-original
                               command "beyond the published
                               TeleGrafix tables."  Incorrect.

     RIP_OneDrawingPalette   — PRESENT, and additionally carries a
                               Class C assertion string.  A drawing-
                               palette command exists in this driver.

     RIP_ExtendedFontStyle   — PRESENT, and distinct from the above.

     RIP_PolyPolygon         — PRESENT.
     RIP_Scroll              — PRESENT, distinct from RIP_CopyBlit.

Names NOT found in any class, which RIPlib's command tables assert:

     SAVE_ICON, KILL_MOUSE_EXT, and every "_EXT"-suffixed name.
     No string containing "_EXT" appears anywhere in the binary.

     Per 12.3 this is NOT proof of absence — only handlers that emit
     diagnostics carry name strings.  It does mean those names have
     no positive support from this artifact, and any claim resting
     on them must say so.


---------------------------------------------------------------------
12.7  CLASS E — THE COMMAND DISPATCH TABLE
---------------------------------------------------------------------

Recovered in full: RVA 0x080820, 129 entries of 40 bytes.  See
segment 13 for the verbatim table and scripts/dll-dispatch-table.py
to regenerate it.

     PROVES:     which command letter reaches which handler, the
                 argument COUNT, and each argument's TYPE.
     PROVES NOT: the handler's NAME.  Naming still requires class
                 B/C evidence or reading the handler body.

This is the strongest class for settling opcode disputes, because a
proposed name either fits the recorded arity and argument types or
it does not.  A name requiring three arguments cannot belong to a
letter the table records as taking seven.

Validation: all 129 handler pointers resolve inside .text, and the
independently recorded anchor (RIP_BOUNDED_TEXT, '"', RVA 0x01A0DA)
matches slot 1 exactly.


---------------------------------------------------------------------
12.8  ADJUDICATION OF DISPUTED OPCODES
---------------------------------------------------------------------

Applying class E to the conflicts raised against RIPlib's command
tables.  "RIPlib" is this project's assignment; "record" is the
TeleGrafix reconstruction maintained by bbs-land.

REFUTED — RIPlib's assignment is incompatible with the binary:

  |J   1 arg (mega2).  RIPlib assigns SAVE_ICON with 2 arguments;
       the table records ONE.  The record's RIP_SET_BASE_MATH
       (base_math:2) is exactly one 2-digit argument.  REFUTED.

  |f   2 args, both XY.  RIPlib assigns FONT_ATTRIB (attrib:2
       res:2) — two MegaNums.  The table records COORDINATE PAIRS,
       not MegaNums.  The record's RIP_SET_WORLD_FRAME
       (x_dim:XY y_dim:XY) matches the recorded types exactly.
       REFUTED.  Consequence: §A2G.3 cannot live on '|f'.

  |K   4 args, all XY.  RIPlib assigns KILL_MOUSE_EXT.  Four
       coordinate pairs describe a rectangle, not a mouse-field
       kill.  The record's RIP_FILLED_RECTANGLE fits.  REFUTED.

  |+   7 args: XY,XY,XY,XY,mega2,mega2,mega2
  |[   7 args: XY,XY,XY,XY,mega2,mega2,mega2
  |]   7 args: XY,XY,XY,XY,mega2,mega2,mega2
       Three letters with IDENTICAL signatures — a command family.
       RIPlib assigns three unrelated commands (SCROLL,
       FILL_POLYGON_EXT, POLYLINE_EXT).  Polygon and polyline
       commands take a variable vertex count by nature, yet these
       entries are fixed-arity 7.  The record's skewed-oval
       chord/pie-slice/arc family explains the shared signature.
       REFUTED.

  |_   6 args: XY,XY,mega2,mega2,XY,XY.  RIPlib assigns DRAW_TO,
       which needs a single coordinate pair.  REFUTED.

  |<   VARIABLE length.  RIPlib assigns GET_IMAGE_EXT, a fixed
       rectangle read.  Variable length fits the record's
       RIP_POLY_POLYGON.  REFUTED.

  |D   VARIABLE length.  RIPlib assigns FILL_PATTERN_EXT with 18
       fixed arguments; the table records a variable-length
       command.  The record's RIP_SET_DRAWING_PALETTE (blocks of
       palette entries) is variable by nature.  REFUTED.

  |2R  1 arg (mega4).  RIPlib assigns REFRESH with ZERO arguments;
       the table records one 4-digit argument, matching the
       record's RIP_SET_REFRESH (res:4).  REFUTED.

  |1S  NO SUCH ENTRY.  RIPlib assigns '1S' = IMAGE_STYLE.  No 'S'
       or 's' appears in the Level 1 band at all.  The record puts
       image style on '1i', which IS present (6 args).  REFUTED.

  |28  NO SUCH ENTRY.  RIPlib assigns '|28' RIP_GRADIENT_FILL and
       attributes it to this driver.  No digit-letter command
       exists in the Level 2 band, and no gradient handler name
       appears in any string class.  The attribution is REFUTED;
       §A2G.13 extends a command this binary does not contain.

APPLICATION STATUS (added 2026-08-12).  Recording a refutation is not
the same as acting on it, and for months this register did the former
without the latter.  Where each entry now stands in the CODE:

  APPLIED   |f   -> RIP_SET_WORLD_FRAME
            |&   -> RIP_SKEWED_OVAL            (see 12.14)
            |-   -> RIP_FILLED_SKEWED_OVAL
            |]   -> RIP_SKEWED_OVAL_ARC
            |[   -> RIP_SKEWED_OVAL_PIE_SLICE
            |+   -> RIP_SKEWED_OVAL_CHORD
            |_   -> RIP_FILLED_OVAL_CHORD
            |K   -> RIP_FILLED_RECTANGLE.  Handler 0x01bee5 orders
                    (arg0,arg2) and (arg1,arg3) through 0x1003112e —
                    normalising x0/x1 and y0/y1, i.e. rectangle setup.
                    SyncTERM's ripper.c agrees.  The mouse-field kill it
                    displaced was redundant: '|1k' already does that.
            |<   -> RIP_POLY_POLYGON.  Handler 0x01e80a reads arg[0] as
                    a count and walks the rest; ICONS/POLYPOLY.RIP
                    exercises it and prints "RIP_POLY_POLYGON" on
                    screen.  Wire layout read off that file:
                    count:2 { nverts:2 (x:2 y:2)* }*.  Filled even-odd
                    across all contours, because the demo places a
                    circle behind the shape to show the holes.
                    Clipboard capture stays on '|1C'.

            |J   -> RIP_SET_BASE_MATH.  Not a naming question after all.
                    Handler 0x01f32e names itself RIP_SetBaseMath and
                    accepts exactly 0x24 (36) and 0x40 (64), forcing 36
                    for anything else, then stores the byte in engine
                    state — it selects the MegaNum RADIX for everything
                    that follows, which is why it appears near the top of
                    20 of the 35 shipped scenes.  RIPlib had a clipboard
                    slot save here, with no dispatch basis, consuming a
                    slot on each of the corpus's 24 uses.  The slot
                    mechanism is RIPlib's own and moves to '|3J'.
                    Radix caveat: see D-10.
            |D   -> RIP_SET_DRAWING_PALETTE.  Handler 0x01f46a names
                    itself and validates argc == count + 3, count <= 256,
                    start <= 255, bits == 8, which gives the layout
                    outright: start:2 count:2 bits:1 then count * rgb:4 —
                    the block form of '|d' RIP_OneDrawingPalette.  The
                    8x8 user fill pattern it displaced is '|s'
                    RIP_FILL_PATTERN, already implemented, same payload.
            |1S  -> REMOVED.  Neither 'S' nor 's' exists in the driver's
                    Level 1 band.  Image style is '|1i' RIP_ImageStyle
                    (slot 98, RVA 0x00c39a), which RIPlib already
                    implements and which real scenes use.  The duplicate
                    is deleted rather than aliased: accepting an opcode
                    the protocol does not define is how a stream
                    desynchronises silently.
            |2R  -> now consumes its res:4.  The entry records one mega4;
                    RIPlib read it as a zero-argument command.

  NOT A CODE DEFECT
            |28  RIPlib's GRADIENT_FILL has no entry in this driver, but
                 section 6a already carries the corrected provenance
                 (PROVENANCE CORRECTED 2026-08-12): it stands as a RIPlib
                 extension and is no longer attributed to TeleGrafix.
                 Nothing further to apply.

Every entry in this register is now applied.

SETTLED BY NAME — 52 handlers name themselves in their own error
paths (scripts/dll-name-handlers.py; segment 13 carries the full
column).  Where a disputed letter's handler names itself, the
dispute ends:

     |f  ->  RIP_SetWorldFrame       (B3 — decisive, with arity)
     |J  ->  RIP_SetBaseMath         (B2 — decisive)
     |D  ->  RIP_SetDrawingPalette   (B6 — decisive)
     |d  ->  RIP_OneDrawingPalette   (B6 — decisive; NOT font style)
     |;  ->  RIP_PolyMarker          (B4 — decisive; NOT BUTTON_EXT)
     |y  ->  RIP_ExtendedFontStyle   (extended font style is '|y')
     |1i ->  RIP_ImageStyle          (B8 — decisive; there is no '1S')
     ESC ->  rip_query               (the Escape-introduced command)
     |2ESC-> RIP_SwitchDirectory
     |W  ->  RIP_WriteMode           (see 12.10)

WHERE FONT ATTRIBUTES ACTUALLY LIVE:

     |q  ->  RIP_FontAttrib          1 argument (mega2)

     This resolves the question underneath B3/X4.  A font-attribute
     command DOES exist in the driver — on '|q', taking a single
     argument — and RIPlib placed the feature on '|f', which is
     RIP_SetWorldFrame.  §A2G.3 should move to '|q' rather than
     being abandoned.  The facing-bit layout still needs the
     handler body read (see 12.11).

Other names recovered that correct or confirm the tables: '|=' is
RIP_LineStyle, '|S' RIP_FillStyle, '|s' RIP_FillPattern, '|Q'
RIP_SetPalette, '|a' RIP_OnePalette, '|M' RIP_SetColorMode, '|n'
RIP_SetCoordinateSize, '|N' RIP_SetBorder, '|r' RIP_TextMetric,
'|v' RIP_ViewPort, '|w' RIP_TextWindow, '|Y' RIP_FontStyle, '|b'
RIP_ExtendedTextWindow, '|p' RIP_FilledPolygon, '|2C' RIP_PortCopy,
'|2p' RIP_PortDelete (both corroborated by class C assertion
strings), '|2P' RIP_PortDefine, '|2s' RIP_SwitchPort, '|1U'
RIP_Button, '|1B' RIP_ButtonStyle, '|1M' RIP_Mouse, '|1I'
RIP_LoadIcon, '|1D' RIP_Define, '|1F' RIP_FileQuery.

SUPPORTED — RIPlib's assignment fits the binary:

  |&   5 args: XY,XY,mega2,mega2,mega2.  RIPlib's ICON_STYLE
       (x0,y0 + three mode fields) matches exactly, and the
       original reconstruction documented this letter as
       RIP_ICON_DISPLAY_STYLE from dispatch analysis.  RIPlib's
       name has genuine binary provenance here; the record's
       RIP_SKEWED_OVAL does not fit this signature.

SETTLED BY FIELD DIAGNOSTICS (added 2026-08-12).  Each handler names
the field it rejects, which identifies arguments without guesswork.
Recovered for 66 of 129 handlers by
scripts/dll-handler-semantics.py; the full listing is segment 13 §13.5.
These close the remaining B4/B6 questions outright:

  |<  "Must have at least two vertices to make a polygon"
      "Insufficient vertices (2)"
      -> POLY_POLYGON, confirmed by the handler's own words.
         RIPlib's GET_IMAGE_EXT is refuted beyond the arity argument.

  |D  "More than 256 entries"      "Start is out of range"
      "Invalid number of bits"     "Illegal RGB value"
      -> a palette-block write, exactly RIP_SetDrawingPalette.
         RIPlib's FILL_PATTERN_EXT (18 fixed args) is refuted.

  |d  "Color palette index out of range"  "Bits value out of range"
      "RGB Color value is out of range!"
      -> index / bits / rgb.  Disassembly of RVA 0x01CF95 confirms
         index <= 0xFF, bits == 8 exactly, rgb <= 0xFFFFFF.  This is
         now implemented; see B6 in the CHANGELOG.

  |;  "Invalid marker number"  "Invalid marker rotation angle (>=360)"
      "Invalid marker flags value"
      -> RIP_PolyMarker, not BUTTON_EXT.  B4 closes here.

  |q  "Invalid font attributes"
      "Font attributes not supported for system fonts"
      -> independently confirms the 2026-08-12 relocation of font
         attributes to '|q', AND that the bitmap system font ignores
         them — which is what RIPlib already does.

  |r  "Invalid text metric mode"  "Invalid text metric domain"
      -> RIP_TextMetric takes a mode and a domain.  Still
         unimplemented (D-5), but no longer semantically unknown.

  |j  "Unable to create temp brush"
      -> brush-related, with two coordinate pairs.  The only one of
         the four missing commands still genuinely unidentified.

UNDECIDED on arity alone:

  |;   7 args: XY,XY,mega2,XY,XY,mega2,mega2.  Neither BUTTON_EXT
       (RIPlib) nor RIP_MARKER (record) is excluded.  Needs the
       handler body.

  |d   3 args: mega2,mega1,mega4.  Fits both EXT_FONT_STYLE
       (fid,attr,size — RIPlib) and a single-palette-entry write
       (record).  Note '|y' carries an 11-argument font command,
       which supports the record's position that extended font
       style lives on '|y'; and RIP_OneDrawingPalette has class
       B AND class C evidence.  Leans to the record.

  (|W was resolved by disassembly — see 12.10.)

ALSO CONFIRMED:

  An ESC-introduced command exists — slot 110, letter byte 0x1B,
  handler RVA 0x046F66, 1 argument (mega4).  This corroborates the
  record's account of a literal-Escape command form.


---------------------------------------------------------------------
12.10  CLASS F — '|W' WRITE MODE, SETTLED BY DISASSEMBLY
---------------------------------------------------------------------

The write-mode ordering was the highest-impact open question and is
now closed by reading the code.  The full chain, three functions:

1.  HANDLER — RVA 0x02102C (`RIP_WriteMode()`, named by its own
    error string at 0x1007DD08).  It range-checks and stores:

         0x02104a  cmp     ebx, 4
         0x02104d  jbe     0x10021066        ; >4 -> "Invalid argument"
         ...
         0x02108d  mov     eax, [esi + 0xa]
         0x021091  movsx   ecx, word [eax + 8]
         0x021097  imul    ecx, ecx, 0x61
         0x02109a  mov     byte [ecx+eax+4], bl   ; store RAW wire value

    The value written is the wire value, unmodified.  There is no
    renumbering here.

2.  APPLY — RVA 0x00E6E9 reads it straight back and hands it to GDI:

         0x00e703  movzx   ax, byte [ecx+edx+4]   ; the stored mode
         0x00e70a  call    0x1000E6B3             ; translate
         0x00e716  push    [esi + 0x62]           ; HDC
         0x00e719  call    [0x10096408]           ; GDI32!SetROP2

3.  TRANSLATE — RVA 0x00E6B3, a five-way branch:

         mov ax, 0x0D                 ; default
         cmp ecx,1 -> mov ax, 0x07
         cmp ecx,2 -> mov ax, 0x0F
         cmp ecx,3 -> mov ax, 0x09
         cmp ecx,4 -> mov ax, 0x06

    Against wingdi.h:

         wire 0  -> 0x0D  R2_COPYPEN   COPY
         wire 1  -> 0x07  R2_XORPEN    XOR
         wire 2  -> 0x0F  R2_MERGEPEN  OR
         wire 3  -> 0x09  R2_MASKPEN   AND
         wire 4  -> 0x06  R2_NOT       NOT

THE WIRE ORDERING IS THEREFORE:

     0 = COPY,  1 = XOR,  2 = OR,  3 = AND,  4 = NOT

Two entries in segment 11 are refuted by this:

  §BUG.7 claims the DLL's internal constants (COPY 0 / XOR 1 / OR 2)
  differ from "the protocol wire values ... 0=COPY, 1=OR, 3=XOR".
  No such distinction exists.  The byte taken off the wire is the
  index into the translation above, unmodified, so XOR is 1 ON THE
  WIRE.  §BUG.7 is not a DLL bug; it is an error in this document,
  and it must be withdrawn.

  §DEAD.3 claims AND and NOT were "parsed and stored but the
  pixel-write paths only had switch cases for three modes."  The
  translation maps all five, including AND (R2_MASKPEN) and NOT
  (R2_NOT).  AND and NOT were IMPLEMENTED, not dead.  §A2G.1's
  claim to activate them is therefore unfounded.

Consequence for RIPlib: include/drawing.h currently defines
DRAW_MODE_OR=1, DRAW_MODE_AND=2, DRAW_MODE_XOR=3, and the '|W'
handler passes the wire byte through unchanged, so RIPlib renders
XOR where the protocol means OR and vice versa.  The fix is the
four constants.


---------------------------------------------------------------------
12.11  COMMANDS ARE OVERLOADED BY ARGUMENT COUNT
---------------------------------------------------------------------

11 of the 129 dispatch entries carry a letter byte of 0x00.  They are
not padding.  Each one immediately follows a real command, shares
that command's handler address, and differs only in ARITY:

     letter  slots            handler     arities
     |h      32,33,34,35,36,37  0x01CAE1  3, 3, 5, 2, 2  (+ parent 3)
     |t      61,62,63           0x01E4A4  2, 3, 7
     |x      71,72,73           0x01BC1D  2, 3, 7
     |z      77,78,79           0x01E449  2, 3, 7

A zero letter therefore means "another accepted signature for the
preceding letter", and the driver selects among them by how many
arguments the stream actually supplies.

This is the same mechanism §11.2 Erratum 1 already describes for the
'b' collision, where RIP_SET_BASE_MATH and RIP_EXTENDED_TEXT_WINDOW
are told apart by argument length.  The dispatch table shows it is
not a special case but a general facility: '|h' accepts six distinct
signatures.

IMPLEMENTATION CONSEQUENCE: a parser that binds one fixed arity per
command letter cannot accept everything this driver accepts.  RIPlib
dispatches on the letter with a single expected argument layout, so
alternate-arity forms of '|h', '|t', '|x' and '|z' will mis-parse —
and, because a wrong length shifts every subsequent field, the error
is silent rather than caught.  This has not been reconciled against
RIPlib's parser and should be treated as an open defect, not a
documented deviation.


---------------------------------------------------------------------
12.12  OPEN DEFECTS IN RIPlib SURFACED BY THIS ANALYSIS
---------------------------------------------------------------------

These are NOT deviations.  Each is a place where RIPlib's behaviour
is wrong against the driver and no decision has been taken.  Listed
so they are tracked rather than absorbed silently.

D-1  RESOLVED 2026-08-12 — and the resolution reverses the original
     recommendation.

     The letter half is fixed: '|f' is RIP_SetWorldFrame and font attributes
     moved to '|q'.  What remained was the claim that RIPlib still owed a
     world->device coordinate transform.  Measurement says it does not.

     Sampling every 2-character coordinate in the L/R/B commands of the
     shipped RIPtel 3.1 scenes that set the corpus-standard frame
     '|fZKQO' (1280x960):

          coordinate values sampled : 31,036
          values greater than 640   :    119   (0.4%)
          maximum value observed    :  1,280

     Content authored in a 1280x960 world space would spread across that
     range.  It does not: 99.6% of coordinates are already device-space.
     Applying a world->device scale would therefore shrink almost the whole
     corpus to half size — the transform would be the regression, not its
     absence.

     RIPlib stores the frame (so it is available to an embedder) and applies
     no scaling.  That is now a measured decision rather than an unfinished
     one.  The handful of values reaching exactly 1280 are worth a second
     look if full-canvas content ever turns up, but nothing in the shipped
     corpus needs the transform.

     Original entry, retained for context:
     '|f' WAS PARSED AS FONT_ATTRIB, BUT IT IS RIP_SetWorldFrame.
     Severity: high — this is live corpus content, not a corner case.
     The handler names itself RIP_SetWorldFrame and takes two XY
     coordinate pairs; RIPlib reads two MegaNums as attrib:2 res:2.
     The corpus standard '|fZKQO' (1280x960) appears in the prologue
     of most shipping scenes, so RIPlib mis-parses the opening of
     ordinary 3.x content and silently applies a font attribute.
     FIX: move §A2G.3's font attributes to '|q' (RIP_FontAttrib,
     1 argument — the driver's own home for them) and implement '|f'
     as the world frame.  Both halves change wire behaviour.

D-2  RESOLVED 2026-08-12.  Length-based signature dispatch implemented
     for all four overloaded letters.

     '|t', '|x' and '|z' share one three-signature pattern whose lengths
     are distinct, so dispatch is exact:

          4 chars   count:2 steps:2                header
          5 chars   count:1 x:XY y:XY              move-to
         13 chars   count:1 + three XY pairs       curve-to, continuing
                                                   from the current point

     That is an ordinary poly-bezier stream, and it also settled B8's
     claim about '|t': the driver's level-0 '|t' handler (RVA 0x01E4A4)
     sits beside '|z' (0x01E449) with a structurally identical body and
     an added write-mode apply.  It is RIP_POLY_BEZIER_LINE, not
     RIP_REGION_TEXT.  Region text is '|1t', which RIPlib already had, so
     correcting the letter lost nothing.

     '|h' carries six signatures on one handler.  Lengths 4 and 6 are
     unambiguous and now read their own layouts — previously they were
     read with the 8-character layout, which pulled the id and flags from
     past the end of the parameters.

     CORRECTED 2026-08-12.  An earlier draft said the two 8-character
     forms and the two 3-character forms were separated "on state we have
     not recovered".  That was asserted without reading the handler, and
     it is wrong.  '|h' (RVA 0x01CAE1) is a thin wrapper: after a
     protection check it passes BOTH the parameter block and the argument
     COUNT through to a shared routine at RVA 0x1001799E.  Selection is by
     argument count, explicitly — there is no hidden state.

     The six entries occupy consecutive slots 32-37 with character counts
     8, 4, 6, 8, 3, 3.  Taking the first entry whose template fits the
     available length gives 8 -> slot 32, 4 -> slot 33, 6 -> slot 34,
     3 -> slot 36, which is exactly what RIPlib implements; the duplicate
     entries are unreachable under that rule.  The internals of 0x1001799E
     have not been traced, so first-match is the consistent reading rather
     than a proven one.

     Corpus check: '|h' has ZERO uses across the 116 shipped scripts, so
     no real content depends on this either way.

     Scope note: the FSM accumulates parameters to the closing '|', so a
     wrong arity never desynchronised the frame.  The damage was confined
     to misreading fields within the one command — wrong picture, right
     stream position.

D-3  RESOLVED 2026-08-12.  '!' TRIGGERED ANYWHERE IN A LINE.
     The IDLE handler entered GOT_BANG on any '!', not only at a line
     boundary, so ordinary prose containing an ANSI sequence followed by
     '!' parsed as a command.  The spec-sanctioned way to start a scene
     mid-line is the SOH/STX introducer; that is implemented, and the
     relaxation was withdrawn with it — src/ripscrip.c now admits '!'
     only at a line boundary (state 0, "'!' introduces a command ONLY at
     a line boundary").  Tracked as X5.

D-4  '|28' GRADIENT IS RIPlib-ORIGINAL, NOT INHERITED.
     Severity: documentation only — corrected in segment 6A.

---------------------------------------------------------------------
12.13  CLASS G — RIPSCRIP.HLP, THE DRIVER'S OWN NAME TABLE
---------------------------------------------------------------------

Added 2026-08-12.  The RIPtel 3.1 install ships RIPSCRIP.HLP alongside
the driver.  Despite the extension it is not a WinHelp file: it opens
"RIPscrip Help File Resource" and contains two ordered tables the driver
indexes at runtime —

     * the complete ERROR MESSAGE table, and
     * a 93-entry FUNCTION NAME table, grouped by command level and
       alphabetical within each group.

This is a THIRD independent evidence class, and the strongest one for
naming: unlike the string-table method (class B) it covers handlers that
emit no diagnostics at all.  It was overlooked until late because the
analysis had been working from the DLL alone.

     PROVES:     which commands exist, by name, per level.
     PROVES NOT: the letter each name binds to.  The table is
                 alphabetical, not dispatch-ordered, so it must be
                 cross-referenced against the dispatch table.

Cross-referencing it closed three handlers that the binary alone could
not name, and independently confirmed several earlier identifications
(RIP_Point = '|j', RIP_CopyBlit = '|1g', RIP_ImageStyle = '|1i'):

  |1k   RIP_KillEnclosedMouseFields.  The Level 1 group carries both
        RIP_KillMouseFields (the plain '|1K') and this one.  The handler
        (RVA 0x00C474) matches exactly: it orders the coordinate pairs,
        applies the same transform '|j' uses, assembles a RECT via
        USER32!SetRect and passes it onward inside the drawing lock/dirty
        bracket.  IMPLEMENTED — kills the mouse fields wholly enclosed by
        the rectangle, the selective counterpart to '|1K'.

  |2Y   RIP_SwitchStyle.  The Level 2 group has exactly twelve names and
        eleven were already bound; RIP_SwitchStyle was the remainder, and
        its (slot:1, flags:2) shape matches the other Switch* commands.
        IMPLEMENTED as the graphics-style slot selector.

  |3ESC RIP_EnterBlockMode, confirmed by a name-string reference inside
        the handler's tight bounds (0x024B4E..0x0251CB).

CROSS-CHECKED against bbs-land/remote-imaging-protocol 2026-08-12.  They
mined the same help resource independently (their
version/3.0/research/riptel-help-extraction.md), which makes their
reading a genuine second opinion rather than a restatement.  Results:

  CONFIRMED, with a correction we needed.  Their 2.0 command reference
  binds RIP_KILL_ENCLOSED_MOUSE_FIELDS to Level 1 letter 'k' with
  arguments 'x0:XY y0:XY x1:XY y1:XY flags:4' — matching the binding
  derived here.  Crucially it also documents the flags field, which the
  binary alone did not reveal:

       1  kill only fields completely contained
       2  kill only fields that intersect the rectangle
       4  kill fields entirely outside the rectangle
       "If 1, 2 and 4 are not present, then NO fields are deleted."

  A first implementation here ignored the flags and always killed the
  contained set — which destroys fields on a command whose documented
  behaviour with flags=0 is to destroy nothing.  Now corrected.

  CONFIRMED.  '|1c' RIP_SET_MOUSE_CURSOR, 'cursor_style:2 res:4'.
  CONFIRMED.  RIP_SwitchStyle is one of the switchable data tables,
  supporting the '|2Y' binding.
  CONFIRMED.  RIP_EnterBlockMode is a real wire command (they cite
  2.00a4 and SyncTERM ripper.c:17069), supporting '|3ESC'.

  INVALIDATED — a speculation recorded here was wrong.  RIP_ProcessFile
  and RIP_AudioSupport are NOT wire commands: they are entries in the
  client-side DLL API that RIPTEL.EXE imports.  So the earlier note that
  '|3D' at 0x024AF4 might be RIP_ProcessFile does not stand, and the
  premise behind it — that every remaining Level 3 NAME must bind to a
  remaining Level 3 LETTER — is false.  Several of the 93 names are host
  API and never appear on the wire at all.

  NOT RESOLVED BY THEM EITHER.  bbs-land lists the same Level 3 names
  without opcodes, and states outright that RIP_SwitchDirectory's "wire
  opcode is unknown".  Two independent reconstructions working from the
  same help resource and the same binary both stop here.

RECIPROCAL AUDIT — their reference checked against the dispatch table.

Every command row in their 3.0 reference was compared against the
driver's own dispatch entry (letter, arity, argument widths).  Findings,
in both directions:

  THEIR NOTATION IS BETTER THAN OURS.  Most apparent mismatches were an
  artefact of this side's parser, not their errors: they write widths as
  'CM' (colour-mode dependent) alongside 'XY' (coordinate-size
  dependent), which is exactly what the driver's own 'color' and 'XY'
  argument-type bytes mean.  Once decoded that way, '|c', '|S' and the
  rest agree with the binary exactly.  A model that only understood
  fixed digit counts under-reads the protocol.

  '|3e' — THEY RESOLVE ONE OF OURS.  Their reference binds level 3 'e'
  to RIP_BAUD_EMULATION (evidence 2.A0), and RIP_BaudEmulation is in the
  driver's function-name table.  This segment previously carried '|3e'
  as style-slot protection, on diagnostics that had bled in from a
  neighbouring handler under loose bounds.  Corrected, and implemented.

  '|1A' — WE RESOLVE ONE OF THEIRS.  Their reference carries a row
  literally titled "1A (unidentified)", noting "6 digits observed
  (layout unresolved)".  The handler at RVA 0x00DC58, bounded tightly by
  the next entry (62 bytes), pushes BOTH 'Invalid article number' and
  'RIP_SelectArticle()'.  It is RIP_SelectArticle, and its dispatch
  entry records mega2 + mega4 = 6 characters, matching their corpus
  observation exactly.  Worth sending upstream.

  THE Switch* WIDTHS — RESOLVED 2026-08-12, IN THE DISPATCH TABLE'S
  FAVOUR.  This was recorded as unresolved in both projects.  Their
  reference documents '|2s' as 'port-num:1 flags:2 res:3' (6 chars)
  where the dispatch entry records mega1 + mega2 (3), and '|2T' as
  'window_num:1 res:1' (2) against 3.  The note allowed that both could
  be true if trailing reserved bytes were consumed outside the template.

  They are not.  The shipped corpus contains three '|2s' commands and
  every one of them is THREE characters:

       !|2s000     port 0, flags 0
       !|2s002     port 0, flags 2
       !|2s100     port 1, flags 0

  There is no res:3.  The dispatch entry is complete, the whole Switch*
  family is uniformly mega1 + mega2 (slots 111, 112, 114, 118, 119, 121
  all agree), and RIPlib's reader — port:1 then flags:2 — is correct as
  written.  Worth sending upstream: a consumer that trusts the
  6-character layout will over-consume three bytes and desynchronise the
  rest of the frame.

  Method note: this is the second question this session settled by
  measuring the corpus rather than reasoning about the binary, after
  D-1.  Where vendor content exercises a command, it outranks both
  documents.

  ONE OPEN ITEM ON THIS SIDE — SINCE RESOLVED.  '|F' RIP_FILL showed
  argc=0 at RVA 0x01B2FD, one byte before '|G' at 0x01B2FE, and was
  guessed to be a thunk or a mis-parse.  It is neither: 0x01B2FD is a
  bare 'ret', the tail of the preceding function, so THE 3.0 DRIVER
  STUBS OUT FLOOD FILL.  Their 'x:XY y:XY border:CM' is still the right
  reading for the WIRE, and RIPlib implements it that way; the driver
  simply declines to act on it.  See D-9.

So the two '|3D' entries remain unbound ('|3e' is now resolved).  Established: '|3D' at
0x024AF4 copies its text argument into a 256-byte buffer and calls a
routine referencing "ICONS"; '|3D' at 0x038BD2 is a 15-byte thunk.  None
appears in the 116-file corpus.  They stay recorded rather than guessed,
and that is now a position two projects share rather than a gap unique
to this one.


---------------------------------------------------------------------

D-8  '|1k' AND '|3D' — HOW FAR THE ANALYSIS ACTUALLY GOT.
     Recorded 2026-08-12 after these were re-examined.  An earlier draft
     called their semantics "not recovered", which overstated the effort
     spent: the dispatch entry and the absence of a diagnostic string had
     been checked, but the handlers themselves had not been read.  They
     have now been read, and the honest position is:

     '|1k'  (RVA 0x00C474, 5 args: XY,XY,XY,XY,mega4) — SUBSTANTIALLY
            recovered.  It orders the two coordinate pairs, applies the
            same world/device transform '|j' uses (RVA 0x10031084),
            assembles a RECT via USER32!SetRect, and passes that rect plus
            the 4-digit argument to RVA 0x10012D63, bracketed by the same
            lock/dirty pair the drawing commands use.  So it is a
            rectangle operation with a 4-digit parameter.  What 0x10012D63
            actually does is not established: it references no strings, so
            naming it needs semantic tracing rather than string mining.

     '|3D'  RESOLVED 2026-08-12 — see below.  The earlier position, that
            both handlers "reference NO strings at all" and nothing beyond
            the dispatch entry was established, was true only of STRING
            evidence.  Following the CALL TARGETS settled it, and resolving
            the driver's import table named the decisive one.

            Slot 122 (RVA 0x038BD2) is a five-instruction thunk that hands
            arg[0] to 0x100282CA, which busy-waits on WINMM!timeGetTime.
            Its arithmetic fixes the unit beyond doubt: the count is split
            into chunks of 3900 with 0xFDE8 = 65000 ms waited per chunk
            (3900/60 = 65 s), then the remainder waited as
            remainder * 1000 / 60 ms.  So '|3D' is RIP_DELAY and its field
            is in SIXTIETHS OF A SECOND.

            Slot 125 (RVA 0x024AF4) is a different command that happens to
            share the letter.  It copies a TEXT parameter into a 256-byte
            buffer, looks it up via 0x1003F71A, calls 0x1003F80E with the
            result, and on a return of 2 calls 0x10006C01 — which names
            itself RIP_Suspend() in its own error path.  It never reads the
            decoded argument array, so it does not match its argc=1/mega4
            row; that row is the mis-associated one.  What it looks up is
            not established, and it is NOT the reading RIPlib implements.

            RIPlib implements the slot-122 reading and deliberately does
            NOT busy-wait: a rendering library that blocks its caller for
            up to 65 seconds per chunk is unusable on the cooperative and
            single-threaded hosts RIPlib targets.  The request is recorded
            and handed over by rip_take_delay(); ignoring it is safe,
            because a delay is a pacing hint and not a rendering
            instruction.

            METHOD NOTE.  Three evidence classes had already been tried on
            these handlers and all three came up empty, because every one
            of them keys on STRINGS.  What worked was resolving the import
            directory and reading call targets — the same pass also
            confirmed GDI32!Polygon as the skewed-oval renderer (12.14).
            Where a handler names nothing, name what it CALLS.

     Both are absent from the shipped corpus, so no vendor content
     exercises them.  '|3D' is now implemented as RIP_DELAY; '|1k' is
     implemented with the flags semantics bbs-land documents, and only
     the identity of 0x10012D63 remains open.  That residue is a bounded,
     evidenced gap — not an unknown, and not a claim of completeness.

     Corpus note: this entry and several others said "116-file corpus".
     The RIPtel 3.1 installation examined here ships 35 .RIP scenes
     (scripts/corpus-scan.py; 12,328 command instances, 70 distinct
     opcodes).  Where 116 appears in older text it refers to a larger
     collection catalogued elsewhere, not to what was measured.

D-5  RESOLVED 2026-08-12.  FOUR DRIVER COMMANDS WERE UNIMPLEMENTED.
     Measured by diffing the dispatch table against RIPlib's handler
     switch.  Of 73 Level-0 commands in the driver, RIPlib implemented
     69.  The four missing were:

          |j   2 args (XY, XY)      — unnamed in the string table
          |r   3 args               — RIP_TextMetric
          |x   var                  — FILLED_POLY_BEZIER (the unfilled
                                      'z' form was already implemented)
          |y   11 args              — RIP_ExtendedFontStyle

     All four are now implemented.  '|y' was the significant one: it is
     the driver's real extended font-style command, and RIPlib had
     extended font style on '|d' — which the driver uses for
     RIP_OneDrawingPalette (see 12.8, B6).  Both halves of B6 are now
     corrected: '|d' is a palette command and '|y' carries extended font
     style, in the 26-character layout that independently matches
     bbs-land's reading.

     The corpus census (scripts/corpus-scan.py) confirms full Level-0
     coverage: all 70 distinct opcodes across 12,328 command instances
     in the 35 shipped scenes reach a handler.

D-6  MITIGATED 2026-08-12; ONE DECISION LEFT.  The mapping is now
     RIPLIB_PALETTE_BASE (include/riplib_platform.h), overridable at
     configure time, range-checked so the 16 EGA entries must fit in
     0..255, and documented with the reason the offset exists.  A port
     that owns its framebuffer builds with -DRIPLIB_PALETTE_BASE=0 and
     gets identity mapping.  So the policy is no longer baked in.

     What remains is the DEFAULT, which is still 240 — the value the
     first consumer needed.  Flipping it to 0 would make the neutral
     choice the default, but it silently changes every pixel value a
     current consumer receives, so it is a deliberate release decision
     rather than a cleanup.  Recorded, not taken.

     Original text follows.

     '§A2G.6' BAKES A HOST POLICY INTO THE LIBRARY.
     src/ripscrip.c:212 maps every EGA index to framebuffer value
     240 + idx, justified in segment 6 by a conflict with "the xterm-256
     color palette used by the VT100/ANSI text renderer".  That is an
     assumption about the HOST's text renderer sharing the framebuffer,
     not a property of RIPscrip.  A consumer with a plain 16-colour
     framebuffer receives indices 240-255 it never asked for.  This is a
     platform-independence violation in the same family as the branding
     leaks, but structural rather than cosmetic, so the branding lint
     cannot see it.

D-7  'riplib_host_tx' CARRIES CONSUMER TERMINOLOGY IN THE PUBLIC API.
     include/riplib_platform.h:88.  One of exactly three functions every
     port must implement, and its name says "card".  Renaming is a
     breaking API change and therefore a v2.0.0-shaped decision.

D-9  WITHDRAWN 2026-08-12, THE SAME DAY IT WAS RAISED.  It alleged
     "the dispatch parser mis-types some arguments".  Both symptoms were
     re-checked against the raw table bytes and both are FAITHFUL
     RECORDS; the extractor is correct and the defect does not exist.
     Recorded rather than deleted because blaming one's own tooling for
     an accurate reading is a mistake worth leaving visible.

     Symptom 1, '|&' (slot 3) typed XY, XY, mega2, mega2, mega2.  The
     raw entry really is ff ff 02 02 02.  The apparent contradiction
     with the handler — which hands (arg0,arg1) AND (arg2,arg3) to the
     coordinate mapper at 0x10031084 — is not a contradiction, because
     the two things describe different layers:

          the TYPE byte gives the WIRE WIDTH (how many digits to read)
          the coordinate mapper is SEMANTIC (scaling after decode)

     A radius is a coordinate-like quantity that gets scaled, and it can
     still be transmitted as a fixed 2-digit MegaNum.  '|&' does exactly
     that; '|-' (ff ff ff ff 02) transmits its radii at coordinate
     width.  At the default coordinate size of 2 both encode to the same
     10 characters, which is why TeleGrafix's demo shows an identical
     payload shape for the pair.  See D-11 for what that costs.

     Symptom 2, '|F' RIP_FILL at RVA 0x01B2FD with argc=0, one byte
     before '|G' at 0x01B2FE.  0x01B2FD disassembles to a single 'ret'
     — it is the tail of the preceding function, and 0x01B2FE is a real
     'push ebp' prologue.  So the entry is accurate and the finding is
     about the DRIVER, not the table:

          THE 3.0 DRIVER STUBS OUT FLOOD FILL.  '|F' is dispatched, so
          the letter is recognised and its frame consumed, but the
          handler returns immediately.  Slot 27 is the only Level 0 row
          whose handler is a bare 'ret'; the other argc=0 rows ('E'
          RIP_ERASE_VIEW at 0x01ad6f, 'e' RIP_ERASE_WINDOW at 0x01ad98,
          Level 1 'K' at 0x00c543) all have real prologues.  No shipped
          scene uses '|F'.

     RIPlib implements '|F' as the 1.54 specification defines it
     (x:2 y:2 border:2), matching SyncTERM and IcyTerm, and that stays.
     A 3.0 driver declining to flood-fill is not a reason for a library
     that also serves 1.54 content to drop the command.  Note that
     RIPlib once changed '|F' to take zero arguments on the strength of
     this very argc=0 reading, described in the code as "DLL internal
     behavior"; that change was later reverted against the spec.  This
     entry explains what was actually being observed.

     Consequence: argument type bytes AND counts in segment 13 are
     records of the binary and have held up under every check made.
     Where a handler's behaviour appears to disagree, expect a
     wire-versus-semantics distinction like symptom 1 before suspecting
     the table.

D-11 RESOLVED 2026-08-12.  COORDINATE WIDTH WAS RECORDED BUT NOT
     HONOURED.  The dispatch record types many arguments 0xFF ("width per
     SET_COORDINATE_SIZE") and a few 0xFE ("width per SET_COLOR_MODE"),
     and the driver resolves both at decode time (resolver at RVA
     0x039DE0).  RIPlib parsed '|n' into rip_state_t.coordinate_size and
     then read fixed 2-digit fields at fixed offsets in 262 places, so a
     stream selecting any other width desynchronised from its first
     coordinate.

     FIXED BY NORMALISING THE PAYLOAD, not by rewriting 262 call sites.
     Before dispatch, when the negotiated widths are not the default, the
     command's payload is rewritten so every argument is two digits and
     the handlers never see the difference.  scripts/dll-argtypes.py
     emits the per-command type table this needs; only the 56 commands
     that actually contain a 0xFF or 0xFE argument are carried, because
     a command whose widths are all literal is already correct.

     Properties worth stating, because each was a bug on the way here:

       - Literal-width fields are copied VERBATIM at their own width.
         Re-emitting a mega1 or mega4 field as two digits corrupts it.
       - The rewrite is in place, which is safe only because coord_w >= 2
         means the negotiated fields shrink and literals do not move.  No
         scratch buffer, so nothing lands in the dispatcher's stack frame
         — an earlier version used a 1 KB local, GCC inlined it, and
         execute_rip_command went from 648 bytes to 1416.
       - The type list is MEASURED before anything is written.  Bailing
         out mid-rewrite leaves a half-converted payload, which is worse
         than not trying; the first version did exactly that and the
         existing metadata tests caught it.
       - With the default width the whole path is skipped, so the common
         case is byte-for-byte unchanged.

     LOSSY ONLY ABOVE 1295, the largest value two digits hold.  That is
     acceptable for RIPlib specifically: it renders into a fixed 640x400
     device space and deliberately does not apply a world-to-device
     transform (D-1), so a coordinate above 1295 is off-screen whatever
     width carried it.  A port that grows a world transform must revisit
     this rather than inherit it.

     rip_state_t.coord_size_unsupported remains, and is now cleared when
     a command is successfully normalised, so it means what it says: a
     width this build could not handle.

D-14 THREE FIELD LISTS THAT DISAGREE WITH THE DISPATCH RECORD, LEFT
     UNCHANGED.  Recorded 2026-08-12 from a field-by-field comparison of
     every RIPlib handler against the driver's own argument types.  Of 47
     comparable commands, 19 match exactly, 19 differ only in notation
     (a literal 2 where the record says width-negotiated, identical at
     the default), and 9 genuinely differ.  Six of those nine were
     resolved -- '|k', '|=', '|D' in v2.0.1, '|3e' and '|1I' here, and
     '|1i' proved to be a false alarm (its 24-character payloads carry a
     12-character reserved tail RIPlib correctly ignores).  These three
     are NOT resolved, and are recorded rather than guessed:

     '|1G' RIP_COPY_REGION.  Slot 95 records argc 7,
     FF FF FF FF 01 01 FF -- four coordinates, two single digits, then
     ONE further coordinate, twelve characters in total.  RIPlib requires
     fourteen and reads a destination PAIR at offsets 10 and 12, citing
     the earlier reconstruction's "8 args".  Only one trailing coordinate
     exists in the record, so a destination pair cannot be mapped onto it
     without inventing a field.  The command has no corpus uses, so
     neither reading can be validated against content.  Left as it is:
     RIPlib's version at least performs a coherent copy, and replacing it
     with a layout that cannot be interpreted would be worse.

     '|:' RIP_MOUSE_REGION_EXT.  Slot 11 records argc 11, ten coordinates
     and one single digit -- twenty-one characters.  RIPlib requires
     twenty-two and reads six fields.  No corpus uses.

     '|1g' COPY_BLIT.  Slot 96 records argc 8, six coordinates then two
     single digits.  RIPlib reads seven fields and stops after the first
     of the two trailing digits.  No corpus uses.

     The common shape is worth naming: all three come from the original
     reconstruction rather than from the dispatch record, all three
     disagree with it, and none is exercised by shipped content -- which
     is exactly why they survived.  A command no scene sends is a command
     no test can check.

D-13 STATE RECORDED "FOR THE HOST" THAT NO HOST CAN READ.  Recorded
     2026-08-12 by diffing every field of rip_state_s against its uses:
     of 111 fields, 24 are written and never read anywhere in the
     library.  Most carry a comment along the lines of "recorded so an
     embedder that cares can act on it".  No embedder can: rip_state_t
     is INTERNAL by policy (ADR-0001, opaque-by-policy), and the only
     accessors the public header offers are rip_set_url_handler() and
     rip_take_delay().  So these fields are, in practice, dead:

          baud_emulation      encoded_stream_type   mouse_cursor_id
          coordinate_res      encoded_stream_len    refresh_res
          coord_size_unsupported  header_type       text_metric_mode
          mega_base           header_id             text_metric_domain
          viewport_scale      header_flags          char_spacing

     This is not an argument for deleting them — parsing a field and
     recording it is what keeps a frame in sync and is often the honest
     alternative to guessing at semantics.  It is an argument that the
     comments overstate what a consumer can do, and that anything worth
     recording is worth an accessor.  '|3D' RIP_DELAY is the pattern to
     follow: the field is recorded AND rip_take_delay() exposes it.

     Two further fields were not "recorded for the host" but simply
     dead, and both are now dealt with:

       line_cont           REMOVED.  Declared for '\' continuation, only
                           ever assigned false.  Continuation is real and
                           works, but through prev_state and the
                           LINE_CONT FSM state, not this flag.

       utf8_pipe_pending   KEPT, but its comment now says NOT
                           IMPLEMENTED.  It describes accepting a UTF-8
                           transcoded introducer where '|' has become
                           U+00A6 (0xC2 0xA6).  Nothing in the FSM sets
                           or reads it and no 0xC2/0xA6 handling exists,
                           so such a stream is not recognised.  That is a
                           real gap, and the declaration made it look
                           solved.

     Two were gaps in RENDERING rather than API, and both are now
     CLOSED (2026-08-12):

       bez_steps      '|t', '|x' and '|z' carry an nsteps field in their
                      4-character header form.  It was parsed and never
                      consulted: filled curves always flattened to 12
                      segments and outlines always used draw_bezier()'s
                      adaptive estimate, so a stream asking for coarse
                      geometry was given smooth curves regardless.
                      draw_bezier_steps() splits the fixed-count
                      flattener out of draw_bezier(), which now delegates
                      to it, and the RIPscrip layer passes the stream's
                      count when one was set.  Unset still means
                      adaptive, so default quality is unchanged.  No
                      shipped scene uses the header form, which is why
                      the corpus renders identically.

       char_spacing   '|y' carries an inter-character spacing percentage
                      that the driver enforces non-zero.  Every text path
                      used the glyph's own advance, so condensed and
                      expanded text rendered the same as normal.
                      bgi_font_set_char_spacing() applies it at both
                      per-glyph advance sites, module-scoped to match
                      draw_set_color() and the other renderer state
                      rather than changing a public signature.

                      LIMIT: this reaches the STROKE fonts only.  The
                      bitmap path renders a whole run through draw_text()
                      with a fixed 8-pixel cell, so spacing does not
                      apply there.  Every '|y' in shipped content
                      requests 100 -- normal -- so nothing real is
                      affected either way.

D-12 RESOLVED 2026-08-12 — THE BASE-64 ALPHABET, AND WHO USES IT.
     This supersedes D-10 below, which is retained for the record of how
     the search went wrong.  The answer was in TeleGrafix's own content,
     not in the binary, and the binary only confirmed it afterwards.

     THE ALPHABET.  ICONS/TUNNEL.RIP writes 64 consecutive palette
     entries with '|d'.  Their indices must increase by exactly one and
     their RGB values by exactly four; only one alphabet makes both
     sequences come out right, and it is the only reading under which
     '0z' (61) is followed by '0#', '0&' and then '10' (64):

          '0'-'9' ->  0..9        'a'-'z' -> 36..61
          'A'-'Z' -> 10..35       '#'     -> 62      '&' -> 63

     The two symbols past 'z' are '#' and '&' — printable, and neither
     is '|'.  The candidate table at RVA 0x07EEE8 recorded under D-10
     was wrong, as its zero .text references suggested.

     CORROBORATION FROM THE BINARY.  A 4-digit base-64 field spans
     0..64^4-1 = 0..0xFFFFFF exactly — which is the bound the palette
     handler enforces with "RGB Color value is out of range!".  The RGB
     field is 24-bit and only reaches 24 bits in this radix; in base 36
     four digits cap at 1679615 and the check could never fire.

     WHICH COMMANDS.  The radix is per-command, not global.  The flag
     word at dispatch entry +0x26 — the trailing bytes of each record —
     carries a 2-bit field, and the parser's predicate at 0x039D70 reads
     it before falling back to the global base byte at (state+2)+0x38:

          1  always base 36        '|J', '|N'          (2 entries)
          2  always base 64        '|D', '|d', '|h', '|y'  (4 entries)
          3  follow the global base                    (96 entries)

     The predicate picks between two character validators: 0x100210B2
     accepts only 0x30-0x39 and 0x41-0x5A (base 36 exactly), while
     0x100210D0 goes through the CRT ctype table and admits lowercase.

     '|J' being permanently base 36 is the keystone of the design: the
     command that SETS the radix must itself decode unambiguously.  It
     also explains why every '|J' in the corpus is '|J10' — that is 36
     in base 36 and 64 in base 64, so it asserts the current radix
     rather than changing it.

     TUNNEL.RIP settles that the selection really is per-command: it
     carries base-64 '|d' payloads AND '|fZKQO', which is 1280x960 only
     in base 36.  Both in one file.

     WHAT IT COST RIPlib.  rip_mega_digit() is case-INSENSITIVE, which
     is right for base 36 and ruinous here: it folds 'a'..'z' onto
     10..35 and returns 0 for '#' and '&'.  61 of TUNNEL.RIP's 65
     palette entries decoded wrong, with '#' and '&' collapsing onto
     entry 0.  '|y' RIP_ExtendedFontStyle was equally affected across
     195 uses in 25 files: every one carries '1a1a' in its scale fields,
     which is 100,100 in base 64 — a percentage — and a meaningless
     46,46 in base 36.

     FIXED for all four commands via rip_mega_digit64()/rip_mega2_64()/
     rip_mega4_64() in src/rip_meganum.h.  The change is deliberately
     confined to those four; the other 96 entries follow the global base,
     which stays 36, so nothing else moves.

     STILL OPEN: how a stream selects global base 64, given '|J10'
     asserts rather than sets and no corpus file sends '|J1S'.  It does
     not matter for the four always-64 commands, which is why the fix
     lands without it.

D-10 SUPERSEDED BY D-12.  BASE-64 MEGANUM IS ACCEPTED BUT NOT DECODED.
     Recorded
     2026-08-12.  '|J' RIP_SET_BASE_MATH (RVA 0x01f32e) selects the
     MegaNum radix, and the handler accepts exactly two values: 0x24
     (36) and 0x40 (64), forcing 36 for anything else.  So the protocol
     has a base-64 mode.

     RIPlib records the selected base in rip_state_t.mega_base and
     reproduces the driver's validation, but its decoders
     (src/rip_meganum.h) are base 36 unconditionally.  The reason is
     that the base-64 DIGIT ALPHABET has not been recovered: '0'-'9',
     'A'-'Z' and 'a'-'z' account for only 62 symbols, and which two
     characters carry the remaining values — and in what order — is not
     established by anything read so far.  Guessing would silently
     corrupt every numeric field on a base-64 stream, which is worse
     than the gap; this is the same reasoning already applied to '|y'
     (D-5).

     Impact is nil on real content: all 24 uses of '|J' across the 35
     shipped scenes are '|J10', which is base 36.  A stream that selects
     base 64 will currently mis-decode, and that is a known, recorded
     limitation rather than an unnoticed one.

     PROGRESS 2026-08-12 — the reader is found, the alphabet is not.
     0x1003E8EB, which '|J' calls, only STORES the base: into
     (state+2)+0x38 and a second slot at (state+0xe) indexed by
     (word at +0x0a) * 16 + 5.  Enumerating every byte read of +0x38
     across .text gives exactly ONE consumer (a third apparent hit at
     0x05200F is a false positive — 8A 4C 38 04 is mov cl,[eax+edi+4],
     where 0x38 is a SIB byte, not a displacement):

          0x039D70   a predicate returning 0 or 1.  It consults the
                     colour mode at +0x3a, then two flag bits in the
                     dispatch entry's word at +0x26, and only as a final
                     fallback returns (base == 0x40).

     That predicate has exactly one caller, 0x03A02E, inside the parser's
     per-character loop — the same loop that rejects bytes >= 0x7F and
     treats 0x5C ('\') as the line-continuation character, which is the
     continuation POLYPOLY.RIP uses.

     So the base does not select a digit TABLE at the point of decode; it
     feeds a per-character predicate that decides how the accumulator
     treats the byte.  The digit-to-value conversion itself is further
     down that loop and is still not isolated.  This also means the
     candidate at 0x07EEE8 is now LESS likely, not more: nothing indexes
     it, and the one place the base is consulted does not look up a table
     at all.

     Incidental confirmation from the same loop: at 0x03A004 the parser
     computes its dispatch entry as

          lea eax, [ebp + ebp*4]            ; index * 5
          lea ecx, [eax*8 + 0x10080820]     ; * 8  ->  index*40 + base

     which is the driver validating RIPlib's recorded table layout —
     0x080820, 40-byte entries — from its own code rather than from the
     reconstruction that first asserted it.

     CANDIDATE ALPHABET, NOT ADOPTED.  RVA 0x07eee8 (.data) holds
     exactly 64 contiguous bytes, ASCII 0x20..0x5F — space through
     underscore — which would make a base-64 MegaNum digit simply
     (ch - 0x20), covering 0..63 with no gaps.  That is the right shape
     and the right length.  It is NOT adopted, for two reasons:

       - it has ZERO references from .text, so nothing observed actually
         uses it as a lookup table; and
       - its neighbours in .data are 'USASCII', 'KANJI', 'VERBOSE',
         'TWOCHAR', 'THREECHAR' — a character-set keyword group — so a
         printable-ASCII run there is at least as likely to be a charset
         map as a radix table.

     Adopting it on shape alone is exactly the guess this defect exists
     to avoid.  It is recorded so the next pass starts here instead of
     rediscovering it.


---------------------------------------------------------------------
12.14  CLASS H — NEWCMDS.RIP, TELEGRAFIX'S OWN COMMENTED DEMO
---------------------------------------------------------------------

A ninth evidence class, and the strongest yet for command IDENTITY:
the RIPterm/RIPtel installation ships 35 .RIP scenes, and one of them
(ICONS/NEWCMDS.RIP, 1,747 bytes, dated 8 April 1997) is a commented
demonstration file in which TeleGrafix names each command it exercises:

     !|! Show RIP_SKEWED_OVAL
     !|N01|&20151G0M1M

     !|! Show a RIP_SKEWED_OVAL_ARC
     !|N01|]50151G0M20601M

     !|! Show a RIP_FILLED_OVAL_CHORD
     !|N01|_B03F90601G0M|!  With    a border
     !|N00|_B05P90601G0M|!  Without a border

This is not an inference from the binary; it is the vendor writing down
what the letter means, next to a working example of its argument layout.

WHAT IT ESTABLISHES

     |&   RIP_SKEWED_OVAL             5 args   10 chars
     |-   RIP_FILLED_SKEWED_OVAL      5 args   10 chars
     |]   RIP_SKEWED_OVAL_ARC         7 args   14 chars
     |[   RIP_SKEWED_OVAL_PIE_SLICE   7 args   14 chars
     |+   RIP_SKEWED_OVAL_CHORD       7 args   14 chars
     |_   RIP_FILLED_OVAL_CHORD       6 args   12 chars

Every one of those arities matches the dispatch table's recorded argc
exactly.  The file also proves the coordinate layout on its own: before
drawing anything it strokes a grid

     !|L2000209Q   (x=72)     !|L0015HR15  (y=41)
     !|L5000509Q   (x=180)    !|L003FHR3F  (y=123)
     !|L8000809Q   (x=288)    !|L005PHR5P  (y=205)
     !|LB000B09Q   (x=396)

and then places each shape on an intersection.  Decoding the demo
payloads as MegaNum pairs puts every shape's first two arguments on a
grid node, and leaves radii 52/22 IDENTICAL across all seven shapes —
which is what a "same shape, seven variants" demo must look like.

FIELD ORDER, SETTLED BY DISASSEMBLY

The handler for '|-' (RVA 0x01c348) and the handler for '|&'
(RVA 0x01f904) are instruction-for-instruction identical apart from
frame size and stack offsets — the filled and outline members of one
shape.  Both load five arguments and hand (arg0,arg1) and then
(arg2,arg3) to the SAME coordinate-pair mapper at 0x10031084, then pass
everything to the generator at 0x10010160:

     push ecx           ; POINT buffer (8 KB)
     push eax           ; &bounding rect
     push ebx           ; 0x168 = 360      <- end angle
     push 0             ;                  <- start angle
     push edi           ; arg[4]  = skew
     push [ebp-0x14]    ; arg[3]  = ry
     push [ebp-0x10]    ; arg[2]  = rx
     push [ebp-0x0c]    ; arg[1]  = cy
     push [ebp-0x08]    ; arg[0]  = cx
     push esi           ; engine state
     call 0x10010160
     ...
     call Polygon(hdc, pts, 360)

So the non-arc members are the arc generator with start/end pinned to
0..360, and the driver renders the whole family as a 360-point polygon
rather than with a GDI ellipse call.

WHAT 'SKEW' ACTUALLY IS

The generator at 0x10010160 indexes two Q14 fixed-point tables — sine at
RVA 0x07b638, cosine at 0x07b098, 360 entries each, verified against
libm to 1 LSB — and its inner loop is a plain 2-D rotation:

     X  = rx * cos(t) >> 14          Y  = ry * sin(t) >> 14
     px = cx + (X * cos(skew) - Y * sin(skew)) >> 14
     py = cy - (X * sin(skew) + Y * cos(skew)) >> 14

emitting one point per degree over [start,end] inclusive and tracking a
bounding rect as it goes.  'skew' is therefore a ROTATION ANGLE IN WHOLE
DEGREES, not a shear factor or an aspect ratio.  The Y subtraction is
the screen-coordinate inversion, so angles run counter-clockwise from
east.  RIPlib reproduces this arithmetic exactly; see
rip_skewed_oval_points() in src/ripscrip.c.

The cosine table equals sin(t+90) for 358 of its 360 entries and differs
by one LSB on the other two, so RIPlib ships a single sine table.

WHAT IT COST

RIPlib had all six letters bound to unrelated commands — ICON_STYLE,
TEXT_XY_EXT, POLYLINE_EXT, FILLED_POLYGON_EXT, SCROLL and DRAW_TO — and
rendered them as rectangles and line segments.  Section 12.12 had
already marked four of the six REFUTED on arity grounds alone; the code
was never changed to match, which is the failure this class caught.  The
two capabilities that had no protocol basis but were worth keeping (icon
display style, bounded text box) moved to '|3&' and '|3-', letters the
driver's Level 3 set does not use.

---------------------------------------------------------------------
12.16  CLASS I — WHAT A HANDLER CALLS, NOT WHAT IT SAYS
---------------------------------------------------------------------

Classes B, C, F and G between them named most of the dispatch table, and
they share a blind spot: every one of them keys on STRINGS.  A handler
that pushes no name, raises no diagnostic and reports no error is
invisible to all four at once, which is precisely why '|3D' survived
three separate attempts (D-8).

Class I asks the other question: not what a handler SAYS, but what it
CALLS.  scripts/dll-handler-imports.py resolves the import directory,
walks each handler and its callees to a bounded depth, and reports the
Win32 APIs reached.  Drawing commands are self-identifying under this
lens, because GDI names its primitives after the shapes they draw.

WHAT IT INDEPENDENTLY CONFIRMED.  Every one of these was decided on
other grounds first and then checked against this class:

     |<   GDI32!PolyPolygon   — the ONLY handler in the table that
                                reaches it.  RIP_POLY_POLYGON, settled.
     |K   GDI32!Rectangle     — the same primitive as '|B' RIP_BAR and
                                '|R' RIP_RECTANGLE.  A filled rectangle,
                                not a mouse operation.
     |D   SetPaletteEntries   — identical API set to '|d'
                                RIP_OneDrawingPalette, '|a'
                                RIP_ONE_PALETTE and '|Q' RIP_SET_PALETTE.
                                A palette command.
     |&   GDI32!Polygon       — with '|+', '|-', '|[' and '|P'.  The
                                skewed-oval family really is rendered as
                                a polygon, as 12.14 deduced from the
                                360-point buffer.
     |]   GDI32!Polyline      — NOT Polygon, unlike its four siblings.
                                The arc is stroked and open, which is how
                                RIPlib implements it.
     |J   (nothing)           — reaches no drawing or resource API at
                                all, which is what a pure state setter
                                like RIP_SET_BASE_MATH should look like.
     |3D  WINMM!timeGetTime   — via its callee.  This is the one that
                                broke the deadlock; see D-8.

LIMITS, WHICH MATTER FOR HOW FAR THIS CAN BE PUSHED.

  - Absence proves nothing.  The sweep is depth-bounded, so a handler
    can reach a primitive further down than the cutoff.  '|_' shows no
    GDI call at depth 2 yet plainly draws: it calls 0x100125C0, which
    normalises angles against 0x168 (360) — the arithmetic a chord's
    start/end angles need.  Read a null result as "not reached within
    the bound", never as "does nothing".

  - Scaffolding drowns signal unless filtered.  Nearly every handler
    brackets its work with the same lock/unlock, caret-hide and
    offscreen-DC sequence, so BitBlt, CreateCompatibleDC, SelectObject,
    GlobalLock, DrawFocusRect and friends appear almost everywhere and
    discriminate nothing.  The script carries an explicit NOISE set;
    that set is a judgement and is worth revisiting before relying on a
    marginal result.

  - It classifies, it does not name.  Class I tells you a command draws
    an ellipse.  It cannot tell you the command is called
    RIP_OVAL_PIE_SLICE.  It is a corroborator and a tie-breaker, not a
    replacement for the string classes.

---------------------------------------------------------------------
12.15  STILL OPEN
---------------------------------------------------------------------

Requires reading further handler bodies:

     * GFXSTYLE facing-bit offsets (bold/italic/underline/shadow)

     * '|;' — RESOLVED 2026-08-12.  It is RIP_PolyMarker: the handler at
       RVA 0x01E4FF names itself and validates all three of its scalar
       fields with distinct diagnostics, which gives the signature
       outright —

            x:XY y:XY marker:mega2 w:XY h:XY rotation:mega2 flags:mega2

            cmp marker,   0x24  -> "Invalid marker number"
            cmp rotation, 0x168 -> "Invalid marker rotation angle (>=360)"
            cmp flags,    3     -> "Invalid marker flags value"

       so marker < 36, rotation < 360, flags <= 3.  TeleGrafix's
       ICONS/MARKER.RIP ("RIPscrip Markers") exercises exactly numbers
       0..35, rotations 0..300 and sizes from 1x1 upward, matching every
       bound.  Class I corroborates: the handler reaches GDI32!Polygon.

       RIPlib had this letter as RIP_BUTTON_EXT and added a MOUSE REGION
       per call.  That was worse than a wrong shape — the corpus issues
       361 of these, so a scene of markers manufactured hundreds of
       phantom clickable areas.  Now corrected, with the driver's own
       validation reproduced rather than clamping bad fields.

       THE 36 GLYPHS — RECOVERED 2026-08-12, so this is now closed.
       The handler special-cases marker 0 (`test edi, edi` at 0x01E643)
       and hands it to the shared ellipse generator at 0x10010160 with a
       0..360 sweep, so marker 0 is a circle.  Every other number goes to
       0x1000F3C6, which indexes a descriptor table:

            mov  eax, [ebp+0xc]              ; marker number
            imul eax, eax, 6                 ; 6 bytes per entry
            lea  esi, [eax + 0x1007ca48]     ; table base

       Each descriptor is { uint16 count; int32 (*points)[2]; } and each
       point is a pair of int32 in a normalised +/-50 space, scaled by the
       command's half-extent and rotated by its skew using the same Q14
       trig tables as the skewed-oval family.  462 points across 36
       glyphs; the coordinate range fits int8_t.

       Extracted by scripts/dll-marker-glyphs.py and carried in
       src/ripscrip.c.  Method note: the three string-based evidence
       classes had nothing to say here, and the table was found the same
       way '|3D' was — by following what the handler CALLS and what it
       pushes, rather than what it says.

     * disambiguation of '|d' — settled: it is RIP_OneDrawingPalette
       (12.8, B6), with '|D' the block form (12.12).
     * what 0x10012D63 does for '|1k'.  PARTIALLY ANSWERED by class I:
       its chain reaches GDI32!GetStockObject and USER32!FillRect via
       0x10012DE2, so it erases a region — consistent with a mouse-field
       delete that also clears where the fields were.  The exact
       relationship between the two calls it makes (0x10012E27 and
       0x10012DE2) is not established.

     * handler names for letters with no class B/C string.  This is now
       bounded rather than open-ended: class I (12.16) classifies what
       those handlers DO even where no string names them, and it did so
       for every drawing command in the table.  What remains genuinely
       unnamed are non-drawing handlers, which reach no distinctive API
       and so cannot be separated this way.

     * base-64 MegaNum digit alphabet (D-10) — the decoder that reads
       the base byte at (state+2)+0x38 has not been located.

Until those are done, no segment may state those specific claims as
DLL-derived fact.

=====================================================================
==                    END OF SEGMENT 12                             ==
==             Binary Provenance & Evidence Classes                 ==
=====================================================================
