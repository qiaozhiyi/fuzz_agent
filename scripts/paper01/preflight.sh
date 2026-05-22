#!/usr/bin/env bash
# scripts/paper01/preflight.sh
#
# Run before any Paper 1 experiment. Verifies the environment is in a
# state where reproducible runs can begin. Exits non-zero on any red.
#
# Modes:
#   default        — full check (Docker container OR host)
#   --host         — running on host before docker build; skip in-container checks
#   --in-container — running inside the Docker image (default if /.dockerenv exists)
#   --canonical    — require FUZZPILOT_CANONICAL_PLATFORM (default linux/amd64)
#
# Checks:
#   - OS / arch sanity (Linux amd64 and arm64 containers pass)
#   - AFL++ installed and version captured
#   - FuzzPilot built (build/fuzzpilot)
#   - Mutator microbench built (build/tools/mutator_microbench/mutator_microbench)
#   - FuzzPilot mutator shared lib present (ELF .so expected in container)
#   - cJSON target binary present
#   - cJSON seeds count >= 20
#   - libpng seeds count >= 20 (warn only)
#   - Java + Ghidra headless present (for full-agent)
#   - Python deps: pandas, matplotlib, yaml
#   - Model API key env var present (warn only — required at run time, not build time)
#   - Disk space sufficient (≥ 50 GB free)
#   - Git working tree clean OR explicit override
#   - CPU governor performance (warn only)
#   - No stale API key in tracked artifacts

set -uo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
FAIL=0
WARN=0
IN_CONTAINER=0
[[ -f /.dockerenv ]] && IN_CONTAINER=1
REQUIRE_CANONICAL="${FUZZPILOT_REQUIRE_CANONICAL_PLATFORM:-0}"
for arg in "$@"; do
  case "$arg" in
    --host)         IN_CONTAINER=0 ;;
    --in-container) IN_CONTAINER=1 ;;
    --canonical)    REQUIRE_CANONICAL=1 ;;
  esac
done
CANONICAL_PLATFORM="${FUZZPILOT_CANONICAL_PLATFORM:-linux/amd64}"

red()    { printf "  \033[31m✗\033[0m %s\n" "$*"; FAIL=$((FAIL+1)); }
yellow() { printf "  \033[33m!\033[0m %s\n" "$*"; WARN=$((WARN+1)); }
green()  { printf "  \033[32m✓\033[0m %s\n" "$*"; }

normalize_arch() {
  case "$1" in
    x86_64|amd64) echo "amd64" ;;
    aarch64|arm64) echo "arm64" ;;
    *) echo "$1" ;;
  esac
}

metadata_value() {
  local key="$1"
  local file="${FUZZPILOT_IMAGE_METADATA:-${REPO_ROOT}/build/fuzzpilot_image_metadata.env}"
  [[ -f "${file}" ]] || return 1
  awk -F= -v k="${key}" '$1 == k { sub(/^[^=]*=/, ""); print; found=1; exit } END { exit found ? 0 : 1 }' "${file}"
}

echo "FuzzPilot Paper 1 preflight"
echo "repo: ${REPO_ROOT}"
echo "mode: $([[ ${IN_CONTAINER} -eq 1 ]] && echo in-container || echo host)"
echo

# --- OS / arch
echo "[env]"
OS="$(uname -s)"
ARCH="$(uname -m)"
NORM_ARCH="$(normalize_arch "${ARCH}")"
RUNTIME_PLATFORM="unknown"
if [[ "${OS}" == "Linux" && ( "${NORM_ARCH}" == "amd64" || "${NORM_ARCH}" == "arm64" ) ]]; then
  RUNTIME_PLATFORM="linux/${NORM_ARCH}"
  green "OS/arch: ${RUNTIME_PLATFORM}"
elif [[ "${IN_CONTAINER}" -eq 1 ]]; then
  red "OS/arch: ${OS} ${ARCH} (container expects Linux amd64 or arm64)"
else
  yellow "OS/arch: ${OS} ${ARCH} (host dev only; experiments should run in Docker)"
fi
if [[ "${IN_CONTAINER}" -eq 1 ]]; then
  IMAGE_PLATFORM="${FUZZPILOT_IMAGE_PLATFORM:-$(metadata_value target_platform 2>/dev/null || echo "${RUNTIME_PLATFORM}")}"
  green "image platform: ${IMAGE_PLATFORM}"
  if [[ "${REQUIRE_CANONICAL}" == "1" ]]; then
    if [[ "${IMAGE_PLATFORM}" == "${CANONICAL_PLATFORM}" || "${RUNTIME_PLATFORM}" == "${CANONICAL_PLATFORM}" ]]; then
      green "canonical platform: ${CANONICAL_PLATFORM}"
    else
      red "canonical platform required: ${CANONICAL_PLATFORM}; current=${IMAGE_PLATFORM}/${RUNTIME_PLATFORM}"
    fi
  elif [[ "${IMAGE_PLATFORM}" != "${CANONICAL_PLATFORM}" && "${RUNTIME_PLATFORM}" != "${CANONICAL_PLATFORM}" ]]; then
    yellow "non-canonical platform: ${IMAGE_PLATFORM} (paper-comparable data default is ${CANONICAL_PLATFORM})"
  fi
fi

# --- AFL++
echo "[afl++]"
if command -v afl-fuzz >/dev/null 2>&1; then
  green "afl-fuzz available: $(command -v afl-fuzz)"
else
  if [[ ${IN_CONTAINER} -eq 1 ]]; then
    red "afl-fuzz not on PATH"
  else
    yellow "afl-fuzz not on host (OK — installed inside container)"
  fi
fi

# --- FuzzPilot binary
echo "[fuzzpilot binary]"
if [[ -x "${REPO_ROOT}/build/fuzzpilot" ]]; then
  green "build/fuzzpilot present"
else
  if [[ ${IN_CONTAINER} -eq 1 ]]; then
    red "build/fuzzpilot missing — run: cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build -j"
  else
    yellow "build/fuzzpilot missing on host (OK — built inside container)"
  fi
fi

# --- microbench binary
echo "[microbench binary]"
if [[ -x "${REPO_ROOT}/build/tools/mutator_microbench/mutator_microbench" ]]; then
  green "mutator_microbench built"
else
  if [[ ${IN_CONTAINER} -eq 1 ]]; then
    red "mutator_microbench missing"
  else
    yellow "mutator_microbench missing on host (OK — built inside container)"
  fi
fi

# --- mutator shared lib
echo "[mutator shared lib]"
MUT_SO="${REPO_ROOT}/build/mutators/fuzzpilot/libfuzzpilot_mutator.so"
MUT_DYLIB="${REPO_ROOT}/build/mutators/fuzzpilot/libfuzzpilot_mutator.dylib"
if [[ ${IN_CONTAINER} -eq 1 ]]; then
  if [[ -f "${MUT_SO}" ]]; then
    if command -v file >/dev/null 2>&1; then
      if file "${MUT_SO}" | grep -q "ELF"; then
        green "mutator: ${MUT_SO} (ELF for ${NORM_ARCH})"
      else
        red "mutator: ${MUT_SO} is not ELF (rm -rf build/ and rebuild inside the container)"
      fi
    else
      green "mutator: ${MUT_SO}"
    fi
  else
    red "FuzzPilot mutator .so not found at ${MUT_SO}"
  fi
else
  if [[ -f "${MUT_SO}" || -f "${MUT_DYLIB}" ]]; then
    green "mutator artifact present"
  else
    yellow "mutator not built on host (OK — built inside container)"
  fi
fi

# --- cJSON target
echo "[cjson target]"
if [[ -x "${REPO_ROOT}/experiments/targets/cjson/cjson_fuzzer" ]]; then
  green "cjson_fuzzer present"
  if [[ ${IN_CONTAINER} -eq 1 ]] && command -v file >/dev/null 2>&1; then
    if file "${REPO_ROOT}/experiments/targets/cjson/cjson_fuzzer" | grep -q "ELF"; then
      green "cjson_fuzzer is ELF"
    else
      red "cjson_fuzzer is not an ELF binary; rebuild target inside Docker"
    fi
  fi
  if command -v shasum >/dev/null; then
    HASH="$(shasum -a 256 "${REPO_ROOT}/experiments/targets/cjson/cjson_fuzzer" | awk '{print $1}')"
    green "cjson_fuzzer sha256=${HASH}"
  fi
else
  if [[ ${IN_CONTAINER} -eq 1 ]]; then
    red "cjson_fuzzer not built — run: scripts/build_ubuntu_targets.sh"
  else
    yellow "cjson_fuzzer missing on host (OK — built inside container)"
  fi
fi

# --- additional target binaries
echo "[other targets]"
for pair in \
  "vuln_target:${REPO_ROOT}/experiments/targets/vuln_target/vuln" \
  "libpng:${REPO_ROOT}/experiments/targets/libpng/libpng_fuzzer"; do
  label="${pair%%:*}"
  path="${pair#*:}"
  if [[ -x "${path}" ]]; then
    if [[ ${IN_CONTAINER} -eq 1 ]] && command -v file >/dev/null 2>&1; then
      if file "${path}" | grep -q "ELF"; then
        green "${label}: ELF binary present"
      else
        red "${label}: not an ELF binary; rebuild target inside Docker"
      fi
    else
      green "${label}: binary present"
    fi
  else
    if [[ ${IN_CONTAINER} -eq 1 ]]; then
      red "${label}: binary missing at ${path}"
    else
      yellow "${label}: binary missing on host (OK — built inside container)"
    fi
  fi
done

# --- seeds
echo "[seeds]"
CJSON_SEEDS="$(find "${REPO_ROOT}/experiments/targets/cjson/seeds" -type f 2>/dev/null | wc -l | tr -d ' ')"
if [[ "${CJSON_SEEDS}" -ge 20 ]]; then
  green "cjson: ${CJSON_SEEDS} seed files"
elif [[ "${CJSON_SEEDS}" -gt 0 ]]; then
  yellow "cjson: only ${CJSON_SEEDS} seed files (recommend >=20; run scripts/init_seeds.sh)"
else
  red "cjson: no seeds under experiments/targets/cjson/seeds/"
fi
LIBPNG_SEEDS="$(find "${REPO_ROOT}/experiments/targets/libpng/seeds" -type f 2>/dev/null | wc -l | tr -d ' ')"
if [[ "${LIBPNG_SEEDS}" -ge 20 ]]; then
  green "libpng: ${LIBPNG_SEEDS} seed files"
elif [[ "${LIBPNG_SEEDS}" -gt 0 ]]; then
  yellow "libpng: only ${LIBPNG_SEEDS} seed files (recommend >=20)"
else
  yellow "libpng: no seeds (optional E5)"
fi

# --- Java + Ghidra
echo "[ghidra]"
if command -v java >/dev/null 2>&1; then
  JAVA_VER="$(java -version 2>&1 | head -1)"
  green "java: ${JAVA_VER}"
else
  if [[ ${IN_CONTAINER} -eq 1 ]]; then
    red "java not on PATH (required for Ghidra headless)"
  else
    yellow "java not on host (OK — installed inside container)"
  fi
fi
GHIDRA_HOME="${FUZZPILOT_GHIDRA_HOME:-/opt/ghidra}"
if [[ -x "${GHIDRA_HOME}/support/analyzeHeadless" ]]; then
  green "Ghidra headless: ${GHIDRA_HOME}/support/analyzeHeadless"
else
  if [[ ${IN_CONTAINER} -eq 1 ]]; then
    red "Ghidra headless missing at ${GHIDRA_HOME}/support/analyzeHeadless"
  else
    yellow "Ghidra missing on host (OK — installed inside container)"
  fi
fi

# --- Python deps
echo "[python deps]"
if command -v python3 >/dev/null 2>&1; then
  green "python3: $(python3 -V)"
  for mod in pandas matplotlib yaml; do
    if python3 -c "import ${mod}" 2>/dev/null; then
      green "  python module: ${mod}"
    else
      if [[ ${IN_CONTAINER} -eq 1 ]]; then
        red "  python module missing: ${mod}"
      else
        yellow "  python module missing on host: ${mod} (OK — installed in container)"
      fi
    fi
  done
else
  red "python3 not on PATH"
fi

# --- model API key (only warn — needed at run time, not preflight time)
echo "[model api]"
if [[ -n "${FUZZPILOT_MODEL_API_KEY:-}" ]]; then
  green "FUZZPILOT_MODEL_API_KEY set (length=${#FUZZPILOT_MODEL_API_KEY})"
else
  yellow "FUZZPILOT_MODEL_API_KEY unset — required at run time for full-agent (E1b, E2b, E2c)"
fi

# --- disk
echo "[disk]"
FREE_GB=$(df -Pk "${REPO_ROOT}" 2>/dev/null | awk 'NR==2 {printf "%.0f", $4/1024/1024}')
if [[ -n "${FREE_GB}" && "${FREE_GB}" -ge 50 ]]; then
  green "free disk: ${FREE_GB} GB"
elif [[ -n "${FREE_GB}" ]]; then
  red "free disk: ${FREE_GB} GB (<50 GB; insufficient for 19 runs)"
else
  yellow "could not determine free disk"
fi

# --- git state
echo "[git]"
if [[ -d "${REPO_ROOT}/.git" ]]; then
  COMMIT="$(git -C "${REPO_ROOT}" rev-parse HEAD 2>/dev/null || echo unknown)"
  green "commit: ${COMMIT}"
  if [[ -n "$(git -C "${REPO_ROOT}" status --porcelain 2>/dev/null)" ]]; then
    yellow "working tree dirty — record git.patch with each run for reproducibility"
  else
    green "working tree clean"
  fi
else
  META_REF="$(metadata_value vcs_ref 2>/dev/null || echo unknown)"
  META_BRANCH="$(metadata_value vcs_branch 2>/dev/null || echo unknown)"
  META_DIRTY="$(metadata_value vcs_dirty 2>/dev/null || echo unknown)"
  green "image metadata: commit=${META_REF} branch=${META_BRANCH} dirty=${META_DIRTY}"
fi

# --- CPU governor (Linux only)
echo "[cpu]"
if [[ -f /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor ]]; then
  GOV="$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor)"
  if [[ "${GOV}" == "performance" ]]; then
    green "cpu governor: performance"
  else
    yellow "cpu governor: ${GOV} (recommend: sudo cpupower frequency-set -g performance)"
  fi
else
  yellow "cpu governor unknown (not Linux?)"
fi

# --- API key leaks
echo "[api-key scan]"
if [[ -d "${REPO_ROOT}/.git" ]]; then
  LEAKS="$(git -C "${REPO_ROOT}" grep -nE 'sk-[A-Za-z0-9_-]{20,}' -- ':!docs/**' 2>/dev/null || true)"
  if [[ -z "${LEAKS}" ]]; then
    green "no obvious API-key patterns in tracked source"
  else
    red "possible API key leaks:"
    echo "${LEAKS}"
  fi
fi

# --- manifest sanity
echo "[manifest]"
MANIFEST="${REPO_ROOT}/experiments/manifests/paper01_preprint.yaml"
if [[ -f "${MANIFEST}" ]]; then
  green "manifest present: ${MANIFEST}"
else
  red "manifest missing: ${MANIFEST}"
fi

echo
if [[ ${FAIL} -eq 0 ]]; then
  echo -e "preflight: \033[32mPASS\033[0m  warnings=${WARN}"
  exit 0
else
  echo -e "preflight: \033[31mFAIL\033[0m  failures=${FAIL} warnings=${WARN}"
  exit 1
fi
