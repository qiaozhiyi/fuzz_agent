#!/usr/bin/env bash
# scripts/paper01/runners/run_e2a.sh
# Paper 1 — E2a rule-only × 3 (cjson, 14400s each, ~4h wall on 3 parallel)
# FuzzPilot loop + recipe mutator, LLM disabled. Does NOT need API key.
exec "$(dirname "${BASH_SOURCE[0]}")/_run_exp.sh" E2a "$@"
