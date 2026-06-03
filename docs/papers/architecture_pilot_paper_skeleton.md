# FuzzPilot Architecture Pilot Paper Skeleton

This skeleton turns the architecture pilot into a paper draft plan. Use it with
`architecture_pilot_runbook.md` for execution and
`architecture_pilot_claim_matrix.md` for claim-to-evidence interpretation. Use
`architecture_pilot_related_work.md` when writing the related-work and design
rationale sections.

## Working Title

FuzzPilot: Off-Hot-Path Semantic Multi-Agent Control for Greybox Fuzzing

## One-Sentence Claim

FuzzPilot separates AFL++ mutation, semantic LLM planning, and bounded
micro-campaign validation so LLM guidance can influence greybox fuzzing without
putting model latency or unchecked proposals on the mutation hot path.

## Research Questions

| RQ | question | primary comparison | primary evidence | stage needed |
|---|---|---|---|---|
| RQ1 | Does the full architecture improve final path discovery under equal budgets? | `full-agent / baseline-afl` | `Architecture Claim Effect Sizes`, `Throughput Ratios` | B for pilot, C for main |
| RQ2 | Does static semantic grounding contribute beyond telemetry-only LLM control? | `full-agent / no-static-analysis`, `full-agent / no-semantic-context` | effect-size rows, static context metadata, schema-valid/fallback rates | B/C |
| RQ3 | Does micro-campaign validation filter proposals better than direct promotion? | `full-agent / ai-direct` | effect-size row, `Promotion Impact Windows`, validated/direct promotion counts | B/C |
| RQ4 | Does multi-agent decomposition add value over a single specialist/planner? | `full-agent / single-agent-coordinator`, `full-agent / single-agent-dictionary` | effect-size rows, `LLM Accounting` | B/C |
| RQ5 | Can the system keep fuzzing throughput within an acceptable envelope? | `full-agent / baseline-afl` on `execs_per_sec` | `Throughput Ratios`, runner metadata, acceptance gates | A for smoke, B/C for claims |
| RQ6 | Are semantic proposals reliable enough to be audited and reproduced? | LLM-bearing modes versus failed/fallback decisions | schema-valid rate, auth/fallback/error kinds, replay payload hashes, promotion artifacts | B/C |

Stage A can only show that the matrix runs end-to-end. Do not use Stage A to
claim effectiveness. Stage S is infrastructure only.

## Paper Structure

| section | purpose | required evidence |
|---|---|---|
| 1. Introduction | State the evaluation problem: LLMs are useful for semantic reasoning but dangerous in the fuzzing hot path. Present FuzzPilot's three-plane answer. | Motivation figure, one compact result from Stage B/C if available |
| 2. Background and Related Work | Position against LLM fuzzing, semantic fuzzing, multi-agent fuzzing, and prudent fuzzing evaluation. | `architecture_pilot_related_work.md`, related-work pressure from `architecture_pilot_claim_matrix.md` |
| 3. System Architecture | Define data plane, semantic control plane, and validation plane. Emphasize boundaries and invariants. | Architecture diagram, event/metadata examples |
| 4. Implementation | Explain static context loading, agent proposal format, recipe interface, micro-campaign ranking, and runner controls. | Code-level references and generated metadata fields |
| 5. Experimental Design | Define targets, modes, stages, blocked ordering, CPU slots, API-key handling, and acceptance gates. | Manifest, runbook, preflight output, status JSON |
| 6. Results | Answer RQ1-RQ5 using generated report sections. | Acceptance report, CSV summary, effect-size table |
| 7. Discussion | Interpret weak/strong results, cost, threat model, and why this is architecture evidence rather than a model benchmark. | LLM accounting, promotion windows, failure cases |
| 8. Threats to Validity | Cover target count, local host size, AFL++ randomness, API latency, model drift, static extraction quality, runtime host noise, and no crash-disclosure pipeline. | Metadata gates, runtime noise summary, and explicit non-goals |
| 9. Conclusion | Restate the architecture contribution and what remains for venue-scale evaluation. | Stage C if available, otherwise Stage B pilot framing |

## Figures and Tables

| artifact | type | source | use |
|---|---|---|---|
| F1 Three-plane architecture | figure | system design and controller flow | Introduction/System Architecture |
| F2 Experiment scheduling policy | figure or table | `paper_architecture_pilot.yaml`, runner metadata | Experimental Design |
| T1 Stage and target matrix | table | manifest + status script | Experimental Design |
| T2 Acceptance gates | table | `stage_*_acceptance.md` `Gates` | Results validity before claims |
| T3 Architecture claim effect sizes | table | `Architecture Claim Effect Sizes` | Main RQ1-RQ4 results |
| T4 Throughput ratios | table | `Throughput Ratios` | RQ5 overhead |
| T5 LLM accounting | table | `LLM Accounting` | Cost and reliability |
| T6 Promotion impact windows | table | `Promotion Impact Windows` | Mechanism evidence for RQ3 |
| T7 LLM reliability and replayability | table | `agent_decisions.jsonl`, replay hashes, error/fallback counters | RQ6 and reproducibility |
| A1 Runner metadata checklist | appendix table | `runner_metadata.json` | Reproducibility |
| A2 Runtime noise summary | appendix table | `*_noise_summary.json` | Host stability during long stages |

The paper should not introduce hand-computed tables when the summary script
already emits canonical tables. If a new paper table is needed, add it to
`architecture_pilot_summary.py` or to a small export script so the table is
reproducible.

## Result-Driven Writing Rules

- If Stage B/C gates fail, write only about infrastructure lessons and do not
  claim architecture effectiveness.
- If `full-agent / baseline-afl` is positive but throughput is below the gate,
  frame the result as a control-plane prototype and fix overhead before a
  venue submission.
- If semantic ablations are flat, keep the architecture claim but narrow static
  grounding to an observability/debugging contribution until better extractors
  are added.
- If `ai-direct` beats `full-agent`, the validation plane is not yet tuned; the
  paper can still discuss the need for validation but cannot claim it helps.
- If single-agent modes beat `full-agent`, claim a modular proposal interface,
  not that eight agents are inherently better.
- If `no-mutator` is close to `full-agent`, move the recipe interface from a
  primary contribution to an implementation mechanism.

## Related-Work Positioning

Use recent LLM-fuzzing papers as pressure tests:

- Off-hot-path design answers systems where model latency can perturb fuzzing.
- Static semantic grounding answers work showing target context improves LLM
  proposals.
- Micro-campaign validation answers work where LLM output is generated but not
  independently stress-tested before use.
- Multi-agent decomposition should be presented as proposal diversity plus a
  common AFL++ judge, not as an agent-count result.
- The low-noise protocol should be explicit because LLM/API costs, CPU
  contention, and fuzzing nondeterminism can otherwise create false effects.
- FuzzingBrain V2 and FuzzAgent should be used as the strongest contrast: they
  pursue end-to-end vulnerability or harness workflows, while FuzzPilot isolates
  campaign-control architecture on a fixed libxml2 harness.

## Minimum Submission Bundle

Before drafting the results section, collect:

- `experiments/manifests/paper_architecture_pilot.yaml`
- `results/fuzzpilot_architecture_pilot/frozen_inputs/stage_b_inputs.json` or
  `stage_c_inputs.json`
- `stage_b_acceptance.md` or `stage_c_acceptance.md`
- `stage_b_summary.csv` or `stage_c_summary.csv`
- wrapper `*_stage_plan.json` for the cited stage
- all corresponding `*_status.json` files
- all per-cell `runner_metadata.json` files
- preflight logs for the cited stage
- runtime `*_noise_summary.json` for the cited stage
- exact git revision, dirty diff hash, target binary hashes, model fingerprint,
  and static context hashes from metadata
- the frozen manifest and static context used before the cited stage started

Do not cite a stage if any bundle item is missing.
