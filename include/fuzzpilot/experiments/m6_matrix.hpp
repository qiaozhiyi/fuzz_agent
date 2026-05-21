#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace fuzzpilot {

struct M6MatrixOptions {
  std::vector<std::filesystem::path> config_paths;
  std::filesystem::path work_dir = "work_m6";
  std::filesystem::path out_dir = "results/m6_matrix";
  int repeats = 1;
  int main_budget_sec = 86400;
  int micro_budget_sec = 300;
  bool check_runtime_paths = false;
};

struct M6MatrixResult {
  std::filesystem::path manifest_path;
  std::filesystem::path report_path;
  std::size_t target_count = 0;
  std::size_t planned_runs = 0;
  std::size_t error_count = 0;
  std::size_t warning_count = 0;
};

M6MatrixResult write_m6_matrix(const M6MatrixOptions& options);
std::string m6_matrix_result_json(const M6MatrixResult& result);

}  // namespace fuzzpilot
