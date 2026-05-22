#!/usr/bin/env bash
# scripts/paper01/runners/run_e1a.sh
# Paper 1 — E1a baseline-afl × 5 (cjson, 14400s each, ~5h wall on 4 parallel)
# Pure AFL++ baseline. Does NOT need FUZZPILOT_MODEL_API_KEY.
exec "$(dirname "${BASH_SOURCE[0]}")/_run_exp.sh" E1a "$@"
