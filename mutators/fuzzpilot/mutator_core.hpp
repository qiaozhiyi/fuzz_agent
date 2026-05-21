#pragma once

#include "recipe_cache.hpp"
#include "telemetry_ring.hpp"

#include <cstddef>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

struct FpMutator {
  explicit FpMutator(unsigned int seed);

  RecipeCache recipes;
  TelemetryRing telemetry;
  std::mt19937 rng;
  std::vector<unsigned char> out;
  std::string current_seed;
  // Cached debug flag — read once at construction so we don't make a
  // getenv() syscall on every mutation call (which fires millions of
  // times per minute on the AFL++ hot path).
  bool debug_enabled = false;
  // Pre-hashed seed identity for recipe lookup. Reset on queue_get /
  // queue_new_entry; computed lazily on first mutation of a new seed.
  // Eliminates the O(n) hash over the full input buffer on every call.
  std::string seed_hash_cache;
  bool seed_hash_valid = false;
};

extern "C" {
void *fp_mutator_init(unsigned int seed);
size_t fp_mutator_fuzz(void *data,
                       unsigned char *buf,
                       size_t buf_size,
                       unsigned char **out_buf,
                       unsigned char *add_buf,
                       size_t add_buf_size,
                       size_t max_size);
void fp_mutator_deinit(void *data);
unsigned char fp_mutator_queue_get(void *data, const char *filename);
void fp_mutator_queue_new_entry(void *data, const char *filename_new_queue,
                                const char *filename_orig_queue);
}

