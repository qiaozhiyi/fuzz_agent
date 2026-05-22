#!/usr/bin/env bash
# scripts/paper01/runners/run_e2c.sh
# Paper 1 — E2c no-static-analysis ablation (cjson, full-agent variant).
# REQUIRES FUZZPILOT_MODEL_API_KEY.
exec "$(dirname "${BASH_SOURCE[0]}")/_run_exp.sh" E2c "$@"
