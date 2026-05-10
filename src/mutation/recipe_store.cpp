#include "fuzzpilot/mutation/recipe_store.hpp"

#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>

namespace fuzzpilot {

RecipeStore::RecipeStore(std::filesystem::path root) : root_(std::move(root)) {}

void RecipeStore::ensure_layout() const {
  std::filesystem::create_directories(root_);
  std::filesystem::create_directories(root_ / "strategies");
  std::filesystem::create_directories(root_ / "compact");
}

std::filesystem::path RecipeStore::write_strategy(const SeedMutationStrategy& strategy) const {
  ensure_layout();
  const auto path = root_ / "strategies" / (strategy.id + ".json");
  {
    std::ofstream output(path);
    if (!output) {
      throw std::runtime_error("failed to write strategy: " + path.string());
    }
    output << strategy_json(strategy) << "\n";
  }
  {
    std::ofstream index(root_ / "index.tsv", std::ios::app);
    if (!index) {
      throw std::runtime_error("failed to update recipe index: " + (root_ / "index.tsv").string());
    }
    index << strategy.id << "\t" << path.filename().string() << "\t"
          << strategy.selector.mode << "\t" << strategy.selector.seed_id << "\n";
  }
  return path;
}

namespace {

std::string selector_key(const SeedMutationStrategy& strategy) {
  if (strategy.selector.mode == "seed_id") {
    return strategy.selector.seed_id;
  }
  if (strategy.selector.mode == "seed_hash") {
    return strategy.selector.seed_hash;
  }
  if (strategy.selector.mode == "family") {
    return strategy.selector.family;
  }
  return "global";
}

void write_compact_recipe_body(std::ostream& output, const SeedMutationStrategy& strategy) {
  output << "# FuzzPilot v1 compact mutator recipe\n";
  output << "id=" << strategy.id << "\n";
  output << "selector=" << strategy.selector.mode << "," << selector_key(strategy) << "\n";
  output << "priority=" << strategy.priority << "\n";
  output << "ttl_sec=" << strategy.ttl_sec << "\n";
  for (const auto& weight : strategy.operator_weights) {
    output << "operator=" << mutation_op_name(weight.op) << "," << weight.weight << "\n";
  }
  for (const auto& range : strategy.focus_ranges) {
    output << "focus=" << range.begin << "," << range.end << "\n";
  }
  for (const auto& range : strategy.protect_ranges) {
    output << "protect=" << range.begin << "," << range.end << "\n";
  }
  for (const auto& token : strategy.dictionary_tokens) {
    output << "token=" << token << "\n";
  }
}

std::string safe_filename(std::string value) {
  for (char& c : value) {
    const bool safe = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                      (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
    if (!safe) {
      c = '_';
    }
  }
  if (value.empty()) {
    return "recipe";
  }
  return value;
}

std::vector<std::string> split_tab(const std::string& line) {
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

}  // namespace

std::filesystem::path RecipeStore::write_global_mutator_recipe(
    const SeedMutationStrategy& strategy) const {
  ensure_layout();
  const auto path = root_ / "global.recipe";
  std::ofstream output(path);
  if (!output) {
    throw std::runtime_error("failed to write mutator recipe: " + path.string());
  }
  write_compact_recipe_body(output, strategy);
  return path;
}

std::filesystem::path RecipeStore::write_compact_recipe(const SeedMutationStrategy& strategy) const {
  ensure_layout();
  const auto errors = validate_strategy(strategy);
  if (!errors.empty()) {
    std::ostringstream message;
    message << "invalid strategy " << strategy.id << ": ";
    for (std::size_t i = 0; i < errors.size(); ++i) {
      if (i != 0) {
        message << "; ";
      }
      message << errors[i];
    }
    throw std::runtime_error(message.str());
  }

  if (strategy.selector.mode == "global") {
    return write_global_mutator_recipe(strategy);
  }

  const auto filename = safe_filename(strategy.id) + ".recipe";
  const auto path = root_ / "compact" / filename;
  {
    std::ofstream output(path);
    if (!output) {
      throw std::runtime_error("failed to write compact recipe: " + path.string());
    }
    write_compact_recipe_body(output, strategy);
  }
  {
    std::ofstream index(compact_index_path(), std::ios::app);
    if (!index) {
      throw std::runtime_error("failed to update compact recipe index: " +
                               compact_index_path().string());
    }
    index << strategy.selector.mode << "\t" << selector_key(strategy) << "\t"
          << strategy.priority << "\t" << filename << "\t" << strategy.id << "\n";
  }
  return path;
}

std::filesystem::path RecipeStore::write_compact_recipes(
    const std::vector<SeedMutationStrategy>& strategies) const {
  ensure_layout();
  std::ofstream reset(compact_index_path(), std::ios::trunc);
  if (!reset) {
    throw std::runtime_error("failed to reset compact recipe index: " +
                             compact_index_path().string());
  }
  reset << "# selector_mode\tselector_key\tpriority\tfile\tstrategy_id\n";
  reset.close();

  for (const auto& strategy : strategies) {
    write_strategy(strategy);
    write_compact_recipe(strategy);
  }
  return compact_index_path();
}

std::filesystem::path RecipeStore::compact_index_path() const {
  return root_ / "recipe_index.tsv";
}

RecipeLookupResult RecipeStore::lookup_compact_recipe(const std::string& seed_id,
                                                      const std::string& seed_hash) const {
  RecipeLookupResult best;
  std::ifstream input(compact_index_path());
  if (!input) {
    return best;
  }

  std::string line;
  while (std::getline(input, line)) {
    if (line.empty() || line[0] == '#') {
      continue;
    }
    const auto parts = split_tab(line);
    if (parts.size() < 5) {
      continue;
    }
    const auto& mode = parts[0];
    const auto& key = parts[1];
    bool match = false;
    if (mode == "seed_id" && !seed_id.empty()) {
      match = seed_id == key || seed_id.find(key) != std::string::npos;
    } else if (mode == "seed_hash" && !seed_hash.empty()) {
      match = seed_hash == key;
    }
    if (!match) {
      continue;
    }
    uint32_t priority = 0;
    try {
      priority = static_cast<uint32_t>(std::stoul(parts[2]));
    } catch (...) {
    }
    if (!best.hit || priority >= best.priority) {
      best.hit = true;
      best.selector_mode = mode;
      best.selector_key = key;
      best.priority = priority;
      best.recipe_path = root_ / "compact" / parts[3];
      best.strategy_id = parts[4];
    }
  }
  return best;
}

}  // namespace fuzzpilot
