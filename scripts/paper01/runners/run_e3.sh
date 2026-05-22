#!/usr/bin/env bash
# scripts/paper01/runners/run_e3.sh
# Paper 1 — E3 mutator microbench × 15 (cjson, ~minutes on 4 cores)
# Does NOT need FUZZPILOT_MODEL_API_KEY.
exec "$(dirname "${BASH_SOURCE[0]}")/_run_exp.sh" E3 "$@"
