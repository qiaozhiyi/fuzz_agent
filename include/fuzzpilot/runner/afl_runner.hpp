#pragma once

#include "fuzzpilot/config.hpp"

#include <cstddef>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "fuzzpilot/micro/manager.hpp"

namespace fuzzpilot {

struct AflLaunchSpec {
  std::filesystem::path afl_fuzz;
  std::vector<std::string> argv;
  std::map<std::string, std::string> env;
  std::filesystem::path output_dir;
};

AflLaunchSpec build_main_afl_spec(const AppConfig& config,
                                  const std::filesystem::path& output_dir,
                                  const std::filesystem::path& recipe_store,
                                  bool resume = false,
                                  const std::filesystem::path& resume_input_dir = {});

std::filesystem::path main_restart_output_dir(
    const std::filesystem::path& run_dir,
    std::size_t restart_index);

AflLaunchSpec build_main_restart_afl_spec(
    const AppConfig& config,
    const std::filesystem::path& run_dir,
    const std::filesystem::path& recipe_store,
    std::size_t restart_index,
    const std::filesystem::path& restart_input_dir);

AflLaunchSpec build_micro_afl_spec(const AppConfig& config,
                                   const MicroCampaignSpec& micro_spec,
                                   const std::filesystem::path& dict_override = {});

std::string shell_preview(const AflLaunchSpec& spec);

}  // namespace fuzzpilot
