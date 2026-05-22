# .devcontainer/Dockerfile
#
# Devcontainer image for GitHub Codespaces / VS Code Dev Containers.
# Mirrors the toolchain layers of docker/ubuntu/Dockerfile (system deps,
# AFL++ from source, Ghidra) but does NOT bake the source tree or build
# artifacts into the image — the workspace is bind-mounted instead.
#
# First build: ~10-15 minutes (AFL++ source build + Ghidra download).
# Subsequent container starts: seconds.

FROM ubuntu:22.04

ARG DEBIAN_FRONTEND=noninteractive
ARG AFLPP_REPO=https://github.com/AFLplusplus/AFLplusplus.git
ARG AFLPP_REF=v4.21c
ARG AFLPP_MAKE_JOBS=2
ARG GHIDRA_VERSION=11.1.2_PUBLIC
ARG GHIDRA_RELEASE=20240709
ARG GHIDRA_URL=https://github.com/NationalSecurityAgency/ghidra/releases/download/Ghidra_11.1.2_build/ghidra_${GHIDRA_VERSION}_${GHIDRA_RELEASE}.zip

RUN for i in 1 2 3 4 5; do \
      apt-get update -o Acquire::Retries=3 -o Acquire::http::No-Cache=true && break; \
      echo "apt-get update attempt $i failed, retrying after $((i*15))s"; \
      sleep "$((i * 15))"; \
    done \
  && apt-get install -y --no-install-recommends \
    automake \
    bash \
    bison \
    build-essential \
    ca-certificates \
    clang \
    cmake \
    curl \
    file \
    flex \
    git \
    gnupg \
    less \
    libglib2.0-dev \
    libpng-dev \
    libpixman-1-dev \
    libsqlite3-dev \
    lld \
    llvm-dev \
    locales \
    nano \
    ninja-build \
    openjdk-17-jdk-headless \
    openssh-client \
    pkg-config \
    python3 \
    python3-dev \
    python3-matplotlib \
    python3-pandas \
    python3-pip \
    python3-setuptools \
    python3-yaml \
    sqlite3 \
    sudo \
    tmux \
    unzip \
    vim \
    wget \
    zlib1g-dev \
  && locale-gen en_US.UTF-8 \
  && rm -rf /var/lib/apt/lists/*

ENV LANG=en_US.UTF-8

# AFL++ from source. Pinned to match docker/ubuntu/Dockerfile.
RUN for i in 1 2 3; do \
      git -c http.version=HTTP/1.1 clone --depth 1 --branch "${AFLPP_REF}" "${AFLPP_REPO}" /tmp/aflpp && break; \
      rm -rf /tmp/aflpp; \
      sleep "$((i * 5))"; \
    done \
  && cd /tmp/aflpp \
  && make source-only -j"${AFLPP_MAKE_JOBS}" \
  && make install \
  && cd / && rm -rf /tmp/aflpp \
  && command -v afl-fuzz \
  && command -v afl-clang-fast

# Ghidra.
RUN mkdir -p /opt \
  && wget -q -O /tmp/ghidra.zip "${GHIDRA_URL}" \
  && unzip -q /tmp/ghidra.zip -d /opt \
  && mv "/opt/ghidra_${GHIDRA_VERSION}" /opt/ghidra \
  && rm /tmp/ghidra.zip

ENV GHIDRA_INSTALL_DIR=/opt/ghidra \
    FUZZPILOT_GHIDRA_HOME=/opt/ghidra \
    AFL_I_DONT_CARE_ABOUT_MISSING_CRASHES=1 \
    AFL_SKIP_CPUFREQ=1 \
    AFL_SKIP_CRASHES=1 \
    PATH="/opt/ghidra/support:${PATH}" \
    CC=clang \
    CXX=clang++

CMD ["sleep", "infinity"]
