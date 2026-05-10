#pragma once

#include <cstdint>
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
  std::vector<std::string> tokens;

  CompactMutationOp choose_operator(std::mt19937& rng) const;
};

class RecipeCache {
 public:
  RecipeCache();

  void load_from_environment();
  bool loaded() const { return loaded_; }
  const CompactRecipe& global() const { return global_; }
  const CompactRecipe& lookup(const std::string& seed_name,
                              const unsigned char* data,
                              std::size_t size,
                              bool* hit) const;

 private:
  void add_recipe(CompactRecipe recipe);

  CompactRecipe global_;
  std::vector<CompactRecipe> seed_id_recipes_;
  std::unordered_map<std::string, CompactRecipe> seed_hash_recipes_;
  bool loaded_ = false;
};
