#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# Backward-compatible entrypoint for older docs/scripts. The portable Docker
# path now lives in fuzzpilot_docker.sh and defaults to the host architecture;
# set FUZZPILOT_DOCKER_PLATFORM=linux/amd64 for paper-canonical runs.
exec "${ROOT_DIR}/scripts/fuzzpilot_docker.sh" smoke "$@"
