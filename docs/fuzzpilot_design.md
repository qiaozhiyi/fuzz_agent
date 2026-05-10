# FuzzPilot 设计总纲

> 主题：Agent 驱动的自诊断灰盒 fuzzing，让 AFL++ 在 coverage plateau 后自动判断可能原因，并由一组接入大模型或本地模型 API 的 C++ agent 动态生成诊断、干预方案、调度策略和每个种子的变异策略，通过短预算干预逃离停滞。
>
> 实验环境优先级：macOS Apple Silicon / arm64 原生环境。

## 1. 项目定位

FuzzPilot 不是一个“LLM 生成 seed”的外挂工具，也不是把模型调用塞进 mutation hot path 的慢速 mutator。它是一个完全采用 C/C++ 实现、面向 AFL++ 的 agentic fuzzing 控制系统。它把 fuzzing 停滞视为可诊断事件：当 AFL++ 的 coverage 增长停滞时，系统收集运行时遥测，一组专门的 model-backed agents 通过大模型 API 或本地模型 API 读取 diagnosis blackboard，分别负责诊断、调度、比较约束、字典、格式结构、mutation policy、corpus 管理、crash 邻近探索和最终协调；controller 再用短预算 micro-campaign 验证这些 agent proposals，最后把有效干预和变异策略提升到主 fuzzing 循环。

系统的核心运行时、agent、telemetry、数据库访问、进程管理、custom mutator、实验执行器和报告生成器都使用 C 或 C++ 实现。Python 不作为生产运行时依赖，也不作为 fuzzing 控制链路的一部分。

大模型或本地模型 API 是 FuzzPilot 的主要策略驱动来源，而不是附加选项。除 custom mutator hot path、AFL++ 进程控制、telemetry 采样、SQLite 写入、schema 校验、reward 计算等对延迟或确定性高度敏感的模块外，所有重点策略模块默认采用接入模型 API 的 agent 实现。v0 必须实现模型 agent runtime、结构化 prompt/request、结构化 JSON 输出、schema 校验、决策记录和 replay；规则逻辑与 bandit 作为 baseline、fallback、guardrail 和消融对照存在。模型可以来自云端 OpenAI-compatible API，也可以来自本地模型服务，例如 Ollama、llama.cpp server、vLLM 或其他兼容 `/v1/chat/completions` 的服务。

核心思想：

```text
baseline AFL++ run
    -> plateau detector
    -> diagnosis blackboard
    -> model-backed agent runtime
    -> diagnosis/scheduler/cmp/dictionary/format/mutator/corpus/crash/coordinator agents call model API
    -> model agents propose interventions and seed mutation strategies
    -> schema validation and action-space guardrails
    -> mutation strategy manager materializes per-seed recipes
    -> AFL++ native custom mutator executes recipes in the hot path
    -> short-budget micro-campaigns
    -> intervention ranking
    -> promote winning intervention and mutation policy
    -> continue main fuzzing
```

## 2. 研究目标与非目标

### 2.1 研究目标

1. 让 AFL++ 在停滞时自动选择下一步策略，而不是固定使用默认 mutation loop。
2. 把 fuzzing 停滞拆成可验证的干预假设，例如比较约束、结构破坏、seed 能量分配不合理、语料冗余、字典不足等。
3. 让模型 API 驱动的 agent 智能控制每个 seed 的变异策略，包括 operator 权重、offset 范围、token 使用、保护区间、splice 候选、结构修复策略和 budget。
4. 把大模型或本地模型 API 纳入所有重点策略 agent：Diagnosis、Coordinator、Scheduler、Cmp、Dictionary、Format、Mutator、Corpus、Crash Triage 和 Report Analysis。
5. 支持至少一种远程大模型 API 和一种本地模型 API 接入形态，优先采用 OpenAI-compatible HTTP 协议，便于在云端模型与本地模型之间切换。
6. 用 AFL++ custom mutator 在 hot path 执行 agent 生成的 mutation recipe，避免远程模型或重型推理阻塞执行速度。
7. 用短预算 micro-campaign 验证多个干预方案，避免模型 agent 的未经验证建议直接污染主 fuzzing。
8. 建立 mutation/seed/campaign/agent-decision 级遥测数据，为后续 RL、bandit、小模型策略训练和模型决策分析做基础。
9. 在 macOS arm64 上先跑出稳定可复现实验，再扩展到 Linux 大规模 benchmark。

### 2.2 非目标

1. v0 不追求把 AFL++ core 完全重写；优先通过 C/C++ controller 和 AFL++ custom mutator 接管策略。
2. v0 不要求模型每次变异都参与决策；每次变异由本地 C/C++ mutator 根据 agent recipe 高速执行，模型在 controller/agent planning、plateau diagnosis、policy update、micro-campaign planning 和 result analysis 阶段参与动态决策。
3. v0 不使用 Python 作为 controller、agent、mutator 或实验 runner。
4. v0 不以二进制-only、QEMU mode、内核 fuzzing 为主要目标。
5. v0 不追求完全准确解释 plateau 的真实原因，只要求干预选择和 seed mutation policy 在经验上有效。
6. v0 不把模型输出当作无需验证的权威结论；所有模型 agent 建议必须经过 schema 校验、action-space 限制和 micro-campaign 实测。
7. v0 把强 agent 化作为工程重点，但论文贡献不只声称“多 agent 数量多”，而强调模型 agent 分工、typed proposal、持久记忆、skill 沉淀与 micro-campaign 验证闭环。

## 3. macOS arm64 实验环境约束

### 3.1 推荐环境

```text
OS: macOS 14+ / 15+
CPU: Apple Silicon arm64
Package manager: Homebrew under /opt/homebrew
Compiler: Homebrew LLVM clang/clang++
Fuzzer: AFL++ native build
Implementation: C17 for low-level mutator ABI, C++20 for controller/agents/runtime
Database: SQLite through C API or sqlite modern C++ wrapper
Config parser: yaml-cpp or TOML11
JSON: nlohmann/json or simdjson
```

### 3.2 推荐依赖

```bash
brew install aflplusplus
brew install llvm
brew install cmake ninja pkg-config
brew install sqlite coreutils
brew install yaml-cpp nlohmann-json fmt spdlog
```

典型环境变量：

```bash
export PATH="/opt/homebrew/opt/llvm/bin:$PATH"
export CC=afl-clang-fast
export CXX=afl-clang-fast++
export AFL_SKIP_CPUFREQ=1
export AFL_NO_UI=1
```

### 3.3 macOS arm64 注意事项

1. 官方大规模 fuzzing benchmark 通常偏 Linux/Docker，macOS arm64 上不适合作为最终横向对比环境。
2. v0 应优先选择源码可编译、输入为文件、无复杂系统调用依赖的 parser target。
3. 尽量使用 AFL++ LLVM instrumentation 和 persistent mode harness，避免依赖 QEMU mode。
4. macOS 上进程调度、温控、后台任务会影响 wall-clock 公平性，核心实验建议以 execs budget 和多轮重复为主。
5. Apple Silicon 性能核/能效核不可像 Linux 那样方便 pin CPU，实验时应减少后台任务，保持电源接入，关闭自动睡眠。
6. Fuzzing 产生大量小文件，APFS 文件系统开销明显；需要时可使用临时目录或 RAM disk，但必须在实验记录中注明。
7. ASan/UBSan 在 macOS 上可用，但 crash 栈、符号化和 sanitizer 行为可能和 Linux 不完全一致。
8. 不要把 macOS arm64 结果直接声称能代表 Linux x86_64 结果；论文主实验后续应补 Linux 复现实验。
9. C++ controller 应避免依赖 Linux-only `/proc`、`epoll`、`inotify`；macOS 上优先使用 POSIX API、`kqueue` 或轮询。
10. AFL++ custom mutator 在 macOS 上应编译为 `.dylib`，同时保留 Linux `.so` 构建路径，确保后续迁移。

## 4. 总体架构

```text
+----------------------+        +----------------------+
|  C++ CLI / daemon    |        |  C++ Config Registry |
+----------+-----------+        +----------+-----------+
           |                               |
           v                               v
+------------------------------------------------------+
|              FuzzPilot C++ Controller                |
| plateau detection / agent orchestration / promotion   |
+----------+------------+--------------+---------------+
           |            |              |               
           v            v              v               
+---------------+ +-------------+ +--------------------+
| AFL++ Runner  | | Telemetry   | | C++ Agent Runtime  |
| main/micro    | | Collector   | | model-backed agents|
+-------+-------+ +------+------+ +---------+----------+
        |                |                  |
        v                v                  v
+---------------+ +-------------+ +--------------------+
| AFL++ workers | | SQLite DB   | | Model API Gateway  |
| target exec   | | JSONL logs  | | OpenAI-compatible  |
+-------+-------+ +------+------+ +---------+----------+
        |                |                  |
        v                v                  v
+---------------+ +-------------+ +--------------------+
| Target binary | | Decision    | | Mutation Strategy  |
| execution     | | replay log  | | Manager / Store    |
+-------+-------+ +------+------+ +---------+----------+
        |                |                  |
        v                v                  v
+------------------------------------------------------+
| AFL++ C/C++ custom mutator: per-seed recipe execution |
+------------------------------------------------------+
        |
        v
+------------------------------------------------------+
|          Micro-campaign validation and reports        |
+------------------------------------------------------+
```

## 5. 核心数据流

### 5.1 主循环

```text
1. controller 启动主 AFL++ campaign
2. telemetry collector 定期读取 fuzzer_stats、plot_data、queue、crashes
3. plateau detector 判断是否进入停滞状态
4. 若未停滞，mutation strategy manager 仍周期性更新 per-seed recipe
5. AFL++ custom mutator 根据 seed id / seed hash / queue metadata 选择 recipe 并高速变异
6. 若停滞，controller 冻结当前 corpus snapshot
7. C++ agent runtime 构建 diagnosis blackboard，并为每个战略 agent 裁剪上下文
8. Diagnosis/Scheduler/Cmp/Dictionary/Format/Mutator/Corpus/Crash/Coordinator 等 model-backed agents 分别调用模型 API，生成各自的候选和理由
9. rule-based logic 和历史 reward/bandit 策略只补充 baseline、fallback、guardrail 和消融候选
10. controller 校验每个模型 agent 输出的 schema、action space、budget 和风险等级
11. Coordinator Agent 合并各 agent proposals，形成 micro-campaign plan
12. micro-campaign manager 为每个干预/策略组合启动短预算 fuzzing
13. evaluator 比较各干预产生的新 edge/path/crash 和 mutation reward
14. winning intervention 与 mutation policy 被提升到主 fuzzing
15. Result Analysis Agent 可读取 reward 摘要，生成下一轮 agent memory 和策略修正建议
16. 系统记录本次 plateau、每个 agent 的 prompt/context hash、候选、recipe、结果和决策原因
```

### 5.2 关键原则

1. 主 campaign 和 micro-campaign 必须共享同一 corpus snapshot，避免不公平。
2. micro-campaign 的 budget 必须明确，可用 wall time、execs、或 execs+time 双约束。
3. 所有 agent 只能提出干预和 mutation strategy，不能直接调用 shell 或修改 AFL++ 进程。
4. 所有干预和 per-seed recipe 必须可序列化、可复现、可回放。
5. custom mutator 在 hot path 不做阻塞 IO，不调用模型 API，不做复杂数据库查询。
6. controller 必须支持 fallback 到 AFL++ 默认策略和默认 AFL++ mutation。
7. agent 控制种子变异策略必须通过受限 action space 表达，避免不可控生成任意变异逻辑。
8. 模型 agent 是 controller/agent planning 的核心执行者，但模型输出必须经过 C++ schema 校验和 micro-campaign 实测，不能直接污染主 campaign。
9. 延迟敏感边界明确固定：custom mutator、AFL++ runner 的 signal/process control、telemetry collector、SQLite writer、schema validator、coverage/reward numeric evaluator 必须是确定性 C/C++ 路径；模型只参与策略生成、解释、候选排序和结果分析。

## 6. 模块设计

## 6.1 CLI 模块

### 职责

提供统一入口，负责读取配置、启动实验、恢复实验、生成报告。

### 命令草案

```bash
fuzzpilot init
fuzzpilot run --config configs/libpng.yaml
fuzzpilot resume --run-id RUN_ID
fuzzpilot report --run-id RUN_ID
fuzzpilot replay-intervention --intervention-id ID
fuzzpilot list-targets
```

### 输入

1. target 配置文件。
2. AFL++ 路径。
3. seed corpus 路径。
4. 输出目录。
5. agent/intervention 配置。

### 输出

1. run id。
2. 实时状态摘要。
3. SQLite 记录。
4. JSONL 事件日志。
5. 报告图表。

## 6.2 配置模块

### 配置文件格式

建议使用 YAML：

```yaml
project: libpng_fuzzpilot_v0
target:
  name: libpng_read_fuzzer
  binary: ./build/libpng_read_fuzzer
  args: ["@@"]         
  input_dir: ./seeds/libpng
  timeout_ms: 1000
  memory_mb: 1024

afl:
  afl_fuzz: /opt/homebrew/bin/afl-fuzz
  afl_showmap: /opt/homebrew/bin/afl-showmap
  base_env:
    AFL_SKIP_CPUFREQ: "1"
    AFL_NO_UI: "1"
  main_budget_sec: 3600
  plateau_window_sec: 600
  plateau_min_new_edges: 0

micro_campaign:
  enabled: true
  budget_sec: 180
  max_parallel: 2
  promote_metric: new_edges

mutation_strategy:
  enabled: true
  recipe_store: ./work/recipes
  refresh_interval_sec: 15
  default_policy: afl_fallback
  max_recipes_per_seed: 4
  recipe_ttl_sec: 900
  custom_mutator_path: ./build/libfuzzpilot_mutator.dylib
  hot_path_io: mmap
  agent_controls:
    operator_weights: true
    offset_ranges: true
    protect_ranges: true
    dictionary_tokens: true
    splice_candidates: true
    length_repair: true

model_api:
  enabled: true
  required_for_dynamic_decision: true
  provider: openai_compatible
  endpoint: http://127.0.0.1:11434/v1/chat/completions
  endpoint_env: FUZZPILOT_MODEL_ENDPOINT
  api_key_env: FUZZPILOT_MODEL_API_KEY
  model: local-fuzzpilot-policy
  timeout_ms: 30000
  max_output_tokens: 2048
  temperature: 0.2
  decision_interval_sec: 120
  max_candidates_per_plateau: 6
  require_schema_validation: true
  record_prompts: true
  replay_cache: ./work/agent_decisions

agent_runtime:
  strategic_mode: model_api_required
  require_model_for_strategic_agents: true
  rule_fallback_enabled: true
  max_parallel_model_calls: 2
  per_agent_timeout_ms: 30000
  agent_memory_store: ./work/agent_memory
  model_agents:
    coordinator: true
    plateau_diagnosis: true
    scheduler: true
    cmp: true
    dictionary: true
    format: true
    mutator: true
    corpus: true
    crash_triage: true
    result_analysis: true

agents:
  scheduler: true
  cmp: true
  dictionary: true
  format: true
  corpus: true
  mutator: true
  mutation_policy: true
  model_policy: true
  model_format: true
```

### 校验规则

1. target binary 必须存在且可执行。
2. seed input_dir 必须存在且非空。
3. 输出目录不能覆盖未完成 run，除非显式 resume。
4. micro-campaign budget 必须小于主 campaign budget。
5. macOS 下默认设置 `AFL_SKIP_CPUFREQ=1` 和 `AFL_NO_UI=1`。
6. `custom_mutator_path` 必须是当前架构可加载的 arm64 `.dylib`。
7. mutation strategy 的 action space 必须在 schema 中声明，agent 不能输出任意 C/C++ 代码。
8. `model_api.enabled=true` 时必须配置 `model` 和 `endpoint` 或 `endpoint_env`。
9. `model_api.required_for_dynamic_decision=true` 时，`fuzzpilot run` 必须能成功完成模型 health check；失败时只能进入明确标记的 degraded/fallback 模式，不能声称启用了模型动态决策。
10. 模型输出必须通过 intervention schema、seed strategy schema、budget 限制和 action allowlist 校验。
11. `agent_runtime.strategic_mode=model_api_required` 时，Coordinator、Diagnosis、Scheduler、Dictionary、Format、Mutator、Corpus 等重点策略 agent 必须使用模型 API；规则实现只能作为 fallback、guardrail 或 ablation。
12. `agent_runtime.max_parallel_model_calls` 必须受限，避免多个 agent 同时请求模型导致 controller 卡死或成本失控。

## 6.3 环境管理模块

### 职责

1. 检测 AFL++ 工具链。
2. 检测 LLVM/clang 路径。
3. 记录操作系统、CPU、内存、Homebrew 包版本。
4. 生成可复现实验环境快照。

### 需要记录的信息

```text
uname -a
sw_vers
sysctl -n machdep.cpu.brand_string
afl-fuzz -V
clang --version
fuzzpilot --version
fuzzpilot-mutator --version
git commit hash
target binary hash
seed corpus hash
```

### macOS 特殊处理

1. 若 `/opt/homebrew/bin/afl-fuzz` 不存在，提示安装 AFL++ 或指定路径。
2. 若 target 是 x86_64 binary，明确标记为 Rosetta/cross-arch，不纳入主实验。
3. 若使用 RAM disk，记录挂载路径和大小。
4. 若 custom mutator 不是 arm64 Mach-O 动态库，拒绝运行。
5. 若 controller、mutator、target 架构不一致，拒绝纳入主实验。

## 6.4 Target 管理模块

### 职责

管理 target 构建产物、运行参数、seed corpus、字典、sanitizer 配置。

### TargetSpec

```json
{
  "name": "libpng_read_fuzzer",
  "binary": "./build/libpng_read_fuzzer",
  "args": ["@@"],
  "input_dir": "./seeds/libpng",
  "dict": "./dicts/png.dict",
  "cmplog_binary": "./build/libpng_read_fuzzer_cmplog",
  "custom_mutator": "./build/libfuzzpilot_mutator.dylib",
  "timeout_ms": 1000,
  "memory_mb": 1024,
  "persistent": false,
  "sanitizers": ["asan", "ubsan"],
  "mutation_profile": {
    "seed_id_mode": "filename",
    "header_protect_bytes": 8,
    "max_input_size": 1048576
  }
}
```

### v0 推荐 target

优先选择：

1. 文件输入。
2. 编译简单。
3. 在 macOS arm64 可原生运行。
4. 有初始 seed。
5. 有稳定 coverage 增长曲线。

候选类型：

```text
PNG/JPEG/GIF parser
JSON/XML/YAML parser
archive parser
ELF/Mach-O parser
SQL parser
regex parser
```

## 6.5 AFL++ Runner 模块

### 职责

封装 AFL++ 进程启动、停止、暂停、状态检测和输出目录管理。

### Runner 类型

1. `MainRunner`：主 fuzzing campaign。
2. `MicroRunner`：短预算干预验证 campaign。
3. `BaselineRunner`：baseline/ablation campaign。

### 主 campaign 命令模板

```bash
AFL_SKIP_CPUFREQ=1 AFL_NO_UI=1 \
AFL_CUSTOM_MUTATOR_LIBRARY=./build/libfuzzpilot_mutator.dylib \
FUZZPILOT_RECIPE_STORE=./work/recipes \
afl-fuzz \
  -i INPUT_DIR \
  -o OUTPUT_DIR \
  -m MEMORY_LIMIT \
  -t TIMEOUT_MS \
  -- TARGET_BINARY @@
```

### micro-campaign 命令模板

```bash
AFL_SKIP_CPUFREQ=1 AFL_NO_UI=1 \
INTERVENTION_ENV=... \
AFL_CUSTOM_MUTATOR_LIBRARY=./build/libfuzzpilot_mutator.dylib \
FUZZPILOT_RECIPE_STORE=MICRO_RECIPE_STORE \
afl-fuzz \
  -i CORPUS_SNAPSHOT \
  -o MICRO_OUTPUT_DIR \
  -m MEMORY_LIMIT \
  -t TIMEOUT_MS \
  -- TARGET_BINARY @@
```

### 进程控制

1. 使用 C++ `posix_spawn` 或 `fork` + `execve` 启动 AFL++。
2. 记录 PID、start time、environment、command line。
3. budget 到期后优雅发送 SIGINT。
4. 超时未退出时发送 SIGTERM，最后才 SIGKILL。
5. 每个 AFL++ 输出目录单独隔离。

### macOS 注意事项

1. 不依赖 Linux `/proc`。
2. 进程资源统计使用 POSIX `getrusage`、macOS `libproc` 或 `ps` 兼容解析。
3. 不假设 CPU affinity 可用。
4. 文件描述符上限可能需要检查 `ulimit -n`。

## 6.6 Telemetry Collector 模块

### 职责

定期采集 AFL++ 运行数据并写入 SQLite/JSONL。

### 数据来源

1. `fuzzer_stats`
2. `plot_data`
3. `queue/`
4. `crashes/`
5. `hangs/`
6. `.state/`
7. 可选：`afl-showmap`
8. custom mutator mutation telemetry
9. mutation recipe store 状态

### 采样周期

```text
主 campaign: 每 10-30 秒
micro-campaign: 每 5-10 秒
```

### 解析字段

典型字段：

```text
execs_done
execs_per_sec
paths_total
paths_favored
paths_found
paths_imported
max_depth
cur_path
pending_favs
pending_total
variable_paths
unique_crashes
unique_hangs
bitmap_cvg
stability
last_path
last_crash
last_hang
recipe_hits
recipe_misses
mutations_by_operator
mutations_by_seed_family
strategy_refresh_count
```

### 输出事件

```json
{
  "event": "telemetry_tick",
  "run_id": "run_001",
  "campaign_id": "main_001",
  "ts": 1710000000,
  "execs_done": 1200000,
  "execs_per_sec": 2300.5,
  "paths_total": 456,
  "unique_crashes": 0,
  "bitmap_cvg": 3.14,
  "recipe_hits": 84512,
  "recipe_misses": 902,
  "top_mutation_ops": {
    "insert_token": 12000,
    "overwrite_range": 41500,
    "splice": 2200
  }
}
```

## 6.7 Coverage Mapper 模块

### 职责

对 corpus snapshot 做 coverage 测量，获得更稳定的 micro-campaign 比较指标。

### 方法

1. 使用 `afl-showmap` 对 corpus 逐个或批量测量。
2. 输出 edge bitmap hash。
3. 计算两个 corpus 的新增 edge。
4. 记录新增 edge 数、新增 tuple 数、覆盖 bitmap 差异。

### v0 简化

如果 `afl-showmap` 在某些 target 上不可用，则先使用 AFL++ `fuzzer_stats` 的 paths/coverage 字段作为近似指标。

### 关键输出

```text
edge_count
new_edges_vs_parent
bitmap_hash
coverage_delta
```

## 6.8 Plateau Detector 模块

### 职责

判断主 campaign 是否进入 plateau，并触发 agent 干预流程。

### v0 判定规则

```text
在 plateau_window_sec 内：
1. paths_total 没有增长，或增长低于阈值
2. unique_crashes 没有增长
3. execs_done 增长正常
4. execs_per_sec 未明显崩溃
```

### 伪代码

```cpp
if (execs_delta > min_execs && new_paths_delta <= max_new_paths) {
  if (seconds_since_last_path >= plateau_window_sec) {
    trigger_plateau();
  }
}
```

### PlateauEvent

```json
{
  "plateau_id": "plateau_0007",
  "run_id": "run_001",
  "campaign_id": "main_001",
  "start_ts": 1710000000,
  "window_sec": 600,
  "execs_delta": 1800000,
  "new_paths_delta": 0,
  "new_crashes_delta": 0,
  "reason": "no_new_paths"
}
```

### 进阶判定

1. 覆盖斜率低于历史分位数。
2. queue 增长但 coverage 不增长，说明 corpus 冗余。
3. exec/sec 下降，说明 target 进入慢路径或 hang 边缘。
4. bitmap 密度过高，提示 coverage signal 饱和。

## 6.9 Diagnosis Blackboard 模块

### 职责

为所有 agent 提供统一状态视图。agent 不直接读取文件系统，而是从 blackboard 获取规范化 telemetry。

### Blackboard 内容

```json
{
  "plateau": {...},
  "main_metrics": {...},
  "queue_summary": {...},
  "crash_summary": {...},
  "coverage_summary": {...},
  "mutation_summary": {...},
  "seed_strategy_summary": {...},
  "recent_interventions": [...],
  "target_profile": {...},
  "available_actions": [...],
  "available_mutation_actions": [
    "set_operator_weights",
    "set_offset_ranges",
    "set_protect_ranges",
    "set_dictionary_tokens",
    "set_splice_candidates",
    "enable_length_repair",
    "set_seed_energy_bias"
  ]
}
```

### 设计原因

1. 防止不同 agent 解析不同版本状态。
2. 方便记录 agent context hash。
3. 方便复现 agent 决策。
4. 作为模型 prompt/request 的唯一规范化上下文来源，防止模型读取未经裁剪的文件系统状态。
5. 方便后续比较 remote LLM、本地模型、RL、bandit 和规则 agent。

## 6.10 Agent Framework 模块

FuzzPilot 的 agent framework 采用 **model-backed agents first** 原则：除少数低延迟、确定性模块外，重点策略 agent 都通过 Model API Gateway 调用远程大模型或本地模型来生成候选策略。C++ agent 本身负责上下文裁剪、prompt 组装、schema 声明、模型调用、输出校验、fallback 和 replay 记录；模型负责策略推理和候选生成。

v0 必须至少实现以下模型驱动 agent：

1. `CoordinatorAgent`：合并多 agent proposals，规划 micro-campaign。
2. `PlateauDiagnosisAgent`：解释 plateau 信号，选择需要激活的 specialist agents。
3. `SchedulerAgent`：生成 seed energy、seed focus 和 corpus sampling 策略。
4. `CmpAgent`：生成比较约束、cmplog、dictionary token 干预。
5. `DictionaryAgent`：生成 token pool、临时 dictionary 和 token mutation recipe。
6. `FormatAgent`：生成 protect/focus ranges、结构保持策略和修复候选。
7. `MutatorAgent`：生成 per-seed operator weights、offset policy 和 budget。
8. `CorpusAgent`：生成 corpus prune/diversity/snapshot 策略。
9. `CrashTriageAgent`：生成 crash-adjacent mutation 和最小化建议。
10. `ResultAnalysisAgent`：总结 micro-campaign reward，更新 agent memory 和下一轮优先级。

规则实现只承担四种职责：

1. 模型 API 不可用时 fallback。
2. 对模型输出做 schema、allowlist、budget、risk guardrail。
3. 提供 rule-only baseline 和 ablation。
4. 提供极低延迟或确定性计算，例如 reward、hash、coverage delta。

### 6.10.0 Hermes-inspired Agent Runtime 原则

FuzzPilot 的 agent runtime 参考 Hermes Agent 的强 agent 化思想，但针对 fuzzing 做安全收敛：模型 agent 可以长期运行、并行协作、积累记忆和技能；但所有可执行动作必须落到受限 action schema、typed proposal 和 C++ validator 上。

借鉴点：

1. **Orchestrator + Workers**：CoordinatorAgent 是 orchestrator，其余 specialist agents 是 workers。Coordinator 不直接替代专家推理，而是分解 plateau 任务、分配上下文、收集 typed result、合并计划。
2. **Isolated Contexts**：每个 agent 只接收与职责相关的 blackboard slice，避免把完整日志、完整 corpus 或其他 agent 的冗长解释直接塞给模型。
3. **Typed Message Passing**：agent 之间不共享可变内存，不互相转述自然语言；它们通过 `AgentTask`、`AgentProposal`、`AgentCritique`、`AgentMemoryPatch` 等结构化对象通信。
4. **Parallel Specialist Execution**：plateau 后可以并行运行 Dictionary/Format/Mutator/Corpus/Cmp agents，再由 Coordinator 聚合；并发数受 `max_parallel_model_calls`、预算和 rate limit 控制。
5. **Resource-aware Scheduling**：agent runtime 记录每个 agent 的 latency、token、cost、validation failure、historical reward，动态决定下一轮是否启用、降级或缩小上下文。
6. **Persistent Memory**：agent memory 写入 SQLite/JSONL，保存目标格式假设、有效 token、失败 intervention、获胜 recipe family、模型输出校验失败原因和 micro-campaign reward。
7. **Skill / Policy Accumulation**：当某类 target 或 seed family 上反复出现有效策略时，ResultAnalysisAgent 将其沉淀为可复用 skill/policy snippet，下次 prompt 以精简形式召回。
8. **Reflection After Action**：micro-campaign 结束后不是只算 reward，还调用 ResultAnalysisAgent 对“为什么成功/失败”做结构化总结，更新 agent trust score 和 prompt memory。

FuzzPilot 不照搬通用 agent 的无限工具调用模型。所有工具调用都由 controller 执行；agent 只产出 proposal、critique、memory patch 和 policy patch。

### 6.10.1 Agent 接口

所有 agent 实现统一接口：

```cpp
struct AgentTask {
  std::string task_id;
  std::string agent_name;
  std::string objective;
  std::string blackboard_slice_json;
  std::string action_schema_json;
  std::string output_schema_json;
  uint32_t budget_sec;
  uint32_t timeout_ms;
};

struct AgentContext {
  Blackboard blackboard;
  std::filesystem::path run_dir;
  std::filesystem::path recipe_store;
  uint64_t now_unix_sec;
  std::string agent_name;
  std::string action_schema_json;
  std::string output_schema_json;
  std::string agent_memory_json;
};

struct AgentProposal {
  std::vector<Intervention> interventions;
  std::vector<SeedMutationStrategy> seed_strategies;
  std::vector<std::string> critiques;
  std::string memory_patch_json;
};

class IAgent {
 public:
  virtual ~IAgent() = default;
  virtual std::string_view name() const = 0;
  virtual std::string_view version() const = 0;
  virtual AgentProposal propose(const AgentContext& ctx) = 0;
};
```

模型驱动 agent 额外通过统一 gateway 调用模型：

```cpp
struct ModelRequest {
  std::string agent_name;
  std::string system_prompt;
  std::string user_context_json;
  std::string output_schema_json;
  uint32_t timeout_ms;
  uint32_t max_output_tokens;
};

struct ModelResponse {
  std::string provider;
  std::string model;
  std::string request_id;
  std::string response_json;
  std::string context_hash;
  std::string response_hash;
  uint64_t latency_ms;
  bool schema_valid;
  std::string error;
};

class IModelGateway {
 public:
  virtual ~IModelGateway() = default;
  virtual ModelResponse complete_json(const ModelRequest& request) = 0;
};
```

### 6.10.2 Intervention 格式

```json
{
  "id": "intv_cmp_0001",
  "agent": "CmpAgent",
  "hypothesis": "plateau may be caused by unsolved comparisons",
  "action": "enable_cmplog",
  "params": {
    "mode": "cmplog",
    "budget_sec": 180
  },
  "expected_signal": "new_edges",
  "risk": "medium",
  "reproducible": true
}
```

### 6.10.3 SeedMutationStrategy 格式

```json
{
  "id": "strategy_png_000123",
  "agent": "FormatAgent",
  "seed_selector": {
    "mode": "seed_id",
    "seed_id": "id:000123"
  },
  "priority": 80,
  "ttl_sec": 900,
  "budget": {
    "max_mutations": 50000,
    "max_execs": 50000
  },
  "operator_weights": {
    "overwrite_range": 0.25,
    "insert_token": 0.30,
    "arith": 0.15,
    "splice": 0.15,
    "delete_block": 0.05,
    "clone_block": 0.10
  },
  "offset_policy": {
    "focus_ranges": [[32, 512]],
    "protect_ranges": [[0, 8], [12, 16]],
    "alignment": 1
  },
  "dictionary_tokens": ["IHDR", "IDAT", "IEND"],
  "splice_policy": {
    "prefer_same_magic": true,
    "max_partner_size_delta": 4096
  },
  "repair_policy": {
    "length_fields": [{"offset": 16, "width": 4, "endian": "be", "covers": [20, 512]}],
    "checksum": "none"
  },
  "expected_signal": "new_edges"
}
```

### 6.10.4 Agent 原则

1. agent 只提出干预和 seed mutation strategy，不直接执行 shell、写 AFL++ 输出目录或修改 target。
2. 干预必须有明确 action 和参数。
3. seed mutation strategy 必须只使用受限 mutation action space。
4. 干预和 strategy 必须声明 expected signal。
5. 干预和 strategy 必须可回放。
6. 模型 agent 的输出必须经过 C++ schema 校验后才能进入 strategy store。
7. agent 运行时间必须受限，超时则丢弃该 agent 本轮 proposal，并记录 degraded/fallback。
8. 战略 agent 默认使用模型 API；只有配置明确为 ablation 或 fallback 时才运行纯规则版本。
9. 每个模型 agent 必须拥有独立的 prompt、输出 schema、risk policy 和 replay log，避免所有策略被一个笼统 prompt 吞掉。

## 6.11 Mutation Strategy Manager 模块

### 职责

把多个 agent 产出的 seed mutation strategy 合并、去重、排序、物化，并提供给 AFL++ custom mutator 高速读取。

### 输入

1. agent proposals 中的 `SeedMutationStrategy`。
2. 当前 queue seed metadata。
3. 最近 mutation telemetry。
4. 最近 micro-campaign reward。
5. 全局 fallback mutation policy。

### 输出

1. per-seed recipe 文件或 mmap 数据区。
2. global recipe index。
3. strategy promotion/expiration 事件。
4. strategy reward 统计。

### Recipe 查找键

```text
seed filename: AFL++ queue 文件名，例如 id:000123,...
seed hash: sha256(seed bytes)
seed family: parent lineage / depth / magic / size bucket
global fallback: 没命中特定 seed 时使用
```

### Hot Path 设计

custom mutator 每次处理 seed 时：

```text
1. 根据 seed filename 或 seed hash 查找 recipe id
2. 若命中特定 recipe，加载已缓存的 operator weights / ranges / tokens
3. 若未命中，使用 seed family recipe
4. 若仍未命中，使用 AFL++ fallback
5. 执行一次或多次快速 mutation
6. 将 operator、offset、recipe id 写入轻量 telemetry buffer
```

### C/C++ 数据结构草案

```cpp
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
  uint32_t begin;
  uint32_t end;
};

struct OperatorWeight {
  MutationOp op;
  float weight;
};

struct SeedRecipe {
  uint64_t recipe_id;
  uint64_t seed_hash_hi;
  uint64_t seed_hash_lo;
  uint32_t priority;
  uint32_t ttl_sec;
  std::vector<OperatorWeight> weights;
  std::vector<ByteRange> focus_ranges;
  std::vector<ByteRange> protect_ranges;
  std::vector<std::string> dictionary_tokens;
  RepairPolicy repair;
};
```

### 合并规则

1. 同一个 seed 多个 recipe 时，按 priority、历史 reward、agent trust score 排序。
2. protect ranges 取并集。
3. focus ranges 取高置信度交集，若为空则取并集。
4. dictionary tokens 去重后按历史命中率截断。
5. operator weights 做归一化，低权重 operator 保留最小探索概率。
6. recipe 过期后自动回退到 family/global policy。

### Reward 归因

```text
recipe reward =
  new_edges_from_seed_family
+ new_paths_from_recipe
+ crash_bonus
- invalid_input_penalty
- slow_exec_penalty
```

v0 中 reward 先按 micro-campaign 级别归因；后续 AFL++ core hook 可做到 mutation-level 精确归因。

## 6.12 Coordinator Agent

### 职责

Coordinator Agent 是模型驱动 agent，而不是纯规则排序器。它通过模型 API 读取各 specialist agent 的 proposals、历史 reward、当前 budget 和 risk policy，生成本轮 micro-campaign plan。规则排序只作为 fallback 和最终 guardrail。

1. 汇总各 agent 的 intervention。
2. 汇总各 agent 的 seed mutation strategy。
3. 去重、过滤不合法干预和不合法 recipe。
4. 控制 micro-campaign 数量，避免爆炸。
5. 根据历史成功率调整 agent 优先级和 strategy priority。

### 输入

1. blackboard。
2. agent proposals。
3. 历史 intervention success rate。
4. 历史 mutation strategy reward。
5. 当前可用 CPU/budget。

### 输出

1. 待验证 intervention 列表。
2. 待验证 mutation strategy 列表。
3. micro-campaign 执行计划。

### v0 策略

```text
每次 plateau 最多选择 4-6 个 intervention
至少包含一个 default/control campaign
模型根据 blackboard、历史 reward 和互补性选择候选
规则 fallback 优先选择过去成功率高且最近未尝试的 intervention
每个 intervention 可以绑定一个或多个 seed mutation strategy
```

## 6.13 Scheduler Agent

### 假设

plateau 可能来自 seed energy 或 queue selection 不合理，导致算力浪费在低收益 seed family 上。

### Agent 驱动方式

Scheduler Agent 默认调用模型 API。C++ 层提供 queue summary、seed family features、历史 reward、可用 sampling action schema；模型输出 seed focus/corpus sampling/budget bias 策略。规则排序只用于 fallback 和 safety cap。

### 干预动作

1. 提高 favored seeds 的能量。
2. 偏向 rare-depth seeds。
3. 偏向最近产生 coverage 的 seed family。
4. 对 corpus 做分层抽样后重启 campaign。
5. 为高价值 seed family 生成更高 mutation budget 的 recipe。
6. 为长期低收益 seed 生成降权或 AFL++ fallback recipe。

### v0 实现

不直接改 AFL++ scheduler，而是由模型 agent 生成新的 input corpus 计划：

```text
queue snapshot
-> 按 depth/mtime/file size/favored 标记排序
-> 采样 top-k 或 diversity-k
-> micro-campaign 使用该 corpus 作为 -i
-> 同时生成 seed_energy_bias 和 per-family mutation budget recipe
```

### 未来深改点

在 AFL++ 内部暴露 seed selection hook 和 energy hook。
在 custom mutator 侧先实现 per-seed mutation budget 控制，后续再进入 AFL++ core。

## 6.14 Cmp Agent

### 假设

plateau 可能来自 magic bytes、memcmp、strcmp、整数比较等约束。

### Agent 驱动方式

Cmp Agent 默认调用模型 API。C++ 层提供 binary strings 摘要、已有 dictionary、cmp/cmplog 可用性、recent queue token 摘要和 action schema；模型决定是否启用 CMPLOG、laf/split-compare 变体、临时 dictionary 或比较字段定向 recipe。

### 干预动作

1. 启用 CMPLOG 版本 target。
2. 启用 laf-intel/split-compare 构建。
3. 从 binary strings 和 cmp telemetry 中抽取 tokens。
4. 生成临时 dictionary。
5. 把比较常量注入 per-seed recipe 的 dictionary tokens。
6. 对疑似比较字段生成 overwrite/token-insert 高权重策略。

### v0 实现

1. 如果存在 cmplog build，模型 agent 可提出 CMPLOG micro-campaign。
2. 如果没有 cmplog build，模型 agent 基于 strings/token extraction 摘要生成 dict 候选。
3. micro-campaign 使用 `-x generated.dict`。
4. mutation strategy manager 为相关 seed 生成 `insert_token` / `dictionary_overwrite` 偏置 recipe。

### 输出信号

```text
new_edges
new_paths
comparison-adjacent coverage growth
```

## 6.15 Dictionary Agent

### 假设

plateau 可能来自缺少关键 token、magic bytes、关键字、分隔符或协议字段名。

### Agent 驱动方式

Dictionary Agent 默认调用模型 API。C++ 层只做 token extraction、去重、长度限制和字符安全过滤；模型负责根据 target profile、seed 摘要、binary strings、recent successes 和失败历史选择 token families、token 权重和插入/覆盖策略。

### 输入来源

1. 初始 seeds。
2. target binary strings。
3. successful queue entries。
4. crash inputs。
5. 模型决策层提供的格式/token 假设。

### 干预动作

1. 生成临时 AFL++ dictionary。
2. 合并已有 dictionary。
3. 过滤过长、过短、重复 token。
4. 为 micro-campaign 添加 `-x dict`.
5. 为 custom mutator 生成 per-seed token pool，控制 token 插入/覆盖概率。

### v0 fallback token 规则

```text
ASCII printable strings
magic header bytes
common delimiters
file format keywords
recent successful mutation substrings
```

## 6.16 Format Agent

### 假设

plateau 可能来自输入结构容易被 mutation 破坏，例如长度字段、chunk 边界、嵌套结构、checksum。

### Agent 驱动方式

Format Agent 默认调用模型 API。C++ 层提供 seed byte摘要、magic/header、公共前缀、可打印 token、长度字段候选和格式 action schema；模型输出 protect/focus ranges、结构保持 mutation profile、修复候选和需要验证的格式假设。

### v0 职责

1. 通过模型 agent 对 seed 做轻量结构推断。
2. 标记应保护区域和可变区域。
3. 生成 structure-preserving mutation recipe。
4. 记录模型格式假设，供 micro-campaign 验证和报告。

### 输入

1. seed 样本。
2. 文件 magic。
3. 可打印 token。
4. 长度字段候选。

### 输出

```json
{
  "id": "strategy_format_0001",
  "agent": "FormatAgent",
  "seed_selector": {"mode": "seed_family", "magic": "89504e470d0a1a0a"},
  "operator_weights": {
    "overwrite_range": 0.25,
    "insert_token": 0.25,
    "splice": 0.20,
    "delete_block": 0.05,
    "clone_block": 0.25
  },
  "offset_policy": {
    "protect_ranges": [[0, 8], [12, 16]],
    "focus_ranges": [[32, 256]]
  },
  "repair_policy": {
    "length_fields": [{"offset": 16, "width": 4, "endian": "be", "covers": [20, 256]}]
  }
}
```

### v0 fallback 简化

先只支持通用启发式：

1. 保护文件头前 N 字节。
2. 保护高频公共前缀。
3. 对 body 做集中变异。
4. 对长度字段只记录候选，不强行修复。

### 后续增强

1. 针对 PNG/JPEG/ELF/JSON/XML 做格式插件。
2. 大模型或本地模型生成格式约束候选，再由 micro-campaign 验证。
3. 自定义 mutator 根据 recipe 执行结构保持变异。
4. 把字段级 reward 回传给 Format Agent，逐步修正 protect/focus ranges。

## 6.17 Mutator Agent

### 假设

plateau 可能来自 mutation operator 分布不适合当前阶段。

### Agent 驱动方式

Mutator Agent 是模型驱动策略核心。C++ 层提供每个 seed family 的 size/depth/reward/token/coverage 摘要、可用 mutation action schema 和历史 recipe reward；模型输出 per-seed/per-family `SeedMutationStrategy`。custom mutator 只执行已经物化的 recipe，不调用模型。

### 干预动作

1. 为指定 seed 或 seed family 生成 operator weight distribution。
2. 为指定 seed 生成 focus offset ranges 和 protect ranges。
3. 选择 token insertion、dictionary overwrite、arith、splice、block mutation 的比例。
4. 根据历史收益调整 mutation budget。
5. 选择 AFL++ fallback、custom recipe、hybrid recipe 三种执行模式。
6. 为 micro-campaign 生成多组竞争 mutation recipes。

### v0 实现

通过模型 agent 生成 recipe，再由 C/C++ custom mutator 直接执行 recipe。Mutator Agent 不依赖 Python，不直接修改 AFL++ 源码；它只写入 strategy store，由 custom mutator 在 AFL++ 进程内加载。

v0 的核心是让 agent 智能控制种子变异策略：

```text
seed metadata + telemetry + history reward
-> Mutator Agent 调用模型 API 生成 SeedMutationStrategy
-> Mutation Strategy Manager 合并/排序/物化 recipe
-> AFL++ custom mutator 按 recipe 变异该 seed
-> mutation telemetry 回流
-> micro-campaign 验证 recipe 有效性
```

### Mutation Strategy 类型

```text
explore_unknown_offsets:
  高随机性，覆盖全文件，适合新 seed

protect_header_mutate_body:
  保护 magic/header/common prefix，集中改 body

cmp_token_injection:
  提高 insert_token / dictionary_overwrite 权重

splice_same_family:
  在同 magic/同 size bucket seed 之间 splice

rare_edge_exploit:
  对最近触发 rare edge 的 seed 增加 budget

crash_adjacent_mutation:
  对 crash 邻近 seed 做小步 mutation
```

### 输出 Recipe 示例

```json
{
  "id": "strategy_mutator_0009",
  "agent": "MutatorAgent",
  "seed_selector": {"mode": "seed_id", "seed_id": "id:000321"},
  "priority": 90,
  "operator_weights": {
    "overwrite_range": 0.20,
    "insert_token": 0.40,
    "arith": 0.10,
    "splice": 0.20,
    "delete_block": 0.05,
    "clone_block": 0.05
  },
  "offset_policy": {
    "focus_ranges": [[24, 128], [256, 512]],
    "protect_ranges": [[0, 8]]
  },
  "dictionary_tokens": ["IHDR", "IDAT", "IEND"],
  "budget": {"max_mutations": 100000}
}
```

### 未来深改点

1. AFL++ havoc operator 权重 hook。
2. mutation offset selection hook。
3. per-seed mutation recipe hook。
4. mutation-level telemetry hook。

## 6.18 Corpus Agent

### 假设

plateau 可能来自 corpus 膨胀、重复 seed 太多、queue 中低质量输入消耗过多 energy。

### Agent 驱动方式

Corpus Agent 默认调用模型 API。C++ 层提供 corpus summary、seed family clustering、coverage retention estimate、exec/sec 影响和安全 action schema；模型输出 prune/diversity/snapshot 策略。实际文件复制、删除候选隔离和 snapshot 生成仍由确定性 C++ 执行。

### 干预动作

1. corpus minimization。
2. diversity sampling。
3. seed family pruning。
4. size-tiered corpus split。
5. crash/hang 隔离。
6. 为保留下来的 seed family 生成不同 mutation policy。

### v0 实现

1. 模型 agent 根据 queue 文件名、mtime、size、depth 和 reward 摘要做近似选择。
2. 可选调用 `afl-cmin`，但 macOS 上先视目标兼容性谨慎使用。
3. micro-campaign 使用新 corpus snapshot。
4. 对被选中的 seed family 提高 recipe priority，对被剪枝 family 降低 recipe priority。

### 输出指标

```text
corpus_size_before
corpus_size_after
coverage_retention
execs_per_sec_delta
new_edges_delta
recipe_priority_delta
```

## 6.19 Crash Triage Agent

### 职责

Crash Triage Agent 默认采用模型 API 做策略分析，但 crash 去重、hash、信号记录和 sanitizer 原始事实提取必须由确定性 C++ 完成。模型只能基于摘要提出 crash-focused campaign、crash-adjacent mutation 和最小化建议，不能把 exploitability 判断当作结论。

1. 监控 crashes/hangs。
2. 去重 crash。
3. 记录 sanitizer 输出。
4. 调用模型判断是否需要 crash-focused campaign。
5. 生成 crash-adjacent mutation strategy。

### v0 实现

1. 按 crash 文件 hash 去重。
2. 按 stderr 关键栈信息近似聚类。
3. 记录 target return code、signal、sanitizer type。
4. 模型 agent 读取 crash summary，输出 targeted mutation 和 replay/minimization 候选。

### 后续增强

1. 使用 `llvm-symbolizer`。
2. 生成最小复现命令描述和 C++ replay helper 输入。
3. 对 crash-adjacent seed 做 targeted mutation。
4. 为 crash-adjacent seed 生成 small-step mutation recipe，用于寻找同类新 crash 或最小触发条件。

## 6.20 Model API Gateway 模块

### 职责

封装大模型和本地模型调用，是所有 model-backed agents 的统一底座，而不是单独替代 agent runtime 的“大脑”。每个战略 agent 负责定义自己的 system prompt、上下文裁剪、action schema、输出 schema 和 risk policy；Model API Gateway 负责执行 OpenAI-compatible HTTP 调用、超时控制、重试、速率限制、response hash、replay cache 和错误归一化。

该模块必须用 C++ 实现。推荐优先支持 OpenAI-compatible HTTP API，使同一套代码可接入云端模型、本地 Ollama/vLLM/llama.cpp server 或企业内网模型服务。模型不进入 AFL++ hot path，不在 custom mutator 中调用，不直接执行 shell，不直接修改 AFL++ 输出目录。

v0 的默认开发目标是：

1. 支持 `openai_compatible` provider。
2. 支持本地模型 endpoint，例如 `http://127.0.0.1:11434/v1/chat/completions`。
3. 支持远程模型 endpoint，例如 `https://api.openai.com/v1/chat/completions`。
4. 支持 API key 从环境变量读取，禁止把密钥写入实验记录。
5. 支持 request/response hash、schema validation result 和 replay cache。
6. 支持模型超时、失败、输出无效时通知对应 agent 进入 rule-based fallback，并在 run 状态中标记 degraded。
7. 支持 per-agent rate limit 和 `max_parallel_model_calls`，防止 agent storm。

### 输入

1. blackboard 摘要。
2. target profile。
3. 最近 plateau 历史。
4. 可用 action schema。
5. mutation action allowlist。
6. 最近 micro-campaign reward。
7. 历史 intervention 胜率和 agent trust score。
8. 当前 budget、并行度和风险限制。

### 输出

模型输出必须是结构化 JSON，不接受自由文本直接执行。不同 agent 有不同 schema；例如 Mutator Agent 可以输出：

```json
{
  "agent_decision_id": "mdec_0007",
  "agent": "MutatorAgent",
  "diagnosis": {
    "summary": "plateau likely caused by missing format tokens and weak seed scheduling",
    "confidence": 0.62,
    "supporting_signals": ["no_new_paths", "execs_healthy", "low_dictionary_hits"]
  },
  "interventions": [
    {
      "id": "intv_dictionary_0007",
      "action": "dictionary_probe",
      "hypothesis": "parser may be blocked on missing magic tokens",
      "params": {"budget_sec": 180},
      "expected_signal": "new_edges",
      "risk": "low"
    }
  ],
  "seed_strategies": [
    {
      "id": "strategy_model_0007",
      "seed_selector": {"mode": "family", "family": "small_png_like"},
      "priority": 80,
      "ttl_sec": 900,
      "operator_weights": {
        "insert_token": 0.35,
        "overwrite_range": 0.25,
        "arith": 0.15,
        "splice": 0.10,
        "delete_block": 0.05,
        "clone_block": 0.10
      },
      "offset_policy": {
        "focus_ranges": [[8, 4096]],
        "protect_ranges": [[0, 8]]
      },
      "dictionary_tokens": ["IHDR", "IDAT", "IEND"],
      "expected_signal": "new_edges"
    }
  ],
  "fallback_plan": "run default_control and dictionary_probe if schema validation fails"
}
```

所有 agent 输出必须经过 C++ schema 校验、action space 检查、budget 限制、risk policy 和 replay 记录后才能进入 strategy store 或 micro-campaign manager。

### Request / Prompt 原则

1. 只让模型选择、组合或参数化已有 intervention 和 mutation action。
2. 不让模型直接生成 shell 命令。
3. 不让模型直接修改主 campaign。
4. 不让模型生成任意 C/C++ 代码或 arbitrary mutator logic。
5. prompt 中必须包含 action schema、输出 JSON schema、budget 上限和禁止事项。
6. prompt 中的 telemetry 必须来自 blackboard 摘要，不直接塞入未裁剪的大型日志。
7. 所有输出走 JSON schema 校验，失败时记录原因并进入 fallback。
8. 模型可以给出 rationale，但 rationale 只用于审计和报告，不作为执行指令。

### 动态决策时机

1. plateau 触发时必须调度一组 model-backed agents，除非配置明确处于 replay 或 degraded 模式。
2. 非 plateau 期间可以按 `decision_interval_sec` 低频调度 Mutator/Dictionary/Scheduler agents，用于更新全局 mutation policy、dictionary hints 和 seed family policy。
3. micro-campaign 结束后必须调度 ResultAnalysisAgent，总结结果并更新 agent memory；最终 winner 仍由 evaluator reward 决定。
4. crash 出现后可以调度 CrashTriageAgent 做 crash-adjacent seed strategy 建议，但 crash triage 结论必须保守记录。

### 记录

```text
provider
endpoint hash
model name
request id
agent name
prompt/context hash
response hash
latency_ms
timeout/error status
schema validation result
selected intervention ids
selected strategy ids
fallback/degraded flag
replay cache path
```

## 6.21 Micro-Campaign Manager 模块

### 职责

为每个 intervention 或 intervention+mutation strategy 组合创建短预算 AFL++ run，并在结束后收集结果。

### 执行流程

```text
1. freeze corpus snapshot
2. build intervention-specific input dir/env/dict/recipe_store/mutator
3. start micro AFL++ process
4. monitor until budget exhausted
5. collect telemetry
6. collect mutation telemetry
7. compute intervention reward and strategy reward
8. clean or archive output
```

### 并行策略

macOS arm64 上建议 v0 默认顺序执行或低并行度：

```text
max_parallel = 1 or 2
```

原因：

1. 避免温控和性能核调度导致不公平。
2. 减少 IO 干扰。
3. 便于复现。

### 输出

```json
{
  "micro_id": "micro_001",
  "intervention_id": "intv_cmp_0001",
  "strategy_ids": ["strategy_mutator_0009"],
  "budget_sec": 180,
  "execs_done": 320000,
  "new_paths": 4,
  "new_edges": 12,
  "unique_crashes": 0,
  "recipe_hits": 280000,
  "recipe_misses": 1200,
  "reward": 12.4
}
```

## 6.22 Evaluator / Ranker 模块

### 职责

比较 micro-campaign 结果，选择 winning intervention 和 winning mutation strategy。

### v0 reward

```text
reward =
  1.0 * new_edges
+ 3.0 * new_paths
+ 10.0 * unique_crashes
- 0.1 * duplicate_inputs
- invalid_input_penalty
- recipe_miss_penalty
- overhead_penalty
```

### 更公平的 reward

按 execs 归一化：

```text
reward_per_million_execs =
  reward / max(1, execs_done / 1_000_000)
```

### Winner 规则

1. 必须优于 default micro-campaign。
2. 如果所有干预都不优于 default，则不提升任何干预。
3. 如果 crash 出现，Crash Triage Agent 可覆盖普通排名。
4. winning intervention 可以和 winning strategy 分开提升，例如保留 default campaign 配置但提升某个 per-seed recipe。

## 6.23 Promotion 模块

### 职责

把 winning intervention 和 winning mutation strategy 应用回主 campaign。

### v0 实现方式

1. 停止当前主 campaign。
2. 将 winning micro-campaign 的 queue 合并回主 corpus。
3. 将 winning strategy 写入主 campaign 的 recipe store。
4. 使用 winning intervention 配置重启主 campaign。
5. 记录 promotion event。

### 后续深改

1. 不重启 AFL++，通过 control socket 动态调整策略。
2. AFL++ 内部暴露 scheduler/mutator knobs。
3. 支持 per-seed policy 更新。
4. custom mutator 通过 mmap 或 lock-free ring buffer 热更新 recipe。

## 6.24 Storage 模块

### 数据库

使用 SQLite。所有事件同时写 JSONL，便于调试和长期归档。

### 表设计

#### runs

```sql
CREATE TABLE runs (
  id TEXT PRIMARY KEY,
  project TEXT,
  target_name TEXT,
  start_ts INTEGER,
  end_ts INTEGER,
  status TEXT,
  os TEXT,
  arch TEXT,
  afl_version TEXT,
  target_hash TEXT,
  seed_hash TEXT
);
```

#### campaigns

```sql
CREATE TABLE campaigns (
  id TEXT PRIMARY KEY,
  run_id TEXT,
  type TEXT,
  parent_campaign_id TEXT,
  intervention_id TEXT,
  output_dir TEXT,
  start_ts INTEGER,
  end_ts INTEGER,
  budget_sec INTEGER,
  status TEXT
);
```

#### telemetry

```sql
CREATE TABLE telemetry (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  campaign_id TEXT,
  ts INTEGER,
  execs_done INTEGER,
  execs_per_sec REAL,
  paths_total INTEGER,
  unique_crashes INTEGER,
  unique_hangs INTEGER,
  bitmap_cvg REAL,
  recipe_hits INTEGER,
  recipe_misses INTEGER,
  raw_json TEXT
);
```

#### plateaus

```sql
CREATE TABLE plateaus (
  id TEXT PRIMARY KEY,
  run_id TEXT,
  campaign_id TEXT,
  detected_ts INTEGER,
  window_sec INTEGER,
  execs_delta INTEGER,
  new_paths_delta INTEGER,
  reason TEXT,
  blackboard_json TEXT
);
```

#### interventions

```sql
CREATE TABLE interventions (
  id TEXT PRIMARY KEY,
  plateau_id TEXT,
  agent TEXT,
  action TEXT,
  params_json TEXT,
  hypothesis TEXT,
  expected_signal TEXT,
  status TEXT
);
```

#### seed_strategies

```sql
CREATE TABLE seed_strategies (
  id TEXT PRIMARY KEY,
  plateau_id TEXT,
  agent TEXT,
  selector_json TEXT,
  operator_weights_json TEXT,
  offset_policy_json TEXT,
  dictionary_tokens_json TEXT,
  repair_policy_json TEXT,
  priority INTEGER,
  ttl_sec INTEGER,
  status TEXT,
  created_ts INTEGER
);
```

#### mutation_events

```sql
CREATE TABLE mutation_events (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  campaign_id TEXT,
  strategy_id TEXT,
  seed_id TEXT,
  recipe_id TEXT,
  operator TEXT,
  offset INTEGER,
  size_before INTEGER,
  size_after INTEGER,
  ts INTEGER
);
```

#### micro_results

```sql
CREATE TABLE micro_results (
  id TEXT PRIMARY KEY,
  intervention_id TEXT,
  strategy_ids_json TEXT,
  campaign_id TEXT,
  execs_done INTEGER,
  new_paths INTEGER,
  new_edges INTEGER,
  unique_crashes INTEGER,
  recipe_hits INTEGER,
  recipe_misses INTEGER,
  reward REAL,
  promoted INTEGER
);
```

#### agent_decisions

```sql
CREATE TABLE agent_decisions (
  id TEXT PRIMARY KEY,
  run_id TEXT,
  plateau_id TEXT,
  agent TEXT,
  model_provider TEXT,
  model_name TEXT,
  task_json TEXT,
  context_hash TEXT,
  response_hash TEXT,
  latency_ms INTEGER,
  schema_valid INTEGER,
  fallback_used INTEGER,
  error TEXT,
  proposal_json TEXT,
  created_ts INTEGER
);
```

#### agent_memory

```sql
CREATE TABLE agent_memory (
  id TEXT PRIMARY KEY,
  run_id TEXT,
  target_name TEXT,
  agent TEXT,
  memory_type TEXT,
  key TEXT,
  value_json TEXT,
  evidence_json TEXT,
  reward REAL,
  confidence REAL,
  updated_ts INTEGER
);
```

#### agent_skills

```sql
CREATE TABLE agent_skills (
  id TEXT PRIMARY KEY,
  agent TEXT,
  target_family TEXT,
  skill_name TEXT,
  trigger_json TEXT,
  policy_json TEXT,
  source_decision_ids_json TEXT,
  success_count INTEGER,
  failure_count INTEGER,
  updated_ts INTEGER
);
```

#### crashes

```sql
CREATE TABLE crashes (
  id TEXT PRIMARY KEY,
  run_id TEXT,
  campaign_id TEXT,
  path TEXT,
  sha256 TEXT,
  signal TEXT,
  sanitizer TEXT,
  stack_hash TEXT,
  first_seen_ts INTEGER
);
```

## 6.25 Reporting 模块

### 输出报告

1. coverage over time。
2. plateau timeline。
3. intervention win rate。
4. mutation strategy win rate。
5. operator reward distribution。
6. baseline 对比。
7. crash summary。
8. overhead summary。
9. agent decision trace。
10. agent memory/skill evolution。

### 图表

```text
coverage_vs_time.svg
coverage_vs_execs.svg
plateau_escape_rate.svg
intervention_win_matrix.svg
mutation_strategy_win_matrix.svg
operator_reward_distribution.svg
agent_decision_timeline.svg
agent_latency_cost.csv
agent_memory_changes.jsonl
ablation_summary.csv
```

图表由 C++ report module 直接生成 SVG/CSV，避免引入 Python plotting 依赖。

### 论文表格

```text
Table 1: target information
Table 2: baseline comparison
Table 3: ablation study
Table 4: intervention success rate
Table 5: seed mutation strategy success rate
Table 6: overhead
Table 7: model-agent ablation and replay stability
```

## 7. Intervention 目录设计

每个 intervention 是一个可序列化对象，包含：

```text
name
agent
hypothesis
preparation steps
afl env changes
input corpus transformation
dictionary changes
custom mutator recipe changes
seed mutation strategy changes
expected metric
rollback logic
```

### v0 interventions

#### default_control

不改变任何 AFL++ 配置，custom mutator 使用 `afl_fallback` policy，作为 micro-campaign 对照组。

#### cmplog_probe

使用 CMPLOG build 或 comparison-friendly config 测试是否能突破比较约束。

#### dictionary_probe

生成临时 dictionary 并运行短预算 campaign。
同时生成 token-heavy seed recipes，测试 agent 控制 token insertion/overwrite 是否有效。

#### corpus_prune_probe

对 queue snapshot 做精简，检查是否提升 exec/sec 和 coverage 增长。

#### seed_focus_probe

选择高 depth、新近成功或 favored seed 子集作为 input corpus。
同时提高这些 seed 的 mutation budget，降低低收益 seed family 的 recipe priority。

#### structure_preserve_probe

使用 custom mutator 或预处理策略保护 header/common prefix。
核心验证点是 protect_ranges 是否减少无效输入并提升 coverage。

#### havoc_boost_probe

偏向更激进的 havoc/splice 配置。v0 通过 C/C++ custom mutator 模拟 AFL++ 内部 operator 权重控制。

#### per_seed_recipe_probe

为高价值 seed、rare-depth seed、recently-interesting seed 分别生成不同 recipe，验证 per-seed mutation strategy 是否优于 global mutation policy。

#### cmp_token_recipe_probe

把比较常量和 binary strings 注入指定 seed 的 token pool，显著提高 `insert_token` 和 `dictionary_overwrite` 权重。

## 8. Custom Mutator 设计

### 目标

实现 structure-preserving 和 recipe-based mutation，为 Format Agent、Dictionary Agent、Cmp Agent 和 Mutator Agent 提供执行后端。它是 agent 智能控制种子变异策略的 hot-path 执行器。

### v0 形态

只使用 C/C++ 实现 AFL++ custom mutator。macOS arm64 输出 `.dylib`，Linux 输出 `.so`。不提供 Python mutator 路径，避免实验链路出现额外解释器依赖。

推荐实现：

```text
C ABI: afl_custom_init / afl_custom_fuzz / afl_custom_deinit
C++20 core: recipe cache, weighted operator sampler, range selector, telemetry buffer
Build: CMake + Ninja
macOS artifact: libfuzzpilot_mutator.dylib
Linux artifact: libfuzzpilot_mutator.so
```

### AFL++ Custom Mutator ABI

核心导出函数：

```c
void *afl_custom_init(afl_state_t *afl, unsigned int seed);
size_t afl_custom_fuzz(void *data,
                       unsigned char *buf,
                       size_t buf_size,
                       unsigned char **out_buf,
                       unsigned char *add_buf,
                       size_t add_buf_size,
                       size_t max_size);
void afl_custom_deinit(void *data);
```

可选导出函数：

```c
unsigned char afl_custom_queue_get(void *data,
                                   const unsigned char *filename);
void afl_custom_queue_new_entry(void *data,
                                const unsigned char *filename_new_queue,
                                const unsigned char *filename_orig_queue);
```

这些 queue hook 用于建立 seed filename 到 recipe 的映射。

### 输入 recipe

```json
{
  "id": "recipe_000123",
  "seed_selector": {"mode": "seed_id", "seed_id": "id:000123"},
  "priority": 80,
  "ttl_sec": 900,
  "protect_ranges": [[0, 8]],
  "focus_ranges": [[8, 4096]],
  "tokens": ["IHDR", "IDAT", "IEND"],
  "operator_weights": {
    "insert_token": 0.3,
    "overwrite_range": 0.3,
    "delete_block": 0.1,
    "arith": 0.2,
    "splice": 0.1
  },
  "repair_policy": {
    "length_fields": [],
    "checksum": "none"
  }
}
```

### 变异执行逻辑

```text
1. custom mutator 接收 AFL++ 当前 seed buffer
2. 根据 queue filename、seed hash 或 family metadata 查找 recipe
3. 采样 mutation operator
4. 根据 focus/protect ranges 选择 offset
5. 执行变异
6. 可选执行 length/checksum repair
7. 输出变异样本
8. 将 operator/offset/recipe 命中信息写入 telemetry buffer
```

### 热路径约束

1. 不进行网络请求。
2. 不调用模型 API。
3. 不解析大型 JSON。
4. 不做 SQLite 查询。
5. 不分配过多临时内存。
6. recipe 通过启动时加载、mmap、二进制缓存或周期性轻量刷新进入 mutator。

### 输出 telemetry

custom mutator 可额外写 JSONL：

```json
{
  "seed": "id:000123",
  "recipe_id": "recipe_000123",
  "operator": "insert_token",
  "offset": 32,
  "token": "IDAT",
  "size_before": 128,
  "size_after": 132,
  "protected_range_hit": false
}
```

为了降低开销，v0 可以先把 telemetry 写入 per-process ring buffer，controller 定期采集；调试模式下再启用 JSONL。

## 9. AFL++ 深改路线

v0 使用 C++ 外部 controller + C/C++ custom mutator。v1/v2 再逐步改 AFL++。

### v1：轻量 hook

1. 在 queue entry 选择后输出事件。
2. 在 seed 保存时输出 parent-child lineage。
3. 在 mutation stage 前后输出 operator 统计。
4. 支持通过文件、mmap 或 Unix socket 加载 intervention config 和 seed recipe。
5. 输出当前 seed id 给 custom mutator，减少 filename/hash 查找成本。

### v2：policy hook

1. seed selection hook。
2. energy assignment hook。
3. havoc operator weight hook。
4. mutation offset hook。
5. per-seed mutation recipe hook。
6. seed saving/pruning hook。

### v3：低延迟控制面

1. shared memory 或 Unix domain socket。
2. controller 超时自动 fallback。
3. per-seed policy cache。
4. mutation recipe cache。
5. mutation telemetry lock-free ring buffer。

## 10. 实验设计

### 10.1 实验问题

RQ1：FuzzPilot 是否能在 plateau 后比 AFL++ default 更快产生新 coverage？

RQ2：micro-campaign validation 是否比随机/轮换 intervention 更有效？

RQ3：Hermes-inspired orchestrator + specialist model agents 是否比单一 intervention selector 更有效？

RQ4：agent 控制的 per-seed mutation strategy 是否优于 global mutation policy？

RQ5：C/C++ custom mutator 的 recipe 执行开销是否可接受？

RQ6：哪些 plateau 类型最容易被干预逃离？

RQ7：agent memory 和 skill/policy accumulation 是否提升后续 plateau 决策质量？

RQ8：本地模型 agent 与远程大模型 agent 在效果、延迟、成本和可复现性上如何权衡？

### 10.2 Baselines

```text
B0: AFL++ default
B1: AFL++ + CMPLOG/static config
B2: AFL++ + fixed dictionary
B3: AFL++ + random intervention after plateau
B4: AFL++ + round-robin intervention after plateau
B5: AFL++ + global custom mutator policy
B6: AFL++ + random per-seed recipe
B7: single model-agent mutation strategy selector
B8: model-agent proposals without validation
B9: full model-agent runtime without persistent memory
B10: full model-agent runtime with memory disabled/replay-only
Ours: Hermes-inspired model-agent runtime + persistent memory + budgeted micro-campaign validation + per-seed mutation strategy
```

### 10.3 公平性

1. 所有方法使用相同初始 seeds。
2. 所有方法使用相同总 CPU budget。
3. micro-campaign 的额外预算必须计入总预算。
4. 每个 target 至少重复 N 次，建议 v0 为 5 次，论文级别为 20 次以上。
5. macOS arm64 结果只作为原型和技术报告依据，强会投稿应补 Linux 环境。
6. custom mutator baseline 必须和 FuzzPilot 使用相同 `.dylib`，只关闭 agent recipe，以隔离 mutator 框架开销。

### 10.4 指标

主指标：

```text
new edges
new paths
time-to-next-coverage after plateau
plateau escape rate
unique crashes
valid input ratio
```

辅助指标：

```text
execs/sec
corpus size
duplicate seed ratio
micro-campaign overhead
intervention win rate
mutation strategy win rate
recipe hit rate
operator reward distribution
promotion success rate
agent validation failure rate
agent latency/cost
agent memory hit rate
skill reuse success rate
```

### 10.5 Plateau escape 定义

一次 plateau escape 满足：

```text
在 intervention promotion 后的 escape_window_sec 内，
new_paths_delta > 0 或 new_edges_delta > threshold
```

### 10.6 推荐 v0 实验规模

```text
targets: 3-5 个
baselines: 4-5 个
repeat: 5 次
single run: 30-120 分钟
micro budget: 2-5 分钟
```

### 10.7 论文级实验规模

```text
targets: 10-20 个
baselines: 6-8 个
repeat: 20 次
single run: 6-24 小时
micro budget: 总预算的 5%-20%
mutation strategy ablation: global policy / random per-seed / single-agent / multi-agent validated
```

## 11. macOS arm64 实验流程

### 11.1 准备 target

```bash
export PATH="/opt/homebrew/opt/llvm/bin:$PATH"
export CC=afl-clang-fast
export CXX=afl-clang-fast++

mkdir -p build
cd build
cmake -G Ninja ..
ninja
```

### 11.2 smoke test

```bash
./target_fuzzer seeds/sample
```

### 11.3 AFL++ smoke run

```bash
AFL_SKIP_CPUFREQ=1 AFL_NO_UI=1 \
AFL_CUSTOM_MUTATOR_LIBRARY=./build/libfuzzpilot_mutator.dylib \
FUZZPILOT_RECIPE_STORE=./work/recipes \
afl-fuzz -i seeds -o out -- ./target_fuzzer @@
```

### 11.4 FuzzPilot run

```bash
fuzzpilot run --config configs/target.yaml
```

### 11.5 报告生成

```bash
fuzzpilot report --run-id RUN_ID
```

## 12. 仓库结构建议

```text
fuzz_agent/
  CMakeLists.txt
  docs/
    fuzzpilot_design.md
  configs/
    examples/
      libpng.yaml
      json_parser.yaml
  include/
    fuzzpilot/
      config.hpp
      env.hpp
      ids.hpp
      json_schema.hpp
      runner/
        afl_runner.hpp
        process.hpp
      telemetry/
        collector.hpp
        afl_stats.hpp
        coverage.hpp
        mutation_events.hpp
      plateau/
        detector.hpp
      agents/
        agent.hpp
        agent_runtime.hpp
        agent_memory.hpp
        agent_task.hpp
        coordinator.hpp
        plateau_diagnosis_agent.hpp
        scheduler_agent.hpp
        cmp_agent.hpp
        dictionary_agent.hpp
        format_agent.hpp
        mutator_agent.hpp
        corpus_agent.hpp
        crash_agent.hpp
        result_analysis_agent.hpp
      model/
        gateway.hpp
        openai_compatible.hpp
        replay_cache.hpp
      mutation/
        strategy.hpp
        recipe_store.hpp
        operator_sampler.hpp
        range_selector.hpp
      interventions/
        intervention.hpp
        default.hpp
        cmplog.hpp
        dictionary.hpp
        corpus_prune.hpp
        seed_focus.hpp
        structure.hpp
      micro/
        manager.hpp
        evaluator.hpp
      storage/
        db.hpp
      report/
        summary.hpp
  src/
    cli/
      main.cpp
    config.cpp
    env.cpp
    runner/
      afl_runner.cpp
      process_posix.cpp
    telemetry/
      collector.cpp
      afl_stats.cpp
      coverage.cpp
      mutation_events.cpp
    plateau/
      detector.cpp
    agents/
      agent_runtime.cpp
      agent_memory.cpp
      coordinator.cpp
      plateau_diagnosis_agent.cpp
      scheduler_agent.cpp
      cmp_agent.cpp
      dictionary_agent.cpp
      format_agent.cpp
      mutator_agent.cpp
      corpus_agent.cpp
      crash_agent.cpp
      result_analysis_agent.cpp
    model/
      gateway.cpp
      openai_compatible.cpp
      replay_cache.cpp
    mutation/
      strategy.cpp
      recipe_store.cpp
      operator_sampler.cpp
      range_selector.cpp
    interventions/
      default.cpp
      cmplog.cpp
      dictionary.cpp
      corpus_prune.cpp
      seed_focus.cpp
      structure.cpp
    micro/
      manager.cpp
      evaluator.cpp
    storage/
      db.cpp
    report/
      summary.cpp
  mutators/
    fuzzpilot/
      CMakeLists.txt
      afl_custom_mutator.c
      mutator_core.cpp
      mutator_core.hpp
      recipe_cache.cpp
      recipe_cache.hpp
      telemetry_ring.cpp
      telemetry_ring.hpp
  db/
    schema.sql
  tools/
    fuzzpilot_report.cpp
    corpus_hash.cpp
    build_target.cpp
    run_baselines.cpp
    aggregate_results.cpp
  experiments/
    README.md
  results/
    .gitkeep
```

## 13. 里程碑

### M0：文档与实验骨架

1. 完成设计文档。
2. 建立 CMake/C++ repo 结构。
3. 实现 C++ config loader。
4. 实现 C++ AFL++ smoke runner。
5. 实现最小 custom mutator `.dylib`，先提供 AFL++ fallback。

### M1：Telemetry + Plateau

1. 解析 `fuzzer_stats`。
2. 定期写 SQLite。
3. 实现 plateau detector。
4. 采集 custom mutator recipe hit/miss telemetry。
5. 生成基础 coverage 曲线。

### M2：Mutation Strategy

1. 定义 `SeedMutationStrategy` schema。
2. 实现 recipe store。
3. 实现 operator sampler。
4. 实现 protect/focus range selector。
5. 实现 per-seed recipe lookup。

### M3：Micro-campaign

1. corpus snapshot。
2. 启动 default micro-campaign。
3. 启动多个 intervention+strategy micro-campaign。
4. 计算 intervention reward 和 strategy reward。

### M4：Hermes-inspired Model Agent Runtime

1. 实现 Model API Gateway。
2. 支持 OpenAI-compatible 远程 API 与本地模型 API。
3. 实现 AgentTask、typed proposal bus、per-agent prompt、输出 JSON schema、action allowlist 和 replay cache。
4. 实现 CoordinatorAgent orchestrator。
5. 实现 PlateauDiagnosis/Scheduler/Cmp/Dictionary/Format/Mutator/Corpus/Crash/ResultAnalysis model-backed agents。
6. 实现 isolated blackboard slice 和 per-agent context compression。
7. 实现 agent memory、agent skill/policy snippet 和 ResultAnalysis 反思更新。
8. 实现 C++ rule-based agents 作为 baseline/fallback/guardrail。
9. 实现 winner promotion。

### M5：Custom mutator 完整化

1. 实现 structure-preserving mutator。
2. 支持 recipe。
3. 支持 mutator telemetry。
4. 支持 token insertion、range overwrite、arith、splice、block clone/delete。

### M6：实验矩阵

1. 跑 3-5 个 target。
2. 跑 baseline。
3. 跑 mutation strategy ablation。
4. 生成报告。

### M7：论文原型

1. 固化系统图。
2. 固化 RQ。
3. 固化实验结果。
4. 写 arXiv 技术报告。

## 14. 风险与缓解

### 风险 1：micro-campaign 开销太大

缓解：

1. 限制每次 plateau 最多 4 个 intervention。
2. 使用 execs budget。
3. 只在 plateau 后触发。
4. 历史低收益 agent 降权。

### 风险 2：agent 建议没有用

缓解：

1. 所有建议必须 A/B 验证。
2. default control 永远参与。
3. 不优于 default 就不提升。
4. mutation strategy 必须和 AFL++ fallback、random per-seed recipe 做消融。

### 风险 3：macOS 实验不可比

缓解：

1. macOS arm64 用于原型和工程验证。
2. 最终论文补 Linux benchmark。
3. 所有实验记录架构和系统版本。

### 风险 4：模型动态决策不稳定或贡献被质疑

缓解：

1. 模型 agent runtime 是 MVP 必备能力，但实验必须提供 full-agent、single-agent、rule-only、random 和 replay ablation。
2. 模型输出不直接执行，必须通过 schema、action allowlist、budget 和 micro-campaign reward 验证。
3. 远程模型不可用时支持本地模型 endpoint；模型完全不可用时进入 degraded fallback，并在报告中单独标记。
4. 记录每个 agent 的 task/context hash、response hash、latency、validation result、memory patch 和 replay cache，保证可审计和可复跑。
5. 核心 claim 放在“模型 agent 分工生成候选 + typed guardrail + 实测验证 + seed-level strategy promotion + 经验沉淀”的闭环，不把模型文字解释当作证据。

### 风险 5：AFL++ 深改影响稳定性

缓解：

1. v0 C++ 外部 controller + AFL++ custom mutator。
2. v1 只加 telemetry hook。
3. v2 再加 policy hook。
4. 所有 hook 超时 fallback。

### 风险 6：custom mutator 热路径开销过高

缓解：

1. recipe 预编译成二进制缓存。
2. mutator 内不查 SQLite。
3. mutator 内不解析大型 JSON。
4. 使用固定容量 buffer 和对象池。
5. 默认关闭高频 JSONL，改用 ring buffer 聚合 telemetry。

## 15. 最小 MVP 定义

MVP 必须能完成：

1. 启动 AFL++ 主 campaign。
2. 采集 `fuzzer_stats`。
3. 判断 plateau。
4. 冻结 corpus snapshot。
5. 构建并加载 C/C++ custom mutator。
6. 生成并加载至少一种 per-seed mutation recipe。
7. 调度至少 4 个 model-backed strategic agents：
   - CoordinatorAgent
   - PlateauDiagnosisAgent
   - MutatorAgent
   - Dictionary/Format/Scheduler/Corpus 中至少一个 specialist
8. 每个模型 agent 基于 isolated blackboard slice 生成结构化 intervention、critique、memory patch 或 `SeedMutationStrategy` 候选。
9. 对模型输出执行 schema 校验、action allowlist 校验、budget 限制和 replay 记录。
10. 启动至少 3 个 micro-campaign：
   - default control
   - model-agent-proposed intervention
   - rule-based dictionary/corpus-prune/seed-focus intervention
11. 计算 intervention reward 和 mutation strategy reward。
12. 调度 ResultAnalysisAgent 总结 micro-campaign 结果并更新 agent memory。
13. 选择 winner。
14. 记录 SQLite、agent decision replay log 和 agent memory。
15. 生成 coverage/plateau/strategy/agent-decision 报告。

MVP 不要求：

1. 固定依赖某一个商业云模型；可以使用本地 OpenAI-compatible 模型服务完成 MVP。
2. 模型参与每一次 mutation。
3. AFL++ core patch。
4. crash exploitability 判断。
5. Linux benchmark。

## 16. 论文叙事建议

### 推荐标题

```text
FuzzPilot: Agentic Greybox Fuzzing with Model-Driven Seed Mutation Strategies
```

### 核心论点

```text
Existing greybox fuzzers optimize isolated decisions, but they provide limited support for diagnosing coverage plateaus and adapting mutation at the seed level. FuzzPilot turns plateau handling into an agentic, model-driven, feedback-grounded intervention and seed-mutation-strategy selection problem.
```

### 贡献

1. 提出 plateau diagnosis/intervention selection 问题。
2. 提出 model-agent-driven per-seed mutation strategy，把 operator、offset、token、保护区间和 budget 纳入统一 action space。
3. 设计参考 Hermes Agent 思想的 fuzzing agent runtime：orchestrator-worker、isolated context、typed proposal、parallel specialists、persistent memory 和 skill/policy accumulation。
4. 设计 C/C++ AFL++ 外部控制框架、custom mutator 执行器和后续内核 hook 路线。
5. 提出模型 agent、规则 fallback 和 micro-campaign 验证结合的候选生成与实测选择机制。
6. 实现可复现 telemetry/reward/agent-decision/memory/report pipeline。
7. 在多个 parser target 上验证 plateau escape、模型 agent 动态决策和 seed-level mutation strategy 的效果。

## 17. 下一步任务清单

1. 初始化 CMake/C++ 项目结构。
2. 写 `TargetSpec` 和 YAML loader。
3. 写 C++ AFL++ runner。
4. 写 `fuzzer_stats` parser。
5. 写 plateau detector。
6. 写 SQLite schema。
7. 写 `SeedMutationStrategy` 和 recipe store。
8. 写 C/C++ custom mutator 最小版本。
9. 写 Model API Gateway，支持 OpenAI-compatible 远程和本地 endpoint。
10. 写 AgentTask、typed proposal bus、per-agent JSON schema、action allowlist、health check 和 replay cache。
11. 写 Coordinator/PlateauDiagnosis/Mutator/Dictionary/Format/Scheduler/Corpus model-backed agents 的最小版本。
12. 写 agent memory 和 skill/policy snippet store。
13. 写 default/dictionary/corpus-prune 三个 rule-based fallback intervention。
14. 写 micro-campaign manager。
15. 跑第一个 macOS arm64 target。
16. 生成第一张 coverage vs time、strategy win rate 和 agent-decision effectiveness 图。
