# FuzzPilot Architecture Pilot Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the paper-ready architecture pilot features and low-noise experiment workflow described in `docs/superpowers/specs/2026-06-02-fuzzpilot-architecture-pilot-design.md`.

**Architecture:** Keep FuzzPilot's data plane, semantic control plane, and validation plane separated. Add missing ablation modes as controller/runtime switches, add a deterministic bandit-style micro-campaign scheduler, and add a host-aware runner/manifest for controlled 4-core experiments.

**Tech Stack:** C++20/CMake, AFL++, Bash, Python 3, YAML manifests, SQLite run artifacts, existing FuzzPilot smoke binaries and CTest suite.

---

## File Structure

- Modify `include/fuzzpilot/controller/run.hpp`: add options for `ai-direct`, semantic context suppression, and agent subset selection if they are not already represented.
- Modify `src/cli/main.cpp`: expose CLI switches and help text.
- Modify `src/controller/run.cpp`: normalize new ablation modes, wire direct promotion, suppress semantic context when requested, and pass agent subset filters into task configuration.
- Modify `include/fuzzpilot/micro/manager.hpp` and `src/micro/manager.cpp`: add a deterministic validation scheduler interface that can select probe/final micro-campaign order.
- Modify `include/fuzzpilot/micro/evaluator.hpp` and `src/micro/evaluator.cpp`: add helper scoring functions for bandit scheduling if the existing reward type is insufficient.
- Modify `tools/spiral_micro_smoke.cpp` or create `tools/architecture_pilot_smoke.cpp`: test direct-promotion planning, agent subset filtering, semantic-context suppression, and scheduler ordering without launching real AFL++.
- Modify `CMakeLists.txt`: register any new smoke binary.
- Create `experiments/manifests/paper_architecture_pilot.yaml`: controlled Stage A/B/C matrix.
- Create `scripts/paper01/runners/run_architecture_pilot.sh`: 4-core/noise-controlled runner.
- Modify `scripts/prepare_paper01_data.py` or create `scripts/paper01/architecture_pilot_summary.py`: aggregate pilot metrics without disturbing existing paper01 outputs.
- Create `docs/papers/architecture_pilot_runbook.md`: reproduce the staged experiment.

## Task 1: Add CLI and Config Surface for New Ablations

**Files:**
- Modify: `include/fuzzpilot/controller/run.hpp`
- Modify: `src/cli/main.cpp`
- Modify: `src/controller/run.cpp`
- Test: existing `fuzzpilot_check_libxml2_glm` plus a new smoke assertion

- [ ] **Step 1: Write failing smoke assertions**

Add checks to `tools/spiral_micro_smoke.cpp` near the existing ablation-related checks:

```cpp
{
  fuzzpilot::RunOptions options;
  options.ablation_mode = "ai-direct";
  // The smoke should call a small helper, added in Step 3, that normalizes
  // ablation options without requiring a real run.
  auto normalized = fuzzpilot::normalize_run_options_for_test(options, config);
  if (normalized.ablation_mode != "ai-direct" ||
      !normalized.direct_promote_without_microcampaign) {
    std::cerr << "ai-direct did not enable direct promotion mode\n";
    return 30;
  }
}

{
  fuzzpilot::RunOptions options;
  options.ablation_mode = "single-agent-coordinator";
  auto normalized = fuzzpilot::normalize_run_options_for_test(options, config);
  if (normalized.agent_subset != "CoordinatorAgent") {
    std::cerr << "single-agent-coordinator did not select CoordinatorAgent\n";
    return 31;
  }
}

{
  fuzzpilot::RunOptions options;
  options.ablation_mode = "no-semantic-context";
  auto normalized = fuzzpilot::normalize_run_options_for_test(options, config);
  if (!normalized.suppress_semantic_context) {
    std::cerr << "no-semantic-context did not suppress semantic context\n";
    return 32;
  }
}
```

- [ ] **Step 2: Run the targeted smoke and verify it fails**

Run:

```bash
cmake --build build --target fuzzpilot_spiral_micro_smoke --parallel 2
ctest --test-dir build -R fuzzpilot_spiral_micro_smoke --output-on-failure
```

Expected: build or test fails because `normalize_run_options_for_test` and new
`RunOptions` fields do not exist yet.

- [ ] **Step 3: Add minimal option fields and helper**

In `include/fuzzpilot/controller/run.hpp`, extend `RunOptions`:

```cpp
bool direct_promote_without_microcampaign = false;
bool suppress_semantic_context = false;
std::string agent_subset;
```

Declare:

```cpp
RunOptions normalize_run_options_for_test(RunOptions options, const AppConfig& config);
```

In `src/controller/run.cpp`, factor the existing ablation normalization into a
helper used by both tests and `run_mvp`:

```cpp
RunOptions normalize_run_options_for_test(RunOptions options, const AppConfig& config) {
  options.ablation_mode = normalize_ablation_mode(options.ablation_mode);
  apply_ablation_overrides(options, config);
  return options;
}
```

The implementation should keep existing behavior for current modes and add:

```cpp
} else if (options.ablation_mode == "ai-direct") {
  options.direct_promote_without_microcampaign = true;
} else if (options.ablation_mode == "single-agent-coordinator") {
  options.agent_subset = "CoordinatorAgent";
} else if (options.ablation_mode == "single-agent-dictionary") {
  options.agent_subset = "DictionaryAgent";
} else if (options.ablation_mode == "no-semantic-context") {
  options.suppress_semantic_context = true;
}
```

Update the unsupported-mode error to include these new modes.

- [ ] **Step 4: Add CLI help text**

In `src/cli/main.cpp`, extend the `--ablation` help string with:

```text
ai-direct|single-agent-coordinator|single-agent-dictionary|no-semantic-context
```

- [ ] **Step 5: Verify targeted smoke passes**

Run:

```bash
cmake --build build --target fuzzpilot_spiral_micro_smoke --parallel 2
ctest --test-dir build -R fuzzpilot_spiral_micro_smoke --output-on-failure
```

Expected: `fuzzpilot_spiral_micro_smoke` passes.

## Task 2: Wire Agent Subset and Semantic Context Suppression

**Files:**
- Modify: `src/controller/run.cpp`
- Modify: `include/fuzzpilot/agents/agent_runtime.hpp` if helper signatures require it
- Test: `tools/agent_runtime_smoke.cpp` or `tools/spiral_micro_smoke.cpp`

- [ ] **Step 1: Write failing agent subset test**

Add to `tools/agent_runtime_smoke.cpp`:

```cpp
std::vector<fuzzpilot::AgentTask> tasks = fuzzpilot::default_agent_tasks(
    "{}", "plateau_subset", 30, "");
fuzzpilot::filter_agent_tasks_for_subset(tasks, "CoordinatorAgent");
if (tasks.size() != 1 || tasks.front().agent != "CoordinatorAgent") {
  std::cerr << "agent subset filter did not keep only CoordinatorAgent\n";
  return 20;
}
```

- [ ] **Step 2: Run test to verify failure**

Run:

```bash
cmake --build build --target fuzzpilot_agent_runtime_smoke --parallel 2
ctest --test-dir build -R fuzzpilot_agent_runtime_smoke --output-on-failure
```

Expected: build fails because `filter_agent_tasks_for_subset` does not exist.

- [ ] **Step 3: Implement agent subset helper**

In `include/fuzzpilot/agents/agent_runtime.hpp`, declare:

```cpp
void filter_agent_tasks_for_subset(std::vector<AgentTask>& tasks,
                                   const std::string& agent_name);
```

In `src/agents/agent_runtime.cpp`, implement:

```cpp
void filter_agent_tasks_for_subset(std::vector<AgentTask>& tasks,
                                   const std::string& agent_name) {
  if (agent_name.empty()) return;
  std::vector<AgentTask> filtered;
  for (const auto& task : tasks) {
    if (task.agent == agent_name) {
      filtered.push_back(task);
    }
  }
  tasks = std::move(filtered);
}
```

Call it in `src/controller/run.cpp` immediately after `configure_agent_tasks`.

- [ ] **Step 4: Suppress semantic context**

In `src/controller/run.cpp`, when building `static_context_json` and blackboard
JSON for agent calls, use:

```cpp
const std::string effective_static_context_json =
    options.suppress_semantic_context ? "{}" : static_context_json;
```

Also avoid writing extracted dictionary tokens into agent proposals when
`suppress_semantic_context` is set. Keep raw AFL telemetry intact.

- [ ] **Step 5: Verify tests**

Run:

```bash
cmake --build build --target fuzzpilot_agent_runtime_smoke fuzzpilot_spiral_micro_smoke --parallel 2
ctest --test-dir build -R 'fuzzpilot_(agent_runtime_smoke|spiral_micro_smoke)' --output-on-failure
```

Expected: both tests pass.

## Task 3: Implement `ai-direct` Promotion

**Files:**
- Modify: `src/controller/run.cpp`
- Modify: `include/fuzzpilot/micro/evaluator.hpp` only if winner status enum must expand
- Modify: `src/micro/evaluator.cpp` only if winner status enum must expand
- Test: `tools/spiral_micro_smoke.cpp`

- [ ] **Step 1: Write failing direct-promotion selection test**

Add a helper test in `tools/spiral_micro_smoke.cpp`:

```cpp
std::vector<fuzzpilot::AgentDecision> direct_decisions;
fuzzpilot::AgentDecision decision;
decision.agent = "DictionaryAgent";
decision.proposal_json =
    "{\"interventions\":[{\"action\":\"dictionary_probe\","
    "\"hypothesis\":\"direct test\",\"tokens\":[\"<tag>\"]}]}";
direct_decisions.push_back(decision);
auto direct = fuzzpilot::select_direct_promotion_for_test(direct_decisions);
if (!direct.has_value() || direct->agent != "DictionaryAgent") {
  std::cerr << "ai-direct did not select a valid proposal\n";
  return 33;
}
```

- [ ] **Step 2: Run targeted smoke and verify failure**

Run:

```bash
cmake --build build --target fuzzpilot_spiral_micro_smoke --parallel 2
ctest --test-dir build -R fuzzpilot_spiral_micro_smoke --output-on-failure
```

Expected: build fails because `select_direct_promotion_for_test` is missing.

- [ ] **Step 3: Implement direct selection helper**

Declare in a suitable header or mark test helper in `run.hpp`:

```cpp
std::optional<AgentDecision> select_direct_promotion_for_test(
    const std::vector<AgentDecision>& decisions);
```

Implementation:

```cpp
std::optional<AgentDecision> select_direct_promotion_for_test(
    const std::vector<AgentDecision>& decisions) {
  for (const auto& decision : decisions) {
    if (decision.model_response.schema_valid &&
        decision.proposal_json.find("\"default_control\"") == std::string::npos &&
        decision.proposal_json.find("\"interventions\"") != std::string::npos) {
      return decision;
    }
  }
  return std::nullopt;
}
```

Use the same helper in `run_mvp` when
`options.direct_promote_without_microcampaign` is true.

- [ ] **Step 4: Add direct promotion events**

When direct promotion succeeds, append:

```json
{"event":"ai_direct_promoted","agent":"...","source_decision_id":"..."}
```

When it cannot select a proposal, append:

```json
{"event":"ai_direct_no_candidate"}
```

- [ ] **Step 5: Verify**

Run:

```bash
cmake --build build --parallel 2
ctest --test-dir build -R fuzzpilot_spiral_micro_smoke --output-on-failure
```

Expected: build passes and targeted smoke passes.

## Task 4: Add Deterministic Bandit Micro Scheduler

**Files:**
- Modify: `include/fuzzpilot/micro/manager.hpp`
- Modify: `src/micro/manager.cpp`
- Test: `tools/spiral_micro_smoke.cpp`

- [ ] **Step 1: Write failing scheduler test**

Add to `tools/spiral_micro_smoke.cpp`:

```cpp
std::vector<fuzzpilot::MicroBanditCandidate> candidates = {
    {.campaign_id = "a", .mean_reward = 1.0, .budget_sec = 10},
    {.campaign_id = "b", .mean_reward = 2.0, .budget_sec = 10},
    {.campaign_id = "c", .mean_reward = 0.5, .budget_sec = 1},
};
auto order = fuzzpilot::rank_micro_bandit_candidates(candidates, 21.0, 1.0);
if (order.empty() || order.front().campaign_id != "c") {
  std::cerr << "bandit scheduler did not prioritize uncertain candidate\n";
  return 34;
}
```

- [ ] **Step 2: Verify failure**

Run:

```bash
cmake --build build --target fuzzpilot_spiral_micro_smoke --parallel 2
```

Expected: build fails because scheduler types are missing.

- [ ] **Step 3: Add scheduler types**

In `include/fuzzpilot/micro/manager.hpp`:

```cpp
struct MicroBanditCandidate {
  std::string campaign_id;
  double mean_reward = 0.0;
  uint32_t budget_sec = 0;
};

struct MicroBanditRank {
  std::string campaign_id;
  double score = 0.0;
};

std::vector<MicroBanditRank> rank_micro_bandit_candidates(
    const std::vector<MicroBanditCandidate>& candidates,
    double total_budget_sec,
    double exploration_c);
```

- [ ] **Step 4: Implement deterministic ranking**

In `src/micro/manager.cpp`:

```cpp
std::vector<MicroBanditRank> rank_micro_bandit_candidates(
    const std::vector<MicroBanditCandidate>& candidates,
    double total_budget_sec,
    double exploration_c) {
  std::vector<MicroBanditRank> ranks;
  for (const auto& c : candidates) {
    const double denom = static_cast<double>(c.budget_sec) + 1.0;
    const double explore = exploration_c *
        std::sqrt(std::log(std::max(1.0, total_budget_sec) + 1.0) / denom);
    ranks.push_back({c.campaign_id, c.mean_reward + explore});
  }
  std::sort(ranks.begin(), ranks.end(), [](const auto& a, const auto& b) {
    if (a.score != b.score) return a.score > b.score;
    return a.campaign_id < b.campaign_id;
  });
  return ranks;
}
```

- [ ] **Step 5: Verify scheduler smoke**

Run:

```bash
cmake --build build --target fuzzpilot_spiral_micro_smoke --parallel 2
ctest --test-dir build -R fuzzpilot_spiral_micro_smoke --output-on-failure
```

Expected: smoke passes.

## Task 5: Add Architecture Pilot Manifest and Runner

**Files:**
- Create: `experiments/manifests/paper_architecture_pilot.yaml`
- Create: `scripts/paper01/runners/run_architecture_pilot.sh`
- Create: `docs/papers/architecture_pilot_runbook.md`

- [ ] **Step 1: Create manifest**

Create `experiments/manifests/paper_architecture_pilot.yaml` with:

```yaml
paper: "fuzzpilot_architecture_pilot"
version: 1
defaults:
  out_root: "results/fuzzpilot_architecture_pilot/runs"
  metadata_script: "scripts/capture_run_metadata.sh"
  stage_a_budget_sec: 1200
  stage_b_budget_sec: 7200
  stage_c_budget_sec: 14400
  llm_parallel: 1
  non_llm_parallel: 3
targets:
  - id: libxml2
    name: libxml2_parser
    config: "experiments/targets/libxml2/config_glm.yaml"
modes:
  - baseline-afl
  - rule-only
  - full-agent
  - ai-direct
  - no-static-analysis
  - no-semantic-context
  - single-agent-coordinator
  - no-mutator
stages:
  A:
    repeats: 1
    budget_sec: 1200
  B:
    repeats: 3
    budget_sec: 7200
  C:
    repeats: 5
    budget_sec: 14400
acceptance:
  min_schema_valid_rate: 0.8
  min_full_agent_throughput_ratio: 0.70
  forbidden_secret_patterns:
    - "[0-9a-fA-F]{32}\\.[A-Za-z0-9_-]{12,}"
    - "Bearer[[:space:]]+[A-Za-z0-9._-]{12,}"
```

- [ ] **Step 2: Create runner skeleton**

Create `scripts/paper01/runners/run_architecture_pilot.sh`:

```bash
#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
MANIFEST="${REPO_ROOT}/experiments/manifests/paper_architecture_pilot.yaml"
FUZZPILOT_BIN="${FUZZPILOT_BIN:-${REPO_ROOT}/build/fuzzpilot}"
STAGE="${1:-A}"

cd "${REPO_ROOT}"
python3 - <<'PY' "${MANIFEST}" "${STAGE}" "${FUZZPILOT_BIN}"
import os, sys, yaml, subprocess, itertools, pathlib, time, json

manifest_path, stage_name, fuzzpilot_bin = sys.argv[1:4]
manifest = yaml.safe_load(open(manifest_path))
stage = manifest["stages"][stage_name]
out_root = pathlib.Path(manifest["defaults"]["out_root"])
out_root.mkdir(parents=True, exist_ok=True)

llm_modes = {"full-agent", "ai-direct", "no-static-analysis",
             "no-semantic-context", "single-agent-coordinator", "no-mutator"}
cpu_count = os.cpu_count() or 1
slots = list(range(max(1, cpu_count)))

def mode_order(repeat):
    modes = list(manifest["modes"])
    shift = repeat % len(modes)
    return modes[shift:] + modes[:shift]

for repeat in range(1, int(stage["repeats"]) + 1):
    for target in manifest["targets"]:
        for mode in mode_order(repeat):
            run_id = f"arch_{stage_name.lower()}_{target['id']}_{mode}_r{repeat:02d}"
            run_dir = out_root / run_id
            run_dir.mkdir(parents=True, exist_ok=True)
            if mode in llm_modes and not os.environ.get("FUZZPILOT_MODEL_API_KEY"):
                (run_dir / "status").write_text("skipped-missing-api-key\n")
                continue
            cpu = slots[(repeat + len(mode) + len(target["id"])) % len(slots)]
            cmd = [
                "taskset", "-c", str(cpu),
                fuzzpilot_bin, "run", "--real-run",
                "--config", target["config"],
                "--ablation", mode,
                "--main-budget-sec", str(stage["budget_sec"]),
                "--work-dir", str(run_dir / "work"),
            ]
            metadata = {
                "run_id": run_id,
                "stage": stage_name,
                "target": target["id"],
                "mode": mode,
                "repeat": repeat,
                "cpu": cpu,
                "cmd": cmd,
                "started_at": int(time.time()),
            }
            (run_dir / "runner_metadata.json").write_text(json.dumps(metadata, indent=2) + "\n")
            (run_dir / "status").write_text("running\n")
            with open(run_dir / "stdout.log", "w") as out, open(run_dir / "stderr.log", "w") as err:
                rc = subprocess.call(cmd, stdout=out, stderr=err)
            (run_dir / "exit_code").write_text(str(rc) + "\n")
            (run_dir / "status").write_text("completed\n" if rc == 0 else "failed\n")
            if mode in llm_modes:
                time.sleep(20)
PY
```

- [ ] **Step 3: Make runner executable**

Run:

```bash
chmod +x scripts/paper01/runners/run_architecture_pilot.sh
```

- [ ] **Step 4: Add runbook**

Create `docs/papers/architecture_pilot_runbook.md` with exact commands:

```markdown
# Architecture Pilot Runbook

## Stage A

```bash
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
FUZZPILOT_MODEL_API_KEY="$KEY" scripts/paper01/runners/run_architecture_pilot.sh A
```

## Rules

- Do not run another fuzzing batch concurrently.
- Keep generated outputs under `results/fuzzpilot_architecture_pilot/`.
- Rotate any API key that appears in logs.
```

- [ ] **Step 5: Verify dry behavior**

Run:

```bash
bash -n scripts/paper01/runners/run_architecture_pilot.sh
python3 - <<'PY'
import yaml
yaml.safe_load(open("experiments/manifests/paper_architecture_pilot.yaml"))
print("manifest ok")
PY
```

Expected: both commands pass.

## Task 6: Add Pilot Summary Script

**Files:**
- Create: `scripts/paper01/architecture_pilot_summary.py`

- [ ] **Step 1: Create script**

Create a script that walks `results/fuzzpilot_architecture_pilot/runs/*` and
emits one CSV row per run:

```python
#!/usr/bin/env python3
import csv, json, sys
from pathlib import Path

root = Path(sys.argv[1] if len(sys.argv) > 1 else "results/fuzzpilot_architecture_pilot/runs")
rows = []
for run_dir in sorted(root.glob("*")):
    if not run_dir.is_dir():
        continue
    meta_path = run_dir / "runner_metadata.json"
    if not meta_path.exists():
        continue
    meta = json.loads(meta_path.read_text())
    decisions = run_dir / "agent_decisions.jsonl"
    decision_count = 0
    schema_valid = 0
    if decisions.exists():
        for line in decisions.read_text(errors="replace").splitlines():
            decision_count += 1
            if '"schema_valid":true' in line:
                schema_valid += 1
    rows.append({
        "run_id": meta.get("run_id", run_dir.name),
        "target": meta.get("target", ""),
        "mode": meta.get("mode", ""),
        "repeat": meta.get("repeat", ""),
        "cpu": meta.get("cpu", ""),
        "status": (run_dir / "status").read_text().strip() if (run_dir / "status").exists() else "",
        "exit_code": (run_dir / "exit_code").read_text().strip() if (run_dir / "exit_code").exists() else "",
        "agent_decisions": decision_count,
        "schema_valid_rate": (schema_valid / decision_count) if decision_count else "",
    })

writer = csv.DictWriter(sys.stdout, fieldnames=[
    "run_id", "target", "mode", "repeat", "cpu", "status", "exit_code",
    "agent_decisions", "schema_valid_rate",
])
writer.writeheader()
writer.writerows(rows)
```

- [ ] **Step 2: Verify on empty/missing results**

Run:

```bash
python3 scripts/paper01/architecture_pilot_summary.py /tmp/nonexistent_arch_pilot
```

Expected: prints only the CSV header and exits 0.

## Task 7: Full Verification

**Files:**
- All modified files

- [ ] **Step 1: Run static diff check**

Run:

```bash
git diff --check
```

Expected: no output, exit 0.

- [ ] **Step 2: Build**

Run:

```bash
cmake --build build --parallel 2
```

Expected: build exits 0.

- [ ] **Step 3: Full CTest**

Run:

```bash
ctest --test-dir build --output-on-failure
```

Expected: all tests pass.

- [ ] **Step 4: Secret scan**

Run:

```bash
find include src tools scripts tests experiments docs -type f \
  -exec grep -IlE '[0-9a-fA-F]{32}\.[A-Za-z0-9_-]{12,}|Bearer[[:space:]]+[A-Za-z0-9._-]{12,}|sk-[A-Za-z0-9_-]{12,}|AIza[0-9A-Za-z_-]{20,}' {} +
```

Expected: no actual credential values. Source files that construct
`Authorization: Bearer` from environment variables are acceptable only after
manual inspection confirms the key is not literal.

## Self-Review

- Spec coverage: all required additions from the design are mapped to tasks.
- Placeholder scan: no `TBD` or open-ended "add tests" tasks remain.
- Type consistency: `RunOptions` fields, helper names, and ablation names are
  consistent across tasks.
- Scope: harness generation, full venue matrix, and crash dedup are intentionally
  deferred to the next paper stage.
