#pragma once

#include <cstdint>
#include <filesystem>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

enum class CompactMutationOp : std::uint8_t {
  BitFlip,
  OverwriteRange,
  InsertToken,
  Arith,
  Splice,
  DeleteBlock,
  DictionaryOverwrite
};

struct CompactRange {
  std::uint32_t begin = 0;
  std::uint32_t end = 0;
};

enum class FieldType : std::uint8_t {
  Data,
  Length,
  Magic,
  Checksum
};

struct CompactField {
  std::uint32_t offset = 0;
  std::uint32_t size = 0;
  FieldType type = FieldType::Data;
  bool is_big_endian = true;      // Most network protocols use Big Endian
  std::uint32_t target_begin = 0; // For Length fields
  std::uint32_t target_end = 0;   // For Length fields
};

struct CompactOperatorWeight {
  CompactMutationOp op = CompactMutationOp::BitFlip;
  double weight = 1.0;
};

struct CompactRecipe {
  std::string id = "fallback";
  std::string selector_mode = "global";
  std::string selector_key = "global";
  std::uint32_t priority = 0;
  std::uint32_t ttl_sec = 0;
  std::vector<CompactOperatorWeight> weights;
  std::vector<CompactRange> focus_ranges;
  std::vector<CompactRange> protect_ranges;
  std::vector<CompactField> fields;
  std::vector<std::string> tokens;

  CompactMutationOp choose_operator(std::mt19937& rng) const;
};

class RecipeCache {
 public:
  // P0.1: hard caps on recipe storage. seed_id_recipes_ is append-only via
  // add_recipe() (called from load_from_environment / reload_if_stale); on
  // long runs with promoted_recipes accumulating in the store, the vector
  // grew unbounded and the mutator process OOM-killed at ~6h24m (rc=137).
  // Eviction policy is priority-asc (lowest priority dropped first) because
  // the vector is already stable-sorted by (priority desc, id asc) in
  // add_recipe — so back() is the lowest-priority entry and pop_back() is O(1).
  static constexpr std::size_t kMaxSeedIdRecipes = 4096;
  static constexpr std::size_t kMaxHashEntries = 8192;

  RecipeCache();

  void load_from_environment();
  // Cheap mtime check against the recipe store. If the controller has
  // written newer global.recipe or recipe_index.tsv since the last
  // load, drops seed-specific caches and re-runs load_from_environment.
  // Safe to call on the AFL hot path (one stat() per file per call).
  void reload_if_stale();
  bool loaded() const { return loaded_; }
  const CompactRecipe& global() const { return global_; }
  // Hot-path lookup. Callers should prefer the `seed_hash_hint` overload
  // and reuse the hash across all mutations of the same seed — the
  // original signature re-hashes the entire input on every call.
  const CompactRecipe& lookup(const std::string& seed_name,
                              const unsigned char* data,
                              std::size_t size,
                              bool* hit) const;
  // Hot-path optimized lookup: caller passes a previously computed seed
  // hash so we don't have to scan the full input on every mutation.
  // If `seed_hash_hint` is empty we fall back to the legacy behaviour.
  const CompactRecipe& lookup(const std::string& seed_name,
                              const unsigned char* data,
                              std::size_t size,
                              const std::string& seed_hash_hint,
                              bool* hit) const;
  // Whether any seed-specific recipes are present. Mutator can use this
  // to skip even the cheap selector_key scan when only the global recipe
  // is loaded.
  bool has_seed_specific() const {
    return !seed_id_recipes_.empty() || !seed_hash_recipes_.empty();
  }
  // P0.1 visibility for diagnostics and tests: post-eviction sizes.
  std::size_t seed_id_recipe_count() const { return seed_id_recipes_.size(); }
  std::size_t seed_hash_recipe_count() const { return seed_hash_recipes_.size(); }
  // Compute the stable input hash used by the seed_hash matching key.
  // Public so the mutator can cache it once per seed.
  static std::string compute_seed_hash(const unsigned char* data, std::size_t size);

 private:
  void add_recipe(CompactRecipe recipe);
  void rebuild_seed_id_index();

  CompactRecipe global_;
  std::vector<CompactRecipe> seed_id_recipes_;
  // O(1) index: selector_key → position in seed_id_recipes_.
  // Rebuilt after any bulk load; lookup() uses this instead of a
  // linear scan (was O(N) per mutation, N up to 4096).
  std::unordered_map<std::string, std::size_t> seed_id_index_;
  std::unordered_map<std::string, CompactRecipe> seed_hash_recipes_;
  bool loaded_ = false;
  // Captured at load_from_environment so reload_if_stale can detect
  // post-init writes from the controller (recipe promotion).
  std::filesystem::path store_path_;
  std::filesystem::file_time_type global_mtime_{};
  std::filesystem::file_time_type index_mtime_{};
};
