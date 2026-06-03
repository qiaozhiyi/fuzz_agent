# FuzzPilot Architecture Pilot Claim Matrix

This matrix maps the architecture paper's claims to the implementation surface,
controlled ablations, report tables, and interpretation rules. It is the writing
guide for turning Stage A/B/C artifacts into a paper argument. Use
`architecture_pilot_paper_skeleton.md` for the paper RQs, section plan, and
figure/table list. Use `architecture_pilot_related_work.md` for the primary
paper sources behind the related-work pressure column.

## Main Thesis

FuzzPilot is an architecture for LLM-assisted greybox fuzzing where model work is
kept off the mutation hot path, grounded in static target semantics, decomposed
across specialized agents, and validated by bounded AFL++ micro-campaigns before
promotion into the main campaign.

The paper should not claim that "LLMs fuzz better" in general. The defensible
claim is narrower: **semantic, off-hot-path, validation-gated LLM control can
improve or explain greybox fuzzing outcomes under controlled resource budgets.**

## Claim Matrix

| claim | related-work pressure | implementation evidence | ablation/comparison | primary report evidence | pass interpretation | fail interpretation |
|---|---|---|---|---|---|---|
| Off-hot-path semantic control | ELFuzz, prudent fuzzing evaluation | Model calls occur in controller windows; runner records throughput and CPU slots | `full-agent / baseline-afl` plus throughput ratio | `Architecture Claim Effect Sizes`, `Throughput Ratios`, runner metadata gates | Coverage/path gains without severe throughput collapse support the architecture | If throughput collapses, narrow the claim to observability/control and fix scheduling overhead |
| Static semantic grounding | ChatAFL, MultiFuzz, Hybrid Fuzzing, SeedAIchemy | `static_analysis.context_path` precomputes Ghidra/string context; hash recorded per run | `full-agent / no-static-analysis`, `full-agent / no-semantic-context` | claim effect-size rows, static context metadata, schema-valid/fallback rates | Positive paired ratios suggest static and semantic context are useful | Null results mean Ghidra/context is not yet producing useful target-specific tokens |
| Micro-campaign validation before promotion | MASFuzzer, FOX, prudent evaluation | Validated `winner_decided` and `promotion` events; direct-promotion bypass exists | `full-agent / ai-direct` | `Promotion Impact Windows`, validated promotion rate, claim effect-size row | `full-agent` beating `ai-direct` supports validation as a filter | If `ai-direct` wins, validation budget/reward is too weak or too slow |
| Multi-agent decomposition | FuzzingBrain V2, FuzzAgent, FunFuzz, MultiFuzz | Full agent set plus subset modes share the same recipe interface | `full-agent / single-agent-coordinator`, `full-agent / single-agent-dictionary` | claim effect-size rows and LLM accounting | Full-agent gains support diverse proposal sources judged by micro-campaigns | If subset wins, paper should claim modular agent architecture, not agent count |
| Recipe strategy interface | Auditable fuzzing systems | Agents emit structured interventions; promoted recipes are materialized and copied | `full-agent / no-mutator` | claim effect-size row, promotion artifacts, post-promotion windows | Positive gain supports recipe-guided mutation as the actuation layer | If no gain, decisions may be useful but mutator actions are not expressive enough |
| Low-noise evaluation protocol | SoK/prudent evaluation practices | Stage wrapper, fixed CPU slots, stage plan, preflight load/memory/swap gates, runtime noise monitor, status JSON | Stage S/A/B/C progression and metadata gates | `*_stage_plan.json`, `runner metadata`, `runner ordering metadata`, preflight logs, `*_noise_summary.json` | Clean metadata makes results auditable and reproducible | Gate failures or runtime noise warnings invalidate or narrow the stage as paper evidence |

## Stage Roles

| stage | role | evidence status | expected use |
|---|---|---|---|
| S | Infrastructure drill | Not paper evidence | Catch broken credentials, target crashes, artifact copy/report wiring, and summary failures before Stage A |
| A | Smoke matrix | Infrastructure evidence only | Confirm all paper modes can run on all targets under low-noise controls |
| B | Pilot dataset | First credible architecture evidence | Decide whether to continue to Stage C and refine paper framing |
| C | Local paper pilot | Main local evidence | Produce the primary effect-size, overhead, and mechanism tables |

## Paper Tables

Use these generated report sections as the canonical table sources:

- `Gates`: infrastructure validity and whether the stage can be cited.
- `Per-Mode Summary`: mode-level status, throughput, paths, decisions, validations, and promotions.
- `LLM Accounting`: token use, latency, and fallback rate.
- `Promotion Impact Windows`: main-run paths/edges gained in 10 and 30 minutes after promotion.
- `Throughput Ratios`: mode throughput normalized to `baseline-afl`.
- `Architecture Claim Effect Sizes`: paired target/repeat ratios and bootstrap confidence intervals for the main claims.

## Decision Rules

- Do not cite Stage S in the paper's main results.
- Do not advance to Stage B unless Stage A has clean wrapper status and no failed gates.
- Do not advance to Stage C unless Stage B has clean wrapper status and no failed gates.
- Do not compare stages from different CPU counts, OS images, target binaries, static context hashes, model names, or endpoint hosts in one primary table.
- Treat any missing stage plan, missing `runner_metadata.json`, model drift, active fuzzing process at preflight, missing API key, auth error, secret-pattern hit, or missing runtime noise summary as an invalidating infrastructure failure.
- Treat any missing or mismatched Stage B/C frozen-input file as an invalidating
  infrastructure failure. The frozen file is written by
  `scripts/paper01/architecture_freeze_inputs.py` and covers manifest, libxml2
  binary, seeds, dictionary, static context, model config, and acceptance gates.
- If Stage B effect sizes are weak but infrastructure gates pass, keep the result: rewrite the claim boundary instead of tuning after looking at outcomes.
- Freeze the Stage B/C manifest, seed corpus, static context, model name, and
  acceptance gates before launching a stage. Do not tune agent prompts,
  micro-campaign budgets, or mode ordering after inspecting partial Stage B/C
  outcomes unless the tuned rerun is labeled as a new exploratory stage.
- For a libxml2-only paper, interpret positive results as architecture evidence
  for one mature parser target. Do not generalize to "all C/C++ libraries"
  without a later multi-target venue stage.

## Writing Boundary

Strong claims require Stage B or C evidence. Stage A can support only "the system
runs end-to-end under controlled conditions." Stage S supports only "the
experiment harness was checked before the paper stages."
