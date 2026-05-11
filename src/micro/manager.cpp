#include "fuzzpilot/micro/manager.hpp"

#include "fuzzpilot/ids.hpp"
#include "fuzzpilot/mutation/recipe_store.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include "fuzzpilot/json_util.hpp"

namespace fuzzpilot {
namespace {

std::filesystem::path find_queue_dir(const std::filesystem::path& afl_output_dir) {
  const auto direct = afl_output_dir / "queue";
  if (std::filesystem::is_directory(direct)) {
    return direct;
  }
  const auto default_worker = afl_output_dir / "default" / "queue";
  if (std::filesystem::is_directory(default_worker)) {
    return default_worker;
  }
  if (std::filesystem::is_directory(afl_output_dir)) {
    return afl_output_dir;
  }
  return {};
}



}  // namespace

CorpusSnapshotResult snapshot_corpus(const std::filesystem::path& afl_output_dir,
                                     const std::filesystem::path& snapshot_dir) {
  const auto queue_dir = find_queue_dir(afl_output_dir);
  if (queue_dir.empty()) {
    throw std::runtime_error("failed to find AFL++ queue under: " + afl_output_dir.string());
  }

  std::filesystem::create_directories(snapshot_dir);
  CorpusSnapshotResult result;
  result.source_queue = queue_dir;
  result.snapshot_dir = snapshot_dir;

  for (const auto& entry : std::filesystem::directory_iterator(queue_dir)) {
    if (!entry.is_regular_file()) {
      continue;
    }
    const auto filename = entry.path().filename().string();
    if (!filename.empty() && filename[0] == '.') {
      continue;
    }
    const auto dest = snapshot_dir / entry.path().filename();
    std::filesystem::copy_file(entry.path(), dest,
                               std::filesystem::copy_options::overwrite_existing);
    ++result.files_copied;
    result.bytes_copied += std::filesystem::file_size(dest);
  }
  if (result.files_copied == 0) {
    throw std::runtime_error("queue snapshot copied zero files from: " + queue_dir.string());
  }
  return result;
}

std::vector<MicroCampaignSpec> plan_micro_campaigns(const AppConfig& config,
                                                    const std::string& plateau_id,
                                                    const std::filesystem::path& snapshot_dir,
                                                    const std::filesystem::path& work_dir,
                                                    bool dry_run) {
  std::vector<MicroCampaignSpec> specs;
  const auto interventions = default_v0_interventions(config.micro_campaign.budget_sec);
  for (const auto& intervention : interventions) {
    MicroCampaignSpec spec;
    spec.id = make_id("micro");
    spec.intervention_id = intervention.id;
    spec.name = intervention.action;
    spec.input_dir = snapshot_dir;
    spec.output_dir = work_dir / plateau_id / intervention.action / "out";
    spec.recipe_store = work_dir / plateau_id / intervention.action / "recipes";
    spec.budget_sec = static_cast<uint32_t>(config.micro_campaign.budget_sec);
    spec.dry_run = dry_run;
    specs.push_back(std::move(spec));
  }
  return specs;
}

void prepare_micro_campaigns(const std::vector<MicroCampaignSpec>& specs) {
  for (const auto& spec : specs) {
    std::filesystem::create_directories(spec.output_dir);
    RecipeStore store(spec.recipe_store);
    auto strategy = make_default_dictionary_strategy({"FUZZ", "MAGIC", "TOKEN"});
    store.write_compact_recipes({strategy});
    std::ofstream manifest(spec.output_dir.parent_path() / "campaign.json");
    manifest << micro_campaign_spec_json(spec) << "\n";
  }
}

std::string corpus_snapshot_json(const CorpusSnapshotResult& snapshot) {
  std::ostringstream out;
  out << "{";
  out << "\"source_queue\":\"" << fuzzpilot::json_escape(snapshot.source_queue.string()) << "\",";
  out << "\"snapshot_dir\":\"" << fuzzpilot::json_escape(snapshot.snapshot_dir.string()) << "\",";
  out << "\"files_copied\":" << snapshot.files_copied << ",";
  out << "\"bytes_copied\":" << snapshot.bytes_copied;
  out << "}";
  return out.str();
}

std::string micro_campaign_spec_json(const MicroCampaignSpec& spec) {
  std::ostringstream out;
  out << "{";
  out << "\"id\":\"" << fuzzpilot::json_escape(spec.id) << "\",";
  out << "\"intervention_id\":\"" << fuzzpilot::json_escape(spec.intervention_id) << "\",";
  out << "\"name\":\"" << fuzzpilot::json_escape(spec.name) << "\",";
  out << "\"input_dir\":\"" << fuzzpilot::json_escape(spec.input_dir.string()) << "\",";
  out << "\"output_dir\":\"" << fuzzpilot::json_escape(spec.output_dir.string()) << "\",";
  out << "\"recipe_store\":\"" << fuzzpilot::json_escape(spec.recipe_store.string()) << "\",";
  out << "\"budget_sec\":" << spec.budget_sec << ",";
  out << "\"dry_run\":" << (spec.dry_run ? "true" : "false");
  out << "}";
  return out.str();
}

}  // namespace fuzzpilot

