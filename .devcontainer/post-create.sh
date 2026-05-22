#!/usr/bin/env bash
# .devcontainer/post-create.sh
#
# Runs once after the devcontainer is created. Mirrors the runtime
# preparation that docker/ubuntu/Dockerfile bakes into the production
# image, but for the bind-mounted workspace.
#
# Idempotent: re-running is safe. Build/seed steps are best-effort —
# warnings here don't block the container from coming up.

set -euo pipefail

cd /work/fuzz_agent

CJSON_REF=fb16e5cf358798aabb049655975cde8427101056
LIBPNG_REF=9ec49c2d56cec19107ddc458b648ce224c9697b3

clone_target() {
  local dest="$1" repo="$2" ref="$3"
  if [[ -d "${dest}/.git" ]]; then
    return 0
  fi
  rm -rf "${dest}"
  for i in 1 2 3; do
    if git -c http.version=HTTP/1.1 clone "${repo}" "${dest}"; then
      git -C "${dest}" checkout "${ref}"
      return 0
    fi
    rm -rf "${dest}"
    sleep "$((i * 5))"
  done
  echo "WARN: failed to clone ${repo}" >&2
  return 1
}

clone_target experiments/targets/cjson/src https://github.com/DaveGamble/cJSON.git "${CJSON_REF}" || true
clone_target experiments/targets/libpng/src https://github.com/pnggroup/libpng.git "${LIBPNG_REF}" || true

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j"$(nproc)"

if [[ -x scripts/build_ubuntu_targets.sh ]]; then
  FUZZPILOT_REQUIRE_TARGETS=0 scripts/build_ubuntu_targets.sh || echo "WARN: target build incomplete"
fi

if [[ -x scripts/init_seeds.sh ]]; then
  bash scripts/init_seeds.sh || echo "WARN: seed init incomplete"
fi

cat <<'EOF'

FuzzPilot devcontainer is ready.

Next steps:
  - Set FUZZPILOT_MODEL_API_KEY as a Codespaces secret (repo Settings -> Codespaces).
  - Smoke test:     ./build/fuzzpilot check-config --config experiments/targets/cjson/config.yaml --runtime
  - Short batch:    bash scripts/paper01/run_batch.sh --exp E1a --parallel 4 --budget-sec 600

EOF
