# Paper 1 — FuzzPilot: Off-Hot-Path LLM Control for AFL++ Campaigns

**Form**: arXiv preliminary report, ~8 pages (single column) or 6 pages (double column).
**Status flag for arxiv**: "Preliminary results; full evaluation in progress."
**Goal**: claim the design contribution (off-hot-path controller + recipe-guided mutator + micro-campaign validation + auditable agent decisions) before related work lands.

---

## Working title candidates

1. *FuzzPilot: Off-Hot-Path LLM Control for AFL++ via Recipe-Guided Mutation and Micro-Campaign Validation*
2. *Decoupling Planning from Mutation: An Off-Path LLM Controller for Greybox Fuzzing*
3. *When the Fuzzer Stalls: Plateau-Triggered, Audit-Logged LLM Guidance for AFL++*

Pick #1 for the preprint. It states the system name (good for citation), the platform (AFL++), and the two technical pillars in one breath.

## One-paragraph abstract (draft)

> Recent work places large language models inside the mutation loop of greybox
> fuzzers, paying a steep throughput cost for every model call. We argue that
> the model belongs **off the hot path**: invoked only at plateau boundaries to
> propose strategy, never to mutate bytes. We present FuzzPilot, an AFL++
> controller built around three ideas: (i) a **recipe abstraction** that
> compiles model proposals into structured guidance consumed by a native AFL++
> custom mutator; (ii) a **micro-campaign protocol** that validates each
> proposal in a short isolated AFL++ run before promoting it; and (iii) an
> **audit trail** of blackboards, agent decisions, and recipe outcomes that
> makes every intervention reproducible after the fact. On cJSON, FuzzPilot
> recovers from coverage plateaus faster than AFL++ alone while keeping the
> mutator's native exec/sec within X% of vanilla AFL++. We release the system,
> configurations, and decision logs to support reproduction.

(Replace X% once mutator micro-benchmark is in.)

## Page budget (target 8 pages, double-column-ish)

| § | Section | Pages | Notes |
|---|---|---|---|
| 1 | Introduction | 1.0 | motivate off-hot-path, 3 contributions |
| 2 | Background & Threat to Throughput | 0.5 | AFL++ loop, why every-call LLM is bad |
| 3 | FuzzPilot Design | 2.0 | architecture figure, the three pillars |
| 4 | Recipe Abstraction & Native Mutator | 1.0 | grammar of recipes, op table |
| 5 | Plateau Detection & Micro-Campaign Protocol | 1.0 | trigger logic, reward, promotion |
| 6 | Preliminary Evaluation | 1.5 | RQ1–RQ3 results on cJSON |
| 7 | Case Study | 0.5 | one plateau breakthrough end-to-end |
| 8 | Related Work | 0.5 | side-by-side table |
| 9 | Limitations, Threats, Future Work | 0.25 | honest about scope |
| 10 | Conclusion | 0.25 | |

## Contributions (state explicitly in §1)

1. A **system architecture** that confines LLM calls to planning windows between AFL++ campaigns, preserving native mutator throughput.
2. A **recipe abstraction + native AFL++ custom mutator** that consumes structured strategy without re-entering the model.
3. A **micro-campaign validation + promotion protocol** that filters bad LLM proposals empirically before they affect the main run.
4. An **end-to-end auditable decision log** (blackboard → proposal → recipe → micro-campaign reward → promotion) and a public artifact.
5. Preliminary evidence on cJSON that the design recovers from plateaus without degrading mutator throughput.

## Research questions (preliminary)

- **RQ1**: Does FuzzPilot recover from coverage plateaus faster than AFL++ baseline under equal wall-clock budget on cJSON?
- **RQ2**: Does the FuzzPilot mutator keep AFL++ end-to-end throughput within a small range of vanilla AFL++ (E1a vs E1b execs_per_sec parity), and does recipe lookup add measurable cost over empty dispatch (F5 microbench, fp-active vs fp-empty)?
- **RQ3**: How often do plateau-triggered proposals get promoted, and what does the decision trail look like for a successful breakthrough?

Note: RQ on multi-target generalization, full ablation matrix, and crash dedup are **deferred to the full paper** (Paper 3). State this clearly in §1 and §9.

## Figures and tables (8 artifacts)

| # | Type | Content | Source |
|---|---|---|---|
| F1 | Diagram | FuzzPilot architecture: AFL++ ↔ telemetry ↔ plateau ↔ blackboard ↔ agents ↔ recipe store ↔ mutator | hand-drawn |
| F2 | Diagram | Recipe lifecycle: proposal → micro-campaign → reward → promote/discard | hand-drawn |
| F3 | Plot | cJSON coverage-over-time, baseline-afl vs full-agent, 5 repeats, median + IQR | exp 1 |
| F4 | Plot | cJSON time-to-recover after first plateau, boxplot, baseline vs full-agent | exp 1 |
| F5 | Plot | Mutator-only exec/sec micro-benchmark (dispatch-symmetric): all three configs dlopen a mutator .so. `vanilla` = `libvanilla_havoc.so` (AFL++ havoc-equivalent ops with matched cost profile, see `tools/mutator_microbench/vanilla_havoc.cpp`); `fp-empty` = FuzzPilot mutator with empty recipe store; `fp-active` = FuzzPilot mutator with populated recipe. Primary claim: fp-active vs fp-empty within 5% (recipe is free). Sanity: fp-empty within an order of magnitude of vanilla (FuzzPilot pays a ~3x dispatch cost for telemetry+cache bookkeeping, expected). End-to-end throughput parity (the real RQ2 number) comes from §6.2's E1a vs E1b exec/sec ratio, not F5. | exp E3 |
| T1 | Table | Per-run summary: paths, bitmap_cvg, exec/sec, plateau events, proposals, promotions, schema_valid rate, fallback rate | exp 1 |
| T2 | Table | Decision-trail walk-through for one successful breakthrough (blackboard summary → proposal → recipe ops → micro reward → main-run effect) | exp 5 case study |
| T3 | Table | Related-work comparison across (LLM placement, mutator hot-path call, validation step, audit log, open source) | manual |

## Related-work positioning (write T3 first, then §8 from it)

Columns: *LLM in hot path?* / *Validates proposal before use?* / *Recipe abstraction?* / *Decision log?* / *Open source?*

Rows: AFL++ baseline, ChatFuzz, LLAMAFUZZ, HyLLfuzz, Semantic-Aware Fuzzing (arXiv:2509.19533), Hybrid Fuzzing w/ LLM (arXiv:2511.03995), Staczzer, MultiFuzz, **FuzzPilot**.

This table is the single most defensible thing in the paper. Build it before writing prose.

## Sentence skeletons for the three pillars (drop into §3)

- **Off-hot-path contract**: "FuzzPilot enforces a single invariant: no model call appears on any code path executed by AFL++ during seed mutation. Model calls occur only when (a) the controller detects a plateau, or (b) a micro-campaign has completed and a promotion decision is required."
- **Recipe abstraction**: "A recipe is a compact, schema-validated description of structured edits — opcode sequences, dictionary inserts, splice targets, byte-range constraints — that the native mutator can dispatch on without parsing free-form model output."
- **Micro-campaign protocol**: "Before any proposal touches the main campaign, FuzzPilot launches an N-second AFL++ run from a corpus snapshot with the proposed intervention installed, scores it by a reward function over new paths and edges, and promotes only the top-K interventions."

## Honest limitations to surface in §9

- Single target family (cJSON) in the preprint; libpng pending.
- Repeat count modest (5×); no formal significance test in this version.
- Crash deduplication pipeline not yet in scope; we report coverage and plateau-recovery, not unique-bug counts.
- Two ablation modes (`random-recipe`, `ai-direct`) not yet wired in CLI; deferred.
- macOS/arm64 is development only; numbers are Linux/x86_64.
- LLM nondeterminism: we pin temperature and seed where possible and report schema-validity / fallback rates rather than relying on model identity alone.

## Threats to validity (§9)

- **Internal**: plateau detector hyperparameters tuned on cJSON could leak into "improvement".
- **External**: one target family is not representative; explicitly defer generalization.
- **Construct**: bitmap_cvg ≠ edges; we report both where available and discuss the gap.
- **Reproducibility**: pinning AFL++ commit, target build hash, config hash, and capturing `git.patch` per run.

## Reproducibility checklist (cite in §1 footnote and §6)

- [ ] GitHub commit hash at paper time
- [ ] AFL++ version + build flags
- [ ] Target build script + binary SHA256
- [ ] Seed corpus archive (tarball checksum)
- [ ] Docker image tag for the eval environment
- [ ] One `run_metadata.json` per run, all under `results/paper01_ai_recipe_mutation/`
- [ ] Agent decision logs and blackboards for the case study
- [ ] Plot-generating scripts in `scripts/paper01/`

## Writing order (most efficient sequence)

1. Build T3 (related-work table) — locks positioning.
2. Draft §3 architecture and F1 figure — the contribution lives here.
3. Run experiment 1 + 4 (see experiment plan) — gives F3/F4/F5/T1.
4. Pick the case study from exp 1 logs — gives T2 and §7.
5. Write §6 around the figures you actually have.
6. Write §1, §2 last (now that you know what you're claiming).
7. Final pass: §8 related work, §9 limitations, abstract.

## Don't-do list for the placeholder version

- Don't claim multi-target generalization.
- Don't report unique-bug counts.
- Don't compare against LLAMAFUZZ / HyLLfuzz numerically — say "qualitative comparison" and put it in T3.
- Don't run statistical significance tests on N=5 and try to look serious.
- Don't bury the off-hot-path claim — it must be in the abstract sentence 1.
