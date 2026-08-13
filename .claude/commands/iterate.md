# /iterate — continuity loop (reconcile → orient → derive → route → drive → emit)

**The spine.** Given where we are, drive the highest-leverage next move — and
leave the next session able to resume from disk alone. *A lens, not a leash:*
"iterate everything" means drive the critical thread, not spray across every
front.

## When
End of every substantial turn; "iterate next steps / continue"; resuming after
context loss. For a single known atomic fix, just do it and emit a one-line
STATE/NEXT.

## Loop

1. **RECONCILE** — fuse state from independent, on-disk signals, never memory:
   `git status --short` and `git log --oneline`, a fresh `ctest`, a fresh
   `dll-conformance.py`, a fresh `dll-validate-claims.py`.

   Mark facts **PROVEN** vs **HYPOTHESIZED**, and grade PROVEN by which
   instrument actually ran. They are not interchangeable:

   | ran | proves |
   |---|---|
   | unit suite | authored behaviour |
   | + conformance | the shape matches the record |
   | + corpus replay | it survives real content, and its *non-rendering* outputs did not move |
   | + claim validator | the documentation is still true of the code |
   | + sanitizers, fuzz | it is not lying about memory or input |

   A command marked correct that has never been under the corpus is HYPOTHESIZED
   on real content, not PROVEN. `git status` is part of this step: a green run
   against an uncommitted working copy proves nothing about the repository.

   Advance-check: measurable delta since the last iteration? None → re-orient
   and increment a no-advance counter.

2. **ORIENT** — name the binding constraint. If this is an audit or verification
   iteration, **pick a LENS, not more effort.** Yield falls off sharply within a
   lens and re-running an exhausted one yields nothing — so name which instrument
   × input class the tree has *not* been under:

   - a defect class not yet expressed as a predicate (`/audit`);
   - a claim not yet re-derivable (`/verify`);
   - non-rendering output not yet counted — what does a consumer read that no
     metric reports?
   - the corpus at a granularity not yet measured (values, not counts);
   - the fuzzer's seed set against a command family it cannot currently reach;
   - a toolchain the tree has not been built under.

   When late passes start finding faults in the *instruments* rather than the
   code, that is the method working: verify the instrument, then continue.

3. **DERIVE + CONCENTRATE** — next steps traceable to a specific command,
   class, or finding. Not a wishlist. Default concentration: **one class at a
   time**, since that is where this project's yield is.

4. **ROUTE** — tag each step `[/arbitrate]` `[/audit]` `[/verify]` `[/debug]`
   `[/decide]`.

5. **DRIVE or HAND OFF** — check the human-seam below. Else execute.

6. **EMIT + COMPOUND** — record any new `D-NN`, register entry, tolerance, or
   check; then emit.

## Human-seam — stop and surface to the operator when:
- a **consequential fork** appears (scope, fidelity posture, release) → `/decide`;
- **N=3 iterations** with no objective advance;
- a change is **cross-cutting and hard to validate** — moving a shared byte
  path, changing emission for every byte, altering the build system. Say what
  the risk is and what would check it, and let the operator choose;
- **an outward-facing action** is implied — a tag, a release, an upstream issue.
  These are not iteration;
- a **milestone** worth surfacing (a class reaching zero; every audit dimension
  clean at once).

Else: drive, then re-enter step 1.

## Emit format (every substantial turn)
```
STATE:    <where we are; PROVEN by which instruments vs HYPOTHESIZED>
ADVANCE:  <objective delta since last iteration, or "none — re-orienting (n/3)">
FRONTIER: <binding constraint; which lens is next and why>
NEXT:     <1–3 steps, each tagged [/arbitrate] [/audit] [/verify] [/debug] [/decide]>
COMPOUND: <D-record / register / check / tolerance recorded>
PROMPTS:  <2–4 paste-ready operator lines for the next session>
```
`PROMPTS` is the continuity handoff — what to type next after full context loss.

## Diminishing returns — how to tell, honestly
Not "the checks are green": they were green while `|1b` was asking the host for
`0000back.bmp` in 36 places. The honest test is whether a **new lens** is
available and untried. When every dimension reports zero, the corpus is clean on
values as well as counts, the fuzzer's seeds reach every command family, and the
remaining items are operator judgement rather than evidence — say so plainly,
name what is still open and why it is open, and stop.

## Interop
Routes to `/arbitrate` `/audit` `/verify` `/debug` `/decide`; fed by their
committed artifacts. Does not replace the instruments — those are the green, and
they are listed in [README.md](README.md).
