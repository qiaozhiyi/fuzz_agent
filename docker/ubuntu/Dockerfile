# docker/ubuntu/Dockerfile
#
# FuzzPilot Paper 1 build & runtime image.
#
# Provides a self-contained Linux container environment with:
#   - Ubuntu 22.04 base
#   - OpenJDK 17 + Ghidra (pinned version) for static analysis
#   - AFL++ built from source at a pinned commit
#   - Build toolchain (cmake / ninja / clang / git)
#   - Python deps for aggregation / plotting from Ubuntu packages
#   - FuzzPilot source + target source checkouts + build output (under /work/fuzz_agent/build)
#   - Initialized seed corpora for cJSON and libpng
#
# Usage:
#   scripts/fuzzpilot_docker.sh build
#   scripts/fuzzpilot_docker.sh preflight
#   FUZZPILOT_MODEL_API_KEY="$KEY" \
#     scripts/fuzzpilot_docker.sh run-batch --exp E1a --parallel 4

FROM ubuntu:22.04

ARG DEBIAN_FRONTEND=noninteractive
ARG TARGETPLATFORM
ARG TARGETARCH
ARG BUILDPLATFORM
ARG VCS_REF=unknown
ARG VCS_BRANCH=unknown
ARG VCS_DIRTY=unknown

# Pinned versions. Bump intentionally with a paper-tagged commit.
ARG AFLPP_REPO=https://github.com/AFLplusplus/AFLplusplus.git
ARG AFLPP_REF=v4.21c
ARG AFLPP_MAKE_JOBS=1
ARG CJSON_REPO=https://github.com/DaveGamble/cJSON.git
ARG CJSON_REF=fb16e5cf358798aabb049655975cde8427101056
ARG LIBPNG_REPO=https://github.com/pnggroup/libpng.git
ARG LIBPNG_REF=9ec49c2d56cec19107ddc458b648ce224c9697b3
ARG GHIDRA_VERSION=11.1.2_PUBLIC
ARG GHIDRA_RELEASE=20240709
ARG GHIDRA_URL=https://github.com/NationalSecurityAgency/ghidra/releases/download/Ghidra_11.1.2_build/ghidra_${GHIDRA_VERSION}_${GHIDRA_RELEASE}.zip

# 1) Base toolchain + libs needed to build targets, AFL++, and FuzzPilot.
RUN apt-get update && apt-get install -y --no-install-recommends \
    automake \
    bash \
    bison \
    build-essential \
    ca-certificates \
    clang \
    cmake \
    curl \
    flex \
    file \
    git \
    libglib2.0-dev \
    libpng-dev \
    libpixman-1-dev \
    libsqlite3-dev \
    lld \
    llvm-dev \
    ninja-build \
    openjdk-17-jdk-headless \
    pkg-config \
    python3 \
    python3-dev \
    python3-matplotlib \
    python3-pandas \
    python3-setuptools \
    python3-yaml \
    sqlite3 \
    unzip \
    wget \
    zlib1g-dev \
  && rm -rf /var/lib/apt/lists/*

# 2) AFL++ from source at pinned ref. Use source-only support so both amd64 and
#    arm64 Docker builds avoid binary-only/QEMU/Frida build dependencies.
RUN for i in 1 2 3; do \
      git -c http.version=HTTP/1.1 clone --depth 1 --branch "${AFLPP_REF}" "${AFLPP_REPO}" /tmp/aflpp && break; \
      rm -rf /tmp/aflpp; \
      sleep "$((i * 5))"; \
    done \
  && test -d /tmp/aflpp/.git \
  && cd /tmp/aflpp \
  && if [ "${TARGETARCH}" != "amd64" ]; then export AFL_NO_X86=1; fi \
  && make source-only -j"${AFLPP_MAKE_JOBS}" \
  && make install \
  && cd / && rm -rf /tmp/aflpp \
  && command -v afl-fuzz \
  && command -v afl-clang-fast

# 3) Ghidra (download + install to /opt/ghidra).
RUN mkdir -p /opt \
  && wget -q -O /tmp/ghidra.zip "${GHIDRA_URL}" \
  && unzip -q /tmp/ghidra.zip -d /opt \
  && mv "/opt/ghidra_${GHIDRA_VERSION}" /opt/ghidra \
  && rm /tmp/ghidra.zip
ENV GHIDRA_INSTALL_DIR=/opt/ghidra
ENV FUZZPILOT_GHIDRA_HOME=/opt/ghidra
ENV PATH="/opt/ghidra/support:${PATH}"
ENV AFL_I_DONT_CARE_ABOUT_MISSING_CRASHES=1
ENV FUZZPILOT_IMAGE_METADATA=/work/fuzz_agent/build/fuzzpilot_image_metadata.env
ENV FUZZPILOT_IMAGE_PLATFORM="${TARGETPLATFORM}"

# 4) Project source.
WORKDIR /work/fuzz_agent
COPY . /work/fuzz_agent/

# 5) Target source checkouts. Do not rely on host submodules or a copied .git.
RUN rm -rf experiments/targets/cjson/src experiments/targets/libpng/src \
  && for i in 1 2 3; do \
      git -c http.version=HTTP/1.1 clone "${CJSON_REPO}" experiments/targets/cjson/src && break; \
      rm -rf experiments/targets/cjson/src; \
      sleep "$((i * 5))"; \
    done \
  && test -d experiments/targets/cjson/src/.git \
  && git -C experiments/targets/cjson/src checkout "${CJSON_REF}" \
  && rm -rf experiments/targets/cjson/src/.git \
  && for i in 1 2 3; do \
      git -c http.version=HTTP/1.1 clone "${LIBPNG_REPO}" experiments/targets/libpng/src && break; \
      rm -rf experiments/targets/libpng/src; \
      sleep "$((i * 5))"; \
    done \
  && test -d experiments/targets/libpng/src/.git \
  && git -C experiments/targets/libpng/src checkout "${LIBPNG_REF}" \
  && rm -rf experiments/targets/libpng/src/.git

# 6) Build FuzzPilot.
ENV CC=clang
ENV CXX=clang++
RUN cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  && cmake --build build -j"$(nproc)" \
  && ctest --test-dir build --output-on-failure

# 7) Build fuzz targets (vuln_target + cjson + libpng).
RUN FUZZPILOT_REQUIRE_TARGETS=1 scripts/build_ubuntu_targets.sh

# 8) Seed corpora.
RUN bash scripts/init_seeds.sh

# 9) Build metadata used at runtime when .git is intentionally absent.
RUN mkdir -p build \
  && { \
    printf 'vcs_ref=%s\n' "${VCS_REF}"; \
    printf 'vcs_branch=%s\n' "${VCS_BRANCH}"; \
    printf 'vcs_dirty=%s\n' "${VCS_DIRTY}"; \
    printf 'target_platform=%s\n' "${TARGETPLATFORM}"; \
    printf 'target_arch=%s\n' "${TARGETARCH}"; \
    printf 'build_platform=%s\n' "${BUILDPLATFORM}"; \
    printf 'aflpp_ref=%s\n' "${AFLPP_REF}"; \
    printf 'cjson_ref=%s\n' "${CJSON_REF}"; \
    printf 'libpng_ref=%s\n' "${LIBPNG_REF}"; \
    printf 'ghidra_version=%s\n' "${GHIDRA_VERSION}"; \
  } > "${FUZZPILOT_IMAGE_METADATA}"

# 10) Permissions on driver scripts.
RUN chmod +x scripts/paper01/*.sh scripts/paper01/*.py scripts/init_seeds.sh docker/ubuntu/entrypoint.sh

# 11) Entrypoint.
ENTRYPOINT ["/work/fuzz_agent/docker/ubuntu/entrypoint.sh"]
CMD ["preflight"]
