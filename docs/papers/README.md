# Paper Roadmap (3 papers from FuzzPilot)

Three-paper plan derived from the current state of the repo and the related-work scan (HyLLfuzz, LLAMAFUZZ, Semantic-Aware Fuzzing, Hybrid-LLM-Mutation, Staczzer, MultiFuzz).

| # | Paper | Form | Status | Plan doc |
|---|---|---|---|---|
| 1 | FuzzPilot: Off-Hot-Path LLM Control for AFL++ | arXiv preliminary, ~8 pages | **In progress — outline + experiment plan ready** | [paper01_arxiv_placeholder.md](paper01_arxiv_placeholder.md), [paper01_experiment_plan.md](paper01_experiment_plan.md) |
| 2 | When Should an LLM Intervene? Empirical Study | full empirical paper, ~10–12 pages | Planned | [paper02_when_to_intervene_plan.md](paper02_when_to_intervene_plan.md) |
| 3 | FuzzPilot Full Evaluation (venue version) | conference, 12–13 pages | Planned | [paper03_full_venue_plan.md](paper03_full_venue_plan.md) |

## What ships when

- **Now → +10 days**: Paper 1 on arXiv. Locks the design contribution before
  similar work lands. Requires only Experiments 1–4 from
  [paper01_experiment_plan.md](paper01_experiment_plan.md).
- **+3 months**: Paper 2 submission. Requires ~4 weeks of CLI work
  (`ai-direct`, `random-recipe`, trigger-policy switches, edge telemetry,
  shadow-run harness, cost accounting) plus 5 targets and the RQ sweeps.
- **+6 months**: Paper 3 submission. Requires crash dedup pipeline,
  head-to-head baselines, full 7-mode × 6–10-target × 10-repeat matrix, and
  bug disclosure.

## Sequencing logic

1. Paper 1 first because **arXiv has no review gate**. Filing it now plants a
   citation flag for the off-hot-path + recipe + micro-campaign design before
   adjacent work (HyLLfuzz already exists; more is coming).
2. Paper 2 second because it **reuses Paper 1's system unchanged**, just adds
   measurement infrastructure. Highest data-to-engineering ratio of the three.
3. Paper 3 last because it carries the most expensive compute and the
   crash-dedup pipeline that doesn't exist yet.

## Single biggest risk across all three

Reproducibility of head-to-head numbers against HyLLfuzz / LLAMAFUZZ /
Semantic-Aware Fuzzing in Paper 3. Mitigation: start Docker-pinning the
baselines now (during Paper 1 engineering downtime), not when Paper 3 writing
starts.

## File map

```
docs/papers/
├── README.md                              ← this file
├── paper01_arxiv_placeholder.md           ← Paper 1 outline + writing order
├── paper01_experiment_plan.md             ← Paper 1 runs, scripts, schedule
├── paper02_when_to_intervene_plan.md      ← Paper 2 RQs, matrix, build-out
└── paper03_full_venue_plan.md             ← Paper 3 targets, ablations, stats
```
