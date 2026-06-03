#!/usr/bin/env bash
set -euo pipefail

TARGET_DIR="/root/fuzz_agent/experiments/targets/libxml2"
SRC_DIR="$TARGET_DIR/src"
INSTALL_DIR="$TARGET_DIR/build/install"

echo "=== 1. Cleaning old build ==="
cd "$SRC_DIR"
make distclean || true
rm -rf "$INSTALL_DIR"
mkdir -p "$INSTALL_DIR"

echo "=== 2. Configuring libxml2 with afl-clang-fast ==="
export CC="afl-clang-fast"
export CXX="afl-clang-fast++"
export CFLAGS="-O2 -g"
export CXXFLAGS="-O2 -g"

./configure \
  --prefix="$INSTALL_DIR" \
  --disable-shared \
  --without-python \
  --without-iconv \
  --without-icu \
  --without-zlib \
  --without-lzma \
  --without-debug

echo "=== 3. Compiling libxml2 ==="
make -j$(nproc)

echo "=== 4. Installing libxml2 to build/install ==="
make install

echo "=== 5. Compiling harness with afl-clang-fast and static linking ==="
cd "$TARGET_DIR"
rm -f libxml2_fuzzer
afl-clang-fast harness.c \
  -I "$INSTALL_DIR/include/libxml2" \
  "$INSTALL_DIR/lib/libxml2.a" \
  -O2 -g -lm -o libxml2_fuzzer

echo "=== 6. Verifying compiled binary ==="
ls -lh libxml2_fuzzer
ldd libxml2_fuzzer || true
echo "Rebuild completed successfully!"
