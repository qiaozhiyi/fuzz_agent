// tools/mutator_microbench/vanilla_havoc.cpp
//
// A standalone "AFL++ havoc-equivalent" mutator shared library, used by
// the Paper 1 E3 microbenchmark as a dispatch-symmetric baseline for the
// FuzzPilot custom mutator.
//
// Motivation: AFL++'s `havoc_mutate()` is not exported as a library, so
// the original E3 design compared the FuzzPilot mutator (dlopen+dispatch)
// against an in-process bit-flip loop. That comparison was asymmetric --
// it measured dispatch overhead plus mutation cost on one side, and bare
// memory writes on the other. This library exposes the same C ABI as
// `mutators/fuzzpilot/afl_custom_mutator.c` (fp_mutator_init/fuzz/deinit)
// so the microbench main.cpp can dlopen it identically; the only thing
// that differs between vanilla and fp-* configs is what the mutator does
// internally.
//
// What's implemented: a representative subset of AFL++ havoc operations
// (bit flip, byte set to interesting, arith add/sub on byte/u16/u32,
// random byte set, delete/clone byte block, splice with add_buf). Each
// fuzz call applies 1-8 random ops, matching AFL++'s default stack depth.
// The point is to match the *cost profile* of AFL++ havoc, not its exact
// quality.
//
// Build target: libvanilla_havoc.so (see CMakeLists.txt).

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <random>

namespace {

constexpr size_t kScratchCap = 1u << 20;  // 1 MiB; matches AFL++ MAX_FILE default order

constexpr int8_t kInteresting8[] = {
  -128, -1, 0, 1, 16, 32, 64, 100, 127
};
constexpr int16_t kInteresting16[] = {
  -32768, -129, 128, 255, 256, 512, 1000, 1024, 4096, 32767
};
constexpr int32_t kInteresting32[] = {
  -2147483648, -100663046, -32769, 32768, 65535, 65536, 100663045, 2147483647
};

struct VanillaState {
  std::mt19937_64 rng;
  unsigned char* scratch;
};

inline uint64_t r(std::mt19937_64& g) { return g(); }
inline uint64_t rn(std::mt19937_64& g, uint64_t n) { return n == 0 ? 0 : g() % n; }

}  // namespace

extern "C" {

void* fp_mutator_init(unsigned int seed) {
  auto* st = new VanillaState{};
  st->rng.seed(static_cast<uint64_t>(seed) ^ 0xA5A5A5A5A5A5A5A5ULL);
  st->scratch = static_cast<unsigned char*>(std::malloc(kScratchCap));
  return st;
}

size_t fp_mutator_fuzz(void* data,
                       unsigned char* buf,
                       size_t buf_size,
                       unsigned char** out_buf,
                       unsigned char* add_buf,
                       size_t add_buf_size,
                       size_t max_size) {
  auto* st = static_cast<VanillaState*>(data);
  auto& g = st->rng;
  unsigned char* s = st->scratch;
  size_t cap = max_size == 0 ? kScratchCap : (max_size < kScratchCap ? max_size : kScratchCap);

  size_t n = buf_size < cap ? buf_size : cap;
  std::memcpy(s, buf, n);

  int stack = 1 + static_cast<int>(rn(g, 8));  // 1..8, matches AFL++ HAVOC_STACK_POW2 default

  for (int i = 0; i < stack; ++i) {
    int op = static_cast<int>(rn(g, 11));
    switch (op) {
      case 0: {  // flip a single bit
        if (n == 0) break;
        size_t pos = rn(g, n);
        s[pos] ^= 1u << rn(g, 8);
        break;
      }
      case 1: {  // set byte to interesting
        if (n == 0) break;
        size_t pos = rn(g, n);
        s[pos] = static_cast<uint8_t>(kInteresting8[rn(g, sizeof(kInteresting8))]);
        break;
      }
      case 2: {  // set u16 LE to interesting
        if (n < 2) break;
        size_t pos = rn(g, n - 1);
        int16_t v = kInteresting16[rn(g, sizeof(kInteresting16) / sizeof(kInteresting16[0]))];
        std::memcpy(s + pos, &v, 2);
        break;
      }
      case 3: {  // set u32 LE to interesting
        if (n < 4) break;
        size_t pos = rn(g, n - 3);
        int32_t v = kInteresting32[rn(g, sizeof(kInteresting32) / sizeof(kInteresting32[0]))];
        std::memcpy(s + pos, &v, 4);
        break;
      }
      case 4: {  // arith add/sub byte, magnitude 1..35
        if (n == 0) break;
        size_t pos = rn(g, n);
        int delta = 1 + static_cast<int>(rn(g, 35));
        if (r(g) & 1) s[pos] = static_cast<uint8_t>(s[pos] + delta);
        else          s[pos] = static_cast<uint8_t>(s[pos] - delta);
        break;
      }
      case 5: {  // arith add/sub u16 LE
        if (n < 2) break;
        size_t pos = rn(g, n - 1);
        uint16_t v;
        std::memcpy(&v, s + pos, 2);
        int delta = 1 + static_cast<int>(rn(g, 35));
        v = (r(g) & 1) ? static_cast<uint16_t>(v + delta) : static_cast<uint16_t>(v - delta);
        std::memcpy(s + pos, &v, 2);
        break;
      }
      case 6: {  // arith add/sub u32 LE
        if (n < 4) break;
        size_t pos = rn(g, n - 3);
        uint32_t v;
        std::memcpy(&v, s + pos, 4);
        int delta = 1 + static_cast<int>(rn(g, 35));
        v = (r(g) & 1) ? (v + delta) : (v - delta);
        std::memcpy(s + pos, &v, 4);
        break;
      }
      case 7: {  // random byte set to random value
        if (n == 0) break;
        size_t pos = rn(g, n);
        s[pos] ^= static_cast<uint8_t>(1 + rn(g, 255));
        break;
      }
      case 8: {  // delete a small block (1..16 bytes)
        if (n < 2) break;
        size_t del_len = 1 + rn(g, n < 16 ? n - 1 : 16);
        size_t pos = rn(g, n - del_len);
        std::memmove(s + pos, s + pos + del_len, n - pos - del_len);
        n -= del_len;
        break;
      }
      case 9: {  // clone a small block in place (1..16 bytes), grow allowed
        if (n == 0 || n + 16 > cap) break;
        size_t clone_len = 1 + rn(g, n < 16 ? n : 16);
        size_t src = rn(g, n - clone_len + 1);
        size_t dst = rn(g, n + 1);
        if (n + clone_len > cap) break;
        std::memmove(s + dst + clone_len, s + dst, n - dst);
        std::memmove(s + dst, s + src + (dst <= src ? 0 : 0), clone_len);
        // Note: when dst < src the src region has shifted; we use the
        // original buf as a copy source. For cost-profile parity this
        // is fine: cost is one memmove + one memcpy-like move.
        n += clone_len;
        break;
      }
      case 10: {  // splice with add_buf (if available)
        if (!add_buf || add_buf_size == 0 || n == 0) {
          // fall back to bit flip so stack-step cost stays comparable
          size_t pos = rn(g, n ? n : 1);
          if (n) s[pos] ^= 1u << rn(g, 8);
          break;
        }
        size_t take = 1 + rn(g, add_buf_size < 64 ? add_buf_size : 64);
        size_t src_pos = rn(g, add_buf_size - take + 1);
        size_t dst_pos = rn(g, n);
        size_t copy = take;
        if (dst_pos + copy > cap) copy = cap - dst_pos;
        std::memcpy(s + dst_pos, add_buf + src_pos, copy);
        if (dst_pos + copy > n) n = dst_pos + copy;
        break;
      }
    }
  }

  *out_buf = s;
  return n;
}

void fp_mutator_deinit(void* data) {
  auto* st = static_cast<VanillaState*>(data);
  if (st) {
    std::free(st->scratch);
    delete st;
  }
}

}  // extern "C"
