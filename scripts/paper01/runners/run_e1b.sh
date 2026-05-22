#!/usr/bin/env bash
# scripts/paper01/runners/run_e1b.sh
# Paper 1 — E1b full-agent × 5 (cjson, 14400s each, ~5h wall on 4 parallel)
# RQ1/RQ2 core evidence. REQUIRES FUZZPILOT_MODEL_API_KEY (LLM agent on).
exec "$(dirname "${BASH_SOURCE[0]}")/_run_exp.sh" E1b "$@"
