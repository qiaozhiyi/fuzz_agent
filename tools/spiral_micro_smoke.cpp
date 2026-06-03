#include "fuzzpilot/agents/agent_runtime.hpp"
#include "fuzzpilot/config.hpp"
#include "fuzzpilot/controller/run.hpp"
#include "fuzzpilot/micro/evaluator.hpp"
#include "fuzzpilot/micro/manager.hpp"
#include "fuzzpilot/runner/afl_runner.hpp"
#include "fuzzpilot/storage/db.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <string>
#include <vector>

namespace {

std::filesystem::path unique_temp_dir() {
  const auto stamp =
      std::chrono::steady_clock::now().time_since_epoch().count();
  return std::filesystem::temp_directory_path() /
         ("fuzzpilot_spiral_micro_smoke_" + std::to_string(stamp));
}

void write_file(const std::filesystem::path& path, const std::string& text) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream out(path);
  out << text;
}

fuzzpilot::AgentDecision make_decision(const std::string& id,
                                       const std::string& agent,
                                       const std::string& proposal_json) {
  fuzzpilot::AgentDecision decision;
  decision.id = id;
  decision.run_id = "run_libxml2_spiral_smoke";
  decision.plateau_id = "plateau_libxml2_spiral_smoke";
  decision.agent = agent;
  decision.task_json = "{}";
  decision.proposal_json = proposal_json;
  decision.model_response.provider = "fake";
  decision.model_response.model = "fake-libxml2";
  decision.model_response.context_hash = "ctx";
  decision.model_response.response_hash = id + "_hash";
  decision.model_response.error_kind = "ok";
  decision.model_response.schema_valid = true;
  decision.fallback_used = false;
  decision.created_ts = 1000;
  return decision;
}

bool contains(const std::string& haystack, const std::string& needle) {
  return haystack.find(needle) != std::string::npos;
}

const fuzzpilot::MicroCampaignSpec* find_by_name(
    const std::vector<fuzzpilot::MicroCampaignSpec>& specs,
    const std::string& name) {
  const auto it = std::find_if(specs.begin(), specs.end(),
                               [&](const auto& spec) {
                                 return spec.name == name;
                               });
  return it == specs.end() ? nullptr : &(*it);
}

}  // namespace

int main() {
  const auto root = unique_temp_dir();
  const auto snapshot_dir = root / "libxml2_snapshot";
  const auto work_dir = root / "libxml2_micro";
  write_file(snapshot_dir / "id:000000,orig:seed.xml", "<root/>");

  fuzzpilot::AppConfig config;
  config.project = "libxml2_fuzz";
  config.target.name = "libxml2_parser";
  config.target.input_dir = snapshot_dir;
  config.micro_campaign.enabled = true;
  config.micro_campaign.budget_sec = 42;

  fuzzpilot::Database db;
  db.open(root / "fuzzpilot.sqlite");
  db.initialize_schema("db/schema.sql");

  db.insert_agent_decision(make_decision(
      "decision_dictionary",
      "DictionaryAgent",
      R"({"interventions":[{"action":"dictionary_probe","hypothesis":"libxml2 XML dictionary gap","tokens":["<!DOCTYPE","CDATA"],"priority":0.90,"spiral_stage":1,"params":{"target":"libxml2"}}]})"));
  db.insert_agent_decision(make_decision(
      "decision_scheduler",
      "SchedulerAgent",
      R"({"interventions":[{"action":"seed_focus_probe","hypothesis":"libxml2 queue has promising structural seeds","tokens":["xmlns"],"priority":0.80,"spiral_stage":2,"params":{"target":"libxml2"}}]})"));
  db.insert_agent_decision(make_decision(
      "decision_mutator",
      "MutatorAgent",
      R"({"interventions":[{"action":"per_seed_recipe_probe","hypothesis":"libxml2 per-seed recipes should inherit XML tokens","tokens":["encoding","version"],"priority":0.70,"spiral_stage":3,"params":{"target":"libxml2"}}]})"));

  const auto specs = fuzzpilot::plan_micro_campaigns(
      config, "plateau_libxml2_spiral_smoke", snapshot_dir, work_dir,
      true, &db, "run_libxml2_spiral_smoke");

  if (specs.size() != 4) {
    std::cerr << "expected exactly 4 libxml2 spiral specs, got "
              << specs.size() << "\n";
    return 1;
  }
  if (specs.front().name != "default_control" ||
      specs.front().spiral_stage != 0 ||
      !specs.front().depends_on_intervention_id.empty()) {
    std::cerr << "stage-0 control spec missing or malformed\n";
    return 2;
  }

  const auto* dictionary = find_by_name(specs, "dictionary_probe");
  const auto* seed_focus = find_by_name(specs, "seed_focus_probe");
  const auto* per_seed = find_by_name(specs, "per_seed_recipe_probe");
  if (dictionary == nullptr || seed_focus == nullptr || per_seed == nullptr) {
    std::cerr << "expected dictionary, seed-focus, and per-seed stages\n";
    return 3;
  }
  if (dictionary->spiral_stage != 1 || seed_focus->spiral_stage != 2 ||
      per_seed->spiral_stage != 3) {
    std::cerr << "spiral stages were not preserved\n";
    return 4;
  }
  if (dictionary->source_decision_id != "decision_dictionary" ||
      seed_focus->source_decision_id != "decision_scheduler" ||
      per_seed->source_decision_id != "decision_mutator") {
    std::cerr << "source decision traceability was lost\n";
    return 5;
  }
  if (dictionary->depends_on_intervention_id != specs.front().intervention_id ||
      seed_focus->depends_on_intervention_id != dictionary->intervention_id ||
      per_seed->depends_on_intervention_id != seed_focus->intervention_id) {
    std::cerr << "spiral dependencies do not climb stage-by-stage\n";
    return 6;
  }

  int previous_stage = -1;
  for (const auto& spec : specs) {
    if (spec.spiral_stage < previous_stage) {
      std::cerr << "specs are not ordered by spiral stage\n";
      return 7;
    }
    previous_stage = spec.spiral_stage;
    const auto json = fuzzpilot::micro_campaign_spec_json(spec);
    if (contains(json, "cjson")) {
      std::cerr << "obsolete cjson target leaked into libxml2 micro spec\n";
      return 8;
    }
    if (spec.spiral_stage > 0 &&
        !contains(json, "\"depends_on_intervention_id\"")) {
      std::cerr << "spec JSON omitted dependency trace\n";
      return 9;
    }
  }

  const auto dictionary_json = fuzzpilot::micro_campaign_spec_json(*dictionary);
  if (!contains(dictionary_json, "\"spiral_stage\":1") ||
      !contains(dictionary_json, "\"source_decision_id\":\"decision_dictionary\"")) {
    std::cerr << "spec JSON omitted spiral stage or source decision\n";
    return 10;
  }

  db.insert_agent_decision(make_decision(
      "decision_cmp",
      "CmpAgent",
      R"({"interventions":[{"action":"dictionary_probe","hypothesis":"cmp-derived XML magic tokens","tokens":["<?xml","<!ENTITY"],"priority":0.90,"spiral_stage":1,"params":{"target":"libxml2"}}]})"));
  db.insert_agent_decision(make_decision(
      "decision_plateau",
      "PlateauDiagnosisAgent",
      R"({"interventions":[{"action":"seed_focus_probe","hypothesis":"plateau shows structural seed family","tokens":["DOCTYPE"],"priority":0.80,"spiral_stage":2,"params":{"target":"libxml2"}},{"action":"per_seed_recipe_probe","hypothesis":"per-seed recipes should climb from focused seeds","tokens":["SYSTEM"],"priority":0.70,"spiral_stage":3,"params":{"target":"libxml2"}}]})"));
  db.insert_agent_decision(make_decision(
      "decision_format",
      "FormatAgent",
      R"({"interventions":[{"action":"dictionary_probe","hypothesis":"format grammar tokens","tokens":["<root","</root>"],"priority":0.90,"spiral_stage":1,"params":{"target":"libxml2"}},{"action":"seed_focus_probe","hypothesis":"format-aware seed family","tokens":["xmlns"],"priority":0.80,"spiral_stage":2,"params":{"target":"libxml2"}}]})"));
  db.insert_agent_decision(make_decision(
      "decision_corpus",
      "CorpusAgent",
      R"({"interventions":[{"action":"seed_focus_probe","hypothesis":"corpus clusters with namespace seeds","tokens":["xlink"],"priority":0.80,"spiral_stage":2,"params":{"target":"libxml2"}}]})"));
  db.insert_agent_decision(make_decision(
      "decision_coordinator",
      "CoordinatorAgent",
      R"({"interventions":[{"action":"dictionary_probe","hypothesis":"coordinate dictionary first","tokens":["CDATA"],"priority":0.90,"spiral_stage":1,"params":{"target":"libxml2"}},{"action":"seed_focus_probe","hypothesis":"then focus promising seeds","tokens":["encoding"],"priority":0.80,"spiral_stage":2,"params":{"target":"libxml2"}},{"action":"per_seed_recipe_probe","hypothesis":"then specialize per seed","tokens":["version"],"priority":0.70,"spiral_stage":3,"params":{"target":"libxml2"}}]})"));

  const auto council_specs = fuzzpilot::plan_micro_campaigns(
      config, "plateau_libxml2_spiral_council", snapshot_dir, work_dir,
      true, &db, "run_libxml2_spiral_smoke");
  if (council_specs.size() != 4) {
    std::cerr << "expected council decisions to coalesce into 4 spiral specs, got "
              << council_specs.size() << "\n";
    return 13;
  }
  const std::vector<std::string> required_agents = {
      "CoordinatorAgent", "PlateauDiagnosisAgent", "SchedulerAgent", "CmpAgent",
      "MutatorAgent", "DictionaryAgent", "FormatAgent", "CorpusAgent"};
  std::string combined_json;
  for (const auto& spec : council_specs) {
    combined_json += fuzzpilot::micro_campaign_spec_json(spec);
  }
  for (const auto& agent : required_agents) {
    if (!contains(combined_json, agent)) {
      std::cerr << "agent contribution missing from coalesced spiral specs: "
                << agent << "\n";
      return 14;
    }
  }
  const auto* council_dictionary = find_by_name(council_specs, "dictionary_probe");
  const auto* council_seed_focus = find_by_name(council_specs, "seed_focus_probe");
  const auto* council_per_seed = find_by_name(council_specs, "per_seed_recipe_probe");
  if (council_dictionary == nullptr || council_seed_focus == nullptr ||
      council_per_seed == nullptr) {
    std::cerr << "coalesced spiral specs omitted one of the expected stages\n";
    return 15;
  }
  if (council_dictionary->depends_on_intervention_id !=
          council_specs.front().intervention_id ||
      council_seed_focus->depends_on_intervention_id !=
          council_dictionary->intervention_id ||
      council_per_seed->depends_on_intervention_id !=
          council_seed_focus->intervention_id) {
    std::cerr << "coalesced spiral dependencies do not climb stage-by-stage\n";
    return 16;
  }

  const auto resume_input = root / "resume_input";
  write_file(resume_input / "id:000001,orig:merged.xml", "<merged/>");
  const auto resume_launch = fuzzpilot::build_main_afl_spec(
      config, root / "main_out", root / "main_recipes", true, resume_input);
  const auto input_flag = std::find(resume_launch.argv.begin(),
                                    resume_launch.argv.end(), "-i");
  if (input_flag == resume_launch.argv.end() ||
      std::next(input_flag) == resume_launch.argv.end() ||
      *std::next(input_flag) != resume_input.string()) {
    std::cerr << "main AFL restart did not use clean resume input dir\n";
    return 17;
  }
  const auto fresh_restart_launch = fuzzpilot::build_main_restart_afl_spec(
      config, root, root / "main_recipes", 0, resume_input);
  const auto expected_restart_out = root / "main_out_restart_0";
  if (fresh_restart_launch.output_dir != expected_restart_out) {
    std::cerr << "main AFL restart reused the original output dir\n";
    return 19;
  }
  const auto output_flag = std::find(fresh_restart_launch.argv.begin(),
                                    fresh_restart_launch.argv.end(), "-o");
  if (output_flag == fresh_restart_launch.argv.end() ||
      std::next(output_flag) == fresh_restart_launch.argv.end() ||
      *std::next(output_flag) != expected_restart_out.string()) {
    std::cerr << "main AFL restart command did not point at fresh output dir\n";
    return 20;
  }
  const auto fresh_input_flag =
      std::find(fresh_restart_launch.argv.begin(),
                fresh_restart_launch.argv.end(), "-i");
  if (fresh_input_flag == fresh_restart_launch.argv.end() ||
      std::next(fresh_input_flag) == fresh_restart_launch.argv.end() ||
      *std::next(fresh_input_flag) != resume_input.string()) {
    std::cerr << "main AFL restart command did not use clean restart seeds\n";
    return 21;
  }

  std::vector<fuzzpilot::MicroResult> control_wins = {
      {.intervention_id = specs[0].intervention_id,
       .campaign_id = specs[0].id,
       .reward = 100.0},
      {.intervention_id = dictionary->intervention_id,
       .campaign_id = dictionary->id,
       .reward = 90.0},
      {.intervention_id = seed_focus->intervention_id,
       .campaign_id = seed_focus->id,
       .reward = 80.0},
      {.intervention_id = per_seed->intervention_id,
       .campaign_id = per_seed->id,
       .reward = 70.0},
  };
  const auto no_winner = fuzzpilot::select_micro_winner_against_control(
      control_wins, specs, std::set<std::string>{});
  if (no_winner.selected) {
    std::cerr << "default control was allowed to win over agent stages\n";
    return 11;
  }

  std::vector<fuzzpilot::MicroResult> agent_wins = control_wins;
  agent_wins[3].reward = 120.0;
  const auto winner = fuzzpilot::select_micro_winner_against_control(
      agent_wins, specs, std::set<std::string>{});
  if (!winner.selected || winner.result_index != 3 ||
      winner.improvement_over_control <= 0.0) {
    std::cerr << "agent stage did not win despite beating control\n";
    return 12;
  }
  if (!fuzzpilot::should_persist_micro_result(agent_wins[3], std::set<std::string>{}) ||
      fuzzpilot::should_persist_micro_result(agent_wins[3],
                                            std::set<std::string>{agent_wins[3].campaign_id})) {
    std::cerr << "failed micro campaign persistence filter is wrong\n";
    return 18;
  }

  const std::vector<fuzzpilot::MicroBanditCandidate> candidates = {
      {.campaign_id = "a", .mean_reward = 1.0, .budget_sec = 10},
      {.campaign_id = "b", .mean_reward = 2.0, .budget_sec = 10},
      {.campaign_id = "c", .mean_reward = 0.5, .budget_sec = 1},
  };
  const auto ranked = fuzzpilot::rank_micro_bandit_candidates(
      candidates, 21.0, 3.0);
  if (ranked.size() != candidates.size() || ranked.front().campaign_id != "c") {
    std::cerr << "bandit scheduler did not prioritize the under-probed candidate\n";
    return 22;
  }
  const auto tie_ranked = fuzzpilot::rank_micro_bandit_candidates(
      {{.campaign_id = "b", .mean_reward = 1.0, .budget_sec = 1},
       {.campaign_id = "a", .mean_reward = 1.0, .budget_sec = 1}},
      10.0, 1.0);
  if (tie_ranked.size() != 2 || tie_ranked.front().campaign_id != "a") {
    std::cerr << "bandit scheduler tie-break was not deterministic\n";
    return 23;
  }

  std::vector<fuzzpilot::AgentDecision> direct_decisions;
  direct_decisions.push_back(make_decision(
      "decision_direct_control",
      "CoordinatorAgent",
      R"({"interventions":[{"action":"default_control","hypothesis":"control only"}]})"));
  direct_decisions.push_back(make_decision(
      "decision_direct_dictionary",
      "DictionaryAgent",
      R"({"interventions":[{"action":"dictionary_probe","hypothesis":"direct XML tokens","tokens":["<tag>"],"priority":0.90}]})"));
  const auto direct = fuzzpilot::select_direct_promotion_for_test(direct_decisions);
  if (!direct || direct->id != "decision_direct_dictionary" ||
      direct->agent != "DictionaryAgent") {
    std::cerr << "ai-direct did not select the first valid non-control proposal\n";
    return 24;
  }

  std::filesystem::remove_all(root);
  std::cout << "spiral micro smoke passed\n";
  return 0;
}
