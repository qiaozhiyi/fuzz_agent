#pragma once

#include "fuzzpilot/mutation/strategy.hpp"

#include <filesystem>
#include <optional>

namespace fuzzpilot {

struct RecipeLookupResult {
  bool hit = false;
  std::string selector_mode;
  std::string selector_key;
  uint32_t priority = 0;
  std::string strategy_id;
  std::filesystem::path recipe_path;
};

class RecipeStore {
 public:
  explicit RecipeStore(std::filesystem::path root);

  void ensure_layout() const;
  std::filesystem::path write_strategy(const SeedMutationStrategy& strategy) const;
  std::filesystem::path write_global_mutator_recipe(const SeedMutationStrategy& strategy) const;
  std::filesystem::path write_compact_recipe(const SeedMutationStrategy& strategy) const;
  std::filesystem::path write_compact_recipes(const std::vector<SeedMutationStrategy>& strategies) const;
  std::filesystem::path compact_index_path() const;
  RecipeLookupResult lookup_compact_recipe(const std::string& seed_id,
                                           const std::string& seed_hash) const;
  const std::filesystem::path& root() const { return root_; }

 private:
  std::filesystem::path root_;
};

}  // namespace fuzzpilot
