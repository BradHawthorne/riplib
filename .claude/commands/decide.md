# /decide — Decision Workflow

**Version**: 2026-05.2 (Quality & Continuity Edition; first-use refinements)
**For**: Solo developer + AI-assisted projects with complex, long-horizon technical scope
**Priorities**: Quality first, continuity of knowledge, durable audit trail. No velocity pressure.

A portable, disciplined decision methodology. Drop this file into `.claude/commands/decide.md` (or your agent's command directory). It is self-bootstrapping and project-agnostic via the CONFIG block.

**The audit trail *is* the value.** This workflow turns decisions into durable knowledge that survives context loss, project phases, and future self (or future AI sessions). It compounds: the third time you consult the graveyard or a rule, it has already paid for itself.

---

## When to invoke /decide (and when not to)

**Use /decide when** you're choosing between options that have non-trivial consequences. The choice is hard to reverse, affects shared resources, opens a new direction, contradicts an existing decision, or you'd want a future-you (or future-AI) to be able to reconstruct *why* in 6+ months. If you'd want an audit trail, /decide it.

**Do NOT invoke /decide for**:

- **Execution of an already-committed decision.** ADRs produce *plans*; implementing the plan is a normal hands-on session (read/edit/build/test/hardware iterate). Re-running /decide to "execute Phase A" or "implement ADR-NNN" is a category error — the plan is the decision; the code is the artifact. Re-invoke /decide only if execution surfaces an unexpected failure mode that warrants a *new* design choice (e.g., the spike fails in a way that opens a fresh trade-off).
- **Bug fixes with a clear root cause.** If you know what's wrong, fix it. The commit message is the audit trail.
- **Trivial choices** below the threshold of "worth recording" (renaming a local variable, tweaking a comment). The Trace path exists for "small but worth a search-record" decisions; below even that bar, just do it.
- **Pure refactors with no behavior change**, unless the refactoring approach itself is contested. Restructuring with one obvious path belongs in commit messages, not ADRs.

**Rule of thumb**: if you already know what you're going to do, just do it. If you're choosing between options and the choice has consequences worth being able to reconstruct later, /decide it. The boundary is the difference between *acting* and *deciding* — /decide is for the second category only.

---

## CONFIG (edit once per project, then leave alone)

```yaml
# === Paths (relative to repo root) ===
# Consolidated to two foundation files + ADR directory. All content the workflow
# reads and writes lives in one of these three locations.
paths:
  decisions:       design/decisions.md      # workflow notes + candidates ledger + committed + parked + graveyard + decisions log + index
  knowledge:       design/knowledge.md      # heuristic rules + prior-art register + open empirical questions
  adr_dir:         design/adr/              # one ADR per committed or killed decision
  subsystem_glob:  design/0*-*.md           # other project-content docs to cross-reference at Commit

# === Classification (controls investment level) ===
classification:
  enabled: true
  criteria:
    full:  "High continuity impact OR high quality/risk surface (shared resources, novel primitives, hardware, irreversible, long maintenance horizon)"
    lite:  "Medium continuity impact or moderate risk — still worth structured reasoning, but doesn't warrant full ceremony"
    trace: "Low continuity value but worth a one-line record so future search finds it"

# === Universal scorecard (per-variant axes; applies at Phase 4 EVALUATE) ===
# These axes discriminate between variants. Decision-level qualities
# (Continuity value, Knowledge capture) are evaluated once at Phase 6 — see
# Phase 6 checklist — because they apply to the decision-as-a-whole, not per variant.
scorecard_universal:
  - { name: Correctness,         type: qualitative, scale: "OK / concern / blocker" }
  - { name: Complexity,          type: qualitative, scale: "OK / concern / blocker", notes: "LOC, special cases, fragility" }
  - { name: Maintainability,     type: qualitative, scale: "OK / concern / blocker", notes: "debuggability, clarity, onboarding cost for future self/AI" }
  - { name: Verification effort, type: qualitative, scale: "OK / concern / blocker" }
  - { name: Risk / uncertainty,  type: qualitative, scale: "OK / concern / blocker" }
  - { name: Reversibility,       type: qualitative, scale: "easy / costly / one-way", notes: "Cost to undo. One-way doors receive extra scrutiny" }

# Lite path uses only this subset (rest may be marked N/A):
scorecard_lite_required: [Correctness, Complexity, Reversibility, Risk / uncertainty]

# === Project-specific scorecard (extend as needed) ===
scorecard_project: []

# === Binding constraints (anti-goals every candidate must respect) ===
constraints: []

# === Operational settings ===
settings:
  min_candidates_phase3_full: 3
  min_candidates_phase3_lite: 2
  context_decay_days:         180
  spike_default_budget:       "2–3 focused days of throwaway exploration"
  require_adr_on:             [commit, kill, park]   # Lite produces a Lite ADR; Trace produces no ADR. Park ADRs capture the analysis-so-far + resume condition.
  default_path:               full
```

---

## Step 0 — Context load + bootstrap + classification (non-negotiable)

1. **Resolve CONFIG paths.** If any required file is missing, scaffold it from the templates in **Appendix A**, report exactly what was created, and proceed.
2. **Read `decisions`** — scan the *Workflow notes*, *Live candidates*, *Parked*, and *Graveyard* sections. Flag any candidate older than `context_decay_days` for refresh.
   - **Mandatory graveyard check**: if this decision matches a killed entry, surface the kill reason + ADR and **stop** unless the user explicitly overrides with what has changed since the kill.
3. **Read `knowledge`** — note applicable HR-NNN rules, any related prior-art entries, and any open unknowns that touch this decision.
4. **Glob `subsystem_glob`** — note any project docs whose open questions this decision touches.
5. **Classify (Phase 0.5)** — match the decision against CONFIG `classification.criteria` and pick a path:
   - **Full** → run the complete workflow below.
   - **Lite** → run with the rules in **Appendix C "Lite path"**.
   - **Trace** → skip to a single line in the *Decisions log* section of `decisions.md`, then exit. (See **Appendix C "Trace path"**.)

   Report path + one-line rationale.

If foundation docs were just scaffolded, the loop still runs — empty sections are fine. Value compounds as you fill them.

---

## The Decision

$ARGUMENTS

---

## Execute the workflow (Full path)

Produce concrete artifacts (file edits, new rows, new ADR). Do not skip phases. **Every phase boundary ends with a Self-audit block** (see *Reporting Format*). This is mandatory and is the AI executor's quality gate.

### Phase 1 — SURVEY

- **Spec deduction**: Quote authoritative sources (language refs, RFCs, datasheets, vendor docs, framework guides) on the interfaces or contracts this decision touches.
- **Prior-art scan**: What do existing libraries, papers, comparable projects, and established patterns do here? Record every source inspected in the *Prior art register* section of `knowledge.md`.
- **Survivorship analysis** — choose one (inline definitions, no lookup needed):
  - **H1** — genuine gap (novel territory; nobody has done this)
  - **H2** — documented failure (people tried, it didn't work, and they wrote it down)
  - **H3** — suspected silent failure (absence with no published rationale; tread carefully)
  - **H4** — different priorities (others' constraints made this irrelevant; ours may differ)
  - **unknown** — couldn't classify; flag for follow-up
- **Cargo-cult check** (if the pattern IS present in prior art) — choose one:
  - **H1'** — optimal for their case
  - **H2'** — legacy / compatibility baggage
  - **H3'** — cargo-culted (no current rationale; copied forward)
  - **N/A**
- **Empirical unknowns**: List questions whose empirical answer would change the design analysis — typically because the constraint or behavior they probe is not documented in any authoritative source you've checked. Append new ones to the *Open empirical questions* section of `knowledge.md`. (An open empirical question is not a failure; it's a next move with a name.)
- **Context refresh**: If this decision touches any candidate or claim older than `context_decay_days`, re-validate against current sources before trusting it.

**Self-audit at phase boundary** (mandatory).

### Phase 2 — RESOURCE, DEPENDENCY & IMPACT MAP

Position the decision in the project's actual topology.

- Which resources, buses, memory tiers, queues, services, data flows, user journeys, or incentive surfaces does it consume or affect?
- **Pairwise interop**: name every other component or feature it shares a resource or dependency with.
- **Contention & impact check** (critical): at realistic load or full intended scope, does this compete with other consumers? Run the numbers on shared budgets (cycles, memory, power/thermal, bandwidth, developer attention, future maintenance load).
- For non-systems domains, map the relevant surfaces: dataflow / IR passes (toolchains), identity/gossip/privacy boundaries (P2P), optionality and incentive coherence (protocol or funding decisions), etc.

If a new contention pattern or high-impact surface appears, document it in the appropriate subsystem doc or in the *Workflow notes* section of `decisions.md`.

**Self-audit at phase boundary** (mandatory).

### Phase 3 — GENERATE (≥ `min_candidates_phase3_full` variants required)

**Anchoring bias enforcement**: you may not proceed to Phase 4 with fewer than three genuinely distinct variants. The first idea must compete; otherwise you're scoring confirmation, not options.

Variants should span at least:

1. **Conventional** — how does established practice solve this? (the baseline to beat or match)
2. **First-principles** — natural fit given the project's actual topology and constraints, ignoring convention.
3. **Creative recombination or inversion** — unexpected combinations of available primitives, OR "what if we did the opposite?" (non-ideal-but-works is often the answer under constraint).

**Anti-goals**: restate every binding constraint from CONFIG `constraints`. Every variant must respect them.

For each variant, separate **MVP scope** (commit to this first) from **Full Vision** (roadmap note only).

**One row per decision, not per variant.** Write **a single row** to the *Live candidates* section of `decisions.md` with status `alive` and today's date. The variants are tracked inside the decision — list them in the row's *One-line summary* field (e.g., "variants: A / B / C") and elaborate in the eventual ADR's *Alternatives considered* section. The `Variant chosen` column stays empty until Phase 6 picks a winner. Losing variants do not need their own terminal state; they live as analysis inside the decision's ADR.

**Self-audit at phase boundary** (mandatory) — including: how do these variants differ in *fundamental approach or assumptions*, not just parameter tweaks?

### Phase 3.5 — VERIFY-PLAN

For each surviving variant, draft a verify plan that will be absorbed into the eventual ADR's *Verification plan* section. (Plans no longer live in a separate file; they ride with the decision they validate.)

Each plan names:

- **Properties to hold**: invariants, safety, liveness, timing, security, continuity.
- **Hard-to-reach states**: edge cases, races, fault paths, adversarial inputs, long-term drift.
- **Validation mix**: unit / integration / on-target / property-based / fuzz / formal / manual / canary / soak.
- **Success meaning**: "proves correctness" vs "builds confidence." Both are valid evidence; name which this plan delivers and why.

**Self-audit at phase boundary** (mandatory).

### Phase 4 — EVALUATE (multi-axis Pareto)

Score every variant against the union of `scorecard_universal` + `scorecard_project`.

**Confirmation bias enforcement**: for each variant, explicitly list what evidence would **falsify** it, then actively search for that evidence. If you finish Phase 4 without finding any disconfirming evidence, either (a) flag it as a confirmation pass and search again, or (b) explicitly assert, listing the specific searches enumerated and where you looked, that a good-faith search found none. Option (b) is acceptable only when option (a) has been done at least once — a clean confirmation pass is not in itself evidence of soundness.

**Required Pareto-view format** (don't collapse to a single ranking):

```
| Variant     | Axis 1 | Axis 2 | ... | Axis N | Pareto status         |
|-------------|--------|--------|-----|--------|------------------------|
| A           | OK     | concern| ... | OK     | dominated by C         |
| B           | OK     | OK     | ... | concern| non-dominated          |
| C           | OK     | OK     | ... | OK     | dominant on this set   |
```

Call out the **non-dominated set** explicitly — that's the actual trade-off frontier where the decision lives, not a leaderboard.

**Scoring scope rule**: the Pareto *table* may show a readability subset of axes — the ones most discriminating for this decision — but the ADR's *Why this won* section must reference the **full** scorecard (universal + project). Any axis omitted from the table must be called out as "uniform across variants" or "N/A for this decision," not silently dropped. The full scorecard exists so future-you can audit whether the dropped axes were genuinely uniform or whether the omission masked a real difference.

**Branch (four quadrants; the partition must be exhaustive)**:

- **High score, low risk** → **Phase 6 Commit** directly. Spike is for *risk reduction*, not ritual — if there's no risk to retire, skip it.
- **High score, high risk** → **Phase 4.5 Spike**, then Commit on pass. (This is the case spike actually exists for.)
- **Low score, fixable** → **Phase 5 Refine**.
- **Low score, unfixable** → **Kill** (write Kill ADR + graveyard row; see Phase 5 → Kill DoD).

**Self-audit at phase boundary** (mandatory) — including: list the specific disconfirming evidence searched for, and where you looked.

### Phase 4.5 — SPIKE (when applicable)

Micro-validation with throwaway code or experiment.

- State **pass / fail / partial** criteria *up front* (so you can't move the goalposts after seeing results).
- Budget: `settings.spike_default_budget`. If you blow the budget without a result, the spike has answered — the thing is harder than predicted; back to Phase 5.
- Mark the candidate row `spike-pending`. On result: pass → Phase 6; fixable fail → Phase 5; unfixable fail → Graveyard.

**Self-audit at phase boundary** (mandatory).

### Phase 5 — REFINE (when a variant is poor-fit but interesting)

- Name the friction in one sentence.
- Generative thought experiment: how would we redesign for better fit? Which primitives could we combine creatively? Does this expand the project's capability footprint?
- **Sunk-cost bias enforcement**: ask explicitly — *"Would I pursue this if seeing it fresh today, with current evidence?"* If invested effort is the only reason to continue, kill it.

Decision options (must pick one — exhaustive):

- **Refine again** — re-enter Phase 3 (regenerate) or Phase 4 (re-score). **Never skip to Commit.**
- **Kill** — see **Kill — Definition of Done** below.
- **De-scope to MVP** — drop ambition, re-evaluate the trimmed version.
- **Park** — see **Park — Definition of Done** below.

**Self-audit at phase boundary** (mandatory).

#### Kill — Definition of Done

Routes here from: Phase 4 (low-score-unfixable quadrant), Phase 4.5 (unfixable spike fail), or Phase 5 (Kill decision). All routes share this DoD.

- [ ] **Kill ADR written** in `adr_dir/NNNN-rejected-*.md` using the **Kill ADR template** (see Appendix A). Set `Status: rejected`.
- [ ] Candidate row moved from *Live candidates* to *Graveyard* in `decisions.md`, with one-line reason + ADR link.
- [ ] Decisions log entry appended: `YYYY-MM-DD | candidate-name | kill | one-line rationale | ADR-link | tags`.
- [ ] If killed via Phase 4.5 spike: record the spike findings inside the Kill ADR — negative results are evidence worth keeping and prevent re-investigation.
- [ ] Open questions surfaced by the kill are logged in `knowledge.md` (a kill sometimes *opens* questions; capture them).
- [ ] If the kill produced a heuristic (e.g., "this whole class of approach doesn't work because X"), add an `HR-NNN` rule to `knowledge.md`.

#### Park — Definition of Done

Routes here from: Phase 5 only (Park decision). Park is a deliberate deferral with a measurable resume condition; it is not "we'll figure it out later."

- [ ] **Park ADR written** in `adr_dir/NNNN-parked-*.md` using the **Park ADR template** (see Appendix A). Set `Status: parked`.
- [ ] Candidate row moved from *Live candidates* to *Parked* in `decisions.md`, with the **measurable resume condition** (a change in the world, not a date).
- [ ] Decisions log entry appended: `YYYY-MM-DD | candidate-name | park | one-line rationale + resume condition | ADR-link | tags`.
- [ ] Resume condition is phrased so a future-self can evaluate it without reloading current context (e.g., "when PSRAM ships standard on the board" beats "when conditions improve").
- [ ] Park ADR includes a **"what would warrant promoting to Kill"** clause — parks that never get resumed should not linger indefinitely.

### Phase 6 — COMMIT (Definition of Done — ALL must be true)

- [ ] **Sunk-cost gate** (always fires, even on clean runs that never entered Phase 5): *"Would I commit to this decision today, with current evidence, if no effort had yet been spent on it?"* If no, return to Phase 5. This is where sunk-cost bias gets caught for runs that bypass Refine.
- [ ] Phase 4 multi-axis scorecard complete on the winning variant, with Pareto status filled in.
- [ ] Phase 3.5 verify plan written into the ADR's *Verification plan* section.
- [ ] Phase 4.5 spike passed (or explicitly waived with rationale recorded in the ADR).
- [ ] **ADR written** in `adr_dir/NNNN-*.md` containing: status (`accepted`), date, classification path, candidate ID, **reversibility class**, search tags, **workflow version**, alternatives, decision, why this won, trade-offs accepted, verification plan, **rollback path**, consequences, open questions, amendments slot. Set `Status: accepted` explicitly.
- [ ] **Global constraint re-check**: with this decision added to the union of currently-committed work, does the project still satisfy every constraint in CONFIG `constraints`? Show the reasoning; don't hand-wave. **Constraint violations are not silently amendable** — if a constraint blocks the winning variant, see *Constraint amendment* in Appendix D.
- [ ] **Decision-level qualities recorded** in the ADR (set once per decision, not per variant):
  - **Continuity value** (high / medium / low) — will future self/AI benefit from this record in 6–24 months?
  - **Knowledge capture** (strong / adequate / weak) — were rationale, alternatives, trade-offs, and falsification searches captured well enough that a future reader can reconstruct *why*?
- [ ] Cross-references updated in affected subsystem docs (matched by `subsystem_glob`).
- [ ] Remaining open questions logged in the *Open empirical questions* section of `knowledge.md`.
- [ ] **Decision-log entry appended** to the *Decisions log* section of `decisions.md` — one line: `YYYY-MM-DD | candidate-name | commit | one-line rationale | ADR-link | tags`.
- [ ] Candidate row moved from *Live candidates* to *Committed decisions* in `decisions.md`, with ADR link, `Validation status: pending` (Phase 7 plan exists but execution may be deferred).
- [ ] *Index by theme* updated in `decisions.md` if this decision opens or extends a theme.

**Self-audit at phase boundary** (mandatory).

### Phase 7 — SYSTEM VALIDATE (plan now; execution may be deferred but never skipped)

Plan macro-validation:

- **Full-system test** alongside other committed work (not in isolation — isolation hides contention).
- **Baseline comparison** with concrete numbers (prior art does X; we do Y; here is the delta and why it matters).
- **Cross-environment generalization**: validating in one environment / on one dataset / with one user is not a general claim. State the N needed for the claim you want to make.
- **Contention re-check** under combined load.
- **Observability**: which metrics will make future "does it still work?" questions answerable from data, not vibes?

If post-implementation surprises occur:

- Derive a heuristic rule; add to the *Heuristic rules* section of `knowledge.md` as `HR-NNN`.
- Kick affected candidates back to Phase 1 with the new evidence.
- Update the *Open empirical questions* section of `knowledge.md` with answers (an answered unknown is itself a finding worth keeping).

**Re-entry pattern** (when a committed decision needs revisiting): see **Appendix D**.

**Meta-evolution reflection** (optional but encouraged for long-term use): did any phase feel mis-scoped or low-value for this type of decision? Any template, prompt, or CONFIG improvement worth capturing? If so, note in `knowledge.md` and consider amending this `/decide` command.

**Self-audit at phase boundary** (mandatory).

---

## Reporting Format

At every phase boundary, emit a status block in this exact format:

```
[Phase N — Name] <decision or state>. <one-line rationale>. <next action>.

Self-audit:
  • Falsification performed: <2–3 specific searches/tests/sources used to try to disprove>
  • Variant distinctness (Phase 3 only): <how variants differ in approach/assumptions, not parameters>
  • Artifact updates: <exact files + sections modified or created>
  • New unknowns surfaced: <list, or "none">
```

At end of invocation:

```
DECISION: <commit | kill | park | spike-pending | refine | lite | trace>
CLASSIFICATION PATH TAKEN: <full | lite | trace> — <brief rationale>
ARTIFACTS UPDATED: <list with paths>
OPEN QUESTIONS: <list or "none">
NEXT STEP: <concrete action>
```

---

## Discipline Reminders

- **Do not skip phases.** The phase you want to skip is usually the one that would have caught the problem.
- **≥ `min_candidates_phase3_full` distinct variants** before Phase 4. Always. (Lite: ≥2.)
- **Falsify, don't just confirm.** Actively hunt disconfirming evidence at Phase 4.
- **Apply all four bias checks** at their phases: anchoring (3), confirmation (4), sunk-cost (5 when entered, **6 always — gate**), survivorship-bidirectional (1 + 4). The Phase 6 sunk-cost gate fires even on clean runs that never entered Phase 5; it catches the bias on the path Phase 5 doesn't see.
- **Refuse to re-investigate graveyard entries** without explicit user override AND a demonstrable change in conditions.
- **Reversibility is first-class.** One-way doors deserve more scrutiny than easily-reversible decisions.
- **Context decays.** A survey from six months ago is a hypothesis, not a fact. Refresh before relying on stale conclusions.
- **Update the foundation docs** — don't just announce decisions. An un-recorded decision is an intention.
- **Heuristic rules belong in `knowledge.md`**, not working notes. The lesson you don't write down, you will learn again — and pay for again.
- **The self-audit is mandatory.** Skipping it once teaches the workflow that it's optional. It is not.

---

## Appendix A — Bootstrap templates

When scaffolding, create the file with the matching template below. Report what was created; let the user commit when ready.

### `decisions.md` (template)

```markdown
# Decisions

This project uses the **/decide** workflow (defined in `.claude/commands/decide.md`).
All workflow state — live candidates, committed decisions, parked work, killed work,
the chronological decisions log, and the theme index — lives in this single file.

---

## Workflow notes & project-specific deviations
_(record any deviations from the standard /decide workflow here as they emerge from retrospectives)_

---

## Index by theme
_(grouped pointers to ADRs and committed candidates. **At Phase 6 Commit: update this section whenever a decision opens a new theme or extends an existing one** — otherwise the index rots and stops earning its keep.)_

- **Theme A**:
- **Theme B**:

---

## Live candidates

| ID    | Name | Status | Variant chosen | ADR | Last touched | One-line summary | Path  |
|-------|------|--------|----------------|-----|--------------|------------------|-------|
| C-001 |      | alive  |                |     | YYYY-MM-DD   |                  | full  |

_Status values: `alive` → `spike-pending` → moves to **Committed** | **Parked** | **Graveyard**._

---

## Committed decisions

| ID    | Name | ADR | Date committed | Reversibility | Validation status | Tags |
|-------|------|-----|----------------|---------------|-------------------|------|

_**Validation status** values: `pending` (Phase 7 plan written, execution deferred), `running` (validation in progress), `passed` (Phase 7 executed and succeeded), `failed-amend` (failed; amendment recorded in ADR), `failed-supersede` (failed; superseded by new ADR — see Appendix D). The Phase 7 plan is part of Commit; its execution status is tracked here so deferred validation cannot silently rot._

---

## Parked

| ID    | Name | Park condition (measurable) | Date parked | ADR (required) | Promote-to-kill clause |
|-------|------|------------------------------|-------------|----------------|-------------------------|

---

## Graveyard (killed candidates)

**Mandatory check** before re-investigating any killed entry. Requires explicit user override AND a demonstrable change in conditions.

| ID    | Name | Date | Reason (one line) | ADR |
|-------|------|------|-------------------|-----|

---

## Decisions log (chronological)

One line per /decide invocation that reached Commit, Kill, Park, or Trace. The ledger above tracks state; this log tracks history.

```
YYYY-MM-DD | candidate-name | decision | one-line rationale | ADR-link | tags
```
```

### `knowledge.md` (template)

```markdown
# Knowledge

Project-specific knowledge that accumulates across decisions: heuristic rules,
prior-art surveys, and open empirical questions. The /decide workflow reads
from here at Step 0 and writes back at Phases 1, 6, and 7.

---

## Heuristic rules (HR-NNN)

Operational rules extracted from Phase 7 surprises, post-mortems, or retrospectives.
Each rule must change a future decision.

### HR-001 — _(title)_
- **Rule**: _(one-sentence imperative)_
- **Origin**: _(what surprise produced this rule)_
- **Applies to**: _(domains or candidate types)_
- **Recorded**: YYYY-MM-DD

---

## Prior art register

Inspected references, libraries, papers, comparable projects, blog posts, RFCs.
Kept so the next /decide run doesn't redo the same search.

| Source | Type (lib/paper/repo/post/RFC) | Inspected for | Date       | Verdict (use / learn-from / reject) | Notes |
|--------|--------------------------------|---------------|------------|--------------------------------------|-------|

---

## Open empirical questions

Questions whose empirical answer would change a design analysis — typically because
the constraint or behavior they probe is not documented in any authoritative source.
Open questions are not failures; they are the next move with a name. **Answered
unknowns stay here with the answer recorded** — they're findings worth keeping.

Unknowns are **grouped by the candidate that raised them**, because decisions often
surface multiple related questions that share status flow (open → answered) and tend
to be answered together (one spike or one hardware test resolves several). A flat
per-question table with a repeated "Raised by" column ages poorly when one decision
generates many unknowns.

### Raised by D-NNN — _(candidate name)_

| ID    | Question | Date raised | Status (open/answered) | Answer + source |
|-------|----------|-------------|------------------------|-----------------|
| U-001 |          | YYYY-MM-DD  | open                   |                 |

_(Add a new `### Raised by D-MMM — ...` section per candidate that surfaces unknowns.
Use `### Cross-cutting` for unknowns that span multiple candidates.)_
```

### ADR template (`adr_dir/0000-template.md`)

```markdown
# ADR-NNNN — _(Title)_

**Status**: accepted | rejected | parked | superseded by ADR-MMMM | amended (see Amendments)
**Date**: YYYY-MM-DD
**Candidate**: C-NNN
**Reversibility**: easy | costly | one-way
**Classification path**: full | lite | trace
**Workflow version**: _(stamp the /decide version from the header of this command — e.g., "2026-05")_
**Search tags / keywords**: _(comma-separated; critical for findability at 6–24 month horizons)_
**Decision-level qualities** _(set once per decision, at Phase 6)_:
- **Continuity value**: high | medium | low
- **Knowledge capture**: strong | adequate | weak

## Context
_(what forces are at play; why this decision is being made now)_

## Alternatives considered
1. **_(variant A)_** — _(one paragraph; why not)_
2. **_(variant B)_** — _(one paragraph; why not)_
3. **_(variant C, the chosen one)_** — _(one paragraph; pointer to "Decision" below)_

## Decision
_(what we are doing, stated as a directive)_

## Why this won
_(deciding axes from the Phase 4 scorecard; the disconfirming evidence searched for and not found)_

## Trade-offs accepted
_(what we are explicitly giving up; what could make us regret this)_

## Verification plan
_(absorbed from Phase 3.5)_
- **Properties to hold**:
- **Hard-to-reach states**:
- **Validation mix**:
- **Success means**: proves-correctness | builds-confidence (and why)

## Rollback path
_(if this decision proves wrong, what does undoing it cost? Concretely: which files / systems / commitments)_

## Consequences
_(downstream effects on other components, future decisions, constraints budget)_

## Open questions
_(what we still don't know; cross-link to entries in `knowledge.md`)_

## Amendments
_(minor revisions made after Acceptance, with date + rationale. Major revisions: supersede with a new ADR and update Status above.)_
```

### Kill ADR template (`adr_dir/NNNN-rejected-*.md`)

Used when a candidate routes to Kill (from Phase 4 low-score-unfixable, Phase 4.5 unfixable spike fail, or Phase 5 Kill). Kill ADRs are commit-shaped only superficially — most of the standard sections become nonsensical for a rejection. Use this shape instead.

```markdown
# ADR-NNNN — _(Title)_ — REJECTED

**Status**: rejected
**Date**: YYYY-MM-DD
**Candidate**: C-NNN
**Classification path**: full | lite
**Workflow version**:
**Search tags / keywords**: _(include a "kill-reason" tag like `unfixable-perf`, `constraint-blocker`, `spike-failed`, `dominated`)_

## Context
_(what was being decided; why it entered the loop)_

## Alternatives considered
_(the variants generated at Phase 3, including the closest-to-being-chosen and why even it didn't survive)_

## Why this was killed
_(the deciding axes from Phase 4 or the spike findings from Phase 4.5 that made every variant unfixable; the disconfirming evidence found)_

## Spike findings (if killed via Phase 4.5)
_(negative results from the spike — these are evidence worth preserving; they prevent re-investigation and inform future related work)_

## What would reopen this
_(measurable change in the world that would warrant re-investigation; this is the override clause cited by the mandatory graveyard check in Step 0. "Nothing — this is a hard kill" is a valid answer; say so if true.)_

## Lessons captured
_(rules / patterns extracted from the kill; cross-link to any HR-NNN added to `knowledge.md`)_
```

### Park ADR template (`adr_dir/NNNN-parked-*.md`)

Used when Phase 5 routes to Park. A park is a deliberate deferral with a measurable resume condition, not a quiet abandonment.

```markdown
# ADR-NNNN — _(Title)_ — PARKED

**Status**: parked
**Date**: YYYY-MM-DD
**Candidate**: C-NNN
**Classification path**: full | lite
**Workflow version**:
**Search tags / keywords**: _(include `parked` and the domain)_

## Context
_(what was being decided; why it entered the loop)_

## Alternatives considered (current state of analysis)
_(the variants generated at Phase 3 and how far the analysis got before park)_

## Why parked (not killed)
_(what's valuable here that warrants deferring rather than burying — typically: blocked by an external dependency, waiting on data, or current constraints make every variant suboptimal)_

## Resume condition (measurable)
_(specific change in the world that re-opens this; phrased so a future-self can evaluate it without reloading current context. "When PSRAM ships standard on the board" beats "when conditions improve.")_

## What would warrant promoting to Kill
_(if the resume condition stays unmet for long enough, when do we bury this instead of letting it linger? Often: a date-bounded review, or a competing decision that closes off this approach.)_

## Open questions
_(what we still don't know; cross-link to entries in `knowledge.md`)_
```

---

## Appendix B — Worked example (toy)

Illustrative end-to-end run, so first-time use has a reference for what "good" looks like.

**Note on self-audit blocks**: the workflow requires a self-audit block at every phase boundary (see *Reporting Format*). In this worked example, self-audit blocks are **elided from most phases for brevity** so the example reads cleanly — **with one complete exemplar at the end of Phase 4** to show the expected format. In a real invocation, every phase boundary gets its block; do not infer from this example's brevity that the requirement collapses.

**Decision**: Should the analytics CLI use SQLite or DuckDB for its embedded query layer?

### Step 0 — Context load + classify
- Read `decisions.md`: no related live candidates, nothing in graveyard.
- Read `knowledge.md`: HR-003 says "prefer single-binary distribution"; relevant.
- Classify: **Full**. Storage engine choice is costly to reverse; affects the core data path; long maintenance horizon.

### Phase 1 — Survey
- Spec deduction: DuckDB docs claim OLAP-optimized columnar execution; SQLite docs claim row-store with broad ecosystem.
- Prior art: `sqlite-utils` (CLI on SQLite), `dbt-duckdb` (analytics on DuckDB), `pandas + read_sql_table`. Recorded in `knowledge.md`.
- Survivorship: most existing CLI tools chose SQLite → **H4** (different priorities — they predate DuckDB or didn't need OLAP).
- Cargo-cult check: SQLite is conventional → **H1'** (it actually is optimal for their case, mostly OLTP-shaped CLIs).
- Empirical unknowns: U-014 "DuckDB binary size on Windows static-link?" — added to `knowledge.md`.

### Phase 2 — Resource / dependency / impact map
- Resources: binary size, RAM, query latency, build complexity.
- Pairwise interop: shares only the storage layer with the rest of the CLI; no contention on shared queues.
- Contention check: at p99 query load, DuckDB allocates aggressively; SQLite is tighter. Numbers: DuckDB ~50 MB resident; SQLite ~5 MB.

### Phase 3 — Generate (≥3 variants)
1. **SQLite** (conventional baseline)
2. **DuckDB** (first-principles for OLAP)
3. **Parquet files on disk + DuckDB as query engine** (creative recombination — storage and query layer decoupled)

Anti-goals: must stay ≤ 20 MB binary, must work offline, no Python runtime.

MVP/Vision split: MVP = single-file storage; Vision = sharded storage for >10 GB datasets.

**One row** written to `decisions.md` *Live candidates* as **C-024** with summary "analytics storage engine; variants: SQLite / DuckDB / Parquet+DuckDB." Variants are tracked inside the decision; losing variants live in the eventual ADR's *Alternatives considered* section, not as separate candidate rows.

### Phase 3.5 — Verify-plan
For each variant: measure binary size, p50/p99 latency on a 100 MB sample dataset, peak RSS. "Builds confidence" — not proving optimality.

### Phase 4 — Evaluate (Pareto table)

For this example, the project's CONFIG `scorecard_project` has been set to `[Performance, Binary size]` (the two domain-specific axes that discriminate here). The Pareto table below shows the **discriminating subset**: per-variant axes that actually differ. Axes uniform across variants (Correctness: all OK, Verification effort: all OK, Risk: all OK) are noted here and recorded in full in the ADR per the *Scoring scope rule*.

| Variant            | Complexity | Maintainability | Reversibility | Performance (project) | Binary size (project) | Pareto status            |
|--------------------|------------|-----------------|---------------|-----------------------|-----------------------|--------------------------|
| SQLite             | OK         | OK              | easy          | weak                  | 5 MB                  | non-dominated (size)     |
| DuckDB             | OK         | OK              | costly        | strong                | 18 MB                 | non-dominated (perf)     |
| Parquet + DuckDB   | concern    | concern         | costly        | strong                | 18 MB + I/O           | dominated by DuckDB      |

Falsification (per #17 escape clause — searches enumerated, good-faith search found none): searched DuckDB issue tracker for "data corruption" / "embedded crash" (none recent); searched SQLite docs + issue tracker for "OLAP performance regression" (confirms OLAP is not the target use case, not a regression).

Non-dominated set: {SQLite, DuckDB}. Genuine trade-off: size vs perf. (Decision-level qualities — Continuity value and Knowledge capture — are evaluated at Phase 6, not here.)

**Self-audit at phase boundary** (mandatory — shown here as the format exemplar; elided from other phases in this example for brevity):

```
[Phase 4 — EVALUATE] Non-dominated set {SQLite, DuckDB}; size-vs-perf trade-off. Branching to Phase 4.5 SPIKE for DuckDB (high score, high risk on binary size + Windows static-link). Next action: 2-day spike with pass criteria stated up front.

Self-audit:
  • Falsification performed:
      - DuckDB issue tracker, search "data corruption" + "embedded crash" (last 12 mo) — no recent. Argues for, not against — flagged as confirmation.
      - SQLite-users mailing list + sqlite.org/forum, search "OLAP performance regression" — confirms OLAP is not the target, not a regression. Falsified the worry that SQLite was being mis-evaluated.
      - Searched HN + lobste.rs for "DuckDB production horror story" — found 2 reports of high RSS under concurrent writers; our use case is single-writer-CLI, so not applicable. Bounded the risk.
      - Per #17 escape clause: option (a) completed (active falsification searches above); recording good-faith result for option (b) — no disconfirming evidence found for the DuckDB-dominates-on-perf claim.
  • Variant distinctness (Phase 3 only): N/A this phase — variants already generated. (At Phase 3, distinctness was: SQLite = row-store/conventional, DuckDB = columnar/first-principles-for-OLAP, Parquet+DuckDB = decoupled-storage-and-compute. Three different architectural assumptions, not three parameter tweaks.)
  • Artifact updates: design/decisions.md (C-024 row scorecard populated), design/knowledge.md (Prior art register +3 entries: sqlite-utils, dbt-duckdb, pandas read_sql_table). No ADR yet — that's Phase 6.
  • New unknowns surfaced: U-014 (DuckDB binary size on Windows static-link) — added to knowledge.md, will be answered by Phase 4.5 spike.
```

### Phase 4.5 — Spike
Budget: 2 days. Pass criterion: DuckDB binary ≤ 25 MB Windows static, p99 ≤ 200 ms on 100 MB sample. Result: **pass** (binary 22 MB, p99 = 140 ms).

### Phase 6 — Commit
- **Sunk-cost gate**: would I commit DuckDB today, with current evidence, if no spike effort had been spent? Yes — the perf delta is large enough that fresh-eyes would still pick it. Gate passes.
- Winner: **DuckDB**. Why: perf headroom matters more than 13 MB binary delta for an *analytics* CLI; reversibility is costly but acceptable.
- ADR-0024 written (4-digit zero-pad). Includes full scorecard (uniform axes called out), verification plan, rollback path ("if migration needed: export to Parquet, re-import to SQLite — script in `tools/`"), tags `storage,analytics,duckdb,sqlite`, workflow version `2026-05`, and decision-level qualities (**Continuity value: high** — storage choice is foundational; **Knowledge capture: strong** — alternatives, spike findings, and falsification searches all recorded).
- **Constraint re-check**: ≤ 20 MB binary **fails** (we're 22 MB). This is **not silently amendable** — the workflow forbids relaxing a constraint inside the same invocation that needs it relaxed (that's goalpost-moving). Two valid paths:
  - (a) Drop to a variant that fits the existing constraint (SQLite, 5 MB), accepting the perf trade-off.
  - (b) **Launch a separate /decide invocation** to amend the `≤ 20 MB binary` constraint, with its own ADR, and re-validate every previously-committed decision against the new bound (see Appendix D *Constraint amendment*). On success, return to this decision and re-commit under the new constraint.
  - For exposition, this example takes path (b): a separate constraint-amendment ADR (ADR-0023) committed first, raising the bound to ≤ 25 MB. Prior commits re-validated against the new bound. Then this DuckDB decision committed cleanly under the amended constraint.
- Cross-references: updated `design/01-storage.md`.
- Decision-log line: `2026-05-24 | analytics-storage-engine | commit DuckDB | perf wins over size for OLAP CLI (under amended ≤25MB constraint, ADR-0023) | adr/0024-duckdb.md | storage,analytics`
- Candidate row C-024 moved from *Live candidates* to *Committed*, with `Variant chosen: DuckDB`, ADR link, and `Validation status: pending`.

### Phase 7 — System validate (plan)
- Soak: run 24h workload against shared CLI binary alongside other features.
- Baseline: compare against an SQLite branch on the same dataset.
- Cross-environment: validate on Linux x86_64, Windows x86_64, macOS arm64 (N=3 to claim "ships everywhere"). One machine ≠ general claim.
- Observability: query duration histogram, peak RSS, binary-on-disk size — all logged per-release.

End-of-invocation summary:
```
DECISION: commit
CLASSIFICATION PATH TAKEN: full — irreversible storage choice, long horizon
ARTIFACTS UPDATED: design/decisions.md (candidates → committed, log, index by theme), design/knowledge.md (prior_art, unknowns), design/adr/0023-amend-binary-size-constraint.md, design/adr/0024-duckdb.md, design/01-storage.md
OPEN QUESTIONS: U-014 still partially open (Windows static-link verified, macOS notarization untested)
NEXT STEP: implement migration in feature/duckdb-storage; run soak after merge; flip C-024 Validation status to `running` when soak starts
```

---

## Appendix C — Lite & Trace path cards

### Lite path (medium continuity / moderate risk)

**Use when**: structured reasoning warranted, but full ceremony is overkill (e.g., picking a library among well-known options; choosing between two equivalent patterns).

Differences from Full:

| Phase | Lite behavior |
|-------|---------------|
| 0     | Same — graveyard check is **mandatory** even on Lite. |
| 1     | 1–3 paragraph survey; survivorship/cargo-cult only if non-obvious. |
| 2     | 1 paragraph; contention check only if shared-resource concern exists. |
| 3     | ≥ `min_candidates_phase3_lite` (default 2) variants; MVP/Vision split optional. |
| 3.5   | 1–2 sentences inline in the eventual ADR; no separate plan. |
| 4     | Required scorecard reduced to `scorecard_lite_required`; **all four bias checks remain mandatory**; Pareto table still required. |
| 4.5   | Skip unless explicitly needed. |
| 5     | Same as Full when entered. |
| 6     | Lite ADR (shorter — omit Verification plan section if Phase 3.5 was inline; required: status (`accepted` explicitly), decision, why, trade-offs, reversibility, rollback, tags, workflow version, decision-level qualities (Continuity value + Knowledge capture)); **sunk-cost gate mandatory** (same prompt as Full); constraint re-check mandatory (constraint amendment requires a separate decision, same as Full); decisions-log entry mandatory; candidates row mandatory; *Index by theme* update mandatory if a theme opens or extends. |
| 7     | 1-line plan: "how will we know it's still working in 3 months?" Logged in ADR. |

**Bias checks never collapse on Lite.** They're the cheapest safeguard against the most common errors.

### Trace path (low continuity, log-only)

**Use when**: decision is small or already made, but worth recording so future search finds the rationale. (e.g., "kept dependency X on version 1.x because 2.x requires Node 20 which we can't adopt yet.")

Workflow:

1. Step 0: graveyard check only (still mandatory).
2. Append one line to *Decisions log* in `decisions.md`: `YYYY-MM-DD | name | decision | rationale | trace | tags`.
3. No ADR. No candidates row. No scorecard. No bias checks. No further phases.

If you find yourself reaching for Trace on something with consequences, reclassify to Lite. The cost of upgrading is small; the cost of under-recording a real decision compounds.

---

## Appendix D — Amend / Supersede / Re-enter / Park-resume / Constraint amendment

When a committed (or parked, or killed) decision is challenged by new evidence, pick one:

- **Amendment** (minor) — fact narrows or a small parameter changes, decision stands. Add an entry to the ADR's *Amendments* section with date + rationale. Update `Status: amended (see Amendments)`. No new ADR.

- **Supersession** (major) — the decision is being replaced. Write a **new ADR** with `Status: accepted`; mark the old ADR `Status: superseded by ADR-MMMM` and add a final entry in its *Amendments* section pointing forward. Keep the old ADR; deletion erases history. Add a row to *Decisions log* with `supersede` decision type.

- **Re-entry** (back to the loop) — evidence overturns the original analysis. Move the candidate back to *Live candidates* in `decisions.md` with a note "re-opened YYYY-MM-DD: <reason>"; flag context-decay; re-enter at Phase 1 with the new evidence. The original ADR stays as historical record; the new run produces a new ADR that may supersede.

- **Park resume** — the *resume condition* recorded in a Park ADR has been met (or judged close enough to revisit). Move the candidate row from *Parked* back to *Live candidates* with a note "park-resumed YYYY-MM-DD: <resume condition met / re-evaluated>." Re-enter at Phase 1 with the current state of the world; the Park ADR becomes part of the prior-analysis trail. The new run produces a new ADR. If re-evaluation concludes the candidate should now be killed instead, write a Kill ADR that supersedes the Park ADR (the *what would warrant promoting to Kill* clause in the Park ADR is what cited this path).

- **Graveyard re-investigation** — re-opening a killed entry requires explicit user override AND named demonstrable change (per the mandatory graveyard check in Step 0). The override clause cited is the *What would reopen this* section of the Kill ADR. If demanded, move the row from *Graveyard* back to *Live candidates* with a note "re-opened YYYY-MM-DD: <named change>"; re-enter at Phase 1; the Kill ADR remains as historical record.

### Constraint amendment (separate decision, never inline)

A binding entry in CONFIG `constraints` is a project-wide invariant: every committed decision was scored *under* it. Loosening, tightening, or replacing one is a substantive decision in its own right and **must not happen inline in another decision that wants the constraint relaxed** — that's goalpost-moving, exactly what Phase 4.5 forbids for spikes.

To amend a constraint:

1. **Launch a separate /decide invocation** with the candidate "amend constraint X from Y to Z."
2. Run the full loop (Full path, typically — constraint amendments are usually high-continuity-impact and irreversible-in-spirit).
3. **Re-validate every currently-committed decision** against the proposed new constraint. Any committed decision that no longer satisfies it must be re-entered (Re-entry above). If even one prior commit can't be saved, the amendment may not be worth it — surface this in the amendment's Phase 4 evaluation.
4. **Write a dedicated ADR** (`adr_dir/NNNN-amend-constraint-*.md`, `Status: accepted`, tags include `constraint-amendment`).
5. On commit of the amendment, update CONFIG `constraints` to reflect the new value, with a comment pointing to the amendment ADR.
6. **Then** return to the original decision that wanted the amended constraint, and proceed under the new bound.

The Phase 6 *Global constraint re-check* line catches violations; it does **not** authorize amending the constraint to make them go away. Silent relaxation is the failure mode this gate exists to prevent.

---

## Installation

1. Copy this file to `.claude/commands/decide.md` (or your agent's equivalent command directory).
2. Run `/decide <first decision>`. Step 0 will scaffold `design/decisions.md`, `design/knowledge.md`, and `design/adr/0000-template.md` from the embedded templates.
3. Edit the CONFIG block once for the project (paths, project-specific scorecard axes, binding constraints).
4. Commit the generated files when ready.

This workflow is tuned for solo AI-assisted work on complex, long-horizon projects where quality and the survival of knowledge across time are the primary objectives. The two-file foundation (`decisions.md` + `knowledge.md`) keeps the workflow surface small enough that future-you and future-AI can absorb it in one read; the ADR directory grows organically without polluting that surface.
