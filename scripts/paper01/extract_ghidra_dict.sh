#!/usr/bin/env bash
# scripts/paper01/extract_ghidra_dict.sh
#
# Run Ghidra analyzeHeadless on a target binary and extract dictionary
# candidates: string literals from .rodata, magic numbers from constant
# loads, and short bytestrings from comparison operands. Writes:
#
#   experiments/targets/<target>/ghidra_extracted.dict   — AFL++ -x format
#   experiments/targets/<target>/ghidra_extracted.json   — full metadata
#
# This is the data point Paper 1 §4.3 (Ghidra channel) claims comes from
# real binary analysis. On cJSON the extraction yields ≤3 useful tokens
# (FUZZ/MAGIC/TOKEN-equivalent), which is the null-result anchor. On
# libxml2/sqlite3/openssl_x509 it should yield substantially more.
#
# Usage: extract_ghidra_dict.sh <target_name>
#
# Targets known to setup: cjson, libxml2, sqlite3, openssl_x509

set -u
TGT="${1:-}"
[[ -z "$TGT" ]] && { echo "usage: $0 <target_name>" >&2; exit 2; }

REPO=/www/fuzz_agent
GHIDRA=/opt/ghidra
TGTDIR="$REPO/experiments/targets/$TGT"

# Map target name → binary path
declare -A BIN
BIN[cjson]="$TGTDIR/cjson_fuzzer"
BIN[libxml2]="$TGTDIR/libxml2_fuzzer"
BIN[sqlite3]="$TGTDIR/sqlite3_fuzzer"
BIN[openssl_x509]="$TGTDIR/x509_fuzzer"

BINARY="${BIN[$TGT]:-}"
[[ -z "$BINARY" || ! -x "$BINARY" ]] && {
  echo "binary missing or not executable: $BINARY" >&2; exit 1
}

PROJ_DIR="$TGTDIR/ghidra_proj"
PROJ_NAME="fuzzpilot_$TGT"
mkdir -p "$PROJ_DIR"

echo "[ghidra:$TGT] importing $BINARY ..."
"$GHIDRA/support/analyzeHeadless" "$PROJ_DIR" "$PROJ_NAME" \
  -import "$BINARY" \
  -overwrite \
  -scriptPath "$REPO/scripts/paper01" \
  -postScript ExtractFuzzpilotDict.java \
  -deleteProject \
  -log "$TGTDIR/ghidra_analyze.log" 2>&1 | tail -10

# The post-script writes ghidra_extracted.dict and .json under TGTDIR.
echo "[ghidra:$TGT] done; dict at:"
ls -la "$TGTDIR/ghidra_extracted.dict" "$TGTDIR/ghidra_extracted.json" 2>/dev/null || true
echo "[ghidra:$TGT] token count:"
grep -cE '^"' "$TGTDIR/ghidra_extracted.dict" 2>/dev/null || echo 0
