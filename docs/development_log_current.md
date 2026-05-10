# FuzzPilot 当前开发日志

日期：2026-05-10  
阶段：MVP 闭环已完成  
主题：Agentic Greybox Fuzzing with Model-Driven Seed Mutation Strategies

## 1. 当前总体状态

FuzzPilot 已经从 M0-M4 的分散能力推进到一个可执行的 MVP 闭环。当前版本可以通过 `fuzzpilot run` 完成一次完整的 dry-run 级自诊断流程：

```text
生成主 AFL++ launch plan
-> 读取主 campaign fuzzer_stats
-> 写入 telemetry / coverage
-> 检测 plateau
-> snapshot corpus
-> 调度 model-backed agents
-> 写 agent decision replay
-> 写 agent memory
-> 创建 micro-campaign
-> 评估 reward
-> 选择 winner
-> promotion 到 promoted recipe store
-> 生成 MVP report
```

本轮 MVP 开发不是新增一个单点命令，而是把 telemetry、plateau、mutation recipe、custom mutator、micro-campaign、model agent runtime、SQLite 和报告串成了一个端到端控制器。

当前验证状态：

```text
cmake --build build-make
ctest --test-dir build-make --output-on-failure
```

结果：

```text
17/17 tests passed
```

## 2. 本轮新增核心能力

### 2.1 MVP 总入口

新增 `fuzzpilot run` 命令，参数包括：

```bash
fuzzpilot run \
  --config configs/examples/libpng.yaml \
  --work-dir build-make/smoke/mvp_run \
  --schema db/schema.sql \
  --afl-output-dir tests/fixtures/afl_out \
  --stats tests/fixtures/fuzzer_stats_older \
  --stats tests/fixtures/fuzzer_stats_newer \
  --provider fake
```

该命令现在负责统一调度：

- run directory 创建
- SQLite 初始化
- `runs` / `campaigns` 状态记录
- 主 campaign launch plan 生成
- main recipe store 生成
- telemetry replay
- coverage CSV 输出
- plateau 事件生成
- corpus snapshot
- agent runtime 调度
- micro-campaign 准备与评估
- winner promotion
- report 输出

### 2.2 主 campaign 管理

新增 `controller/run` 模块：

- `RunOptions`
- `RunSummary`
- `run_mvp`
- `run_summary_json`

MVP dry-run 默认不会真正启动 AFL++，但会生成可执行的 `main_launch.sh`。当使用 `--real-run` 时，控制器会通过 C++ process runner 尝试启动主 AFL++ 进程并记录 PID。

### 2.3 Agent runtime 扩展

MVP 现在默认调度 8 个 strategic agents：

- `CoordinatorAgent`
- `PlateauDiagnosisAgent`
- `SchedulerAgent`
- `CmpAgent`
- `MutatorAgent`
- `DictionaryAgent`
- `FormatAgent`
- `CorpusAgent`

micro-campaign 评估完成后，还会额外调度：

- `ResultAnalysisAgent`

因此 MVP smoke run 会产生 9 条 agent decision。每条 decision 都会写入：

- SQLite `agent_decisions`
- `agent_decisions.jsonl`
- `events.jsonl`
- `agent_memory`
- `agent_memory.jsonl`

### 2.4 Micro-campaign 与 promotion

MVP micro-campaign 现在覆盖 4 类候选：

- `default_control`
- `dictionary_probe`
- `seed_focus_probe`
- `per_seed_recipe_probe`

其中 `per_seed_recipe_probe` 代表 model-agent-proposed mutation strategy 干预。测试 fixture 已设置为该候选胜出，MVP report 中 winner 为：

```text
intv_per_seed_recipe
```

promotion 会生成：

```text
promoted_recipes/recipe_index.tsv
promoted_recipes/global.recipe
```

### 2.5 SQLite 状态持久化

本轮补齐了 run/campaign 生命周期与 agent memory 写入：

- `insert_run`
- `finish_run`
- `insert_campaign`
- `finish_campaign`
- `insert_agent_memory`

一次 MVP smoke run 的数据库记录为：

```text
runs            1
campaigns       5
telemetry       2
plateaus        1
agent_decisions 9
agent_memory    9
micro_results   4
```

### 2.6 报告产物

每次 MVP run 会生成：

- `report.md`
- `coverage.csv`
- `events.jsonl`
- `agent_decisions.jsonl`
- `agent_memory.jsonl`
- `fuzzpilot.sqlite`
- `main_launch.sh`
- `main_recipes/`
- `promoted_recipes/`
- `corpus_snapshot/`

报告包含：

- run / target / plateau 信息
- 主 AFL++ launch plan
- coverage delta
- agent decision 列表
- micro result 列表
- winner intervention
- promoted recipe index

## 3. 阶段完成度

| 阶段 | 名称 | 状态 | 完成度 |
|---|---|---:|---:|
| M0 | 文档与工程骨架 | 完成 | 100% |
| M1 | Telemetry + Plateau | 完成 | 100% |
| M2 | Mutation Strategy | 完成 | 100% |
| M3 | Micro-campaign | MVP 完成，真实 AFL++ 长跑待验证 | 90% |
| M4 | Hermes-inspired Model Agent Runtime | MVP 完成，深层 memory/skill 待增强 | 80% |
| MVP | 端到端自诊断闭环 | 完成 | 100% |
| M5 | Custom mutator 完整化 | 部分完成 | 40% |
| M6 | 实验矩阵 | 未开始 | 0% |
| M7 | 论文原型 / 报告 | 未开始 | 0% |

这里的 MVP 完成指“工程闭环可执行、可测试、可落盘、可报告”。真实 AFL++ target 的长时间实验、benchmark、ablation 和论文级统计仍属于 MVP 之后的研究原型阶段。

## 4. 距离完整研究原型的差距

MVP 之后，项目已经不是“还没有闭环”，而是进入增强和真实实验阶段。主要差距如下：

1. 真实 AFL++ 长跑生命周期还需要增强：预算控制、信号处理、超时停止、异常恢复、日志收集。
2. micro-campaign 当前已能 plan/evaluate/persist，但真实并发执行和隔离运行还需要接入 AFL++ process lifecycle。
3. Model API Gateway 已有 OpenAI-compatible 外壳和 fake provider，后续需要更严格的 JSON schema 校验、重试、rate limit、cost/latency 统计。
4. Agent memory 当前能持久化 memory patch，后续需要 trust score、skill snippet、跨 run 检索和失败降权。
5. Custom mutator 已支持 recipe hot path，后续还要增强结构修复、checksum/length repair、mmap hot reload 和更低开销 telemetry。
6. 真实 target 实验尚未开始，还没有 coverage curve、plateau escape rate、strategy win rate 或 ablation 数据。

当前完整研究原型完成度估算：

```text
约 55%
```

## 5. 下一阶段建议

下一阶段建议进入 M5/M6：

1. 强化真实 AFL++ process lifecycle，补齐 run budget、stop/kill、stderr/stdout 捕获和异常恢复。
2. 让 micro-campaign 能在真实 AFL++ 上按短预算顺序运行。
3. 把 fake provider 的 MVP smoke 保留为 CI，对 DeepSeek / 本地模型增加可选集成测试。
4. 增强 agent schema validator，避免模型输出自由文本或越权 action。
5. 选一个真实 target，例如 libpng 或 json parser，跑第一组 plateau escape 实验。

## 6. 当前判断

FuzzPilot 现在已经达到完整 MVP 阶段：能跑通 agent 驱动的 fuzzing 自诊断控制闭环，并把关键证据写入 SQLite、JSONL、CSV 和 Markdown 报告。

后续重点不再是“把流程串起来”，而是把这个流程变强：真实执行、更严格验证、更强 agent memory、更完整 custom mutator 和真实 benchmark。
