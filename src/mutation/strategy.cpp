#include "fuzzpilot/mutation/strategy.hpp"
#include "fuzzpilot/string_util.hpp"

#include "fuzzpilot/ids.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>
#include <stdexcept>

namespace fuzzpilot {
namespace {} // namespace

std::string mutation_op_name(MutationOp op) {
  switch (op) {
  case MutationOp::BitFlip:
    return "bit_flip";
  case MutationOp::OverwriteRange:
    return "overwrite_range";
  case MutationOp::InsertToken:
    return "insert_token";
  case MutationOp::Arith:
    return "arith";
  case MutationOp::Splice:
    return "splice";
  case MutationOp::DeleteBlock:
    return "delete_block";
  case MutationOp::CloneBlock:
    return "clone_block";
  case MutationOp::DictionaryOverwrite:
    return "dictionary_overwrite";
  }
  return "bit_flip";
}

MutationOp mutation_op_from_name(const std::string &name) {
  if (name == "bit_flip")
    return MutationOp::BitFlip;
  if (name == "overwrite_range")
    return MutationOp::OverwriteRange;
  if (name == "insert_token")
    return MutationOp::InsertToken;
  if (name == "arith")
    return MutationOp::Arith;
  if (name == "splice")
    return MutationOp::Splice;
  if (name == "delete_block")
    return MutationOp::DeleteBlock;
  if (name == "clone_block")
    return MutationOp::CloneBlock;
  if (name == "dictionary_overwrite")
    return MutationOp::DictionaryOverwrite;
  throw std::invalid_argument("unknown mutation op: " + name);
}

void normalize_weights(std::vector<OperatorWeight> &weights) {
  double total = 0.0;
  for (const auto &weight : weights) {
    total += std::max(0.0, weight.weight);
  }
  if (total <= 0.0) {
    const double equal =
        weights.empty() ? 0.0 : 1.0 / static_cast<double>(weights.size());
    for (auto &weight : weights) {
      weight.weight = equal;
    }
    return;
  }
  for (auto &weight : weights) {
    weight.weight = std::max(0.0, weight.weight) / total;
  }
}

std::string strategy_json(const SeedMutationStrategy &strategy) {
  std::ostringstream out;
  out << "{";
  out << "\"id\":\"" << json_escape(strategy.id) << "\",";
  out << "\"agent\":\"" << json_escape(strategy.agent) << "\",";
  out << "\"seed_selector\":{\"mode\":\"" << json_escape(strategy.selector.mode)
      << "\",\"seed_id\":\"" << json_escape(strategy.selector.seed_id)
      << "\",\"seed_hash\":\"" << json_escape(strategy.selector.seed_hash)
      << "\",\"family\":\"" << json_escape(strategy.selector.family) << "\"},";
  out << "\"priority\":" << strategy.priority << ",";
  out << "\"ttl_sec\":" << strategy.ttl_sec << ",";
  out << "\"operator_weights\":{";
  for (std::size_t i = 0; i < strategy.operator_weights.size(); ++i) {
    if (i != 0)
      out << ",";
    out << "\"" << mutation_op_name(strategy.operator_weights[i].op)
        << "\":" << strategy.operator_weights[i].weight;
  }
  out << "},";
  out << "\"offset_policy\":{\"focus_ranges\":[";
  for (std::size_t i = 0; i < strategy.focus_ranges.size(); ++i) {
    if (i != 0)
      out << ",";
    out << "[" << strategy.focus_ranges[i].begin << ","
        << strategy.focus_ranges[i].end << "]";
  }
  out << "],\"protect_ranges\":[";
  for (std::size_t i = 0; i < strategy.protect_ranges.size(); ++i) {
    if (i != 0)
      out << ",";
    out << "[" << strategy.protect_ranges[i].begin << ","
        << strategy.protect_ranges[i].end << "]";
  }
  out << "]},";
  out << "\"dictionary_tokens\":[";
  for (std::size_t i = 0; i < strategy.dictionary_tokens.size(); ++i) {
    if (i != 0)
      out << ",";
    out << "\"" << json_escape(strategy.dictionary_tokens[i]) << "\"";
  }
  out << "],";
  out << "\"repair_policy\":{\"length_fields\":[],\"checksum\":\"none\"},";
  out << "\"expected_signal\":\"" << json_escape(strategy.expected_signal)
      << "\"";
  out << "}";
  return out.str();
}

std::vector<std::string>
validate_strategy(const SeedMutationStrategy &strategy) {
  std::vector<std::string> errors;
  if (strategy.id.empty()) {
    errors.push_back("strategy.id is required");
  }
  if (strategy.selector.mode != "global" &&
      strategy.selector.mode != "seed_id" &&
      strategy.selector.mode != "seed_hash" &&
      strategy.selector.mode != "family") {
    errors.push_back("unsupported seed selector mode: " +
                     strategy.selector.mode);
  }
  if (strategy.selector.mode == "seed_id" &&
      strategy.selector.seed_id.empty()) {
    errors.push_back("seed_id selector requires seed_selector.seed_id");
  }
  if (strategy.selector.mode == "seed_hash" &&
      strategy.selector.seed_hash.empty()) {
    errors.push_back("seed_hash selector requires seed_selector.seed_hash");
  }
  if (strategy.operator_weights.empty()) {
    errors.push_back("operator_weights must not be empty");
  }
  for (const auto &range : strategy.focus_ranges) {
    if (range.end <= range.begin) {
      errors.push_back("focus range end must be greater than begin");
    }
  }
  for (const auto &range : strategy.protect_ranges) {
    if (range.end <= range.begin) {
      errors.push_back("protect range end must be greater than begin");
    }
  }
  for (const auto &token : strategy.dictionary_tokens) {
    if (token.size() > 4096) {
      errors.push_back("dictionary token is too large");
      break;
    }
    if (token.find('\n') != std::string::npos ||
        token.find('\r') != std::string::npos ||
        token.find('\0') != std::string::npos) {
      errors.push_back(
          "dictionary token contains unsupported control characters");
      break;
    }
  }
  return errors;
}

std::string stable_seed_hash_hex(const std::vector<unsigned char> &bytes) {
  uint64_t hash = 1469598103934665603ull;
  for (const unsigned char byte : bytes) {
    hash ^= static_cast<uint64_t>(byte);
    hash *= 1099511628211ull;
  }
  // Performance optimization: Avoid std::ostringstream to bypass
  // locale locking and dynamic allocations.
  std::string out;
  out.reserve(16);
  constexpr char kHex[] = "0123456789abcdef";
  for (int i = 60; i >= 0; i -= 4) {
    out.push_back(kHex[(hash >> i) & 0xf]);
  }
  return out;
}

std::string stable_seed_hash_hex(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("failed to open seed for hashing: " +
                             path.string());
  }
  std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(input)),
                                   std::istreambuf_iterator<char>());
  return stable_seed_hash_hex(bytes);
}

SeedMutationStrategy
make_default_dictionary_strategy(std::vector<std::string> tokens) {
  SeedMutationStrategy strategy;
  strategy.id = make_id("strategy_dictionary");
  strategy.agent = "DictionaryAgent";
  strategy.selector.mode = "global";
  strategy.priority = 50;
  strategy.ttl_sec = 900;
  strategy.operator_weights = {
      {MutationOp::InsertToken, 0.35},
      {MutationOp::DictionaryOverwrite, 0.25},
      {MutationOp::OverwriteRange, 0.20},
      {MutationOp::Arith, 0.10},
      {MutationOp::BitFlip, 0.10},
  };
  normalize_weights(strategy.operator_weights);
  strategy.focus_ranges = {{0, 4096}};
  strategy.protect_ranges = {};
  strategy.dictionary_tokens = std::move(tokens);
  strategy.expected_signal = "new_edges";
  return strategy;
}

SeedMutationStrategy make_seed_focus_strategy(std::string seed_id,
                                              std::vector<std::string> tokens) {
  SeedMutationStrategy strategy =
      make_default_dictionary_strategy(std::move(tokens));
  strategy.id = make_id("strategy_seed");
  strategy.agent = "MutatorAgent";
  strategy.selector.mode = "seed_id";
  strategy.selector.seed_id = std::move(seed_id);
  strategy.priority = 90;
  strategy.operator_weights = {
      {MutationOp::InsertToken, 0.40}, {MutationOp::OverwriteRange, 0.25},
      {MutationOp::Arith, 0.15},       {MutationOp::DeleteBlock, 0.10},
      {MutationOp::BitFlip, 0.10},
  };
  normalize_weights(strategy.operator_weights);
  strategy.focus_ranges = {{4, 4096}};
  strategy.protect_ranges = {{0, 4}};
  return strategy;
}

SeedMutationStrategy
make_seed_hash_strategy(const std::filesystem::path &seed_path,
                        std::vector<std::string> tokens) {
  SeedMutationStrategy strategy =
      make_default_dictionary_strategy(std::move(tokens));
  strategy.id = make_id("strategy_hash");
  strategy.agent = "MutatorAgent";
  strategy.selector.mode = "seed_hash";
  strategy.selector.seed_hash = stable_seed_hash_hex(seed_path);
  strategy.priority = 85;
  strategy.focus_ranges = {{0, 4096}};
  strategy.protect_ranges = {};
  return strategy;
}

SeedMutationStrategy
make_random_recipe_strategy(uint64_t seed, std::vector<std::string> tokens) {
  // Deterministic RNG keyed by `seed` — replay of the same run
  // produces the same recipe. The full list of operators is sampled
  // with uniform random weights, then normalized.
  std::mt19937_64 rng(seed == 0 ? 0xC0FFEEull : seed);
  std::uniform_real_distribution<double> dist(0.05, 1.0);

  SeedMutationStrategy strategy;
  strategy.id = make_id("strategy_random");
  strategy.agent = "RandomRecipeAblation";
  strategy.selector.mode = "global";
  strategy.priority = 50;
  strategy.ttl_sec = 900;
  // All six core operators get a random weight; agent-style focus is
  // skipped so the mutator falls back to whole-buffer random offset
  // selection. This is exactly the baseline-ish behaviour the
  // ablation is supposed to compare against the agent-driven recipe.
  strategy.operator_weights = {
      {MutationOp::BitFlip, dist(rng)},
      {MutationOp::OverwriteRange, dist(rng)},
      {MutationOp::InsertToken, dist(rng)},
      {MutationOp::Arith, dist(rng)},
      {MutationOp::Splice, dist(rng)},
      {MutationOp::DeleteBlock, dist(rng)},
  };
  normalize_weights(strategy.operator_weights);
  // No focus/protect — entire buffer is mutable. This is intentional:
  // the ablation answers "does targeted offset selection matter?"
  strategy.focus_ranges = {};
  strategy.protect_ranges = {};
  strategy.dictionary_tokens = std::move(tokens);
  strategy.expected_signal = "new_edges";
  return strategy;
}

} // namespace fuzzpilot
