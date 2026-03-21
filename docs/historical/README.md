# Historical RIPscrip Protocol Documents

These are the original TeleGrafix Communications protocol documents
preserved for historical reference. They are NOT part of the v3.1
specification — see `docs/spec/` for the current complete reference.

## Files

| Document | Version | Year | Description |
|----------|---------|------|-------------|
| `RIPSCRIP_v154.DOC` | v1.54 | 1993 | Original specification. Defines Level 0 + Level 1 commands, MegaNum encoding, frame format. Binary .DOC format. |
| `RIPSCRIP_v2A4.PRN` | v2.0 Rev 2.A4 | ~1995-96 | Extended specification. Defines Drawing Ports, extended commands, coordinate systems, data tables. Plain text. |
| `ripscrip-v3-RE-notes.md` | v3.0 | 2026 | Reverse engineering analysis of RIPSCRIP.DLL (592KB, Oct 1997). Contains DLL deviation analysis, v2.A4 spec errata, dropped commands, and driver bugs NOT covered in the v3.1 spec. Working notes — partially unrefactored but the deviation/errata sections are authoritative. |

## Copyright

The v1.54 and v2.0 documents are Copyright (c) 1993-1997
TeleGrafix Communications, Inc. Preserved here for historical
reference and interoperability purposes.

The v3.0 RE notes are original analysis work, Copyright (c) 2026
SimVU (Brad Hawthorne).

## Current Specification

For the complete, authoritative protocol reference covering all
versions (v1.54 through v3.1), see:

    docs/spec/01-wire-format.md      through
    docs/spec/10-appendices.md

The v3.1 specification is standalone — it does not require these
historical documents to implement a compliant client.
