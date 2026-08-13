=====================================================================
14.  DIVERGENCE REGISTER
=====================================================================

14.1  THE MEASURE
---------------------------------------------------------------------

RIPlib's measure of correctness is the shipped TeleGrafix driver:

     RIPSCRIP.DLL, 592,896 bytes
     MD5 bade8b1f4e467ac7ad4edb2639738d4c
     from a RIPtel 3.1 install

Not a specification document, not another implementation, and not
RIPlib's own history.  Where any of those disagree with the driver, the
driver wins and the disagreement is recorded here with its reasoning.

Two refinements, both learned the hard way and both load-bearing:

  * WHERE THE HANDLER CONTRADICTS THE RECORD, THE HANDLER WINS.  The
    dispatch record says what the driver ACCEPTS; the handler says what
    it DOES.  '|D's field order, '|F's stub and '|1G's identity were all
    settled by the handler against a record that could not decide them.

  * WHERE SHIPPED CONTENT CONTRADICTS BOTH, CONTENT IS EVIDENCE ABOUT
    THE WORLD, NOT ABOUT THE DRIVER.  It cannot overrule the driver on
    what a field MEANS, but it can and does overrule a decision to
    reject input the driver would reject -- see 14.4.

Reproduce every table below with:

     python scripts/dll-dispatch-table.py <path>/RIPSCRIP.DLL
     python scripts/dll-argtypes.py       <path>/RIPSCRIP.DLL
     python scripts/dll-disasm.py         <path>/RIPSCRIP.DLL <rva>

and check the parser against the record with:

     python scripts/dll-conformance.py    <path>/RIPSCRIP.DLL -v

That last one is the standing check.  It covers four classes -- read
offsets, length gates, radix selection and coverage -- each of which was
first hit as a single bug and only afterwards turned into a check, at
which point every one of them found more of the same.  It exits non-zero
on a defect, so it can gate a build, and it lists the deliberate
tolerances in 14.3.3 by name rather than passing them silently.

and re-derive the findings themselves with:

     python scripts/dll-validate-claims.py <path>/RIPSCRIP.DLL

That one is adversarial by construction.  It states each load-bearing
claim in this register and in 12-dll-provenance.md as a predicate, then
tries to REFUTE it from the image, the shipped corpus and the source --
handler self-naming, the fixed-radix sets, every string-tail prefix
width, the corpus population figures quoted below, and what the code now
does, including negatives such as "'|3e' no longer falls back to mega4".
A claim it cannot re-derive is reported UNVERIFIED rather than passed.

It exists because four documentation defects of one shape were found in
a single day, each by accident: a field list that still described a
defect after the fix, a note saying '|y' "is not implemented yet"
written before it was, a section calling '|3e' an accept-both compromise
a day after that compromise was removed, and a comment asserting that
'X' "is not in the DLL command table" when 'X' is slot 70.  Prose about
code does not notice when the code changes, and a stale conclusion reads
as more authoritative than the behaviour it misdescribes.  See D-27.

Neither tool is run in CI: RIPSCRIP.DLL is not vendored, and will not
be.  Run both by hand against a RIPtel install when the parser changes.


14.2  DIVERGENCES FROM bbs-land/remote-imaging-protocol
---------------------------------------------------------------------

Thirteen commands where their reference and the driver disagree.
RIPlib follows the driver in every one.  Offered as evidence, not as a
verdict on their record: several are plainly sourced from the 1.54
specification or the 2.0 draft rather than from the 3.0 driver, and a
reference documenting an earlier generation of the protocol is not
wrong so much as scoped differently.

The split that matters is whether the TOTAL width agrees.  A consumer
that mis-subdivides a command decodes that one command badly; a
consumer that gets the total wrong desynchronises everything after it.

14.2.1  TOTALS DIFFER -- seven commands, stream desync

     cmd   driver          chars  bbs-land      chars
     ----  --------------  -----  ------------  -----
     |2A   mega1, mega2      3    2               2
     |2B   mega1, mega2      3    2               2
     |2E   mega1, mega2      3    2               2
     |2T   mega1, mega2      3    1 1             2
     |2Y   mega1, mega2      3    1 1             2
     |2s   mega1, mega2      3    1 2 3           6
     |3e   mega2             2    4               4

     The Switch* family is one shape -- a slot digit plus a 2-digit
     field -- recorded identically across slots 111, 112, 114, 118, 119
     and 121.  The corpus agrees: every '|2s' in shipped content is
     three characters ("!|2s000", "!|2s100").  A consumer reading six
     over-consumes three bytes and loses the rest of the frame.

     '|3e' RIP_BAUD_EMULATION is the one place BOTH projects disagreed
     with the driver, and RIPlib was wrong for longer: it preferred a
     mega4 whenever four characters were available, which is the 2.0
     draft's rate:4.  Slot 123 records a single mega2 and the handler
     (RVA 0x038BE1) loads exactly ONE argument -- mov edi,[eax] -- and
     stores it.  There is no second field.  Corrected; see D-16.

14.2.2  SAME TOTAL, DIFFERENT SUBDIVISION -- six commands

     cmd   driver                    bbs-land            chars
     ----  ------------------------  ------------------  -----
     |1I   n n 1 1 1 1 1             n n 2 1 1 1           9
     |1M   2 n n n n 1 1 2 3         2 n n n n 1 1 5      17
     |1R   2 6                       8                     8
     |1T   n n n n 1 1               n n n n 2            10
     |1w   1 3                       4                     4
     |2W   1 n n n n 2 2             1 n n n n 4          13

     The stream stays in sync; individual fields decode wrong.  Four of
     the six merge two adjacent reserved fields into one, which is
     harmless while both are zero and wrong the moment either is not.

     '|1M' is the exception that mattered, and it cut against RIPlib.
     Their 1+1 split of args[5]/args[6] agrees with the driver and with
     the 1.54 specification's 'invertable'/'resetafter'; RIPlib read
     those two single digits as ONE 2-digit hotkey and took its flag
     bits from a reserved column.  Their reference was closer to the
     driver than RIPlib's code was.  See D-15.

     '|1R' is the other one with teeth.  The driver's 8-character fixed
     prefix is exactly where the filename begins, and shipped content
     confirms it -- all 25 '|1R' commands in the corpus start with eight
     zeros ("00000000dragon.txt").  RIPlib read the filename from offset
     0.  See D-19.


14.3  DIVERGENCES FROM THE DRIVER THAT RIPlib MAKES DELIBERATELY
---------------------------------------------------------------------

Recorded so that "RIPlib follows the driver" is a checkable claim
rather than a slogan.

14.3.1  SECURITY: '|3G' RIP_GotoURL LAUNCHES NOTHING

     The driver opens the URL.  RIPlib does not, ever, and will not be
     configured into doing so: with no handler registered the URL is
     validated and stored in rip_state_t.goto_url and nothing else
     happens.  Schemes are restricted to http:// and https://;
     javascript:, data:, file: and vbscript: are refused outright
     rather than delegated to host policy, because those are what turn
     "open a link" into code execution.  A host wanting click-through
     registers a handler and decides for itself.

     This is a deliberate refusal to match the driver's behaviour and
     is not up for reconciliation.  See "Fix SV-2/S2" in src/ripscrip.c.

14.3.2  NO FILE I/O, NO PROCESS LAUNCH

     '|2W' RIP_PortWrite, '|1W' RIP_WRITE_ICON and the file-query family
     validate their arguments the way the driver does and stop there.
     RIPlib has no filesystem.  Requests are surfaced to the embedder
     through the icon/file request queue instead.

14.3.3  '|k' RIP_BACK_COLOR ACCEPTS A ONE-CHARACTER PAYLOAD

     Slot 43 types the argument as colour-width -- two characters at the
     default colour mode -- and RIPlib also accepts one.

     This was removed to match the record exactly, and then restored,
     because the corpus contradicted the assumption behind the removal:
     of 133 '|k' commands in shipped scenes, 132 are two characters and
     one -- N2_BUSI.RIP, "|k0" -- is one.  Rejecting the short form
     would drop a command real content sends, for no gain.  The defect
     that mattered here was reading ONE digit when TWO were present,
     fixed in v2.0.1.

     The general rule this establishes: the record says what the driver
     accepts, not what content exists.  Tightening a gate to match the
     record is right by default and wrong where shipped scenes say
     otherwise, and the corpus is what tells the two apart.  See D-18.

     The rule has been applied twice.  '|=' RIP_LINE_STYLE records eight
     characters and RIPlib admits four, because all three widths the
     corpus sends are real content: of 116 '|=' commands, 107 are eight
     characters, 2 are seven and 7 are four.  The handler reads
     progressively -- off_draw and style at four, the user pattern at
     six, thickness at eight -- rather than rejecting records the driver
     would reject but shipped scenes contain.  The gate was still raised
     from two to four, which rejects truncation below anything real
     content sends.  See D-20.

14.3.4  MODES ACCEPTED BUT NOT PERFORMED

     '|1G' RIP_Scroll validates its mode field 0..6 as the driver does
     and performs the block move, which is common to all seven modes.
     Modes 1..6 additionally run post-scroll effect routines that are
     not implemented.  A scene using them scrolls correctly and loses
     the effect.  See D-14.

     '|1g' RIP_CopyBlit accepts modes 0..5 per the driver; RIPlib's
     raster ops stop at DRAW_MODE_NOT (4), so mode 5 is accepted and
     drawn as COPY.

14.3.5  '|Y' TEXT DIRECTIONS 2 AND 3

     The driver validates the direction field with cmp [ebp-8],1 / jbe
     and reports "Illegal direction" above 1, so it accepts only:

          0   horizontal
          1   BGI VERT_DIR, bottom-to-top

     RIPlib accepts two more, as its own extension:

          2   vertical CCW glyphs, top-to-bottom
          3   vertical CW  glyphs, top-to-bottom

     Every '|Y' in the corpus uses direction 0 or 1, so the extension
     displaces no shipped content.  The font number and size bounds ARE
     enforced as the driver enforces them (0..10 and 1..10); it is only
     the direction range that is wider on purpose.  See D-21.

14.3.6  PROTECTION IS IMPLEMENTED ONLY FOR PORTS

     The driver guards 24 command sites with twelve "its protected!"
     diagnostics covering graphics styles, colour palettes,
     environments, text windows and button styles.  RIPlib implements
     none of those.

     That is inert rather than divergent: 41 commands READ the
     protection word at <state>+0x104 and no dispatched command WRITES
     it, so protection is host-side state that no RIP stream can set.
     The guards cannot fire from content.

     PORT protection is the exception and RIPlib does implement it --
     port 0 permanently, '|2s' bits 0..3 to protect and unprotect the
     destination and source ports, and create/delete refusing a
     protected port.  See D-22.

14.3.7  APPROXIMATED HIT AREAS

     '|:' RIP_MOUSE_REGION_EXT defines a five-vertex region.
     rip_mouse_region_t holds a rectangle, so RIPlib registers the
     BOUNDING BOX of the five vertices: a conservative
     over-approximation for hit-testing rather than a rectangle invented
     from two of the coordinates.  See D-14.

14.3.8  COMMANDS NOT IMPLEMENTED

     '|`' -- slot 83, argc 11 (XY x10 + mega1).  Its handler (RVA
     0x01D963) is structurally identical to '|:' RIP_MOUSE_REGION_EXT:
     the same call sequence, five consecutive coordinate-pair maps, then
     SetBkMode.  It is evidently a sibling of that command, but it
     carries no name in the export table and no shipped scene uses it,
     so its semantics cannot be established.  Recorded rather than
     guessed.

     Level 2 '|2C' RIP_PortCopy, '|2R' and the Switch* family ARE
     implemented; see D-17.

14.3.9  RIPlib-ORIGINAL COMMANDS

     '|1V' SET_VIEWPORT_EXT and '|1X' CLIPBOARD_OP have no dispatch
     entry.  They are RIPlib extensions and are documented as such in
     11-dll-deviations.md DEV.4.  Level 3 '|3&' and '|3-' likewise --
     neither letter appears among the driver's Level 3 commands
     (D, e, ESC, G, R, U), so nothing in the protocol is displaced.


14.4  WHAT THIS REGISTER IS FOR
---------------------------------------------------------------------

A claim of conformance is only as good as the set it was measured over,
and every count in this file is reproducible from the scripts in 14.1.
Three findings in this project came from measuring the MEASUREMENT
rather than the code:

  * The field-list comparison read only the first line of each handler
    comment, silently truncating any signature that wrapped.

  * It dropped every CONTINUATION row of an overloaded command -- rows
    whose letter byte is 0x00, identified only by sharing the named
    entry's handler pointer -- so '|h' presented as one signature
    instead of six, and '|t', '|x' and '|z' as one instead of three.

  * An elided field list in a reference ("c1:2 c2:2 ... c16:2") yields
    only the pairs literally written, which reported '|Q' as a 32-vs-6
    divergence where the reference in fact agrees.

Each of those would have overstated or understated this register.  The
counts here are 13 divergences from bbs-land, 7 of them affecting the
total width -- reproduced twice by independent paths.
