# consumer-handoff/a2gspu/ — content extracted for A2GSPU absorption

This directory holds material that **used to live inside the RIPlib
library** (public headers, source comments, example code) but does **not
belong in a platform-independent library**. It's been extracted here so
the A2GSPU project — the one known downstream that vendors RIPlib via
`scripts/sync-to-a2gspu.sh` — can pull the content into its own
integration layer at its next sync.

## Why this directory exists

RIPlib's stated goal is to be a platform-independent C99 implementation
of the RIPscrip protocol. The May 2026 audit (see `design/decisions.md`,
candidate **C-001**) found pervasive references to Apple II / A2FUSION /
A2GSPU / RP2350 / RIPtel / ProDOS / RIPSCRIP.DLL inside the library's
public API and source. Per the user's directive:

> RIPlib is meant to be platform-independent when possible. Mention of
> Apple II or A2FUSION is counter to the goals of the repo. Parts that
> need to be saved to A2GSPU can be set aside in separate files so the
> information is not lost, but it doesn't belong in RIPlib itself. It
> belongs in the Apple II project that uses it separately.

So: the directory is the **temporary holding area** between RIPlib (which
shouldn't carry this content) and A2GSPU (which will, eventually). The
sync script (`scripts/sync-to-a2gspu.sh`) copies only `src/include/fonts/icons`
into A2GSPU's `libraries/libriplib/`, so this directory is NOT copied
across the sync boundary. A2GSPU's maintainers should mirror these files
into A2GSPU's own integration docs (the natural home is the same
`libraries/libriplib/` tree, alongside `platform_a2gspu.c` and the
A2GSPU-side `README.md`).

## Files in this directory

- **`dll-reference.md`** — reverse-engineering notes about the original
  TeleGrafix `RIPSCRIP.DLL` (binary addresses, field offsets, struct
  layouts) that previously lived as inline comments in `include/ripscrip.h`
  and `src/ripscrip.c`. These are useful to A2GSPU's maintainers for
  cross-referencing behavior against the original DLL; they have zero
  bearing on what the RIPlib public API does today.

- **`integration-notes.md`** — A2GSPU-specific rationale for design
  choices that the library otherwise looks generic about: PSRAM arena
  sizing, the 640×400 framebuffer default, the compositor stub interface
  shape, ProDOS time-sync byte protocol, etc.

- **`audit-trail.md`** — the running log of which file:line locations had
  content lifted out, when, and by whom. Useful if A2GSPU's maintainers
  need to reconcile their copy with RIPlib's history.

## What to do with this directory

**As an A2GSPU maintainer**: at your next RIPlib sync, copy these files
into A2GSPU's own integration documentation. Once they're in A2GSPU,
this directory can be safely removed from RIPlib — open an issue or PR
saying "we have the content; please drop it from RIPlib."

**As a RIPlib maintainer**: do not extend the content here in lieu of
fixing the underlying coupling. If new A2GSPU-specific content shows up
in a PR to RIPlib, ask the PR author to put it in A2GSPU directly. This
directory is one-shot cleanup, not a permanent shed.

## Status

- 2026-05-25: created during the audit; awaiting A2GSPU absorption.
