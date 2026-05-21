#!/usr/bin/env bash
set -euo pipefail

INSTALL_DIR="${FUZZPILOT_GHIDRA_INSTALL_DIR:-/opt}"
SYMLINK="${FUZZPILOT_GHIDRA_SYMLINK:-/opt/ghidra}"
FORCE_INSTALL="${FUZZPILOT_GHIDRA_FORCE_INSTALL:-0}"
TMP_DIR="$(mktemp -d)"

cleanup() {
  rm -rf "$TMP_DIR"
}
trap cleanup EXIT

require_cmd() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "missing required command: $1" >&2
    exit 1
  fi
}

show_headless_help() {
  "$1/support/analyzeHeadless" 2>&1 | head -n 5 || true
}

if [ "$FORCE_INSTALL" != "1" ]; then
  if [ -x "$SYMLINK/support/analyzeHeadless" ]; then
    show_headless_help "$SYMLINK"
    echo "Ghidra already installed at $SYMLINK"
    echo "set FUZZPILOT_GHIDRA_FORCE_INSTALL=1 to reinstall"
    exit 0
  fi

  if [ -d "$INSTALL_DIR" ]; then
    EXISTING_DIR="$(find "$INSTALL_DIR" -maxdepth 1 -type d -name 'ghidra_*_PUBLIC' | sort | tail -n 1)"
    if [ -n "$EXISTING_DIR" ] && [ -x "$EXISTING_DIR/support/analyzeHeadless" ]; then
      ln -sfn "$EXISTING_DIR" "$SYMLINK"
      show_headless_help "$SYMLINK"
      echo "reused existing Ghidra at $EXISTING_DIR"
      echo "symlink: $SYMLINK"
      echo "set FUZZPILOT_GHIDRA_FORCE_INSTALL=1 to reinstall"
      exit 0
    fi
  fi
fi

require_cmd java
require_cmd python3
require_cmd unzip

ASSET_INFO="$(
python3 - <<'PY'
import json
import re
import sys
import urllib.request

url = "https://api.github.com/repos/NationalSecurityAgency/ghidra/releases/latest"
with urllib.request.urlopen(url, timeout=30) as response:
    release = json.load(response)

for asset in release.get("assets", []):
    name = asset.get("name", "")
    if re.match(r"ghidra_.*_PUBLIC_.*\.zip$", name):
        print(name + "\t" + asset["browser_download_url"])
        sys.exit(0)

raise SystemExit("no Ghidra public ZIP asset found in latest release")
PY
)"

ASSET_NAME="${ASSET_INFO%%$'\t'*}"
ASSET_URL="${ASSET_INFO#*$'\t'}"
ZIP_PATH="$TMP_DIR/$ASSET_NAME"

if command -v curl >/dev/null 2>&1; then
  curl -L --fail "$ASSET_URL" -o "$ZIP_PATH"
elif command -v wget >/dev/null 2>&1; then
  wget -O "$ZIP_PATH" "$ASSET_URL"
else
  echo "missing required command: curl or wget" >&2
  exit 1
fi

mkdir -p "$INSTALL_DIR"
unzip -q "$ZIP_PATH" -d "$INSTALL_DIR"

EXTRACTED_DIR="$(find "$INSTALL_DIR" -maxdepth 1 -type d -name 'ghidra_*_PUBLIC' | sort | tail -n 1)"
if [ -z "$EXTRACTED_DIR" ]; then
  echo "failed to locate extracted Ghidra directory under $INSTALL_DIR" >&2
  exit 1
fi

ln -sfn "$EXTRACTED_DIR" "$SYMLINK"
show_headless_help "$SYMLINK"

echo "installed Ghidra at $EXTRACTED_DIR"
echo "symlink: $SYMLINK"
