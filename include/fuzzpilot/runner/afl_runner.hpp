#pragma once

#include "fuzzpilot/config.hpp"

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
                                  const std::filesystem::path& recipe_store);

AflLaunchSpec build_micro_afl_spec(const AppConfig& config,
                                   const MicroCampaignSpec& micro_spec,
                                   const std::filesystem::path& dict_override = {});

std::string shell_preview(const AflLaunchSpec& spec);

}  // namespace fuzzpilot

