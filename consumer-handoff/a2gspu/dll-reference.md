# RIPSCRIP.DLL Reverse-Engineering Reference

This is the historical reverse-engineering of TeleGrafix's `RIPSCRIP.DLL`
(Windows 3.1 era, c. 1995-1997). It originally lived as inline comments
in RIPlib's public headers and source files. Moved here per audit
candidate **C-001** because:

1. RIPlib's public API does not depend on these details; they're
   forensic data about an unrelated binary.
2. Users of RIPlib on non-A2GSPU platforms have no reason to read about
   DLL field offsets when consuming the API.
3. The data is genuinely useful to anyone reasoning about the original
   implementation's behavior — A2GSPU's maintainers most of all.

## State machine — `ripParseStateMachine`

```
DLL location:    0x10039E90
Jump table:      0x1003AB9C
States:          13 (0..12) in the DLL
RIPlib addition: state 13 (LEVEL3_LETTER) for the '3' prefix
```

Mapped states:
| State | RIPlib name             | Purpose                                       |
|-------|-------------------------|-----------------------------------------------|
| 0     | RIP_ST_IDLE             | scanning for '!'  (DLL: PASSTHROUGH)          |
| 1     | RIP_ST_GOT_BANG         | got '!', looking for '|'                      |
| 2     | RIP_ST_COMMAND          | collecting command letter / level prefix      |
| 3     | RIP_ST_ARG_COLLECT      | accumulating MegaNum parameter bytes          |
| 4     | RIP_ST_ARG_DISPATCH     | args complete, ready to call handler          |
| 5     | RIP_ST_LINE_CONT        | received '\' mid-command, waiting for CR/LF   |
| 6     | RIP_ST_LINE_WAIT_LF     | got CR after '\', waiting for LF              |
| 7     | RIP_ST_TEXT_COLLECT     | free-text parameter until '|' (cmds T/@)      |
| 8     | RIP_ST_SUPPRESS         | suppress ANSI fallback until CR/LF or '!'     |
| 9     | RIP_ST_COMMENT          | inside `!|! ... |` comment, skip until '|'    |
| 10    | RIP_ST_LEVEL1_LETTER    | after '1' prefix, waiting for sub-command     |
| 11    | RIP_ST_LEVEL2_LETTER    | after '2' prefix, waiting for sub-command     |
| 12    | RIP_ST_ERROR_RECOVERY   | resync on '|' or newline after bad command    |
| 13    | RIP_ST_LEVEL3_LETTER    | RIPlib-only: after '3' prefix (forward-compat)|

DLL context offset reference:
- `pContext + 0x04` — saved state for line-continuation restore

## Mouse field record — `ripCmd_MouseRegion`

```
DLL location:    0x1000A964
Record size:     0xA3 bytes
```

Field offsets within the DLL's mouse-field record:
| Offset | Purpose                                                       |
|--------|---------------------------------------------------------------|
| +0x20  | flags (MF_ACTIVE=0x04, MF_SEND_CHAR=0x08, MF_RADIO=0x20, MF_TOGGLE=0x40) |
| +0x2B  | hotkey (ASCII key, 0=none)                                    |
| +0x2C  | lineStyle                                                     |
| +0x2D  | lineThick                                                     |
| +0x32  | pHostCmd (pointer to host command string)                     |
| +0x3E  | iconPath (97-byte icon filename, truncated to 64 in RIPlib)   |
| +0xA0  | eventCode                                                     |

Cross-reference: `rip_mouse.c` lines 166-170 in the DLL source.

## Other dispatch addresses

| Symbol                  | DLL RVA / address      | Notes                          |
|-------------------------|------------------------|--------------------------------|
| `ripCmd_MouseRegion`    | 0x1000A964             | Mouse region 1M handler        |
| Level 2 chord 'c' impl  | RVA 0x012663           | §A2G.13 forerunner (cosmetic)  |
| `ripTextVarEngine`      | 0x026218               | $RAND$ uses rand() here        |

## DLL behaviors — preserved vs. corrected

The audit found that RIPlib intentionally diverges from DLL behavior in
several places. The authoritative list lives at
`docs/spec/11-dll-deviations.md` inside RIPlib; this file just notes the
DLL forensic data needed to reconstruct *what* the DLL did. The reasons
for divergence are documented in the spec.

## Source provenance

All of the field offsets above originate from the v3.0.7 RIPSCRIP.DLL
binary (October 1997 build). They have not been re-verified against
newer or older DLL builds. If you cross-reference against a different
DLL build, **independently confirm the offsets** before relying on them.

## Why this matters to A2GSPU specifically

A2GSPU's `platform_a2gspu.c` (RIPlib's vendored copy) implements the
`palette_write_rgb565`, `palette_read_rgb565`, and `card_tx_push` extern
hooks. The DLL-derived field layouts above don't directly map to those
extern points — but they were useful while bringing up A2GSPU's mouse-
region handling and ANSI fallback paths. Keeping them in A2GSPU's docs
means future A2GSPU bring-up of similar features (e.g., dialog widgets
that the DLL implemented but RIPlib hasn't yet) can re-use the forensic
work.
