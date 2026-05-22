# Paper 2 — *When Should an LLM Intervene?*
## An Empirical Study of Plateau-Triggered LLM Guidance in Greybox Fuzzing

**Form**: full empirical-study paper (~10–12 pages double-column).
**Venue target**: ISSTA / ICSE-SEIP / EMSE journal (empirical-friendly).
**Differentiator from Paper 1**: Paper 1 says *the system works*; Paper 2 asks *when does invoking the LLM actually pay off, and what is the cost structure*.
**Why this paper exists**: FuzzPilot is uniquely positioned to study **trigger policy** because the LLM is decoupled from the mutator — you can vary trigger conditions, validation thresholds, and recipe lifetimes without retraining or replumbing.

---

## Core thesis

The question of *whether* to put an LLM in a fuzzer has been studied; the question of *when* and *how cheaply* has not. We quantify the trade-off curve between intervention frequency, model cost, and coverage gain, and we identify a regime where bounded, plateau-triggered LLM guidance dominates both "no LLM" and "LLM-every-window".

## Research questions

- **RQ1 — Trigger policy**: How does plateau-trigger sensitivity (early / median / late) affect coverage and cost compared with fixed-interval triggers and never-trigger?
- **RQ2 — Validation strictness**: Does the micro-campaign validation step measurably filter bad proposals? What is the precision/recall of validation vs. ground-truth post-hoc success on the main run?
- **RQ3 — Recipe lifetime**: How long should a promoted recipe stay active before it is retired? Is there a coverage cliff after N seconds of recipe staleness?
- **RQ4 — Model substitutability**: How sensitive are gains to model choice (small open-weights vs. frontier vs. rule-only baseline)? Where does the marginal $ per new edge curve bend?
- **RQ5 — Cost accounting**: At what hourly fuzzing budget does LLM cost exceed cloud compute cost for an equivalent number of extra AFL++ cores?

## Variables and matrix

Independent variables (controlled, not all crossed):

| Axis | Levels |
|---|---|
| Trigger policy | `never`, `fixed-30min`, `plateau-low-sensitivity`, `plateau-default`, `plateau-high-sensitivity` |
| Validation gate | `off (ai-direct)`, `micro-campaign default`, `micro-campaign strict (top-1)` |
| Recipe lifetime | `oneshot`, `1×budget`, `2×budget`, `until-replaced` |
| Model | `none`, `deepseek-r1-distill`, `gpt-4o-mini`, `claude-haiku`, `claude-sonnet`, optionally a local 7B |
| Target | cJSON, libpng, libxml2, re2, sqlite3 (5 targets — bigger than Paper 1) |
| Repeats | 5 per cell |

Don't cross everything (would be ~5×3×4×6×5×5 = 4500 runs). Use a **fractional design**:

1. Lock all axes at default, sweep one axis at a time per RQ.
2. For RQ5, additionally do a Pareto sweep across (model, trigger) at recipe-lifetime default.

Estimated total runs: ~200–300 × 4 h = **800–1200 core-hours**. Budget two weeks on an 8-core box or three days on a 32-core box.

## Dependent variables

- coverage-over-time (median, IQR)
- bitmap_cvg at fixed checkpoints (1h, 2h, 4h)
- time-to-first-new-path after plateau
- proposals/hour, promotions/hour
- schema_valid rate, fallback rate
- model latency p50/p95, cost per run, cost per new edge
- micro-campaign precision (= fraction of validated proposals that actually help on main) and recall (= fraction of "would-have-helped" proposals that pass validation) — computed by post-hoc shadow runs

## What needs to be built before Paper 2 can start

Most of these don't exist yet in the current CLI:

1. **First-class trigger policy switches** (`--trigger plateau|fixed|never`, plus sensitivity knobs).
2. **`ai-direct` ablation mode** (skip micro-campaign, promote proposal immediately). Already flagged as missing in `validity_report.md`.
3. **`random-recipe` baseline** (proposal sampled from a uniform recipe space).
4. **Cost accounting** in `agent_decisions.jsonl` (tokens in/out, model identity, $ per call). Partially present; needs to be unified across providers.
5. **Edge counts**, not just bitmap_cvg, in telemetry. Same gap noted in `paper_tables.md`.
6. **Shadow-run harness** for the validation precision/recall analysis (RQ2): replays the main run forward with and without each proposal at the time it appeared. This is non-trivial — at least 1 week of engineering.
7. **More targets**: libxml2, re2, sqlite3 harnesses on top of the cJSON/libpng baseline. ~3–5 days of harnessing.
8. **Statistical testing**: Mann–Whitney + Vargha–Delaney Â̂₁₂ utilities, plus a tooling pass to make Paper-1-style box plots with significance annotations.

**Engineering effort gate**: ~4 weeks of focused work before the first paper-2 experiment can launch.

## Headline figures and tables to plan for

| # | Type | RQ |
|---|---|---|
| F1 | Pareto frontier of (cost $/hr, edges-at-4h) across trigger policies × models | RQ1, RQ4, RQ5 |
| F2 | Coverage-over-time small multiples per target × trigger policy | RQ1 |
| F3 | Confusion-matrix-style precision/recall of micro-campaign validation gate | RQ2 |
| F4 | Recipe-lifetime cliff plot: new-paths-per-minute vs. seconds-since-promotion | RQ3 |
| T1 | Per-target per-mode results, 5 targets × 5 modes, with significance markers | all |
| T2 | Cost per new edge across models, per target | RQ4, RQ5 |
| T3 | Trigger-policy budget table (proposals/hr, promotions/hr, cost/hr, new edges/hr) | RQ1, RQ5 |

## Risks and how to retire them

- **Risk: results dominated by one target.** Mitigation: ensure 5 targets and report per-target tables, not just averages.
- **Risk: micro-campaign precision/recall analysis is noisy.** Mitigation: state the limitation, report wide CIs, do at least 5 repeats of the shadow run.
- **Risk: model costs make 5-model sweep expensive.** Mitigation: budget cap per RQ4 cell ($X / target / repeat); if exceeded, narrow to 3 models.
- **Risk: scope creep — this paper grows to 14 pages and 6 RQs.** Mitigation: hard-cut at RQ1–RQ4 if RQ5 turns out shaky. RQ5 can move to a §discussion.

## How Paper 2 cites Paper 1

Paper 2 cites the arxiv preprint as "[FuzzPilot, FOO 2026]" and uses it as the system-of-study without re-explaining the architecture. Section 2 of Paper 2 is "FuzzPilot in 1 page", then everything else is empirical study. This keeps Paper 2's contribution clearly distinct: *measurements over a known system*.

## Schedule sketch (after Paper 1 is on arxiv)

| Week | Work |
|---|---|
| 1–2 | Build ai-direct / random-recipe / trigger-policy CLI; add edge counts to telemetry. |
| 3 | Add cost accounting; build shadow-run harness. |
| 4 | Harness libxml2, re2, sqlite3; smoke test all 5 targets across all modes. |
| 5–7 | Run RQ1, RQ3 sweeps on 5 targets (~400 core-hours total). |
| 8 | Run RQ2 shadow analysis. |
| 9 | Run RQ4 model sweep (cost-capped). |
| 10 | Analysis, plots, statistical tests. |
| 11 | Write paper. |
| 12 | Internal review + venue submission. |

Total: ~3 months wall-clock after Paper 1 ships, assuming one full-time engineer-equivalent.
