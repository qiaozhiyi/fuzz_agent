# FuzzPilot Architecture Pilot Related-Work Map

Verified on 2026-06-03 from primary venue/arXiv pages. This document records how
recent LLM-fuzzing and fuzzing-evaluation work shapes the architecture pilot.
It is not a survey. Its purpose is to keep the paper's novelty claim precise and
to ensure the experiment matrix answers the obvious reviewer objections.

## Sources

| work | source | pressure on FuzzPilot |
|---|---|---|
| FuzzingBrain V2: A Multi-Agent LLM System for Automated Vulnerability Discovery and Reproduction | arXiv 2605.21779, https://arxiv.org/abs/2605.21779 | Recent multi-agent vulnerability systems already combine static/dynamic tools with fuzzer-reproducible evidence. FuzzPilot must focus on its different unit of contribution: controlling an existing greybox campaign through off-hot-path semantic proposals and bounded validation, not on end-to-end vulnerability triage. |
| FuzzAgent: Multi-Agent System for Evolutionary Library Fuzzing | arXiv 2605.14431, https://arxiv.org/abs/2605.14431 | Multi-agent fuzzing is no longer novel by itself. FuzzPilot must claim a different system boundary: off-hot-path AFL++ control plus validation-gated recipe promotion. |
| FunFuzz: An LLM-Powered Evolutionary Fuzzing Framework | arXiv 2605.02789, https://arxiv.org/abs/2605.02789 | Diversity and evolutionary feedback matter. FuzzPilot should justify specialized agents as proposal diversity judged by AFL++ micro-campaigns, not as an agent-count claim. |
| MASFuzzer: Fuzz Driver Generation and Adaptive Scheduling via Multidimensional API Sequences | arXiv 2604.17977, https://arxiv.org/abs/2604.17977 | LLM-generated fuzzing artifacts need scheduling and coverage-guided prioritization. FuzzPilot's micro-campaign scheduler and `ai-direct` ablation must show validation is useful. |
| ELFuzz: Efficient Input Generation via LLM-driven Synthesis Over Fuzzer Space | USENIX Security 2025, https://www.usenix.org/conference/usenixsecurity25/presentation/chen-chuyang | LLM work can be moved outside the fuzzing hot path. FuzzPilot should make this an invariant and measure throughput overhead against AFL++. |
| MultiFuzz: A Dense Retrieval-based Multi-Agent System for Network Protocol Fuzzing | arXiv 2508.14300, https://arxiv.org/abs/2508.14300 | Retrieval and specialized agents can improve semantic fuzzing. FuzzPilot needs static-context and no-semantic-context ablations to isolate this layer. |
| Hybrid Fuzzing with LLM-Guided Input Mutation and Semantic Feedback | arXiv 2511.03995, https://arxiv.org/abs/2511.03995 | Static and dynamic semantic feedback is an active line. FuzzPilot should report semantic/context mechanism evidence separately from raw final coverage so the architecture claim is not reduced to a single score. |
| SeedAIchemy: LLM-Driven Seed Corpus Generation for Fuzzing | arXiv 2511.12448, https://arxiv.org/abs/2511.12448 | Seed quality can dominate fuzzing outcomes. FuzzPilot must keep target seed directories fixed and should not mix seed-generation claims into the architecture pilot. |
| ChatAFL: Large Language Model guided Protocol Fuzzing | NDSS 2024, https://www.ndss-symposium.org/ndss-paper/large-language-model-guided-protocol-fuzzing/ | LLMs can extract protocol structure, but direct reliance on model output raises reliability concerns. FuzzPilot should emphasize structured proposals, schema validation, and AFL++ validation before promotion. |
| FOX: Coverage-guided Fuzzing as Online Stochastic Control | arXiv 2406.04517, https://arxiv.org/abs/2406.04517 | Fuzzing can be framed as online control. FuzzPilot's micro-campaign ranking should be described as conservative control under bounded validation budget. |
| SoK: Prudent Evaluation Practices for Fuzzing | IEEE S&P 2024/arXiv 2405.10220, https://arxiv.org/abs/2405.10220 | Evaluation noise is a first-order threat. FuzzPilot needs repeats, fixed budgets, CPU affinity, run metadata, stage gates, and conservative interpretation. |

## Reviewer Objections and Required Evidence

| likely objection | required answer in paper | implemented evidence |
|---|---|---|
| "Multi-agent LLM fuzzing already exists." | The contribution is not multi-agent count. It is the three-plane architecture: AFL++ data plane, semantic control plane, and validation plane. | `architecture_pilot_paper_skeleton.md` RQs, `architecture_pilot_claim_matrix.md` main thesis, modes `single-agent-coordinator` and `single-agent-dictionary`. |
| "FuzzingBrain/FuzzAgent already have agents plus fuzzing evidence." | FuzzPilot studies a lower-level architectural question: whether an existing AFL++ campaign benefits from semantic control without putting model calls or unchecked outputs in the hot path. | `baseline-afl`, `ai-direct`, `no-semantic-context`, and throughput gates isolate campaign control from end-to-end harness generation or vulnerability triage. |
| "The LLM may just slow down AFL++." | Model calls stay off the mutation hot path and throughput is reported as a gated claim. | `Throughput Ratios`, `full-agent / baseline-afl` on `execs_per_sec`, fixed CPU slots in runner metadata. |
| "Static/Ghidra context may not matter." | Compare full semantic context against static-disabled and semantic-suppressed variants. | `no-static-analysis`, `no-semantic-context`, static context hashes in `runner_metadata.json`. |
| "LLM output is unreliable or hallucinated." | Agents emit structured decisions, schema validity is reported, and proposals need micro-campaign evidence before promotion. | `agent_decisions.jsonl`, schema-valid rate gates, `winner_decided`, validated promotions. |
| "Validation may waste time compared with direct use." | Compare validation-gated promotion against direct promotion under the same budget. | `ai-direct`, `Promotion Impact Windows`, `full-agent / ai-direct` effect-size row. |
| "Seed quality explains the result." | Initial corpora and dictionaries are fixed per target; seed generation is outside this paper. | Manifest target configs, preflight seed checks, no seed-generation mode in the architecture matrix. |
| "The result may be host noise." | Runs are staged, affinity-pinned, metadata-captured, and blocked by preflight load/memory gates. | `run_architecture_stage.sh`, `preflight_architecture_pilot.sh`, `architecture_stage_status.py`, `runner_metadata.json`. |

## Positioning Boundary

FuzzPilot should be positioned as an architecture for controlling an existing
coverage-guided fuzzer, not as an LLM-only fuzzer, harness generator, seed
generator, or protocol-specific mutator. The defensible novelty is:

1. LLM and static-analysis work is kept off the AFL++ mutation hot path.
2. Static target semantics are loaded before measured fuzzing and hashed in run
   metadata.
3. Multiple agents produce diverse structured proposals through a common recipe
   interface.
4. AFL++ micro-campaigns validate proposals before the main campaign sees them.
5. The experiment protocol treats LLM latency, host load, model drift, and
   fuzzing randomness as explicit threats rather than incidental noise.

## Experiment Implications

- Keep `baseline-afl` as the anchor for every effectiveness and throughput
  table.
- Keep `ai-direct` in the primary matrix; otherwise the validation-plane claim
  is untested.
- Keep both `single-agent-coordinator` and `single-agent-dictionary`; otherwise
  the paper cannot separate "many agents" from "one better planner."
- Keep `no-static-analysis` and `no-semantic-context` separate; otherwise
  Ghidra extraction and semantic prompt context are conflated.
- Do not add LLM seed-generation to Stage B/C. That would make the SeedAIchemy
  objection harder to answer and would change the paper's claim.
- Do not add harness generation or crash-triage automation to this paper's
  primary matrix. FuzzingBrain V2 and FuzzAgent make that a different, much
  larger claim. FuzzPilot should instead measure campaign-control mechanisms on
  the fixed libxml2 harness.
- Do not run Ghidra inside measured cells. Use precomputed static context only.
- Do not relax the preflight gates for paper evidence. If the machine is noisy,
  delay the run rather than capturing contaminated cells.

## Writing Guidance

Use this related-work framing in the paper as follows:

- Introduction: state that recent work shows LLMs help fuzzing, but the open
  systems question is how to integrate LLM reasoning without corrupting the
  fuzzing loop or the evaluation.
- Related Work: group papers by generated artifacts, semantic grounding,
  multi-agent coordination, online scheduling, and evaluation methodology.
- Design: argue that FuzzPilot composes these lessons into a controlled
  architecture around AFL++.
- Evaluation: present ablations as responses to the reviewer objections above,
  not as an arbitrary mode list.
