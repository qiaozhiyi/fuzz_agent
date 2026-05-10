#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace fuzzpilot {

enum class MutationOp : uint8_t {
  BitFlip,
  OverwriteRange,
  InsertToken,
  Arith,
  Splice,
  DeleteBlock,
  CloneBlock,
  DictionaryOverwrite
};

struct ByteRange {
  uint32_t begin = 0;
  uint32_t end = 0;
};

struct OperatorWeight {
  MutationOp op = MutationOp::BitFlip;
  double weight = 1.0;
};

struct SeedSelector {
  std::string mode = "global";
  std::string seed_id;
  std::string seed_hash;
  std::string family;
};

struct SeedMutationStrategy {
  std::string id;
  std::string agent = "Builtin";
  SeedSelector selector;
  uint32_t priority = 0;
  uint32_t ttl_sec = 900;
  std::vector<OperatorWeight> operator_weights;
  std::vector<ByteRange> focus_ranges;
  std::vector<ByteRange> protect_ranges;
  std::vector<std::string> dictionary_tokens;
  std::string expected_signal = "new_edges";
};

std::string mutation_op_name(MutationOp op);
MutationOp mutation_op_from_name(const std::string& name);
void normalize_weights(std::vector<OperatorWeight>& weights);
std::string strategy_json(const SeedMutationStrategy& strategy);
std::vector<std::string> validate_strategy(const SeedMutationStrategy& strategy);
std::string stable_seed_hash_hex(const std::vector<unsigned char>& bytes);
std::string stable_seed_hash_hex(const std::filesystem::path& path);

SeedMutationStrategy make_default_dictionary_strategy(std::vector<std::string> tokens);
SeedMutationStrategy make_seed_focus_strategy(std::string seed_id,
                                              std::vector<std::string> tokens);
SeedMutationStrategy make_seed_hash_strategy(const std::filesystem::path& seed_path,
                                             std::vector<std::string> tokens);

}  // namespace fuzzpilot
