#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
Usage:
  scripts/capture_run_metadata.sh --run-id RUN_ID --config PATH --target NAME --out-dir PATH
USAGE
}

# Lightweight JSON escaping for run metadata primitives.
# This intentionally supports common shell-safe text fields without external deps.
json_escape() {
  local escaped="$1"
  escaped="${escaped//\\/\\\\}"
  escaped="${escaped//\"/\\\"}"
  escaped="${escaped//$'\n'/\\n}"
  escaped="${escaped//$'\t'/\\t}"
  escaped="${escaped//$'\r'/\\r}"
  printf '%s' "$escaped"
}

sha256_file() {
  local file_path="$1"
  if command -v shasum >/dev/null 2>&1; then
    shasum -a 256 "$file_path" | awk '{print $1}'
    return
  fi
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$file_path" | awk '{print $1}'
    return
  fi
  echo "Error: neither shasum nor sha256sum is available." >&2
  exit 1
}

metadata_value() {
  local key="$1"
  local file="${FUZZPILOT_IMAGE_METADATA:-/work/fuzz_agent/build/fuzzpilot_image_metadata.env}"
  [[ -f "$file" ]] || return 1
  awk -F= -v k="$key" '$1 == k { sub(/^[^=]*=/, ""); print; found=1; exit } END { exit found ? 0 : 1 }' "$file"
}

RUN_ID=""
CONFIG_PATH=""
TARGET_NAME=""
OUT_DIR=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --run-id)
      RUN_ID="${2:-}"
      shift 2
      ;;
    --config)
      CONFIG_PATH="${2:-}"
      shift 2
      ;;
    --target)
      TARGET_NAME="${2:-}"
      shift 2
      ;;
    --out-dir)
      OUT_DIR="${2:-}"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Error: unknown argument: $1" >&2
      usage
      exit 1
      ;;
  esac
done

if [[ -z "$RUN_ID" || -z "$CONFIG_PATH" || -z "$TARGET_NAME" || -z "$OUT_DIR" ]]; then
  echo "Error: missing required arguments." >&2
  usage
  exit 1
fi

if [[ ! -f "$CONFIG_PATH" ]]; then
  echo "Error: config file not found: $CONFIG_PATH" >&2
  exit 1
fi

mkdir -p "$OUT_DIR"

CAPTURED_AT_UTC="$(date -u +"%Y-%m-%dT%H:%M:%SZ")"
GIT_COMMIT="unknown"
GIT_BRANCH="unknown"
GIT_DIRTY="unknown"
GIT_AVAILABLE=false
OS_NAME="$(uname -s)"
ARCH_NAME="$(uname -m)"
CONFIG_SHA256="$(sha256_file "$CONFIG_PATH")"
TARGET_PLATFORM="$(metadata_value target_platform 2>/dev/null || echo "${FUZZPILOT_DOCKER_PLATFORM:-unknown}")"
BUILD_PLATFORM="$(metadata_value build_platform 2>/dev/null || echo "unknown")"
IMAGE_AFLPP_REF="$(metadata_value aflpp_ref 2>/dev/null || echo "unknown")"
IMAGE_GHIDRA_VERSION="$(metadata_value ghidra_version 2>/dev/null || echo "unknown")"

if command -v git >/dev/null 2>&1 && git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
  GIT_AVAILABLE=true
  GIT_COMMIT="$(git rev-parse HEAD)"
  GIT_BRANCH="$(git rev-parse --abbrev-ref HEAD)"
  if [[ -n "$(git status --porcelain)" ]]; then
    GIT_DIRTY=true
  else
    GIT_DIRTY=false
  fi
else
  GIT_COMMIT="$(metadata_value vcs_ref 2>/dev/null || echo unknown)"
  GIT_BRANCH="$(metadata_value vcs_branch 2>/dev/null || echo unknown)"
  GIT_DIRTY="$(metadata_value vcs_dirty 2>/dev/null || echo unknown)"
fi

{
  printf '{\n'
  printf '  "run_id": "%s",\n' "$(json_escape "$RUN_ID")"
  printf '  "captured_at_utc": "%s",\n' "$(json_escape "$CAPTURED_AT_UTC")"
  printf '  "git_commit": "%s",\n' "$(json_escape "$GIT_COMMIT")"
  printf '  "git_branch": "%s",\n' "$(json_escape "$GIT_BRANCH")"
  printf '  "git_dirty": %s,\n' "$GIT_DIRTY"
  printf '  "config_path": "%s",\n' "$(json_escape "$CONFIG_PATH")"
  printf '  "config_sha256": "%s",\n' "$(json_escape "$CONFIG_SHA256")"
  printf '  "target_name": "%s",\n' "$(json_escape "$TARGET_NAME")"
  printf '  "os": "%s",\n' "$(json_escape "$OS_NAME")"
  printf '  "arch": "%s",\n' "$(json_escape "$ARCH_NAME")"
  printf '  "target_platform": "%s",\n' "$(json_escape "$TARGET_PLATFORM")"
  printf '  "build_platform": "%s",\n' "$(json_escape "$BUILD_PLATFORM")"
  printf '  "aflpp_ref": "%s",\n' "$(json_escape "$IMAGE_AFLPP_REF")"
  printf '  "ghidra_version": "%s"\n' "$(json_escape "$IMAGE_GHIDRA_VERSION")"
  printf '}\n'
} > "$OUT_DIR/run_metadata.json"

if [[ "$GIT_AVAILABLE" == true ]]; then
  git status --short --branch > "$OUT_DIR/git_status.txt"
  {
    # Capture both unstaged and staged changes for reproducibility snapshots.
    git diff
    git diff --cached
  } > "$OUT_DIR/git.patch"
else
  {
    printf 'git repository unavailable in runtime image\n'
    printf 'commit: %s\n' "$GIT_COMMIT"
    printf 'branch: %s\n' "$GIT_BRANCH"
    printf 'dirty: %s\n' "$GIT_DIRTY"
    printf 'target_platform: %s\n' "$TARGET_PLATFORM"
  } > "$OUT_DIR/git_status.txt"
  printf 'git diff unavailable: runtime image was built without .git\n' > "$OUT_DIR/git.patch"
fi

echo "Captured metadata in: $OUT_DIR"
