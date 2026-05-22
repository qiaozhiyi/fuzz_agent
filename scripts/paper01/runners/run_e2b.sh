#!/usr/bin/env bash
# scripts/paper01/runners/run_e2b.sh
# Paper 1 — E2b no-mutator ablation (cjson, full-agent variant).
# REQUIRES FUZZPILOT_MODEL_API_KEY.
exec "$(dirname "${BASH_SOURCE[0]}")/_run_exp.sh" E2b "$@"
