#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
MANIFEST="${MANIFEST:-${REPO_ROOT}/experiments/manifests/paper_architecture_pilot.yaml}"
STAGE="${1:-A}"
FUZZPILOT_BIN="${FUZZPILOT_BIN:-${REPO_ROOT}/build/fuzzpilot}"
MIN_FREE_GIB="${MIN_FREE_GIB:-20}"
MIN_MEM_AVAILABLE_KIB="${MIN_MEM_AVAILABLE_KIB:-2097152}"
MAX_PREEXISTING_LOAD_PER_CPU="${MAX_PREEXISTING_LOAD_PER_CPU:-0.50}"
ALLOW_EXISTING_RESULTS="${ALLOW_EXISTING_RESULTS:-0}"

cd "${REPO_ROOT}"

fail=0
warn=0

pass() { printf '[PASS] %s\n' "$*"; }
note_warn() { printf '[WARN] %s\n' "$*"; warn=$((warn + 1)); }
note_fail() { printf '[FAIL] %s\n' "$*"; fail=$((fail + 1)); }

echo "FuzzPilot architecture pilot preflight"
echo "repo=${REPO_ROOT}"
echo "manifest=${MANIFEST}"
echo "stage=${STAGE}"
echo

if [[ -f "${MANIFEST}" ]]; then
  pass "manifest exists"
else
  note_fail "manifest missing: ${MANIFEST}"
fi

if [[ -x "${FUZZPILOT_BIN}" ]]; then
  pass "fuzzpilot binary exists: ${FUZZPILOT_BIN}"
else
  note_fail "fuzzpilot binary missing: ${FUZZPILOT_BIN}"
fi

if command -v afl-fuzz >/dev/null 2>&1; then
  pass "afl-fuzz available: $(command -v afl-fuzz)"
else
  note_fail "afl-fuzz is not on PATH"
fi

if command -v taskset >/dev/null 2>&1; then
  pass "taskset available for fixed CPU slots"
else
  note_warn "taskset unavailable; runner cannot pin fuzzing cells"
fi

cpu_count="$(python3 - <<'PY'
import os
print(os.cpu_count() or 1)
PY
)"
if [[ "${cpu_count}" -ge 4 ]]; then
  pass "CPU count=${cpu_count}; runner will reserve CPU0 and fuzz on remaining slots"
elif [[ "${cpu_count}" -ge 2 ]]; then
  note_warn "CPU count=${cpu_count}; runner can run, but pilot comparability is weaker"
else
  note_fail "CPU count=${cpu_count}; insufficient for controlled architecture pilot"
fi

mem_kib="$(awk '/^MemTotal:/ {print $2; exit}' /proc/meminfo 2>/dev/null || echo 0)"
if [[ "${mem_kib}" -ge 6291456 ]]; then
  pass "memory total KiB=${mem_kib}"
else
  note_warn "memory total KiB=${mem_kib}; 8GiB-class host is recommended"
fi

mem_available_kib="$(awk '/^MemAvailable:/ {print $2; exit}' /proc/meminfo 2>/dev/null || echo 0)"
if [[ "${mem_available_kib}" -ge "${MIN_MEM_AVAILABLE_KIB}" ]]; then
  pass "memory available KiB=${mem_available_kib}"
else
  note_fail "memory available KiB=${mem_available_kib} < MIN_MEM_AVAILABLE_KIB=${MIN_MEM_AVAILABLE_KIB}"
fi

load_check="$(python3 - "${cpu_count}" "${MAX_PREEXISTING_LOAD_PER_CPU}" <<'PY'
import os
import sys

cpu_count = max(1, int(sys.argv[1]))
max_per_cpu = float(sys.argv[2])
load1, load5, load15 = os.getloadavg()
max_load = cpu_count * max_per_cpu
prefix = "ok" if load1 <= max_load else "fail"
print(f"{prefix}: load average 1/5/15={load1:.2f}/{load5:.2f}/{load15:.2f}; max_preexisting_load={max_load:.2f}")
PY
)"
if [[ "${load_check}" == ok:* ]]; then
  pass "${load_check#ok: }"
else
  note_fail "${load_check#fail: }"
fi

swap_total_kib="$(awk '/^SwapTotal:/ {print $2; exit}' /proc/meminfo 2>/dev/null || echo 0)"
swap_free_kib="$(awk '/^SwapFree:/ {print $2; exit}' /proc/meminfo 2>/dev/null || echo 0)"
if [[ "${swap_total_kib}" -eq 0 ]]; then
  pass "swap is disabled"
elif [[ "${swap_free_kib}" -eq "${swap_total_kib}" ]]; then
  note_warn "swap is configured but unused: total KiB=${swap_total_kib}"
else
  note_fail "swap is in use: total KiB=${swap_total_kib} free KiB=${swap_free_kib}"
fi

free_gib="$(df -BG "${REPO_ROOT}" | awk 'NR==2 {gsub(/G/, "", $4); print $4}')"
if [[ "${free_gib}" -ge "${MIN_FREE_GIB}" ]]; then
  pass "free disk on repo filesystem=${free_gib}GiB"
else
  note_fail "free disk ${free_gib}GiB < MIN_FREE_GIB=${MIN_FREE_GIB}"
fi

if ps -eo args | grep -E 'fuzzpilot run|afl-fuzz|libxml2_fuzzer|cjson_fuzzer|libpng_fuzzer' | grep -v grep >/dev/null; then
  note_fail "active fuzzing processes found; stop them before a low-noise experiment"
else
  pass "no active fuzzpilot/AFL/target fuzzer processes"
fi

if [[ -d results/fuzzpilot_architecture_pilot ]] && [[ "${ALLOW_EXISTING_RESULTS}" != "1" ]]; then
  if find results/fuzzpilot_architecture_pilot -name status -exec grep -l '^running$' {} + 2>/dev/null | grep -q .; then
    note_fail "existing architecture pilot results contain running status files"
  else
    note_warn "results/fuzzpilot_architecture_pilot exists; set ALLOW_EXISTING_RESULTS=1 to append intentionally"
  fi
else
  pass "architecture pilot result directory is clear or explicitly allowed"
fi

if python3 - "${MANIFEST}" "${STAGE}" <<'PY'
import pathlib
import sys
from urllib.parse import urlparse

try:
    import yaml
except ImportError as exc:
    raise SystemExit(f"[FAIL] PyYAML unavailable: {exc}")

manifest_path = pathlib.Path(sys.argv[1])
stage_name = sys.argv[2]
manifest = yaml.safe_load(manifest_path.read_text(encoding="utf-8")) or {}
fail = 0
warn = 0

def ok(msg):
    print(f"[PASS] {msg}")

def bad(msg):
    global fail
    fail += 1
    print(f"[FAIL] {msg}")

def caution(msg):
    global warn
    warn += 1
    print(f"[WARN] {msg}")

stages = manifest.get("stages") or {}
stage = stages.get(stage_name) or {}
if stage_name in stages:
    ok(f"stage {stage_name} exists")
else:
    bad(f"stage {stage_name} missing from manifest")

targets = manifest.get("targets") or []
all_modes = manifest.get("modes") or []
modes = stage.get("modes") or all_modes
model_fingerprints = []
if targets:
    ok(f"targets configured={len(targets)}")
else:
    bad("no targets configured")
if modes:
    ok(f"stage modes configured={len(modes)}")
else:
    bad("no modes configured")
unknown_modes = [mode for mode in modes if mode not in all_modes]
if unknown_modes:
    bad(f"stage {stage_name} references unknown mode(s): {unknown_modes}")

for target in targets:
    config_path = pathlib.Path(target.get("config", ""))
    if not config_path.exists():
        bad(f"{target.get('id')}: config missing: {config_path}")
        continue
    cfg = yaml.safe_load(config_path.read_text(encoding="utf-8")) or {}
    target_cfg = cfg.get("target") or {}
    static_cfg = cfg.get("static_analysis") or {}
    model_cfg = cfg.get("model_api") or {}
    binary = pathlib.Path(target_cfg.get("binary", ""))
    input_dir = pathlib.Path(target_cfg.get("input_dir", ""))
    dict_path = pathlib.Path(target_cfg.get("dict", "")) if target_cfg.get("dict") else None
    if binary.exists() and binary.is_file():
        ok(f"{target.get('id')}: target binary present")
        try:
            is_elf = binary.read_bytes()[:4] == b"\x7fELF"
        except OSError:
            is_elf = False
        if is_elf:
            ok(f"{target.get('id')}: target binary is ELF for this Linux host")
        else:
            bad(f"{target.get('id')}: target binary is not ELF: {binary}")
    else:
        bad(f"{target.get('id')}: target binary missing: {binary}")
    if input_dir.exists() and any(input_dir.iterdir()):
        ok(f"{target.get('id')}: seed directory populated")
    else:
        bad(f"{target.get('id')}: seed directory missing or empty: {input_dir}")
    if dict_path is not None:
        if dict_path.exists():
            ok(f"{target.get('id')}: dictionary present")
        else:
            caution(f"{target.get('id')}: configured dictionary missing: {dict_path}")
    if static_cfg.get("enabled"):
        context_path = pathlib.Path(static_cfg.get("context_path", ""))
        if not context_path.exists():
            bad(f"{target.get('id')}: static_analysis.context_path missing: {context_path}")
        else:
            try:
                context = yaml.safe_load(context_path.read_text(encoding="utf-8")) or {}
            except Exception as exc:
                bad(f"{target.get('id')}: static context is unreadable: {exc}")
                continue
            tokens = context.get("magic_tokens") or []
            backend = context.get("backend", "")
            if tokens:
                ok(f"{target.get('id')}: static context tokens={len(tokens)} backend={backend}")
            else:
                bad(f"{target.get('id')}: static context has zero magic_tokens: {context_path}")
    if model_cfg.get("enabled", True):
        endpoint = model_cfg.get("endpoint", "")
        fingerprint = (
            model_cfg.get("provider", ""),
            model_cfg.get("model", ""),
            urlparse(endpoint).netloc if endpoint else "",
            model_cfg.get("api_key_env", ""),
        )
        model_fingerprints.append((target.get("id"), fingerprint))

unique_models = {fp for _, fp in model_fingerprints}
if len(unique_models) == 1 and model_fingerprints:
    provider, model, host, key_env = next(iter(unique_models))
    ok(f"model config consistent: provider={provider} model={model} host={host} key_env={key_env}")
else:
    for target_id, fp in model_fingerprints:
        caution(f"{target_id}: model fingerprint={fp}")
    bad("architecture pilot targets do not use one consistent model provider/model/endpoint/key env")

if fail:
    raise SystemExit(10)
PY
then
  :
else
  fail=$((fail + 1))
fi

if [[ -n "${FUZZPILOT_MODEL_API_KEY:-}" ]]; then
  pass "FUZZPILOT_MODEL_API_KEY is set"
else
  note_warn "FUZZPILOT_MODEL_API_KEY is unset; LLM-bearing cells will be skipped in real runs"
fi

echo
echo "preflight summary: fail=${fail} warn=${warn}"
if [[ "${fail}" -ne 0 ]]; then
  exit 1
fi
