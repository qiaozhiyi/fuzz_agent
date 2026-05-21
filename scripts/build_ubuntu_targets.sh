#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CFLAGS_COMMON=(-O1 -g)
REQUIRE_TARGETS="${FUZZPILOT_REQUIRE_TARGETS:-0}"
BUILT_TARGETS=()
SKIPPED_TARGETS=()

if [ -n "${FUZZPILOT_TARGET_CC:-}" ]; then
  CC_BIN="$FUZZPILOT_TARGET_CC"
elif command -v afl-clang-fast >/dev/null 2>&1; then
  CC_BIN="afl-clang-fast"
else
  CC_BIN="${CC:-clang}"
fi

require_cmd() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "missing required command: $1" >&2
    exit 1
  fi
}

build_vuln_target() {
  echo "[target] building vuln_target"
  make -C "$ROOT_DIR/experiments/targets/vuln_target" clean
  make -C "$ROOT_DIR/experiments/targets/vuln_target" CC="$CC_BIN"
  BUILT_TARGETS+=("vuln_target")
}

build_cjson_target() {
  local src="$ROOT_DIR/experiments/targets/cjson/src"
  local output="$ROOT_DIR/experiments/targets/cjson/cjson_fuzzer"
  rm -f "$output"
  if [ -f "$src/cJSON.c" ] && [ -f "$src/cJSON.h" ]; then
    echo "[target] building cJSON from submodule source"
    "$CC_BIN" "${CFLAGS_COMMON[@]}" \
      -I "$src" \
      "$ROOT_DIR/experiments/targets/cjson/harness.c" \
      "$src/cJSON.c" \
      -o "$output"
    BUILT_TARGETS+=("cjson")
    return 0
  fi
  if command -v pkg-config >/dev/null 2>&1 && pkg-config --exists libcjson; then
    echo "[target] building cJSON with system libcjson"
    "$CC_BIN" "${CFLAGS_COMMON[@]}" \
      "$ROOT_DIR/experiments/targets/cjson/harness.c" \
      $(pkg-config --cflags --libs libcjson) \
      -o "$output"
    BUILT_TARGETS+=("cjson")
    return 0
  fi
  echo "[target] skipping cJSON: initialize submodules or install libcjson-dev" >&2
  SKIPPED_TARGETS+=("cjson")
}

build_libpng_target() {
  local src="$ROOT_DIR/experiments/targets/libpng/src"
  local output="$ROOT_DIR/experiments/targets/libpng/libpng_fuzzer"
  rm -f "$output"
  if command -v pkg-config >/dev/null 2>&1 && pkg-config --exists libpng; then
    echo "[target] building libpng with system libpng"
    "$CC_BIN" "${CFLAGS_COMMON[@]}" \
      "$ROOT_DIR/experiments/targets/libpng/harness.c" \
      $(pkg-config --cflags --libs libpng) \
      -o "$output"
    BUILT_TARGETS+=("libpng")
    return 0
  fi
  if [ -f "$src/png.h" ]; then
    echo "[target] skipping libpng source build: install libpng-dev or add a source build recipe" >&2
  else
    echo "[target] skipping libpng: submodule is not initialized and system libpng is unavailable" >&2
  fi
  SKIPPED_TARGETS+=("libpng")
}

require_cmd "$CC_BIN"
build_vuln_target
build_cjson_target
build_libpng_target

echo "[target] built target artifact summary:"
file \
  "$ROOT_DIR/experiments/targets/vuln_target/vuln" \
  "$ROOT_DIR/experiments/targets/cjson/cjson_fuzzer" \
  "$ROOT_DIR/experiments/targets/libpng/libpng_fuzzer" 2>/dev/null || true

if [ "${#BUILT_TARGETS[@]}" -gt 0 ]; then
  printf '[target] built: %s\n' "${BUILT_TARGETS[*]}"
fi
if [ "${#SKIPPED_TARGETS[@]}" -gt 0 ]; then
  printf '[target] skipped: %s\n' "${SKIPPED_TARGETS[*]}" >&2
  if [ "$REQUIRE_TARGETS" = "1" ]; then
    exit 2
  fi
fi
