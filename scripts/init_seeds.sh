#!/usr/bin/env bash
# scripts/init_seeds.sh
#
# Populate experiments/targets/{cjson,libpng}/seeds/ with a diverse set
# of representative inputs sourced from the upstream submodule trees.
#
# Idempotent. Safe to re-run. Will NOT overwrite existing files.
#
# Requirements:
#   - cJSON  submodule at experiments/targets/cjson/src/   (auto-init if missing)
#   - libpng submodule at experiments/targets/libpng/src/  (auto-init if missing)
#
# Targets:
#   cJSON  >= 20 seeds (canonical small/medium/edge JSON inputs)
#   libpng >= 20 seeds (varied bit-depths, color types, filters)

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CJSON_SEED_DIR="${REPO_ROOT}/experiments/targets/cjson/seeds"
LIBPNG_SEED_DIR="${REPO_ROOT}/experiments/targets/libpng/seeds"

ensure_submodule() {
  local path="$1"
  if [[ ! -e "${REPO_ROOT}/${path}/.git" && ! -d "${REPO_ROOT}/${path}/.git" ]]; then
    if [[ -z "$(ls -A "${REPO_ROOT}/${path}" 2>/dev/null)" ]]; then
      echo "[seeds] initializing submodule: ${path}"
      git -C "${REPO_ROOT}" submodule update --init -- "${path}" || true
    fi
  fi
}

mkdir -p "${CJSON_SEED_DIR}" "${LIBPNG_SEED_DIR}"
ensure_submodule "experiments/targets/cjson/src"
ensure_submodule "experiments/targets/libpng/src"

# ---------- cJSON seeds ----------

# Always include the original seed.
if [[ ! -f "${CJSON_SEED_DIR}/seed1.json" ]]; then
  cat >"${CJSON_SEED_DIR}/seed1.json" <<'JSON'
{"hello":"world"}
JSON
fi

# Hand-written canonical seeds covering JSON spec corners.
declare -A HAND=(
  [empty_object]='{}'
  [empty_array]='[]'
  [single_string]='"abc"'
  [single_number]='42'
  [boolean_true]='true'
  [boolean_false]='false'
  [null_value]='null'
  [nested_object]='{"a":{"b":{"c":1}}}'
  [array_of_objects]='[{"x":1},{"x":2},{"x":3}]'
  [mixed_array]='[1,"two",true,null,{"k":"v"}]'
  [escaped_string]='"line1\nline2\t\"quoted\"\\backslash"'
  [unicode]='"é中文"'
  [large_int]='9007199254740992'
  [negative_int]='-12345'
  [decimal]='3.14159'
  [exponent]='1.5e-10'
  [whitespace_heavy]='   {  "a"  :  [ 1 , 2 , 3 ]  }   '
  [duplicate_keys]='{"a":1,"a":2}'
  [trailing_comma_invalid]='[1,2,3,]'
  [deeply_nested]='[[[[[[[1]]]]]]]'
  [object_with_array_value]='{"items":[1,2,3,4,5,6,7,8,9,10]}'
  [string_with_specials]='"!@#$%^&*()_+-=[]{}|;:,.<>?/~`"'
  [number_array]='[1.0,2.5,-3,0,100,-100]'
  [boolean_array]='[true,false,true,false]'
  [object_string_keys]='{"key with spaces":"value","key-with-dashes":1,"key_with_underscores":[]}'
)

for name in "${!HAND[@]}"; do
  out="${CJSON_SEED_DIR}/${name}.json"
  if [[ ! -f "${out}" ]]; then
    printf '%s' "${HAND[$name]}" > "${out}"
  fi
done

# Pull additional samples from cJSON's own test tree if present.
CJSON_TESTS_DIR="${REPO_ROOT}/experiments/targets/cjson/src/tests/inputs"
if [[ -d "${CJSON_TESTS_DIR}" ]]; then
  i=0
  while IFS= read -r src; do
    base="$(basename "${src}" | tr -dc '[:alnum:]._-' | tr '[:upper:]' '[:lower:]')"
    dst="${CJSON_SEED_DIR}/upstream_${i}_${base}"
    [[ ! -f "${dst}" ]] && cp "${src}" "${dst}"
    i=$((i+1))
    [[ ${i} -ge 20 ]] && break
  done < <(find "${CJSON_TESTS_DIR}" -maxdepth 1 -type f \( -name "*.json" -o -name "*.txt" \) 2>/dev/null | sort)
fi

CJSON_COUNT="$(find "${CJSON_SEED_DIR}" -type f | wc -l | tr -d ' ')"
echo "[seeds] cjson: ${CJSON_COUNT} seed files"

# ---------- libpng seeds ----------

# Hand-craft minimal PNGs covering different IHDR variants.
# Each is a 1x1 image with different color types/bit depths.
# Format: PNG signature + IHDR + IDAT + IEND chunks (varied).

python3 - <<PY
import os, struct, zlib

base = "${LIBPNG_SEED_DIR}"
os.makedirs(base, exist_ok=True)

PNG_SIG = b'\x89PNG\r\n\x1a\n'

def chunk(tag, data):
    length = struct.pack(">I", len(data))
    crc = struct.pack(">I", zlib.crc32(tag + data) & 0xffffffff)
    return length + tag + data + crc

def png(width, height, bit_depth, color_type, interlace=0, raw=b"\x00\x00"):
    ihdr = struct.pack(">IIBBBBB", width, height, bit_depth, color_type, 0, 0, interlace)
    raw = b"\x00" + raw  # filter byte
    idat = zlib.compress(raw)
    return PNG_SIG + chunk(b"IHDR", ihdr) + chunk(b"IDAT", idat) + chunk(b"IEND", b"")

variants = [
    ("gray_1x1_8bit",       (1, 1, 8,  0, 0, b"\x80")),
    ("gray_1x1_16bit",      (1, 1, 16, 0, 0, b"\x80\x00")),
    ("rgb_1x1_8bit",        (1, 1, 8,  2, 0, b"\xff\x00\x00")),
    ("rgb_1x1_16bit",       (1, 1, 16, 2, 0, b"\x00\xff\x00\x00\x00\x00")),
    ("palette_1x1_8bit",    (1, 1, 8,  3, 0, b"\x00")),
    ("gray_alpha_1x1_8bit", (1, 1, 8,  4, 0, b"\x80\xff")),
    ("rgba_1x1_8bit",       (1, 1, 8,  6, 0, b"\xff\x00\x00\xff")),
    ("gray_2x2_8bit",       (2, 2, 8,  0, 0, b"\x00\xff\x00\xff\xff\x00")),
    ("gray_4x4_8bit",       (4, 4, 8,  0, 0, b"\x00\x10\x20\x30" * 4 + b"\x00" * 4)),
    ("rgb_3x3_8bit",        (3, 3, 8,  2, 0, b"\xff\x00\x00\x00\xff\x00\x00\x00\xff" * 3)),
    ("gray_1x1_1bit",       (1, 1, 1,  0, 0, b"\x80")),
    ("gray_1x1_2bit",       (1, 1, 2,  0, 0, b"\x80")),
    ("gray_1x1_4bit",       (1, 1, 4,  0, 0, b"\x80")),
    ("interlaced_2x2",      (2, 2, 8,  0, 1, b"\x00\xff\x00\xff\xff\x00")),
    ("rgba_2x2_8bit",       (2, 2, 8,  6, 0, b"\xff\x00\x00\xff\x00\xff\x00\xff\x00\x00\xff\xff\xff\xff\xff\xff")),
]

for name, args in variants:
    p = os.path.join(base, f"{name}.png")
    if not os.path.exists(p):
        try:
            with open(p, "wb") as f:
                f.write(png(*args))
        except Exception as e:
            print(f"[seeds] WARN failed to write {name}: {e}")

# Edge cases: corrupt PNGs that still parse partially.
edge_cases = {
    "truncated_after_ihdr.png": PNG_SIG + chunk(b"IHDR", struct.pack(">IIBBBBB", 1, 1, 8, 0, 0, 0, 0)),
    "missing_idat.png":         PNG_SIG + chunk(b"IHDR", struct.pack(">IIBBBBB", 1, 1, 8, 0, 0, 0, 0)) + chunk(b"IEND", b""),
    "wrong_signature.png":      b'\x89PNX\r\n\x1a\n' + chunk(b"IHDR", struct.pack(">IIBBBBB", 1, 1, 8, 0, 0, 0, 0)),
    "extra_chunks.png":         png(1, 1, 8, 0, 0, b"\x80")[:-12]
                                + chunk(b"tEXt", b"Comment\x00fuzzpilot test")
                                + chunk(b"IEND", b""),
    "empty_after_sig.png":      PNG_SIG,
}
for name, data in edge_cases.items():
    p = os.path.join(base, name)
    if not os.path.exists(p):
        try:
            with open(p, "wb") as f:
                f.write(data)
        except Exception as e:
            print(f"[seeds] WARN failed to write {name}: {e}")

print("[seeds] libpng: synthesized variants")
PY

# Pull any extras from libpng's contrib/pngsuite if available.
PNGSUITE_DIR="${REPO_ROOT}/experiments/targets/libpng/src/contrib/pngsuite"
if [[ -d "${PNGSUITE_DIR}" ]]; then
  i=0
  while IFS= read -r src; do
    base="$(basename "${src}")"
    dst="${LIBPNG_SEED_DIR}/upstream_${base}"
    [[ ! -f "${dst}" ]] && cp "${src}" "${dst}"
    i=$((i+1))
    [[ ${i} -ge 20 ]] && break
  done < <(find "${PNGSUITE_DIR}" -maxdepth 1 -type f -name "*.png" 2>/dev/null | sort)
fi

LIBPNG_COUNT="$(find "${LIBPNG_SEED_DIR}" -type f | wc -l | tr -d ' ')"
echo "[seeds] libpng: ${LIBPNG_COUNT} seed files"

if [[ "${CJSON_COUNT}" -lt 20 || "${LIBPNG_COUNT}" -lt 20 ]]; then
  echo "[seeds] WARN: target threshold (>=20) not met; check submodule init."
  exit 1
fi

echo "[seeds] OK"
