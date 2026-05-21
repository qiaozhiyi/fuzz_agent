FROM ubuntu:22.04

ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    afl++ \
    bash \
    build-essential \
    ca-certificates \
    clang \
    cmake \
    curl \
    file \
    git \
    libcjson-dev \
    libpng-dev \
    libsqlite3-dev \
    ninja-build \
    pkg-config \
    python3 \
    sqlite3 \
    zlib1g-dev \
  && rm -rf /var/lib/apt/lists/*

ENV CC=clang
ENV CXX=clang++

WORKDIR /work/fuzz_agent
