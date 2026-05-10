#include "fuzzpilot/mutation/operator_sampler.hpp"

#include <random>

namespace fuzzpilot {

OperatorSampler::OperatorSampler(std::vector<OperatorWeight> weights)
    : weights_(std::move(weights)) {
  normalize_weights(weights_);
}

MutationOp OperatorSampler::sample(std::mt19937& rng) const {
  if (weights_.empty()) {
    return MutationOp::BitFlip;
  }
  std::uniform_real_distribution<double> dist(0.0, 1.0);
  const double needle = dist(rng);
  double cumulative = 0.0;
  for (const auto& weight : weights_) {
    cumulative += weight.weight;
    if (needle <= cumulative) {
      return weight.op;
    }
  }
  return weights_.back().op;
}

}  // namespace fuzzpilot

