#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE="${FUZZPILOT_DOCKER_IMAGE:-fuzzpilot:paper01}"
PLATFORM_SETTING="${FUZZPILOT_DOCKER_PLATFORM:-auto}"
CANONICAL_PLATFORM="${FUZZPILOT_CANONICAL_PLATFORM:-linux/amd64}"
REBUILD_IMAGE="${FUZZPILOT_DOCKER_REBUILD_IMAGE:-0}"

usage() {
  cat <<'USAGE'
Usage:
  scripts/fuzzpilot_docker.sh build
  scripts/fuzzpilot_docker.sh preflight [args...]
  scripts/fuzzpilot_docker.sh smoke [args...]
  scripts/fuzzpilot_docker.sh run-batch --exp E1a [args...]
  scripts/fuzzpilot_docker.sh aggregate [args...]
  scripts/fuzzpilot_docker.sh plots [args...]
  scripts/fuzzpilot_docker.sh all [args...]
  scripts/fuzzpilot_docker.sh shell

Environment:
  FUZZPILOT_DOCKER_PLATFORM=auto|linux/amd64|linux/arm64
  FUZZPILOT_DOCKER_IMAGE=fuzzpilot:paper01
  FUZZPILOT_CANONICAL_PLATFORM=linux/amd64
  FUZZPILOT_DOCKER_REBUILD_IMAGE=1
USAGE
}

host_platform() {
  local arch
  arch="$(uname -m)"
  case "${arch}" in
    x86_64|amd64) echo "linux/amd64" ;;
    arm64|aarch64) echo "linux/arm64" ;;
    *)
      echo "Unsupported host architecture for auto Docker platform: ${arch}" >&2
      echo "Set FUZZPILOT_DOCKER_PLATFORM=linux/amd64 or linux/arm64." >&2
      exit 2
      ;;
  esac
}

resolve_platform() {
  case "${PLATFORM_SETTING}" in
    auto) host_platform ;;
    linux/amd64|linux/arm64) echo "${PLATFORM_SETTING}" ;;
    *)
      echo "Unsupported FUZZPILOT_DOCKER_PLATFORM=${PLATFORM_SETTING}" >&2
      usage >&2
      exit 2
      ;;
  esac
}

image_arch() {
  docker image inspect "${IMAGE}" --format '{{.Os}}/{{.Architecture}}' 2>/dev/null || true
}

build_image() {
  local platform="$1"
  local vcs_ref="unknown"
  local vcs_branch="unknown"
  local vcs_dirty="unknown"

  if git -C "${ROOT_DIR}" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    vcs_ref="$(git -C "${ROOT_DIR}" rev-parse HEAD 2>/dev/null || echo unknown)"
    vcs_branch="$(git -C "${ROOT_DIR}" rev-parse --abbrev-ref HEAD 2>/dev/null || echo unknown)"
    if [[ -n "$(git -C "${ROOT_DIR}" status --porcelain 2>/dev/null)" ]]; then
      vcs_dirty="true"
    else
      vcs_dirty="false"
    fi
  fi

  docker buildx build --load \
    --platform "${platform}" \
    --build-arg "VCS_REF=${vcs_ref}" \
    --build-arg "VCS_BRANCH=${vcs_branch}" \
    --build-arg "VCS_DIRTY=${vcs_dirty}" \
    -t "${IMAGE}" \
    -f "${ROOT_DIR}/docker/ubuntu/Dockerfile" \
    "${ROOT_DIR}"
}

ensure_image() {
  local platform="$1"
  local current
  current="$(image_arch)"
  if [[ "${REBUILD_IMAGE}" == "1" || "${current}" != "${platform}" ]]; then
    if [[ -n "${current}" && "${current}" != "${platform}" ]]; then
      echo "[docker] image ${IMAGE} is ${current}; rebuilding for ${platform}"
    fi
    build_image "${platform}"
  else
    echo "[docker] reusing image ${IMAGE} (${current})"
  fi
}

run_image() {
  local platform="$1"
  shift
  mkdir -p "${ROOT_DIR}/results"

  local env_args=(
    -e "FUZZPILOT_DOCKER_PLATFORM=${platform}"
    -e "FUZZPILOT_CANONICAL_PLATFORM=${CANONICAL_PLATFORM}"
  )
  local maybe_env
  for maybe_env in \
    FUZZPILOT_MODEL_API_KEY \
    FUZZPILOT_MODEL_ENDPOINT \
    FUZZPILOT_PARALLEL \
    FUZZPILOT_SMOKE_BUDGET_SEC \
    FUZZPILOT_REQUIRE_CANONICAL_PLATFORM; do
    if [[ -n "${!maybe_env:-}" ]]; then
      env_args+=(-e "${maybe_env}=${!maybe_env}")
    fi
  done

  docker run --rm --platform "${platform}" \
    -v "${ROOT_DIR}/results:/work/fuzz_agent/results" \
    "${env_args[@]}" \
    "${IMAGE}" "$@"
}

main() {
  local cmd="${1:-}"
  if [[ -z "${cmd}" || "${cmd}" == "-h" || "${cmd}" == "--help" ]]; then
    usage
    exit 0
  fi
  shift || true

  local platform
  platform="$(resolve_platform)"

  case "${cmd}" in
    build)
      build_image "${platform}"
      ;;
    preflight|smoke|run-batch|run|batch|aggregate|plots|all|shell|bash)
      ensure_image "${platform}"
      run_image "${platform}" "${cmd}" "$@"
      ;;
    *)
      echo "unknown command: ${cmd}" >&2
      usage >&2
      exit 2
      ;;
  esac
}

main "$@"
