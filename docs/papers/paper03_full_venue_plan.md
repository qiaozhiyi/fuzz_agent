# Paper 3 — Full System Paper (Venue Submission)
## *FuzzPilot: An Off-Hot-Path LLM Controller for Greybox Fuzzing — Full Evaluation*

**Form**: 12–13 page double-column conference paper.
**Venue ladder**: ISSTA → FSE → USENIX Security → CCS. (Pick by submission deadline relative to when data is ready.)
**Differentiator from Paper 1**: Paper 1 is preliminary on 1 target; Paper 3 is the **full empirical case** with the matrix the field expects.
**Key claim upgrade**: from "preliminary evidence" to "competitive or better than X under equal budgets, with auditable decision trails, on a representative target set."

---

## Paper 3 vs Paper 1 vs Paper 2 — clean delineation

| Aspect | Paper 1 (arxiv) | Paper 2 (empirical) | Paper 3 (venue) |
|---|---|---|---|
| Pitch | System design | Trigger/cost study | Full system + evaluation |
| Targets | 1 family (cJSON) | 5 | 6–10 |
| Repeats | 5 | 5 | 10 |
| Ablations | Partial | Full per RQ | Full matrix |
| Statistical tests | None | Yes | Yes |
| Crash dedup & bugs | No | No | **Yes** |
| Compares to other LLM-fuzzers | Qualitative table | Cost frontier | **Head-to-head numeric** |
| New bugs disclosed | No | No | Aim for ≥3 CVE-class |

If Paper 2 ships first, Paper 3 absorbs Paper 2's results as one section and adds bug-finding + head-to-head. If Paper 2 doesn't ship before Paper 3 is due, Paper 3 picks up Paper 2's RQ1/RQ4 lite.

## Contributions to claim in Paper 3 (delta vs Paper 1)

1. **Expanded evaluation**: 6–10 targets including OSS-Fuzz subjects, 10 repeats per cell, full ablation matrix from `docs/evaluation.md`.
2. **Head-to-head against other LLM-augmented fuzzers** (HyLLfuzz, LLAMAFUZZ, and at least one of Semantic-Aware Fuzzing / Hybrid-LLM-mutation), under equal budget.
3. **Crash deduplication and bug disclosure pipeline**, with concrete unique-bug counts and any CVEs filed.
4. **Statistical rigor**: Mann–Whitney U + Vargha–Delaney Â̂₁₂ per cell, multiple-comparison correction noted, IQR bands on all coverage plots.
5. **Audit-trail case studies** for ≥3 successful breakthroughs and ≥2 failure modes (proposal that passed validation but harmed main run, or vice versa) — required to make the "auditable" claim credible.

## Required target set (6–10)

Pull from the OSS-Fuzz / FuzzBench canon so reviewers don't argue with the choice:

- cJSON (existing)
- libpng (existing)
- libxml2 (xmllint harness — common in fuzzing papers)
- re2 (regex matcher — well-studied)
- sqlite3 (large state machine — stresses planning)
- tcpdump or wireshark dissector (binary protocol)
- harfbuzz or freetype (binary font — popular target)
- one OSS-Fuzz integration target chosen for diversity

Pick at least 6 of these. Diversity matters more than count.

## Required ablation matrix

Full cross of the 5 modes from `docs/evaluation.md`:

`baseline-afl` × `rule-only` × `no-static-analysis` × `no-mutator` × `full-agent`

Plus the two ablations that are currently missing per `validity_report.md`:

- `random-recipe` — random opcode sequences with same recipe schema
- `ai-direct` — LLM proposal without micro-campaign validation

Total: 7 modes × 6–10 targets × 10 repeats × 24h budget per run = **10080–16800 core-hours**. This is the dominant cost. Plan for either:

- a 64-core machine for 7–11 days dedicated, or
- a Kubernetes cluster with autoscaler and run-id-keyed object storage.

## Head-to-head baselines

Reproduce or fairly approximate:

- **AFL++ vanilla** with `-x cjson.dict` etc. (mandatory)
- **HyLLfuzz** (arXiv:2412.15931) — if their artifact is public, run on same targets/budget; if not, document attempt and use their published numbers with explicit caveat.
- **LLAMAFUZZ** (arXiv:2406.07714) — AST 2026 paper, should have artifact.
- Optionally **Semantic-Aware Fuzzing** (arXiv:2509.19533) microservices framework.

The head-to-head table is the single most likely thing a reviewer will Ctrl-F for. Build the comparison protocol *before* running, document budget equality, and pre-register the metrics.

## Crash deduplication pipeline (must build)

Currently absent. Required deliverables:

1. Replay script: every AFL++ crash input is replayed against an ASan-instrumented build, normalized stack trace captured.
2. Dedup key: top-3 frame symbols + bucketed offset, plus the sanitizer category.
3. Triage tags: crash / hang / OOM / flaky.
4. Per-run bug report: unique-bug count by mode, with examples.
5. Optional: CVE disclosure for any 0-days that emerge — pipeline this through `responsible disclosure` text.

Engineering estimate: 1–2 weeks.

## Statistical protocol

For each (target, mode) cell, 10 repeats. For coverage at fixed checkpoints:

- Median, 25/75 percentile reported.
- Pairwise vs. `baseline-afl`: Mann–Whitney U two-sided.
- Effect size: Vargha–Delaney Â̂₁₂.
- Significance threshold α=0.05 with Benjamini–Hochberg FDR for the number of modes × targets comparisons.

For time-to-first-event metrics: Kaplan–Meier curves, log-rank test.

Pre-register these in the paper repo before the first run starts.

## What this paper does NOT cover (deferred to follow-ups)

- Harness generation (explicit non-goal per `project_brief.md`).
- Directed fuzzing toward known vulnerabilities (out of scope — that's Staczzer's lane).
- Embedded / kernel targets.
- Multi-agent coordination beyond the current Coordinator/Dictionary/Mutator triple.

## Schedule (after Paper 2 ships, or in parallel)

| Phase | Weeks | Work |
|---|---|---|
| Build-out | 1–3 | crash dedup pipeline; missing ablations CLI; target harnesses for the 6–10 set; edge telemetry; head-to-head wrappers for HyLLfuzz / LLAMAFUZZ. |
| Pilot | 4 | 1 repeat × all modes × all targets to catch infrastructure bugs and tune budget. |
| Main eval | 5–8 | Full matrix runs (the big compute). |
| Head-to-head | 9 | Run baselines on same targets/budget. |
| Analysis | 10 | Stats, plots, bug triage, case-study mining. |
| Writing | 11–13 | Full paper. |
| Buffer | 14 | Rebuttal-friendly experiments + reproducibility tarball. |

Total: ~3.5 months once the build-out is funded with compute.

## Risk register

- **Head-to-head reproducibility is the single biggest risk.** If HyLLfuzz/LLAMAFUZZ artifacts don't reproduce, you can either (a) report your attempt honestly and use their reported numbers, or (b) defer head-to-head to a follow-up. (a) is fine if documented.
- **Compute overruns**: pre-register the budget per cell. If a cell consistently runs over, cut repeats from 10→5 for the affected cell and disclose.
- **Crash dedup is undefined territory**: lots of papers handwave this. Pre-register the dedup key formula.
- **Reviewer-2 question: "why not use FuzzBench directly?"** Plan an answer: FuzzBench doesn't expose the controller's intervention timing; you need your own infra to study it. But you should run a single FuzzBench-compatible comparison as a fairness anchor.

## Cross-paper consistency rules

- Paper 1 commits the architecture; Paper 3 cannot quietly refactor it without an updated description in Paper 3 §3.
- Paper 1's case-study run-id should also appear in Paper 3 (as a regression check) — proves the system kept working.
- Paper 2's RQ1 trigger sweep findings should inform Paper 3's defaults; document the chosen defaults and why in Paper 3 §6.

## Deliverable list at submission time

- 12-page paper PDF
- Public GitHub release tag `paper03-v1`
- Public Docker image with pinned versions
- Per-target build scripts, seed corpora checksums
- All `results/paper03/` artifacts (or a Zenodo deposit if too large)
- Plot-regen scripts
- Crash dedup tool + per-bug reports
- Disclosure log for any vulnerabilities filed
- `REPRODUCE.md` walking a reviewer through reproducing 1 figure and 1 table end-to-end
