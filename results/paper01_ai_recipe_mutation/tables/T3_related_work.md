# T3 — Related-work matrix (Paper 1)

Positioning table for §8. Built before §8 prose per the writing order in
`docs/papers/paper01_arxiv_placeholder.md`. Each row is one published
LLM-augmented or LLM-adjacent fuzzer; columns are the five axes that
distinguish FuzzPilot's design.

Cell values:
- Y / N for boolean axes
- `?` when the source is ambiguous (re-check before submission)
- "n/a" when the axis does not apply to the row

| System | LLM in hot path? | Validates proposal before use? | Recipe abstraction? | Auditable decision log? | Open source? |
|---|:---:|:---:|:---:|:---:|:---:|
| AFL++ baseline (no LLM) | n/a | n/a | N | partial (fuzzer_stats only) | Y |
| ChatFuzz (Chen et al., 2023) | Y (model emits mutations) | N | N | N | Y |
| LLAMAFUZZ (Sun et al., 2024) | Y (model emits mutations) | N | N | N | Y |
| HyLLfuzz (2024) | Y (hybrid: model + native) | N | partial (templates) | N | Y |
| Semantic-Aware Fuzzing (arXiv:2509.19533) | Y (per-input semantic guidance) | N | N | N | ? |
| Hybrid Fuzzing w/ LLM Mutation (arXiv:2511.03995) | Y | N | N | N | ? |
| Staczzer | N (LLM offline for harness gen) | n/a | N | N | partial |
| MultiFuzz | Y (multi-objective LLM steering) | N | N | N | ? |
| **FuzzPilot (this work)** | **N** (plateau-triggered planner) | **Y** (micro-campaign N-sec run) | **Y** (schema-validated op list) | **Y** (blackboard + decisions + recipes + micro rewards) | **Y** |

## Notes for prose (§8)

- The "LLM in hot path?" column is the headline differentiator — every
  prior LLM-augmented fuzzer puts the model on the mutation hot path or
  one step away. FuzzPilot is the only row that is structurally "N".
- "Validates proposal before use?" is the second differentiator. Even
  systems that batch mutations do not run a separate scoring campaign
  before letting a proposal touch the main run.
- "Recipe abstraction" — the closest analogue is HyLLfuzz's template
  mechanism. Recipes go further: schema-validated structured ops the
  native mutator dispatches without re-parsing model output.
- "Decision log" is a reproducibility axis, not a coverage axis; cite
  the artifact tarball in §6 footnote when referencing this column.

## Citation TODO (lock before submission)

- [ ] ChatFuzz — confirm citation key + venue (USENIX Security '23?)
- [ ] LLAMAFUZZ — find arXiv ID
- [ ] HyLLfuzz — find arXiv ID / venue
- [ ] Semantic-Aware Fuzzing — arXiv:2509.19533, confirm authors
- [ ] Hybrid Fuzzing w/ LLM Mutation — arXiv:2511.03995, confirm authors
- [ ] Staczzer — confirm spelling + venue
- [ ] MultiFuzz — confirm citation key (multiple "MultiFuzz" systems exist)
- [ ] Replace `?` cells with Y/N after one more reading of each paper
