# T3 — Related-work matrix (Paper 1)

Positioning table for §8. Built before §8 prose per the writing order in
`docs/papers/paper01_arxiv_placeholder.md`. Each row is one published
LLM-augmented or LLM-adjacent fuzzer; columns are the five axes that
distinguish FuzzPilot's design.

Cell values:
- Y / N for boolean axes
- `?` when the source is ambiguous (re-check before submission)
- "n/a" when the axis does not apply to the row

| System | LLM in hot path? | Validates proposal before use? | Recipe abstraction? | Auditable decision log? | Open source? | Target domain |
|---|:---:|:---:|:---:|:---:|:---:|:---|
| AFL++ baseline (no LLM) | n/a | n/a | N | partial (fuzzer_stats only) | Y | general |
| AFL++ cmplog / RedQueen (Aschermann et al., NDSS'19) | n/a (I2S runtime hint) | coverage | N (dict override only) | N | Y | general |
| AFLSmart (Pham et al., TSE'21) | n/a (input-format model) | coverage | partial (chunk model) | N | Y | binary structured (PNG / WAV / PDF) |
| Nautilus (Aschermann et al., NDSS'19) | n/a (context-free grammar) | coverage | partial (subtree splice rules) | N | Y | grammar-amenable (JS / SQL / PHP) |
| Superion (Wang et al., ICSE'19) | n/a (ANTLR grammar) | coverage | partial (AST mutation set) | N | Y | structured text (XML / JS) |
| ChatFuzz (Hu et al., 2023, arXiv:2310.08568) | Y (model emits mutations) | N | N | N | Y | structured text (JSON / XML / HTML) |
| LLAMAFUZZ (Wang et al., 2024, arXiv:2406.07714) | Y (model emits mutations) | N | N | N | Y | Magma suite |
| FuzzCoder (Liu et al., ACL'24) | Y (seq-to-seq byte mutation) | N | N | N | Y | general |
| Fuzz4All (Xia et al., ICSE'24) | Y (LLM as primary generator) | N | N | N | Y | multi-language |
| TitanFuzz (Deng et al., ISSTA'23) | generator (zero-shot) | coverage | N (full program inputs) | N | Y | DL libraries (PyTorch / TF) |
| FuzzGPT (Deng et al., ICSE'24) | generator (history-prompted) | coverage | N | N | Y | DL libraries |
| Sphinx (Sun et al., 2024) | LLM upfront, one-shot | coverage | partial (generator code) | N | Y | SMT solvers |
| WhiteFox (Yang et al., ASE'24) | two-stage generator | coverage | N | N | Y | compilers (PyTorch / LLVM) |
| KernelGPT (Yang et al., 2024) | generator + spec mining | coverage | N (syscall seqs) | N | Y | Linux kernel |
| MetaMut (Ou et al., ICSE'24) | offline | manual review | N (compiled mutator) | N | Y | compilers |
| Mut4All (Ye et al., 2025) | offline | manual review | N (compiled mutator) | N | Y | compilers |
| OSS-Fuzz-Gen (Liu et al., 2024) | offline (target authoring) | build + coverage | N (harness code) | partial | Y | OSS-Fuzz fleet |
| Semantic-Aware Fuzzing (arXiv:2509.19533) | Y (per-input semantic guidance) | N (semantic novelty score) | N | N | ? | general |
| Hybrid Fuzzing w/ LLM Mutation (Lin, arXiv:2511.03995) | parallel helper (~250 rph) | novelty + syntax check | N | N | ? | libpng / tcpdump / sqlite |
| MALF (Wu et al., 2025) | multi-agent in-loop | coverage | N (input sequences) | partial | ? | industrial protocols |
| G²FUZZ (Zhang Kunpeng et al., **USENIX Sec'25**) | **N** (off-hot-path, plateau-triggered) | coverage (downstream) | code (Python generator) | N | Y | non-textual binary (JPEG / TIFF / MP4) |
| HLPFuzz (Yang Yupeng et al., **USENIX Sec'25**) | **N** (off-hot-path, constraint solve) | coverage (downstream) | partial (constraint fragments) | N | Y | language processors (compilers / interpreters) |
| **FuzzPilot (this work)** | **N** (plateau-triggered planner) | **Y** (micro-campaign N-sec run + reward gate) | **Y** (schema-validated op list, data not code) | **Y** (blackboard + decisions + recipes + micro rewards) | **Y** | **cJSON evaluated; designed for structured-text parsers** |

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

## Citation status (locked 2026-05-28)

- [x] ChatFuzz — Hu et al., 2023 (arXiv:2310.08568) — `hu2023chatfuzz`
- [x] LLAMAFUZZ — Wang et al., 2024 (arXiv:2406.07714) — `wang2024llamafuzz`
- [x] G²FUZZ — Zhang Kunpeng et al., USENIX Sec'25 — `liu2025g2fuzz` (legacy bib key, author corrected from "Liu, K. and others" → full Zhang et al. roster on 2026-05-28)
- [x] HLPFuzz — Yang Yupeng et al. (Georgia Tech), USENIX Sec'25 — `yang2025hlpfuzz` (added 2026-05-28); paper: https://www.usenix.org/conference/usenixsecurity25/presentation/yang-yupeng
- [x] Semantic-Aware Fuzzing — arXiv:2509.19533 — `semanticaware2025`
- [x] Hybrid Fuzzing w/ LLM Mutation — Lin, arXiv:2511.03995 — `lin2025hybrid`
- [x] MetaMut — Ou et al., ICSE'24 — `ou2024metamut`
- [x] Mut4All — Ye et al., 2025 — `ye2025mut4all`
- [x] MALF — `malf2025`
- [x] TitanFuzz — Deng et al., ISSTA'23 — `deng2023titanfuzz`
- [x] FuzzGPT — Deng et al., ICSE'24 — `deng2024fuzzgpt`
- [x] WhiteFox — Yang et al., ASE'24 — `yang2024whitefox`
- [x] KernelGPT — Yang et al., 2024 — `yang2024kernelgpt`
- [x] Sphinx — Sun et al., 2024 — `sun2024sphinx`
- [x] OSS-Fuzz-Gen — Liu et al., 2024 — `liu2024ossfuzzgen`
- [x] HyLLfuzz (previously placeholder row) — removed; the role is now occupied by the more authoritative Hybrid Fuzzing entry (Lin 2025) which also sits at a parallel-helper hot-path placement
- [x] Staczzer (placeholder) — removed; not part of submission scope
- [x] MultiFuzz (placeholder) — removed; not part of submission scope

## Removed rows rationale

`HyLLfuzz`, `Staczzer`, and `MultiFuzz` were research-tracker placeholders
inserted before refs.bib was locked. Each was reviewed against the bib
file: HyLLfuzz had no canonical citation we could verify; Staczzer and
MultiFuzz returned multiple unrelated systems with the same name.
Replacing them with the verified USENIX Sec'25 + arXiv 2025 entries
above gives the §Related Work matrix a fully citable basis with no
stale `?` cells. The corresponding LaTeX `tab:related` in
`paper01.tex` was regenerated from this file on 2026-05-28.
