#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE="${FUZZPILOT_UBUNTU_IMAGE:-fuzzpilot-ubuntu-smoke}"
PLATFORM="${FUZZPILOT_DOCKER_PLATFORM:-linux/amd64}"
BUILD_DIR="${FUZZPILOT_DOCKER_BUILD_DIR:-/tmp/fuzzpilot-build}"
REBUILD_IMAGE="${FUZZPILOT_DOCKER_REBUILD_IMAGE:-0}"

if [ "$REBUILD_IMAGE" = "1" ] || ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
  docker build --platform "$PLATFORM" -t "$IMAGE" -f "$ROOT_DIR/docker/ubuntu/Dockerfile" "$ROOT_DIR"
else
  echo "[docker] reusing existing image: $IMAGE"
  echo "[docker] set FUZZPILOT_DOCKER_REBUILD_IMAGE=1 to rebuild it"
fi

docker run --rm --platform "$PLATFORM" \
  -v "$ROOT_DIR:/work/fuzz_agent:ro" \
  "$IMAGE" \
  bash -lc "
    set -euo pipefail
    cmake -S /work/fuzz_agent -B '$BUILD_DIR' -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
    cmake --build '$BUILD_DIR'
    ctest --test-dir '$BUILD_DIR' --output-on-failure
    test -f '$BUILD_DIR/mutators/fuzzpilot/libfuzzpilot_mutator.so'
    test -e '$BUILD_DIR/mutators/fuzzpilot/libfuzzpilot_mutator'
    rm -rf /tmp/fuzzpilot-targets
    mkdir -p /tmp/fuzzpilot-targets
    tar -C /work/fuzz_agent \
      --exclude='./.git' \
      --exclude='./.idea' \
      --exclude='./build' \
      --exclude='./build-*' \
      --exclude='./build-make' \
      --exclude='./cmake-build-*' \
      --exclude='./results' \
      --exclude='./work' \
      --exclude='./work_*' \
      -cf - . | tar -C /tmp/fuzzpilot-targets -xf -
    ln -s '$BUILD_DIR' /tmp/fuzzpilot-targets/build
    cd /tmp/fuzzpilot-targets
    FUZZPILOT_REQUIRE_TARGETS=1 scripts/build_ubuntu_targets.sh
    file experiments/targets/vuln_target/vuln | grep -q 'ELF 64-bit.*x86-64'
    file experiments/targets/cjson/cjson_fuzzer | grep -q 'ELF 64-bit.*x86-64'
    file experiments/targets/libpng/libpng_fuzzer | grep -q 'ELF 64-bit.*x86-64'
    for config in \
      experiments/targets/vuln_target/config.yaml \
      experiments/targets/cjson/config.yaml \
      experiments/targets/libpng/config.yaml; do
      '$BUILD_DIR/fuzzpilot' check-config --config \"\$config\" --runtime >/tmp/fuzzpilot_check_config.log
    done
    '$BUILD_DIR/fuzzpilot' afl-command \
      --config experiments/targets/cjson/config.yaml \
      --output-dir /tmp/fuzzpilot_cmd_preview | grep -q 'libfuzzpilot_mutator'
  "
