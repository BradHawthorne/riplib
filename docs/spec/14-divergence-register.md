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

and reproduce section 14.2's comparison -- both projects against the
driver, command by command -- with:

     python scripts/ref-compare.py <path>/RIPSCRIP.DLL <path>/reference.md

Neither the driver nor the reference is vendored, so both are
arguments; with no reference only RIPlib is compared, which is the
useful half day to day.  It exits non-zero if RIPlib disagrees with the
record anywhere, and never fails on the reference disagreeing, because
the reference is evidence and not the measure.

That script was itself a finding.  Until 2026-08-13 the 14.2 counts came
from a copy living in a scratch directory, which meant this section was
the one part of the register NOT reproducible from the repository, and
which rotted unnoticed: it carried hardcoded switch-block line numbers
that went stale as src/ripscrip.c grew, so it bracketed the wrong code
and reported three RIPlib divergences that did not exist -- '|R' showing
'|1R's record, because the ranges had drifted past it.  The version in
scripts/ derives its boundaries from structural markers.

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

WHAT THE EVIDENCE ACTUALLY IS, PER COMMAND.  The dispatch record is the
arbiter throughout, but it is worth being blunt about how much
CORROBORATION each finding has, because it is much less than the length
of this section suggests.  Only FOUR of the thirteen appear in shipped
content at all:

     cmd    corpus uses   widths observed
     ----   -----------   ----------------------------------------
     |1M         38       28,29,32,34,36,37,38,51,61,65,66,71,76
     |1R         25       18,19,20,21,68
     |1T         12       10 -- every one
     |2s          3       3  -- every one  ("002", "100", "000")

     |1I  |1w  |2A  |2B  |2E  |2T  |2W  |2Y  |3e        zero uses

For the nine with zero uses there is no corpus arbiter and there never
will be: the record and the handler body are the whole of the argument.
That is not a weakness of the finding -- the record IS the driver's own
statement of what it accepts -- but it does mean these are settled by
one line of evidence rather than two, and a reader weighing them should
know which.

The two that ARE corroborated corroborate strongly.  Every '|1T' in
shipped content is exactly ten characters, and every '|2s' is exactly
three, which refutes the reference's six outright.  '|2s' is also the
only member of the Switch* family that appears anywhere in the corpus;
the other five are carried by the shared record shape -- slots 111,
112, 114, 118, 119 and 121 all record mega1 + mega2 -- and not by
independent observation.

TESTS.  Five tests in tests/test_ripscrip.c pin the divergences that
can be demonstrated; 14.2.3 lists the ones that cannot, and why, rather
than giving them a test that asserts its own fixture.

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
     and 121.  ALL SIX, not just '|2s': an issue filed upstream named
     only '|2s', which understates the finding fivefold.  The corpus
     agrees as far as it can reach: all three '|2s' commands in shipped
     content are three characters ("!|2s000", "!|2s100", "!|2s002"),
     and no other member appears at all.  A consumer reading six
     over-consumes three bytes and loses the rest of the frame.

     PINNED BY  "|2A |2B |2E |2T |2Y |2s: driver records 3 chars, not 2"
                -- feeds each a 3-character command and a 2-character
                one, and requires the first to take effect and the
                second to be rejected, since a truncated record is one
                the driver throws away.
     PINNED BY  "|2s: driver records 3 chars; the reference's 6 would
                over-consume" -- feeds "!|2s000|X0A00|" and requires the
                pixel to land.  A consumer reading by declared width
                rather than to '|' loses it.

     '|3e' RIP_BAUD_EMULATION is the one place BOTH projects disagreed
     with the driver, and RIPlib was wrong for longer: it preferred a
     mega4 whenever four characters were available, which is the 2.0
     draft's rate:4.  Slot 123 records a single mega2 and the handler
     (RVA 0x038BE1) loads exactly ONE argument -- mov edi,[eax] -- and
     stores it.  There is no second field.  Corrected; see D-16.

     PINNED BY  "|3e RIP_BAUD_EMULATION reads mega2, not the reference's
                rate:4" -- feeds "!|3e0A00|", which decodes to 10 as a
                mega2 and 12960 as a mega4, so the two readings cannot
                be confused.  The test names the mega4 value in its
                failure message, so a regression says which reading it
                fell back to rather than only that it failed.

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

     PINNED BY  "|1R takes the filename at offset 8 (D-19)"
     PINNED BY  "1M defines a mouse region with clk/clr flags + host
                text" -- asserts the host text is exactly "HELLO",
                which can only be true if the fixed prefix is
                seventeen characters wide.  It also pins the clk/clr
                split itself, which is the half of the '|1M' finding
                that IS observable: hotkey must read 0, RIP_MF_INVERT
                must be set and RIP_MF_RESET clear.
     PINNED BY  "|1I fixed prefix is 9 chars; the filename starts there"
                -- named for what it demonstrates.  It does NOT
                discriminate the mode width; see 14.2.3.
     PINNED BY  "|2W leaves the stream in sync (its gate has no
                observable)" -- likewise named for what it shows, which
                is less than the '|2W' finding.  See 14.2.3.

     EVERY TEST ABOVE WAS VERIFIED BY INJECTION -- the parser was
     deliberately regressed to the reference's reading and the test
     required to fail.  Two did not, and both were the test's fault:

       * the Switch* test fed a ONE-character short form, which a gate
         loosened from three to two still rejects, so it passed against
         a regressed parser.  Two characters is the boundary that
         separates the readings; it now uses two.

       * the '|2W' test was named for the gate but measured only
         framing, and passed with the gate regressed from thirteen to
         nine.  '|2W' writes no file and changes no readable state, so
         the gate has no observable at all; the test was renamed to the
         one thing it does establish and the gate moved to 14.2.3.

     A test that cannot fail is worse than no test, because it is
     counted.  Both faults were invisible until the injection ran.


14.2.3  WHAT IS NOT TESTED, AND WHY

     Four of the thirteen cannot be demonstrated, and two more are
     demonstrated only in part.  Listing them is the point: a suite that
     appears to cover thirteen cases while several assert nothing is
     worse than one that says so, because the count is what gets
     quoted.

     '|1M' and '|1T'  -- THE DIVERGENCE ITSELF IS INERT.  Be careful
          to separate two things here, because '|1M' carries both.

          The bbs-land DIVERGENCE is only in how a trailing RESERVED
          span is named: res:2+res:3 against res:5 for '|1M',
          res:1+res:1 against res:2 for '|1T'.  Both readings describe
          identical wire bytes, RIPlib reads neither field, and no
          behaviour can tell them apart.  A test would assert its own
          fixture.  What a consumer depends on is the TOTAL, and that
          IS pinned -- all twelve '|1T' commands in the corpus are ten
          characters, and the '|1M' test's host text can only come out
          right if the prefix is seventeen.

          Separately, '|1M's clk/clr pair is where RIPlib was wrong and
          the reference was RIGHT -- see 14.2.2.  That half is fully
          observable and fully tested.  It is not a divergence from
          bbs-land at all, which is why it is not counted among the
          thirteen; it is listed here only so the two are not confused.

     '|1w'  -- INERT, more so.  Driver 1+3, reference 4, same total,
          and RIPlib's handler body is a bare 'break'.  The command is
          consumed and nothing is done with it, exactly as the record's
          width requires.  There is no observable to assert.  No corpus
          scene sends '|1w' either.

     '|1I'  -- PARTLY TESTED.  Both readings total nine and both put
          the filename at offset 9, so the filename cannot distinguish
          them.  The test pins the nine-character prefix, which is a
          real property that RIPlib's own documentation had wrong, but
          it does NOT discriminate the mode width.  The only field the
          readings place differently is the mode, and even that agrees
          whenever the driver's args[3] is zero.  There are zero '|1I'
          commands in the shipped corpus, so no content exercises the
          difference.  Discriminating it would need a cached icon and a
          visible blit staged purely for the test; the record is clear
          (FF FF 01 01 01 01 01) and is left to carry it.

     '|2W'  -- GATE NOT TESTABLE.  Driver 1+XY*4+2+2 = thirteen fixed
          characters, reference 1+XY*4+4 = eleven, same total, filename
          following either way.  RIPlib's gate is thirteen and was nine
          for a while, so a record truncated before its flags was still
          acted on.  But '|2W' WRITES NO FILE -- it validates the port
          and rectangle and returns, changing nothing a test can read.
          The gate is real and correct; it simply has no observable.
          This was found the honest way: the test WAS named for the
          gate, and it passed with the gate regressed from thirteen to
          nine, because all it measured was framing.

     The other seven are pinned as described in 14.2.1 and 14.2.2, and
     every one of those tests was verified by injection.


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

     TWENTY commands RIPlib documents have no dispatch entry at all.
     This section previously named four of them, which understated the
     extension surface fivefold; the full set is below, obtained by
     subtracting the driver's letter set per level from the letters the
     spec chapters document.

          level 0    '|^'  '|~'
          level 1    '|1N' '|1O' '|1Q' '|1S' '|1V' '|1X' '|1Z'
          level 2    '|20' '|22' '|23' '|24' '|25' '|26' '|28'
                     '|2c' '|2F'
          level 3    '|3&' '|3-'

     The driver's own letters, for comparison:

          level 0    ! " # & ( ) * + , - . : ; < = > @
                     A-Z [ ] _ ` a-z {
                     (74 letters + 11 continuation rows = 85 records)
          level 1    A B C D E F G I K M P R T U W b c e g i k p t w
                     and ESC          (24 letters + ESC = 25 records)
          level 2    A B C E P R T W Y p s
                     and ESC          (11 letters + ESC = 12 records)
          level 3    D G R U e
                     and ESC           (5 letters + ESC =  7 records)

     Record counts exceed letter counts because of the continuation
     rows described in 12.11 -- extra argument signatures sharing a
     handler -- and because level 3 records '|3D' twice, under two
     different handlers.

     Note that level 0 DOES carry '|N' RIP_SetBorder, at slot 48, and
     that RIPlib implements it there.  RIPlib's '|1N' RIP_SET_ICON_DIR
     is a different command that happens to share the letter, and it
     is the LEVEL 1 one that is RIPlib-original.  Revisions of
     13-dll-command-table.md before 2026-08-13 filed slot 48 under
     Level 1 as '|1N', which made level 0 look as though it were
     missing an 'N' and made '|1N' look driver-backed; both readings
     were wrong.

     D-23 in 12-dll-provenance.md had already recorded that "'|1Z',
     '|1N' and '|1O' have NO dispatch entry at all".  That was correct
     and the dispatch table contradicted it for months without either
     being reconciled, because no check compared the two.  Two
     documents in the same repository disagreeing is exactly the class
     of defect that survives review by prose alone.

     None of the twenty displaces a driver command: every one of those
     letters is absent from its level's set, so a stream written for
     the driver cannot collide with a RIPlib extension.  They are
     documented as extensions in 11-dll-deviations.md DEV.4.

     Verify this list with scripts/check-spec-examples.py, which
     reports every documented command whose letter has no dispatch
     entry rather than passing it silently.


14.3.10  COMMANDS THE DRIVER ACCEPTS AND IGNORES

     Three dispatch entries point at a handler that is a single RET
     instruction.  The driver parses the command, dispatches it, and
     does nothing:

          slot   0   '|!'   0x01ad36
          slot   4   '|('   0x01ca84
          slot  27   '|F'   0x01b2fd

     '|F' RIP_FILL is the one that matters.  Flood fill is a NO-OP in
     RIPSCRIP.DLL 3.00.04.  RIPlib implements it fully - x, y and
     border as three two-digit fields, per the v1.54 specification and
     corroborated by IcyTerm's parser, which reads six base-36 digits
     for '|F'.

     This is a deliberate divergence in the direction of DOING MORE
     than the driver, and it is safe in the way the '|Y' direction
     extension is safe: content written for the driver expects nothing
     to happen, and content written for a conforming client gets the
     fill.  Nothing that renders correctly under the driver renders
     incorrectly under RIPlib because of it.

     Note also that an argc of 0 with no argument types does not imply
     a command takes no payload -- '|T' has argc 0 and parses a string
     itself.  Ten of the thirteen argc-0 entries have real bodies.
     Only a bare-RET body proves a command is inert.


14.4  WHAT THIS REGISTER IS FOR
---------------------------------------------------------------------

A claim of conformance is only as good as the set it was measured over,
and every count in this file is reproducible from the scripts in 14.1 --
which became true of section 14.2 only on 2026-08-13, when the tool that
produced those counts was moved into the repository.  Before that this
sentence was an overclaim about the very section most likely to be
challenged.

FIVE findings in this project came from measuring the MEASUREMENT rather
than the code:

  * The field-list comparison read only the first line of each handler
    comment, silently truncating any signature that wrapped.

  * It dropped every CONTINUATION row of an overloaded command -- rows
    whose letter byte is 0x00, identified only by sharing the named
    entry's handler pointer -- so '|h' presented as one signature
    instead of six, and '|t', '|x' and '|z' as one instead of three.

  * An elided field list in a reference ("c1:2 c2:2 ... c16:2") yields
    only the pairs literally written, which reported '|Q' as a 32-vs-6
    divergence where the reference in fact agrees.  Ten commands are
    elided this way; scripts/ref-compare.py now names them on every run
    rather than dropping them silently, because a skipped comparison
    that is not reported reads exactly like an agreement.

  * The same comparison carried hardcoded switch-block line numbers.
    They went stale as src/ripscrip.c grew, so it bracketed the wrong
    code and reported three RIPlib divergences that did not exist --
    '|R' showing '|1R's record.  Boundaries are derived now.

Each of those would have overstated or understated this register.  The
counts here are 13 divergences from bbs-land, 7 of them affecting the
total width -- reproduced twice by independent paths.

The fifth is worth recording separately because it is a different SHAPE
of instrument fault.  The level split quoted in
13-dll-command-table.md was checked by grouping that file's rows by
its own level column and counting the groups.  That can verify a
count but never a MISFILED ROW: slot 48 sat under Level 1 as '|1N',
and two successive corrections of the split (83/26/12/8, then
84/26/12/7) both preserved the misfiling because both measured the
same wrong grouping.  The true split is 85/25/12/7.  A check that
takes its partition from the artefact under test cannot find an error
in that partition; scripts/check-dll-table.py now verifies that each
level is a contiguous slot run and that every row is spelled with its
section's prefix, which is independent of the grouping.


14.5  RECOVERED NAMES THAT CONTRADICT RIPlib'S
---------------------------------------------------------------------

Handlers that can report an error push their own name string before
calling the error reporter, so a name recovered that way is strong
evidence -- stronger than any secondary reference.  Thirty-nine
commands are named on both sides.  Thirty-seven agree once naming
style is normalised ('RIP_ExtendedTextWindow' against
'RIP_EXT_TEXT_WINDOW', 'RIP_FilledPolygon' against
'RIP_FILL_POLYGON', and so on).

TWO do not, and both are recorded rather than resolved:

     command   handler self-name      RIPlib's name
     -------   ------------------     -------------
     '|1A'     RIP_SelectArticle      RIP_PLAY_AUDIO
     '|1N'*    RIP_SetBorder          RIP_SET_ICON_DIR

     * slot 48 is LEVEL 0 '|N'; see 14.3.9.  RIPlib's level-1 '|1N'
       has no dispatch entry, so this row is a collision of letters
       rather than a contradiction, and only '|1A' is a genuine
       disagreement about one record.

For '|1A' the FIELD LAYOUT is settled and the SEMANTICS are not.  The
handler at RVA 0x00DC58 pushes both "Invalid article number" and
"RIP_SelectArticle()", and its entry records mega2 + mega4 -- six
fixed characters then a string.  RIPlib reads exactly those six and
treats the remainder as a filename.  Any consumer relying on the
audio reading should know that the driver's own diagnostics call it
something else; the wire format is the same either way, which is why
this has never affected a shipped scene.

Regenerate this comparison by extracting the NAME column of
13-dll-command-table.md and the leading identifier of each 'case'
comment in src/ripscrip.c, then normalising both to lower case with
'rip_' and underscores stripped.


14.6  THE COMPATIBILITY CONTRACT
---------------------------------------------------------------------

Everything in 14.3 is a place where RIPlib deliberately differs from
the driver.  That is only defensible under a rule, and the rule is
this: THE SYNTAX IS SHARED, SO AN EXTENSION MUST NEVER COST A CLIENT
THAT DOES NOT IMPLEMENT IT ANYTHING BUT THE EXTENSION ITSELF.

A BBS does not know which terminal is connected.  If content carrying
a RIPlib extension corrupts the frame for a stock RIPterm, the
extension has not enhanced the protocol -- it has forked it.  So:

  1. ADDITIVE ONLY.  An extension may add a command, widen an accepted
     range, or implement something the driver stubs.  It may not
     change the meaning of any byte sequence the driver already
     defines.  A stream that renders correctly under the driver must
     render the same way under RIPlib.

  2. UNUSED LETTERS ONLY.  Every RIPlib-original command uses a letter
     absent from the driver's set AT ITS LEVEL (14.3.9 lists both sets
     for comparison).  Nothing is displaced, so a driver-targeted
     stream cannot collide with an extension.

  3. SKIPPABLE.  An unknown command must cost the frame nothing.  This
     is a property of the framing rather than of any command: a
     payload runs until '|', CR or LF, and NOTHING IN THE STREAM
     STATES ITS LENGTH.  A parser that does not know a letter still
     knows where the command ends.

     This is load-bearing in both directions and is easy to break by
     accident -- consuming a fixed argument count from the dispatch
     record instead of scanning to the delimiter would be a natural
     "optimisation" and would desynchronise on every extension anyone
     ever adds, RIPlib's or another implementation's.  Three tests in
     tests/test_ripscrip.c pin it:

          an unknown command letter is skipped, not desynchronised past
          every RIPlib-original command leaves the stream in sync
          a known command with a longer payload than its record still
              ends at '|'

     The second feeds all twenty originals and checks the NEXT command
     still takes effect.  It deliberately measures a state change and
     not a drawn pixel: '|1V' legitimately sets a viewport that clips
     everything, so a missing pixel would prove nothing about sync.
     The third covers a future revision widening a field, so that new
     content degrades on an old client instead of breaking it.

  4. DEGRADES, DOES NOT BREAK.  A client that skips an extension
     should lose only that effect.  '|Y' directions 2 and 3 give
     rotated glyphs where the driver reports "Illegal direction";
     '|F' fills where the driver does nothing (14.3.10); '|^' and '|~'
     push and pop state a stock client simply never restores.  In each
     case the omission is visible as plainer output, not as a wrong
     frame or a lost stream.

     The honest limit: skipping a STATE-CHANGING extension leaves the
     stock client in a state RIPlib would have restored.  Content that
     relies on '|^'/'|~' to bracket a colour change will leave that
     colour set on a client that ignores them.  Authors targeting
     mixed audiences should restore state explicitly rather than
     depend on the stack.

  5. RIPlib EMITS NO RIPscrip.  The library renders and parses; it
     does not generate protocol.  Its only outbound traffic is host
     callbacks -- file and asset requests, sound markers -- on a
     private queue, never on the wire as RIPscrip.  So RIPlib cannot
     put an extension in front of a terminal that did not ask for one;
     that is a content-authoring decision, and this section is the
     guidance for whoever makes it.

The bbs-land divergences in 14.2 are a different matter and this rule
does not license them: those are places a reference and the driver
disagree about EXISTING syntax, where following the driver is
conformance rather than extension.
