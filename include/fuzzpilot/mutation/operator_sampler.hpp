#pragma once

#include "fuzzpilot/mutation/strategy.hpp"

#include <random>
#include <vector>

namespace fuzzpilot {

class OperatorSampler {
 public:
  explicit OperatorSampler(std::vector<OperatorWeight> weights);

  MutationOp sample(std::mt19937& rng) const;
  const std::vector<OperatorWeight>& weights() const { return weights_; }

 private:
  std::vector<OperatorWeight> weights_;
};

}  // namespace fuzzpilot

