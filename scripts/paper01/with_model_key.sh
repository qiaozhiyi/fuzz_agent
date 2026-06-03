#!/usr/bin/env bash
set -euo pipefail

if [[ -n "${FUZZPILOT_MODEL_API_KEY:-}" && "${FORCE_PROMPT_API_KEY:-0}" != "1" ]]; then
  exec "$@"
fi

if [[ ! -t 0 ]]; then
  echo "FUZZPILOT_MODEL_API_KEY is unset and stdin is not a TTY" >&2
  echo "Run from an interactive shell or export FUZZPILOT_MODEL_API_KEY first." >&2
  exit 2
fi

printf 'FUZZPILOT_MODEL_API_KEY: ' >&2
IFS= read -r -s FUZZPILOT_MODEL_API_KEY
printf '\n' >&2
export FUZZPILOT_MODEL_API_KEY

if [[ -z "${FUZZPILOT_MODEL_API_KEY}" ]]; then
  echo "FUZZPILOT_MODEL_API_KEY was empty" >&2
  exit 2
fi

exec "$@"
