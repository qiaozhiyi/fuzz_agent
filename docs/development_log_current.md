# FuzzPilot 当前开发日志

日期：2026-05-10  
阶段：M0-M4 最小实现已完成，准备进入真实 AFL++ campaign 集成与增强  
主题：Agentic Greybox Fuzzing with Model-Driven Seed Mutation Strategies

## 1. 当前总体状态

FuzzPilot 当前已经从纯设计文档推进到可编译、可测试的 C/C++ 原型骨架。现阶段完成了三个关键基础阶段：

1. M0：工程骨架与最小 custom mutator
2. M1：Telemetry + Plateau 检测闭环
3. M2：Mutation Strategy + per-seed recipe hot path
4. M3：Micro-campaign snapshot / plan / evaluator 最小闭环
5. M4：Hermes-inspired model agent runtime 最小闭环

当前项目已经具备：

- CMake/C++20 工程结构
- CLI 基础命令
- YAML 风格配置加载
- SQLite schema 和初始化
- AFL++ command preview
- `fuzzer_stats` 解析
- custom mutator telemetry 聚合
- plateau replay/monitor 链路
- `SeedMutationStrategy` schema
- global / seed-id / seed-hash recipe store
- AFL++ custom mutator `.dylib`
- custom mutator hot path recipe lookup
- DeepSeek API 接入健康检查已通过
- Hermes-inspired agent runtime 已有 fake/openai-compatible gateway 和 typed agent task/decision 最小实现

当前验证状态：

```text
cmake --build build-make
ctest --test-dir build-make --output-on-failure
```

结果：

```text
16/16 tests passed
```

## 2. 已完成阶段

## M0：文档与实验骨架

状态：完成

完成内容：

- 建立 CMake 项目
- 建立 `include/`、`src/`、`mutators/`、`db/`、`configs/`、`tests/` 等目录
- 实现 `fuzzpilot` CLI 基础入口
- 实现配置加载和校验
- 实现 AFL++ command preview
- 实现最小 AFL++ custom mutator
- 构建 macOS arm64 `.dylib`

关键产物：

- `CMakeLists.txt`
- `src/cli/main.cpp`
- `configs/examples/libpng.yaml`
- `mutators/fuzzpilot/libfuzzpilot_mutator.dylib`
- `db/schema.sql`

完成标志：

- 项目可编译
- custom mutator 可构建
- 基础 CTest 通过

## M1：Telemetry + Plateau

状态：完成

完成内容：

- 解析 AFL++ `fuzzer_stats`
- 解析 custom mutator telemetry JSONL
- 将 mutation telemetry 合并进 AFL stats
- 支持 telemetry replay
- 支持真实 AFL++ 输出目录单次/连续采样
- 写入 SQLite `telemetry` / `plateaus`
- 输出 JSONL 事件日志
- 输出基础 coverage CSV
- 实现 plateau detector

新增 CLI：

```bash
fuzzpilot parse-mutator-telemetry
fuzzpilot replay-telemetry
fuzzpilot monitor-telemetry
```

完成标志：

- replay fixture 能写入 telemetry
- plateau 能被检测并写入数据库
- mutator recipe hit/miss 能被采集
- CSV / JSONL / SQLite 三路输出正常

验证结果：

```text
telemetry rows: 2
plateau rows: 1
recipe_hits / recipe_misses: 12 / 3
```

## M2：Mutation Strategy

状态：完成

完成内容：

- 扩展 `SeedMutationStrategy`
- 支持 selector：
  - `global`
  - `seed_id`
  - `seed_hash`
  - `family`
- 实现 strategy validation
- 实现稳定 seed hash
- 实现 operator sampler
- 实现 focus/protect range selector
- 实现 compact recipe store
- 生成 `recipe_index.tsv`
- 生成 `compact/*.recipe`
- 支持 recipe lookup
- custom mutator hot path 接入 per-seed lookup
- custom mutator 优先级：
  - seed id
  - seed hash
  - global recipe
  - fallback mutation

新增 CLI：

```bash
fuzzpilot write-m2-recipes
fuzzpilot lookup-recipe
```

完成标志：

```text
seed_id lookup: hit=true, priority=90
seed_hash lookup: hit=true, priority=85
mutator telemetry: recipe_hits=8, recipe_misses=0
```

这说明 controller 侧能物化 per-seed recipe，mutator 侧能在 AFL++ hot path 中命中并执行对应策略。

## 3. Agent 化方案调整进度

项目方案已经从“模型 API 可选增强”调整为更彻底的 agentic 架构：

- 除低延迟敏感模块外，重点策略模块默认使用 model-backed agents
- 模型 API 是主要策略驱动来源
- 规则逻辑只作为 fallback、guardrail、baseline 和 ablation
- 参考 Hermes Agent 的强 agent 化思想

已写入设计方案的 agent runtime 原则：

- Orchestrator + Workers
- isolated context
- typed message passing
- parallel specialist agents
- resource-aware scheduling
- persistent memory
- skill / policy accumulation
- reflection after action

计划中的 model-backed agents：

- `CoordinatorAgent`
- `PlateauDiagnosisAgent`
- `SchedulerAgent`
- `CmpAgent`
- `DictionaryAgent`
- `FormatAgent`
- `MutatorAgent`
- `CorpusAgent`
- `CrashTriageAgent`
- `ResultAnalysisAgent`

当前状态：

- 设计已完成
- 配置 schema 已预留
- SQLite 表已预留
- DeepSeek API health check 已通过
- `Model API Gateway` 最小实现已完成：
  - fake provider 用于确定性测试
  - openai-compatible provider 外壳用于 DeepSeek / local model
- `AgentTask` / `AgentDecision` / typed proposal 基础对象已实现
- `CoordinatorAgent` / `PlateauDiagnosisAgent` / `MutatorAgent` / `DictionaryAgent` 的 fake model-backed smoke 已实现
- 完整 specialist agent prompt、memory、skill accumulation 仍待增强

DeepSeek API 测试结果：

```text
endpoint: https://api.deepseek.com/chat/completions
model: deepseek-v4-flash
HTTP status: 200
structured JSON output: ok
```

注意事项：

- DeepSeek `deepseek-v4-flash` 需要关闭 thinking 才更适合结构化 agent JSON 输出：

```json
{
  "thinking": {
    "type": "disabled"
  }
}
```

## 4. 当前完成度估算

这里按两个口径估算：

1. MVP 完成度
2. 完整研究原型完成度

## MVP 完成度

MVP 定义为：

- 能启动/监控 AFL++ 主 campaign
- 能采集 telemetry
- 能检测 plateau
- 能生成和加载 per-seed recipe
- 能调度 model-backed agents
- 能启动 micro-campaign
- 能评价 reward
- 能选择 winner
- 能 promotion
- 能生成基础报告

当前已经完成：

- telemetry 基础层
- plateau 基础层
- recipe 基础层
- mutator hot path 基础层
- model API 接入健康检查
- agent runtime 设计

尚未完成：

- 真正 `fuzzpilot run`
- AFL++ process lifecycle
- corpus snapshot
- micro-campaign manager
- evaluator / ranker
- promotion
- Model API Gateway runtime
- model-backed agents runtime
- report generator

估算：

```text
MVP 完成度：约 55%
MVP 剩余：约 45%
```

## 完整研究原型完成度

完整研究原型还包括：

- Hermes-inspired agent runtime
- 多 agent 并行调度
- agent memory
- skill / policy accumulation
- DeepSeek / local model provider
- 实验矩阵
- baseline / ablation
- coverage 图表
- strategy win rate
- agent decision trace
- Linux benchmark
- 论文级报告

估算：

```text
完整研究原型完成度：约 38%
完整研究原型剩余：约 62%
```

## 5. 阶段进度表

| 阶段 | 名称 | 状态 | 完成度 |
|---|---|---:|---:|
| M0 | 文档与工程骨架 | 完成 | 100% |
| M1 | Telemetry + Plateau | 完成 | 100% |
| M2 | Mutation Strategy | 完成 | 100% |
| M3 | Micro-campaign | 最小实现完成，真实 AFL++ 执行待增强 | 65% |
| M4 | Hermes-inspired Model Agent Runtime | 最小实现完成，完整 agent 记忆/技能待增强 | 45% |
| M5 | Custom mutator 完整化 | 部分完成 | 35% |
| M6 | 实验矩阵 | 未开始 | 0% |
| M7 | 论文原型 / 报告 | 未开始 | 0% |

## 6. 剩余核心差距

## 差距 1：还不能真正运行完整 fuzzing campaign

当前可以生成 AFL++ 命令，也可以监控 AFL++ 输出目录，但还没有完整的：

- `fuzzpilot run`
- process spawn
- budget 控制
- SIGINT / SIGTERM / SIGKILL lifecycle
- run directory 管理
- run status 管理

这是 M3 前必须补上的部分。

## 差距 2：还没有 micro-campaign

当前 plateau 能被检测，但检测后还不会自动：

- freeze corpus snapshot
- 创建多个 micro-campaign
- 为每个 campaign 写不同 recipe store
- 启动短预算 AFL++ run
- 收集 micro result

这是系统从“监控器”变成“自诊断控制器”的关键。

## 差距 3：还没有 evaluator / promotion

当前还不会计算：

- intervention reward
- strategy reward
- new edges
- new paths
- crash bonus
- overhead penalty
- default control comparison

也不会把 winning recipe 提升回主 campaign。

## 差距 4：agent runtime 还没实现

虽然设计已经改成强 agent 化，但代码里还没有：

- Model API Gateway
- OpenAI-compatible client
- DeepSeek provider config
- AgentTask
- typed proposal bus
- CoordinatorAgent runtime
- specialist agents runtime
- agent replay cache
- agent memory store
- skill store

这会是 M4 的核心。

## 差距 5：custom mutator 还只是 M2 级别

当前 mutator 已支持：

- global recipe
- seed id recipe
- seed hash recipe
- operator weights
- focus/protect ranges
- token insertion
- overwrite
- arith
- splice
- delete block

但还缺：

- clone block
- richer dictionary overwrite
- structure repair
- length field repair
- checksum repair
- mmap / hot reload
- per-seed budget tracking
- lower-overhead telemetry ring

## 差距 6：没有真实 target 实验数据

当前所有测试都是 fixture / smoke test。还没跑：

- libpng
- json parser
- xml/yaml parser
- archive parser
- regex/sql parser

因此还没有真实 coverage curve、plateau escape rate 或 strategy win rate。

## 7. 下一步建议

下一步应该进入 M3，但建议拆成两个小阶段：

## M3-A：主 campaign runner

目标：

- 实现 `fuzzpilot run`
- 启动 AFL++
- 创建 run directory
- 写 runs / campaigns 表
- 设置 env：
  - `AFL_CUSTOM_MUTATOR_LIBRARY`
  - `FUZZPILOT_RECIPE_STORE`
  - `FUZZPILOT_MUTATOR_TELEMETRY`
- monitor telemetry loop
- budget 到期停止

完成后，FuzzPilot 就能真正控制 AFL++ 主 campaign。

## M3-B：Micro-campaign manager

目标：

- plateau 后 snapshot corpus
- 生成 default/model/rule 三类 micro-campaign
- 为每个 micro-campaign 生成独立 recipe store
- 顺序或低并发运行
- 收集 `micro_results`

完成后，FuzzPilot 才开始具备“自诊断实验控制器”的形态。

## 8. 当前风险

1. 真实 AFL++ target 尚未接入，后续可能暴露路径、env、timeout 和 sanitizer 兼容问题。
2. 当前 YAML loader 是轻量实现，不是完整 YAML parser，复杂配置可能需要切换到 `yaml-cpp`。
3. 当前 recipe 格式是 compact text，后续如果 recipe 增大，需要二进制/mmap 格式。
4. agent runtime 设计已经比较强，M4 实现复杂度会明显高于 M0-M2。
5. DeepSeek API 已验证可用，但密钥不应长期保留在聊天或日志中，建议轮换。

## 9. 总结

当前 FuzzPilot 已完成底座建设：

- 能编译
- 能测试
- 能采集 telemetry
- 能检测 plateau
- 能写 SQLite/JSONL/CSV
- 能生成 per-seed mutation strategy
- 能让 custom mutator 在 hot path 命中 seed-level recipe

但距离完整系统还差关键控制闭环：

```text
run AFL++ campaign
-> detect plateau
-> snapshot corpus
-> call model-backed agents
-> launch micro-campaigns
-> evaluate reward
-> promote winner
-> report results
```

当前最准确的判断是：

```text
底层工程能力：已经成型
自诊断闭环：尚未完成
agent 智能层：设计完成，代码未开始
实验研究层：尚未开始
```
