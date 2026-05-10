#include "mutator_core.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

std::string arg_value(int argc, char** argv, const std::string& name) {
  for (int i = 1; i + 1 < argc; ++i) {
    if (argv[i] == name) {
      return argv[i + 1];
    }
  }
  return {};
}

std::vector<unsigned char> read_file(const std::string& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("failed to open input file: " + path);
  }
  return std::vector<unsigned char>((std::istreambuf_iterator<char>(input)),
                                    std::istreambuf_iterator<char>());
}

bool telemetry_has_recipe_hit(const std::string& path) {
  std::ifstream input(path);
  std::string line;
  while (std::getline(input, line)) {
    const std::string needle = "\"recipe_hits\":";
    const auto pos = line.find(needle);
    if (pos == std::string::npos) {
      continue;
    }
    const auto start = pos + needle.size();
    if (start < line.size() && line[start] != '0') {
      return true;
    }
  }
  return false;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    const auto recipe_store = arg_value(argc, argv, "--recipe-store");
    const auto input_file = arg_value(argc, argv, "--input-file");
    const auto seed_name = arg_value(argc, argv, "--seed-name");
    const auto telemetry = arg_value(argc, argv, "--telemetry");
    if (recipe_store.empty() || input_file.empty() || seed_name.empty() || telemetry.empty()) {
      std::cerr << "usage: fuzzpilot_mutator_smoke --recipe-store PATH --input-file PATH "
                   "--seed-name ID --telemetry PATH\n";
      return 2;
    }

    setenv("FUZZPILOT_RECIPE_STORE", recipe_store.c_str(), 1);
    setenv("FUZZPILOT_MUTATOR_TELEMETRY", telemetry.c_str(), 1);
    auto input = read_file(input_file);
    if (input.empty()) {
      input.push_back(0);
    }

    void* state = fp_mutator_init(1234);
    fp_mutator_queue_get(state, seed_name.c_str());
    for (int i = 0; i < 8; ++i) {
      unsigned char* out = nullptr;
      (void)fp_mutator_fuzz(state, input.data(), input.size(), &out, nullptr, 0, 4096);
    }
    fp_mutator_deinit(state);

    if (!telemetry_has_recipe_hit(telemetry)) {
      std::cerr << "mutator telemetry did not record recipe hit\n";
      return 3;
    }
    std::cout << "mutator recipe lookup smoke passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << "\n";
    return 1;
  }
}
