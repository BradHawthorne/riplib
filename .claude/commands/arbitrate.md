# /arbitrate — establish what a command IS, from the driver

Turn a question about one RIPscrip command into a settled, cited answer:
identity, field layout, bounds, and the offset any trailing string starts at.
**Derive from the binary once; cite forever.** The output is a `D-NN` record in
`docs/spec/12-dll-provenance.md`, not a conclusion held in conversation.

## When
- A command's name, layout, or bounds are in doubt, or rest only on a
  reconstruction, a comment, or another project's reference.
- `/audit` or `/verify` surfaces a command whose behaviour disagrees with its
  record.
- Before implementing or changing any handler whose contract is not already
  recorded.

**Do NOT** use for a command already adjudicated — cite the existing D-record.
**Do NOT** stop at the dispatch record. It says what is accepted, not what the
driver does, and that gap is where most of this project's defects lived.

## The ladder — climb it in order, stop when it settles

1. **The record.** `python scripts/dll-dispatch-table.py <dll>` — argc and the
   type bytes. `0xFF` is coordinate-width (`|n`), `0xFE` colour-width (`|M`),
   anything else a literal digit count. Sum it: that total is the command's
   fixed width, and therefore the offset at which a trailing string begins.
   The record types **only the numeric argument array**; a string is passed
   out-of-band and never appears in it (D-16). An argc of 2 on a command that
   plainly carries text is not a contradiction — see `|@` (D-27).

2. **The handler.** `python scripts/dll-disasm.py <dll> <rva>`. This outranks
   the record. Read for, in rough order of value:
   - **Its own name.** Handlers push a function name before calling the error
     reporter — `"RIP_Scroll"`, `"riprocmd - RIP_CopyBlit()"`, `"RIP_TextXY()"`.
     This alone has renamed commands and caught one name sitting on two.
   - **Which arguments it loads.** `[ecx]`, `[ecx+4]`, … in the prologue. A
     record may accept eight and the handler read seven; the eighth is reserved.
   - **What it calls.** Imports are resolved inline. `GDI32!SetPixel` settled
     `|X`; `USER32!OffsetRect(&r, 0, dest_y - y0)` settled that `|1G` moves
     vertically only and has no destination X at all.
   - **Its bounds and their diagnostics.** `cmp reg,N` guarding a
     `push "<message>"` gives the exact accepted range *and* the driver's own
     name for the field.

   **Bound the disassembly at the next handler entry.** Reading a fixed byte
   count runs into the following function; that is how a neighbour's strings
   were once attributed to `|3e`, and how `|!` — a zero-argument handler — came
   back carrying font and palette diagnostics.

3. **Shipped content.** `python scripts/corpus-scan.py <dir>` for census; scan
   payload widths and column values directly for anything finer. Content
   confirms a layout (all 25 `|1R` payloads begin with exactly eight zeros) and
   settles impact (a column uniformly `'0'` across 36 commands means the field
   RIPlib was reading there was always the same constant). It cannot overrule
   the driver on meaning — but it does decide whether to reject input.

## Persist — the D-record

Append to `docs/spec/12-dll-provenance.md`, in the house style: `D-NN`, an
uppercase title stating the finding, then indented prose. Include:

- the **slot, RVA and record**, so the claim is re-derivable;
- the **evidence that settled it**, quoted — the diagnostic string, the compare,
  the call, the corpus payload. Not "the handler shows"; show it;
- what **RIPlib did before**, and the observable consequence, in numbers where
  the corpus gives them;
- what is **not** recovered. Flag ambiguity; never fill it with a guess. `|2P`'s
  wire bit 0 is consumed by the driver and its meaning is unknown — that is
  recorded as unknown, and the bit is not acted on.

If the finding changes a divergence from bbs-land or a deliberate divergence
from the driver, update `docs/spec/14-divergence-register.md` too.

## Disciplines
- **Describe the driver, not our C.** The record stays true as RIPlib evolves.
- **Quote the evidence.** A D-record that only asserts is a comment, and this
  project has learned exactly what comments are worth (D-27).
- **A name is not a layout.** `|k` had the right name in both projects and the
  wrong field width, on 132 shipped uses.
- **Two commands may share a handler.** Overloaded letters keep their extra
  signatures in continuation rows whose letter byte is `0x00`, identified only
  by a shared handler pointer — `|h` has six, `|t`/`|x`/`|z` three each.
  Filtering rows on a printable letter silently drops them.
- **If it changes behaviour, it needs a regression test that fails without it.**

## Interop
Feeds `/audit` (a newly-known contract becomes a class to check across every
command) and `/verify` (a new claim becomes a predicate in
`scripts/dll-validate-claims.py`). A fork about how faithful to be → `/decide`.
