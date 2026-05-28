#include "recipe_cache.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace {

std::string trim(std::string value) {
  auto not_space = [](unsigned char c) {
    return c != ' ' && c != '\t' && c != '\r' && c != '\n';
  };
  value.erase(value.begin(),
              std::find_if(value.begin(), value.end(), not_space));
  value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(),
              value.end());
  return value;
}

CompactMutationOp op_from_name(const std::string &name) {
  if (name == "overwrite_range")
    return CompactMutationOp::OverwriteRange;
  if (name == "insert_token")
    return CompactMutationOp::InsertToken;
  if (name == "arith")
    return CompactMutationOp::Arith;
  if (name == "splice")
    return CompactMutationOp::Splice;
  if (name == "delete_block")
    return CompactMutationOp::DeleteBlock;
  if (name == "dictionary_overwrite")
    return CompactMutationOp::DictionaryOverwrite;
  return CompactMutationOp::BitFlip;
}

bool parse_range(const std::string &value, CompactRange &range) {
  const auto comma = value.find(',');
  if (comma == std::string::npos) {
    return false;
  }
  try {
    range.begin =
        static_cast<std::uint32_t>(std::stoul(value.substr(0, comma)));
    range.end = static_cast<std::uint32_t>(std::stoul(value.substr(comma + 1)));
    return range.end > range.begin;
  } catch (...) {
    return false;
  }
}

std::string stable_hash_hex(const unsigned char *data, std::size_t size) {
  std::uint64_t hash = 1469598103934665603ull;
  for (std::size_t i = 0; i < size; ++i) {
    hash ^= static_cast<std::uint64_t>(data[i]);
    hash *= 1099511628211ull;
  }
  std::string out(16, '0');
  constexpr char hex_chars[] = "0123456789abcdef";
  for (int i = 15; i >= 0; --i) {
    out[i] = hex_chars[hash & 0xf];
    hash >>= 4;
  }
  return out;
}

std::vector<std::string> split_tab(const std::string &line) {
  std::vector<std::string> parts;
  std::string current;
  for (const char c : line) {
    if (c == '\t') {
      parts.push_back(current);
      current.clear();
    } else {
      current.push_back(c);
    }
  }
  parts.push_back(current);
  return parts;
}

bool is_safe_recipe_filename(const std::string &value) {
  if (value.empty() || value == "." || value == "..") {
    return false;
  }
  return value.find('/') == std::string::npos &&
         value.find('\\') == std::string::npos &&
         value.find("..") == std::string::npos;
}

void normalize(std::vector<CompactOperatorWeight> &weights) {
  double total = 0.0;
  for (const auto &weight : weights) {
    total += std::max(0.0, weight.weight);
  }
  if (total <= 0.0) {
    weights = {{CompactMutationOp::BitFlip, 1.0}};
    return;
  }
  for (auto &weight : weights) {
    weight.weight = std::max(0.0, weight.weight) / total;
  }
}

bool parse_field(const std::string &value, CompactField &field) {
  std::vector<std::string> parts;
  std::string current;
  std::stringstream ss(value);
  while (std::getline(ss, current, ',')) {
    parts.push_back(trim(current));
  }
  if (parts.size() < 3)
    return false;
  try {
    field.offset = static_cast<std::uint32_t>(std::stoul(parts[0]));
    field.size = static_cast<std::uint32_t>(std::stoul(parts[1]));
    field.type = static_cast<FieldType>(std::stoul(parts[2]));
    if (parts.size() >= 4)
      field.is_big_endian = (parts[3] == "1" || parts[3] == "true");
    if (parts.size() >= 6) {
      field.target_begin = static_cast<std::uint32_t>(std::stoul(parts[4]));
      field.target_end = static_cast<std::uint32_t>(std::stoul(parts[5]));
    }
    return true;
  } catch (...) {
    return false;
  }
}

CompactRecipe parse_recipe_file(const std::filesystem::path &path) {
  CompactRecipe loaded;
  std::ifstream input(path);
  if (!input) {
    return loaded;
  }

  std::string line;
  while (std::getline(input, line)) {
    line = trim(line);
    if (line.empty() || line[0] == '#') {
      continue;
    }
    const auto equals = line.find('=');
    if (equals == std::string::npos) {
      continue;
    }
    const auto key = trim(line.substr(0, equals));
    const auto value = trim(line.substr(equals + 1));
    if (key == "id") {
      loaded.id = value;
    } else if (key == "selector") {
      const auto comma = value.find(',');
      loaded.selector_mode =
          trim(comma == std::string::npos ? value : value.substr(0, comma));
      loaded.selector_key =
          trim(comma == std::string::npos ? "" : value.substr(comma + 1));
    } else if (key == "priority") {
      try {
        loaded.priority = static_cast<std::uint32_t>(std::stoul(value));
      } catch (...) {
        loaded.priority = 0;
      }
    } else if (key == "ttl_sec") {
      try {
        loaded.ttl_sec = static_cast<std::uint32_t>(std::stoul(value));
      } catch (...) {
        loaded.ttl_sec = 0;
      }
    } else if (key == "operator") {
      const auto comma = value.find(',');
      const auto name =
          comma == std::string::npos ? value : value.substr(0, comma);
      double weight = 1.0;
      if (comma != std::string::npos) {
        try {
          weight = std::stod(value.substr(comma + 1));
        } catch (...) {
          weight = 1.0;
        }
      }
      loaded.weights.push_back({op_from_name(trim(name)), weight});
    } else if (key == "focus") {
      CompactRange range;
      if (parse_range(value, range)) {
        loaded.focus_ranges.push_back(range);
      }
    } else if (key == "protect") {
      CompactRange range;
      if (parse_range(value, range)) {
        loaded.protect_ranges.push_back(range);
      }
    } else if (key == "field") {
      CompactField field;
      if (parse_field(value, field)) {
        loaded.fields.push_back(field);
      }
    } else if (key == "token") {
      loaded.tokens.push_back(value);
    }
  }
  normalize(loaded.weights);
  return loaded;
}

} // namespace

CompactMutationOp CompactRecipe::choose_operator(std::mt19937 &rng) const {
  if (weights.empty()) {
    return CompactMutationOp::BitFlip;
  }
  std::uniform_real_distribution<double> dist(0.0, 1.0);
  const double needle = dist(rng);
  double cumulative = 0.0;
  for (const auto &weight : weights) {
    cumulative += weight.weight;
    if (needle <= cumulative) {
      return weight.op;
    }
  }
  return weights.back().op;
}

RecipeCache::RecipeCache() {
  global_.weights = {
      {CompactMutationOp::BitFlip, 0.45},
      {CompactMutationOp::OverwriteRange, 0.35},
      {CompactMutationOp::Arith, 0.20},
  };
  normalize(global_.weights);
}

void RecipeCache::load_from_environment() {
  const char *store_env = std::getenv("FUZZPILOT_RECIPE_STORE");
  if (store_env == nullptr || *store_env == '\0') {
    fprintf(stderr, "[M5] FUZZPILOT_RECIPE_STORE not set\n");
    return;
  }
  // Canonicalize the path to defeat directory-traversal tricks like
  // setting `FUZZPILOT_RECIPE_STORE=/tmp/../etc/.../something`. On
  // shared hosts (academic clusters, CI runners) the environment is
  // attacker-controlled by other tenants; without canonical resolution
  // the mutator would happily walk into arbitrary directories.
  std::error_code ec;
  std::filesystem::path store =
      std::filesystem::weakly_canonical(store_env, ec);
  if (ec) {
    fprintf(stderr, "[M5] FUZZPILOT_RECIPE_STORE canonicalize failed: %s\n",
            ec.message().c_str());
    return;
  }
  // The recipe store must exist as a directory (not e.g. a symlink to
  // / or a regular file). exists+is_directory guards against both.
  if (!std::filesystem::is_directory(store, ec) || ec) {
    fprintf(stderr, "[M5] FUZZPILOT_RECIPE_STORE not a directory: %s\n",
            store.c_str());
    return;
  }
  fprintf(stderr, "[M5] Loading recipes from: %s\n", store.c_str());
  const std::filesystem::path global_recipe_path = store / "global.recipe";
  if (std::filesystem::exists(global_recipe_path)) {
    auto loaded = parse_recipe_file(global_recipe_path);
    fprintf(stderr, "[M5] Global recipe loaded: %s, fields: %zu\n",
            loaded.id.c_str(), loaded.fields.size());
    if (loaded.weights.empty()) {
      loaded.weights = global_.weights;
    }
    loaded.selector_mode = "global";
    loaded.selector_key = "global";
    normalize(loaded.weights);
    global_ = std::move(loaded);
    loaded_ = true;
  }

  const std::filesystem::path index_path = store / "recipe_index.tsv";
  std::ifstream index(index_path);
  if (!index) {
    return;
  }
  std::string line;
  while (std::getline(index, line)) {
    line = trim(line);
    if (line.empty() || line[0] == '#') {
      continue;
    }
    const auto parts = split_tab(line);
    if (parts.size() < 4) {
      continue;
    }
    if (!is_safe_recipe_filename(parts[3])) {
      continue;
    }
    auto recipe = parse_recipe_file(store / "compact" / parts[3]);
    if (recipe.weights.empty()) {
      recipe.weights = global_.weights;
    }
    recipe.selector_mode = parts[0];
    recipe.selector_key = parts[1];
    try {
      recipe.priority = static_cast<std::uint32_t>(std::stoul(parts[2]));
    } catch (...) {
    }
    add_recipe(std::move(recipe));
    loaded_ = true;
  }

  // Remember the store + mtimes so reload_if_stale() can pick up any
  // recipe the controller writes after this point. Without this, the
  // mutator was stuck on whatever existed at AFL fork-server start
  // (typically an empty bootstrap `strategy_dictionary`).
  store_path_ = store;
  std::error_code mtime_ec;
  if (std::filesystem::exists(global_recipe_path, mtime_ec)) {
    global_mtime_ =
        std::filesystem::last_write_time(global_recipe_path, mtime_ec);
  }
  if (std::filesystem::exists(index_path, mtime_ec)) {
    index_mtime_ = std::filesystem::last_write_time(index_path, mtime_ec);
  }
}

void RecipeCache::reload_if_stale() {
  if (store_path_.empty()) {
    return; // load_from_environment never succeeded; nothing to reload.
  }
  std::error_code ec;
  const auto global_path = store_path_ / "global.recipe";
  const auto idx_path = store_path_ / "recipe_index.tsv";
  std::filesystem::file_time_type g_mt{};
  std::filesystem::file_time_type i_mt{};
  if (std::filesystem::exists(global_path, ec)) {
    g_mt = std::filesystem::last_write_time(global_path, ec);
  }
  if (std::filesystem::exists(idx_path, ec)) {
    i_mt = std::filesystem::last_write_time(idx_path, ec);
  }
  if (g_mt == global_mtime_ && i_mt == index_mtime_) {
    return; // store unchanged since last load
  }
  // Wipe seed-specific caches so stale entries don't bleed into the new
  // recipe set. global_ is overwritten by load_from_environment if a new
  // global.recipe exists; otherwise it retains the previous value (better
  // than dropping back to the constructor default mid-run).
  seed_id_recipes_.clear();
  seed_hash_recipes_.clear();
  load_from_environment();
  fprintf(stderr,
          "[M5] reload_if_stale: recipe store refreshed (seed_ids=%zu, "
          "seed_hashes=%zu)\n",
          seed_id_recipes_.size(), seed_hash_recipes_.size());
}

void RecipeCache::add_recipe(CompactRecipe recipe) {
  normalize(recipe.weights);
  // Pre-sort the protect/focus ranges so the mutator hot path can
  // binary-search them instead of scanning. Recipes are loaded once at
  // mutator init, so the O(N log N) sort cost is paid once per
  // process; on the hot path we save a linear scan on every mutation.
  std::sort(recipe.protect_ranges.begin(), recipe.protect_ranges.end(),
            [](const CompactRange &a, const CompactRange &b) {
              return a.begin < b.begin;
            });
  std::sort(recipe.focus_ranges.begin(), recipe.focus_ranges.end(),
            [](const CompactRange &a, const CompactRange &b) {
              return a.begin < b.begin;
            });
  if (recipe.selector_mode == "seed_id") {
    seed_id_recipes_.push_back(std::move(recipe));
    // Stable sort by (priority desc, id asc) so the tie-breaking is
    // reproducible across platforms — previously ties depended on
    // filesystem iteration order, which differs between APFS / ext4.
    std::stable_sort(seed_id_recipes_.begin(), seed_id_recipes_.end(),
                     [](const CompactRecipe &lhs, const CompactRecipe &rhs) {
                       if (lhs.priority != rhs.priority) {
                         return lhs.priority > rhs.priority;
                       }
                       return lhs.id < rhs.id;
                     });
  } else if (recipe.selector_mode == "seed_hash") {
    const auto key = recipe.selector_key;
    const auto it = seed_hash_recipes_.find(key);
    // Strictly greater for replacement, with id as a tiebreaker on
    // equal priority. Without this rule, repeated loads of the same
    // recipe set could produce different winners on different
    // platforms.
    if (it == seed_hash_recipes_.end() ||
        recipe.priority > it->second.priority ||
        (recipe.priority == it->second.priority && recipe.id < it->second.id)) {
      seed_hash_recipes_[key] = std::move(recipe);
    }
  }
}

const CompactRecipe &RecipeCache::lookup(const std::string &seed_name,
                                         const unsigned char *data,
                                         std::size_t size, bool *hit) const {
  return lookup(seed_name, data, size, std::string(), hit);
}

const CompactRecipe &RecipeCache::lookup(const std::string &seed_name,
                                         const unsigned char *data,
                                         std::size_t size,
                                         const std::string &seed_hash_hint,
                                         bool *hit) const {
  if (hit != nullptr) {
    *hit = false;
  }

  // Cheap path first: seed-name based selectors. Exact match only.
  // Previous code also accepted `seed_name.find(selector_key) != npos`,
  // which let a recipe targeting `id:000001` accidentally bind to
  // `id:0000010`, `id:000001,orig:foo`, etc. (cpp audit 5-C). Recipes
  // are referenced by AFL corpus basename, which is stable, so exact
  // string equality is the correct selector semantics.
  if (!seed_id_recipes_.empty()) {
    for (const auto &recipe : seed_id_recipes_) {
      if (recipe.selector_key.empty())
        continue;
      if (seed_name == recipe.selector_key) {
        if (hit != nullptr) {
          *hit = true;
        }
        return recipe;
      }
    }
  }

  // Hash-based recipes: prefer the caller-provided cached hash to avoid
  // re-hashing the entire input on every mutation. Falls back to fresh
  // hashing only when no hint was passed.
  if (!seed_hash_recipes_.empty() && data != nullptr && size > 0) {
    const std::string &key =
        !seed_hash_hint.empty() ? seed_hash_hint : stable_hash_hex(data, size);
    const auto it = seed_hash_recipes_.find(key);
    if (it != seed_hash_recipes_.end()) {
      if (hit != nullptr) {
        *hit = true;
      }
      return it->second;
    }
  }

  if (loaded_) {
    if (hit != nullptr) {
      *hit = global_.id != "fallback";
    }
  }
  return global_;
}

std::string RecipeCache::compute_seed_hash(const unsigned char *data,
                                           std::size_t size) {
  return stable_hash_hex(data, size);
}
