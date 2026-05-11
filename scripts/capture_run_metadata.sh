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

if ! command -v git >/dev/null 2>&1; then
  echo "Error: git is required but not found in PATH." >&2
  exit 1
fi

if ! git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
  echo "Error: this script must be run inside a git repository." >&2
  exit 1
fi

if [[ ! -f "$CONFIG_PATH" ]]; then
  echo "Error: config file not found: $CONFIG_PATH" >&2
  exit 1
fi

mkdir -p "$OUT_DIR"

CAPTURED_AT_UTC="$(date -u +"%Y-%m-%dT%H:%M:%SZ")"
GIT_COMMIT="$(git rev-parse HEAD)"
GIT_BRANCH="$(git rev-parse --abbrev-ref HEAD)"
OS_NAME="$(uname -s)"
ARCH_NAME="$(uname -m)"
CONFIG_SHA256="$(sha256_file "$CONFIG_PATH")"

if [[ -n "$(git status --porcelain)" ]]; then
  GIT_DIRTY=true
else
  GIT_DIRTY=false
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
  printf '  "arch": "%s"\n' "$(json_escape "$ARCH_NAME")"
  printf '}\n'
} > "$OUT_DIR/run_metadata.json"

git status --short --branch > "$OUT_DIR/git_status.txt"
{
  # Capture both unstaged and staged changes for reproducibility snapshots.
  git diff
  git diff --cached
} > "$OUT_DIR/git.patch"

echo "Captured metadata in: $OUT_DIR"
