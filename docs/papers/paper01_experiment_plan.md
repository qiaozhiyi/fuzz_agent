# Paper 1 — Experiment Plan and Run-Batch Recipe

**Target wall-clock to first arxiv submission**: 8–10 days on one Linux/x86_64 machine.
**All runs land under**: `results/paper01_ai_recipe_mutation/runs/<exp_id>/<repeat>/`
**All metadata via**: `scripts/capture_run_metadata.sh`

---

## Experiment 1 — cJSON: baseline-afl vs full-agent

Purpose: F3 (coverage-over-time), F4 (time-to-recover after plateau), most of T1.

| Field | Value |
|---|---|
| Target | `cjson_parser` |
| Modes | `baseline-afl`, `full-agent` |
| Repeats per mode | 5 |
| Per-run budget | 4 hours wall-clock |
| Seed corpus | `experiments/targets/cjson/seeds/` (pin tarball) |
| Total machine time | 2 × 5 × 4 h = **40 core-hours**; parallelize on 4 cores → ~10 hours |
| Fresh out-dir | yes, per run |
| Capture | `run_metadata.json`, AFL++ `fuzzer_stats`, `events.jsonl`, `agent_decisions.jsonl` for full-agent only |

Run-id format: `p1_e1_cjson_<mode>_r<NN>`.

Sanity checks before kicking off the batch:
- AFL++ commit pinned; binary hash recorded.
- `cjson_parser` rebuilt on the eval machine; sanitizer settings matching across modes.
- `full-agent` config: temperature pinned, model name pinned, schema validation on.
- Dry-run smoke (15 min) on each mode to catch crashes-on-startup.

## Experiment 2 — cJSON ablations: rule-only and no-mutator

Purpose: support RQ1 claim ("LLM matters") and isolate the mutator's contribution.

| Field | Value |
|---|---|
| Target | `cjson_parser` |
| Modes | `rule-only`, `no-mutator` |
| Repeats | 3 each |
| Budget | 4 h per run |
| Machine time | 2 × 3 × 4 h = **24 core-hours** → ~6 h on 4 cores |

Result rolls into T1 (extra rows) and an extra panel of F3.

## Experiment 3 — Mutator-only micro-benchmark (no AFL++ loop)

Purpose: F5, the throughput claim. **Critical for the "off-hot-path" pitch.**

Three configurations, ranked by mutator overhead:

1. **Vanilla AFL++ mutator** (no FuzzPilot mutator loaded).
2. **FuzzPilot mutator with empty recipe store** — measures dispatch overhead.
3. **FuzzPilot mutator with an active, populated recipe** — measures real-world overhead.

For each: feed the mutator a fixed corpus of 10k seeds, time N=100k mutation calls, repeat 5 times, report mean ± stddev exec/sec. No AFL++ instrumentation, no target execution — this is a pure mutator microbench.

Machine time: <1 hour total. Tooling: small driver under `tools/` that loops `afl_custom_fuzz()`.

If this benchmark doesn't exist yet, **build it before any other experiment** — it is one day of work and de-risks the whole paper.

## Experiment 4 — Case-study selection

Purpose: T2 and §7.

After Experiment 1 completes, scan the 5 `full-agent` runs for one where:
- a plateau triggered an agent proposal,
- the proposal passed schema validation,
- the micro-campaign reward was positive,
- the promotion produced at least one new path within ~10 min of promotion.

Extract from that run:
- the blackboard JSON at proposal time
- the agent decision JSON
- the recipe (op sequence)
- the micro-campaign telemetry (paths/edges delta)
- the main-run coverage curve around the promotion timestamp

These five artifacts populate T2 and Figure 6 (small inline timeline).

No new compute required — this is post-hoc selection.

## Optional Experiment 5 — libpng smoke

Only if Experiments 1–4 finish ahead of schedule.

1 repeat of `baseline-afl` and 1 of `full-agent` on `libpng_parser`, 4 h each. **Do not** include in main RQ tables; mention as "additional data point" in §6 or §9 to soften the single-target threat.

Machine time: 8 core-hours.

## Batch driver — script outline

Write `scripts/paper01/run_batch.sh`:

```text
inputs:  --exp <e1|e2|e3>   --repeats N   --budget-hours H   --out results/paper01_ai_recipe_mutation/runs
behavior: for each (mode, repeat), capture metadata, launch run with fresh out-dir,
          tee logs, on exit copy AFL++ fuzzer_stats + events.jsonl + agent_decisions.jsonl
          into the run dir, then run scripts/paper01/post_run_summary.py.
```

And `scripts/paper01/aggregate.py`:
- reads every run's `fuzzer_stats` and `events.jsonl`,
- emits the rows of T1 as CSV,
- emits per-run coverage CSV at 60-second resolution for F3,
- emits a JSON with plateau events for F4 and the case-study scan.

And `scripts/paper01/plots.py`:
- consumes the aggregated CSV/JSON,
- writes F3, F4, F5 as PDFs into `results/paper01_ai_recipe_mutation/figures/`.

These three scripts are the only new automation work needed for the preprint.

## Day-by-day schedule (single machine, 4 cores available)

| Day | Work |
|---|---|
| 1 | Build mutator micro-bench driver; run Experiment 3; build aggregate/plots scripts on its output. |
| 2 | Pin AFL++ + target builds; record hashes; smoke-test all 4 modes on cJSON for 15 min each. |
| 3–4 | Launch Experiment 1 batch (10 runs × 4 h, 4-way parallel → ~10 h). |
| 5 | Launch Experiment 2 batch (6 runs × 4 h → ~6 h). |
| 6 | Run aggregate + plots; pick case study; assemble T1/T2/F3/F4. |
| 7–8 | Draft §3, §6, §7 with real figures; T3 related-work table. |
| 9 | Draft §1, §2, §8, §9; revise abstract. |
| 10 | Internal proofread; finalize repro tarball; submit to arxiv (cs.SE primary, cs.CR cross-list). |

## Pre-flight checklist before pressing "Submit on arXiv"

- [ ] Every figure regenerable from `scripts/paper01/plots.py` against artifacts in `results/paper01_ai_recipe_mutation/`.
- [ ] `validity_report.md` shows zero missing expected runs for the **preprint-scoped** manifest (write a new manifest at `experiments/manifests/paper01_preprint.yaml` listing only the 16 runs above).
- [ ] Git commit hash referenced in §6 matches a tag, e.g. `paper01-arxiv-v1`.
- [ ] Repro Docker image pushed and referenced by digest.
- [ ] All `agent_decisions.jsonl` scrubbed for API keys (you've had this issue before — `git log` shows several Sentinel commits for it).
- [ ] arXiv categories: `cs.SE` primary, `cs.CR` cross-list. License: CC-BY 4.0 unless you plan to submit to a venue that forbids it.
