#include "mutator_core.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace {

std::size_t random_offset(std::mt19937& rng, std::size_t size) {
  if (size == 0) {
    return 0;
  }
  std::uniform_int_distribution<std::size_t> dist(0, size - 1);
  return dist(rng);
}

bool is_protected(const CompactRecipe& recipe, std::size_t offset) {
  // Recipe loader pre-sorts protect_ranges by `begin` ascending, so a
  // binary search over them is correct here. Linear fallback would
  // also work but burns cycles on every mutation; AFL hot path can't
  // afford the extra branches.
  const auto& ranges = recipe.protect_ranges;
  if (ranges.empty()) {
    return false;
  }
  // upper_bound returns the first range whose `begin` is strictly
  // greater than `offset`. We then step back one (if possible) to find
  // the candidate range that could contain `offset`.
  auto it = std::upper_bound(ranges.begin(), ranges.end(), offset,
      [](std::size_t off, const CompactRange& r) {
        return off < r.begin;
      });
  if (it == ranges.begin()) {
    return false;
  }
  --it;
  return offset >= it->begin && offset < it->end;
}

std::size_t choose_focus_offset(const CompactRecipe& recipe, std::mt19937& rng, std::size_t size) {
  if (size == 0) {
    return 0;
  }
  for (int attempt = 0; attempt < 16; ++attempt) {
    std::size_t candidate = 0;
    if (!recipe.focus_ranges.empty()) {
      std::uniform_int_distribution<std::size_t> range_dist(0, recipe.focus_ranges.size() - 1);
      const auto& range = recipe.focus_ranges[range_dist(rng)];
      const std::size_t begin = std::min<std::size_t>(range.begin, size - 1);
      const std::size_t end = std::min<std::size_t>(std::max(range.end, range.begin + 1), size);
      std::uniform_int_distribution<std::size_t> offset_dist(begin, end - 1);
      candidate = offset_dist(rng);
    } else {
      candidate = random_offset(rng, size);
    }
    if (!is_protected(recipe, candidate)) {
      return candidate;
    }
  }
  // 16 random attempts all hit protected ranges. Previously we fell back
  // to plain random_offset() which IGNORED protect_ranges entirely —
  // structural magic bytes (e.g. the XML header) could be clobbered
  // even when the recipe explicitly marked them protected (cpp audit
  // 5-D). Do a deterministic linear scan from a random start instead,
  // so we respect protection unless EVERY byte is protected.
  const std::size_t start = random_offset(rng, size);
  for (std::size_t i = 0; i < size; ++i) {
    const std::size_t cand = (start + i) % size;
    if (!is_protected(recipe, cand)) {
      return cand;
    }
  }
  return 0;
}

void mutate_bit_flip(FpMutator& mutator, const CompactRecipe& recipe) {
  if (mutator.out.empty()) {
    return;
  }
  const auto offset = choose_focus_offset(recipe, mutator.rng, mutator.out.size());
  const unsigned char bit = static_cast<unsigned char>(1u << (mutator.rng() % 8u));
  mutator.out[offset] ^= bit;
  mutator.telemetry.record(CompactMutationOp::BitFlip,
                           offset, mutator.out.size(), mutator.out.size(), true);
}

void mutate_overwrite_byte(FpMutator& mutator, const CompactRecipe& recipe) {
  if (mutator.out.empty()) {
    return;
  }
  const auto offset = choose_focus_offset(recipe, mutator.rng, mutator.out.size());
  mutator.out[offset] = static_cast<unsigned char>(mutator.rng() & 0xffu);
  mutator.telemetry.record(CompactMutationOp::OverwriteRange,
                           offset, mutator.out.size(), mutator.out.size(), true);
}

void mutate_arith(FpMutator& mutator, const CompactRecipe& recipe) {
  if (mutator.out.empty()) {
    return;
  }
  const auto offset = choose_focus_offset(recipe, mutator.rng, mutator.out.size());
  const int delta = (mutator.rng() % 2u) == 0 ? 1 : -1;
  mutator.out[offset] = static_cast<unsigned char>(mutator.out[offset] + delta);
  mutator.telemetry.record(CompactMutationOp::Arith,
                           offset, mutator.out.size(), mutator.out.size(), true);
}

bool mutate_insert_token(FpMutator& mutator, const CompactRecipe& recipe, std::size_t max_size) {
  if (recipe.tokens.empty() || mutator.out.size() >= max_size) {
    return false;
  }
  std::uniform_int_distribution<std::size_t> token_dist(0, recipe.tokens.size() - 1);
  const auto& token = recipe.tokens[token_dist(mutator.rng)];
  if (token.empty()) {
    return false;
  }
  const std::size_t available = max_size - mutator.out.size();
  const std::size_t count = std::min<std::size_t>(available, token.size());
  // Bounds: offset chosen from [0, out.size()] inclusive — out.size()+1
  // possibilities. Guard against degenerate state where out is empty.
  const auto offset = mutator.out.empty()
                          ? std::size_t{0}
                          : random_offset(mutator.rng, mutator.out.size() + 1);
  if (offset > mutator.out.size()) {
    return false;
  }
  const auto before = mutator.out.size();
  mutator.out.insert(mutator.out.begin() + static_cast<std::ptrdiff_t>(offset),
                     token.begin(), token.begin() + static_cast<std::ptrdiff_t>(count));
  mutator.telemetry.record(CompactMutationOp::InsertToken,
                           offset, before, mutator.out.size(), true);
  return true;
}

bool mutate_delete_block(FpMutator& mutator, const CompactRecipe& recipe) {
  if (mutator.out.size() < 2) {
    return false;
  }
  const auto offset = choose_focus_offset(recipe, mutator.rng, mutator.out.size());
  // Guard the subtraction: if focus_offset returned exactly out.size()
  // (defensive — shouldn't happen) we'd underflow. Also cap by buffer.
  if (offset >= mutator.out.size()) {
    return false;
  }
  const std::size_t max_len = std::min<std::size_t>(16, mutator.out.size() - offset);
  if (max_len == 0) {
    return false;
  }
  std::uniform_int_distribution<std::size_t> len_dist(1, max_len);
  const auto len = len_dist(mutator.rng);
  const auto before = mutator.out.size();
  mutator.out.erase(mutator.out.begin() + static_cast<std::ptrdiff_t>(offset),
                    mutator.out.begin() + static_cast<std::ptrdiff_t>(offset + len));
  mutator.telemetry.record(CompactMutationOp::DeleteBlock,
                           offset, before, mutator.out.size(), true);
  return true;
}

bool mutate_splice(FpMutator& mutator,
                   const unsigned char* add_buf,
                   std::size_t add_buf_size,
                   std::size_t max_size) {
  if (add_buf == nullptr || add_buf_size == 0 || mutator.out.size() >= max_size) {
    return false;
  }
  const auto offset = random_offset(mutator.rng, mutator.out.size() + 1);
  const auto add_offset = random_offset(mutator.rng, add_buf_size);
  const std::size_t available = max_size - mutator.out.size();
  const std::size_t count = std::min<std::size_t>({available, add_buf_size - add_offset, 32});
  if (count == 0) {
    return false;
  }
  const auto before = mutator.out.size();
  mutator.out.insert(mutator.out.begin() + static_cast<std::ptrdiff_t>(offset),
                     add_buf + add_offset, add_buf + add_offset + count);
  mutator.telemetry.record(CompactMutationOp::Splice,
                           offset, before, mutator.out.size(), true);
  return true;
}
}  // namespace

void apply_structural_repairs(FpMutator& mutator, const CompactRecipe& recipe) {
  if (recipe.fields.empty()) {
    return;
  }

  // Use cached debug flag — never call getenv() in the hot path.
  const bool debug = mutator.debug_enabled;
  if (debug) {
    fprintf(stderr, "[M5] Applying %zu structural repairs to buffer of size %zu\n",
            recipe.fields.size(), mutator.out.size());
  }

  for (const auto& field : recipe.fields) {
    // Bounds check using a checked-add to avoid uint32 wrap when both
    // `offset` and `size` are near UINT32_MAX (recipe could come from an
    // adversarial LLM proposal).
    if (field.size == 0) continue;
    if (field.offset >= mutator.out.size()) continue;
    if (static_cast<std::uint64_t>(field.offset) + field.size > mutator.out.size()) continue;

    if (field.type == FieldType::Length) {
      // Calculate actual length of target range
      std::uint32_t len = 0;
      if (field.target_end > field.target_begin &&
          field.target_end <= mutator.out.size()) {
        len = field.target_end - field.target_begin;
      } else if (field.target_begin > 0 && field.target_begin < mutator.out.size()) {
        len = static_cast<std::uint32_t>(mutator.out.size() - field.target_begin);
      } else {
        const std::uint64_t after = static_cast<std::uint64_t>(field.offset) + field.size;
        len = after <= mutator.out.size()
                  ? static_cast<std::uint32_t>(mutator.out.size() - after)
                  : 0;
      }
      if (debug) {
        fprintf(stderr, "[M5] Repairing LENGTH field at offset %u with value %u\n",
                field.offset, len);
      }

      // Write length back in correct endianness — guarded above so all
      // accesses are in-range.
      if (field.size == 4) {
        if (field.is_big_endian) {
          mutator.out[field.offset] = (len >> 24) & 0xff;
          mutator.out[field.offset + 1] = (len >> 16) & 0xff;
          mutator.out[field.offset + 2] = (len >> 8) & 0xff;
          mutator.out[field.offset + 3] = len & 0xff;
        } else {
          mutator.out[field.offset] = len & 0xff;
          mutator.out[field.offset + 1] = (len >> 8) & 0xff;
          mutator.out[field.offset + 2] = (len >> 16) & 0xff;
          mutator.out[field.offset + 3] = (len >> 24) & 0xff;
        }
      } else if (field.size == 2) {
        if (field.is_big_endian) {
          mutator.out[field.offset] = (len >> 8) & 0xff;
          mutator.out[field.offset + 1] = len & 0xff;
        } else {
          mutator.out[field.offset] = len & 0xff;
          mutator.out[field.offset + 1] = (len >> 8) & 0xff;
        }
      }
    } else if (field.type == FieldType::Checksum) {
      // Simple XOR Checksum repair for demonstration
      std::uint8_t xor_sum = 0;
      for (std::size_t i = 0; i < mutator.out.size(); i++) {
        if (i >= field.offset && i < field.offset + field.size) continue;
        xor_sum ^= mutator.out[i];
      }
      if (field.size == 1) {
        mutator.out[field.offset] = xor_sum;
      }
    }
  }
  if (debug) {
    fprintf(stderr, "[M5] Structural repairs applied successfully\n");
  }
}


FpMutator::FpMutator(unsigned int seed) : rng(seed) {
  recipes.load_from_environment();
  // Read debug flag once at construction so the hot path never calls
  // getenv() — that syscall isn't free and serialised getenv access on
  // Linux/macOS shows up in mutation-rate microbenchmarks.
  if (const char* dbg = std::getenv("FUZZPILOT_MUTATOR_DEBUG")) {
    debug_enabled = (dbg[0] != '\0' && dbg[0] != '0');
  }
}

extern "C" void *fp_mutator_init(unsigned int seed) {
  return new FpMutator(seed);
}

extern "C" size_t fp_mutator_fuzz(void *data,
                                  unsigned char *buf,
                                  size_t buf_size,
                                  unsigned char **out_buf,
                                  unsigned char *add_buf,
                                  size_t add_buf_size,
                                  size_t max_size) {
  if (data == nullptr || out_buf == nullptr || buf == nullptr || buf_size == 0 || max_size == 0) {
    return 0;
  }
  auto& mutator = *static_cast<FpMutator*>(data);
  bool recipe_hit = false;
  // Compute the seed hash at most once per seed entry — re-use the cache
  // until queue_get / queue_new_entry invalidates it. Skip entirely when
  // there are no seed-hash recipes loaded.
  if (mutator.recipes.has_seed_specific() && !mutator.seed_hash_valid) {
    mutator.seed_hash_cache = RecipeCache::compute_seed_hash(buf, buf_size);
    mutator.seed_hash_valid = true;
  }
  const CompactRecipe& recipe = mutator.recipes.lookup(mutator.current_seed, buf, buf_size,
                                                        mutator.seed_hash_cache, &recipe_hit);
  mutator.out.assign(buf, buf + std::min(buf_size, max_size));

  if (recipe_hit) {
    mutator.telemetry.hit();
  } else {
    mutator.telemetry.miss();
  }

  const auto op = recipe.choose_operator(mutator.rng);
  bool changed = false;
  switch (op) {
    case CompactMutationOp::InsertToken:
    case CompactMutationOp::DictionaryOverwrite:
      changed = mutate_insert_token(mutator, recipe, max_size);
      break;
    case CompactMutationOp::OverwriteRange:
      mutate_overwrite_byte(mutator, recipe);
      changed = true;
      break;
    case CompactMutationOp::Arith:
      mutate_arith(mutator, recipe);
      changed = true;
      break;
    case CompactMutationOp::Splice:
      changed = mutate_splice(mutator, add_buf, add_buf_size, max_size);
      break;
    case CompactMutationOp::DeleteBlock:
      changed = mutate_delete_block(mutator, recipe);
      break;
    case CompactMutationOp::BitFlip:
    default:
      mutate_bit_flip(mutator, recipe);
      changed = true;
      break;
  }

  if (!changed) {
    mutate_bit_flip(mutator, recipe);
  }

  apply_structural_repairs(mutator, recipe);

  *out_buf = mutator.out.data();
  return mutator.out.size();
}

extern "C" void fp_mutator_deinit(void *data) {
  if (data == nullptr) {
    return;
  }
  auto* mutator = static_cast<FpMutator*>(data);
  mutator->telemetry.flush_from_environment();
  delete mutator;
}

extern "C" unsigned char fp_mutator_queue_get(void *data, const char *filename) {
  if (data != nullptr && filename != nullptr) {
    auto* m = static_cast<FpMutator*>(data);
    // Per-corpus-entry tick is a natural cadence to re-check the recipe
    // store: the controller writes new recipes on plateau detection, and
    // this hook fires often enough that updates land within seconds but
    // not so often that the two stat() calls become a hot-path drag.
    m->recipes.reload_if_stale();
    // Flush telemetry on the same cadence so recipe_hits/misses survive
    // an external SIGKILL (rc=137 from the orchestrator watchdog
    // otherwise means fp_mutator_deinit never fires and the counters
    // never reach the JSONL file — cpp audit 1-B / Task #1B).
    m->telemetry.flush_from_environment();
    m->current_seed = filename;
    // Invalidate cached seed hash — next mutation will recompute once.
    m->seed_hash_valid = false;
    m->seed_hash_cache.clear();
  }
  return 1;
}

extern "C" void fp_mutator_queue_new_entry(void *data,
                                           const char *filename_new_queue,
                                           const char *filename_orig_queue) {
  (void)filename_orig_queue;
  if (data != nullptr && filename_new_queue != nullptr) {
    auto* m = static_cast<FpMutator*>(data);
    m->current_seed = filename_new_queue;
    m->seed_hash_valid = false;
    m->seed_hash_cache.clear();
  }
}
