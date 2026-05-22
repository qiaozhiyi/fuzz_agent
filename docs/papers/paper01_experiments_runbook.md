# Paper 1 — 实验运行手册（Experiments Runbook）

本文档把 Paper 1（arXiv 占位版）所需的全部实验落到「一条命令、一个产物路径、一个验收标准」的颗粒度。任何人按本文档从一台干净的 Docker-capable 机器开始，都能跑完全部实验、得到论文里所有图表的源数据。

> 配套的 outline 见 [paper01_arxiv_placeholder.md](paper01_arxiv_placeholder.md)；高阶计划见 [paper01_experiment_plan.md](paper01_experiment_plan.md)；本文件是可执行细节。

---

## 0. 实验总览

5 个实验单元、16 次 AFL++ 完整 run、15 次 mutator 微基准、1 次 case-study 抽取。总核时约 64–72 core-hours，4 核机器约 16–18 小时，8 核机器约 8–9 小时。

| ID | 实验 | 目标 | 模式 | 时长 | repeats | runs | 用于 |
|---|---|---|---|---|---|---|---|
| E1a | 主对比 baseline | cjson | `baseline-afl` | 4 h | 5 | 5 | F3, F4, T1 |
| E1b | 主对比 full-agent | cjson | `full-agent` | 4 h | 5 | 5 | F3, F4, T1, T2 |
| E2a | ablation rule-only | cjson | `rule-only` | 4 h | 3 | 3 | T1 |
| E2b | ablation no-mutator | cjson | `no-mutator` | 4 h | 3 | 3 | T1 |
| E3 | mutator 微基准 | — | 3 configs | ~2 min | 5 | 15 micro | F5 |
| E4 | case study 抽取 | — | 复用 E1b | — | — | 0 | T2, §7 |
| E5 (可选) | libpng smoke | libpng | baseline + full-agent | 4 h | 1 | 2 | §9 |

---

## 1. 环境与前置

### 1.1 硬件

- Docker Buildx 可用（macOS/Linux host、amd64/arm64 host 均可）
- 论文正式可比较数据固定 `FUZZPILOT_DOCKER_PLATFORM=linux/amd64`
- ≥ 4 物理核可独占给 AFL++（hyperthreading 关闭推荐）
- ≥ 16 GB RAM
- ≥ 100 GB 磁盘（每个 4h run 产物 ~1–2 GB）
- 关闭 CPU 频率扰动：`sudo cpupower frequency-set -g performance`，并保留 `AFL_SKIP_CPUFREQ=1` 在 config 中

### 1.2 软件版本（必须固定记录）

| 组件 | 锁定版本 | 记录位置 |
|---|---|---|
| AFL++ | `v4.21c` 或 build-time 选定的 commit | `run_metadata.json:afl_version` |
| FuzzPilot | Git tag `paper01-arxiv-v1` | `run_metadata.json:git_commit` |
| 模型 provider | deepseek-chat（或固定的 OpenAI 兼容端点） | `config.yaml:model_api` |
| 模型 temperature | `0.0` | config 中显式写 |
| 操作系统 | `uname -a` 全量 | metadata 自动捕获 |
| CMake | 3.22+ | metadata |
| 编译器 | g++/clang++ 版本 | metadata |

### 1.3 一次性准备

```bash
# 构建自包含 Docker 镜像
FUZZPILOT_DOCKER_PLATFORM=linux/amd64 scripts/fuzzpilot_docker.sh build

# 验证 model API key
export FUZZPILOT_MODEL_API_KEY="..."   # 不要 echo 出来；脚本里只引用变量名

# 跑预检
FUZZPILOT_DOCKER_PLATFORM=linux/amd64 scripts/fuzzpilot_docker.sh preflight --canonical
```

`preflight.sh` 必须输出全绿才能进入实验阶段。任何一项红色都先修。

### 1.4 目录骨架

```
results/paper01_ai_recipe_mutation/
├── runs/                  ← 所有 AFL run 产物（每 run 一个子目录）
├── microbench/            ← E3 微基准产物
├── aggregated/            ← aggregate.py 的输出（CSV/JSON）
├── figures/               ← plots.py 的输出（F3/F4/F5 PDF）
└── tables/                ← T1/T2 的 markdown 表格
```

```
experiments/manifests/paper01_preprint.yaml   ← 16 run 的清单
scripts/paper01/
├── run_batch.sh           ← 主跑批入口
├── preflight.sh           ← 预检
├── aggregate.py           ← 产物聚合
├── plots.py               ← 画图
└── modes/                 ← 每个 ablation 的 config 覆盖文件
    ├── baseline-afl.yaml
    ├── rule-only.yaml
    ├── no-mutator.yaml
    └── full-agent.yaml
```

---

## 2. 实验细节

### 2.1 实验 E1a — cjson baseline

**目的**：建立 AFL++ vanilla 在等价预算下的 coverage-over-time 基线。
**预期结果**：5 条 coverage 曲线，中位数与 IQR 可成为 F3 的对照。

**单 run 命令模板**（脚本会代你生成实际命令）：

```bash
RUN_ID=p1_e1_cjson_baseline-afl_r01
OUT=results/paper01_ai_recipe_mutation/runs/${RUN_ID}

FUZZPILOT_DOCKER_PLATFORM=linux/amd64 \
  scripts/fuzzpilot_docker.sh run-batch --exp E1a --repeats 1
```

**完成判据（每个 run）**：

- `${OUT}/work/run_*/fuzzer_stats` 存在且 `execs_done > 1e6`
- `${OUT}/work/run_*/coverage.csv` ≥ 200 行（4 h × ~每秒一行）
- `events.jsonl` 含至少一条 `main_afl_launched` 事件
- 无 `crashes_unique` 之外的异常终止；`run_metadata.json` 状态为 `completed`

**批命令**：

```bash
FUZZPILOT_DOCKER_PLATFORM=linux/amd64 scripts/fuzzpilot_docker.sh run-batch --exp E1a --repeats 5
```

预计时长：单 run 4 h；4 核并行约 5 h。

### 2.2 实验 E1b — cjson full-agent

**目的**：FuzzPilot 完整闭环（plateau → blackboard → agent → micro-campaign → promotion → recipe-guided mutation）。
**额外预期**：每 run 至少触发 1 次 plateau，至少 1 次 micro-campaign，至少 1 次 promotion，否则视为 case study 候选不足。

```bash
FUZZPILOT_DOCKER_PLATFORM=linux/amd64 scripts/fuzzpilot_docker.sh run-batch --exp E1b --repeats 5
```

**额外捕获**（baseline 没有）：
- `agent_decisions.jsonl`
- `agent_memory.jsonl`
- `main_recipes/`（promotion 后的 recipe）
- `fuzzpilot.sqlite`（含 plateau 表、proposal 表、micro 表）

**完成判据**：除 E1a 的全部 + `schema_valid_rate ≥ 0.7`（在该 run 的 agent_decisions 中），否则需检查模型配置。

### 2.3 实验 E2a — cjson rule-only

**目的**：仅用规则化 recipe（不调用 LLM），证明 LLM 提议是必要的。
**与 full-agent 的差异**：跳过模型调用，micro-campaign 仍跑，规则源自 `mutation_strategy` 内置策略。

```bash
FUZZPILOT_DOCKER_PLATFORM=linux/amd64 scripts/fuzzpilot_docker.sh run-batch --exp E2a --repeats 3
```

### 2.4 实验 E2b — cjson no-mutator

**目的**：关掉 recipe-guided mutator，证明 mutator 是收益来源之一。

```bash
FUZZPILOT_DOCKER_PLATFORM=linux/amd64 scripts/fuzzpilot_docker.sh run-batch --exp E2b --repeats 3
```

### 2.5 实验 E3 — Mutator-only 微基准

**目的**：证明 off-hot-path 的 throughput claim。三个配置：

| Config | 描述 |
|---|---|
| `vanilla` | AFL++ 自带 havoc/splice，不加载 FuzzPilot custom mutator |
| `fp-empty` | 加载 FuzzPilot mutator，但 recipe store 为空（测纯派发开销） |
| `fp-active` | 加载 FuzzPilot mutator + 一个已 promote 过的 recipe（来自 E1b） |

**驱动程序**：`tools/mutator_microbench/`（C++ ，独立可执行），喂 10 000 个 cjson 种子做 100 000 次 `afl_custom_fuzz()` 调用，计时。

**单次命令**：

```bash
FUZZPILOT_DOCKER_PLATFORM=linux/amd64 scripts/fuzzpilot_docker.sh run-batch --exp E3
```

**批命令**：

```bash
FUZZPILOT_DOCKER_PLATFORM=linux/amd64 scripts/fuzzpilot_docker.sh run-batch --exp E3
```

每个 config 跑 5 repeats，共 15 个 JSON 产物，每个文件包含 `{mean_ns_per_call, mean_exec_per_sec, stddev}`。

**完成判据**：每个 JSON 存在，`mean_exec_per_sec` 非零，`fp-empty` 与 `vanilla` 差距 ≤ 5%（如果不满足，论文的 throughput claim 要重写）。

### 2.6 实验 E4 — Case study 抽取

**目的**：从 E1b 的 5 个 run 里挑 1 个，把"plateau → proposal → recipe → micro-reward → promotion → 新 path"的完整链路抽出来。

**抽取脚本**：

```bash
FUZZPILOT_DOCKER_PLATFORM=linux/amd64 scripts/fuzzpilot_docker.sh aggregate case-study
```

**筛选规则**（写在 aggregate.py 里，固定不要手挑）：

1. 至少 1 个 plateau 触发了 model proposal
2. proposal `schema_valid == true`
3. micro-campaign reward > 0
4. promote 后 10 分钟内 main run 出现 ≥ 1 个新 path
5. 满足以上的 run 中，取 plateau 时间戳最小者（最早出现的清晰 case 最易讲）

无符合者：先扩 E1b 到 7 repeats，再不行就调高 plateau 灵敏度重跑 E1b。

### 2.7 实验 E5 — libpng smoke（可选）

仅当 E1–E4 提前完成时跑：

```bash
FUZZPILOT_DOCKER_PLATFORM=linux/amd64 scripts/fuzzpilot_docker.sh run-batch --exp E5 --repeats 1
```

数据进入 §9 作为「初步外推」的 1 个数据点，不进主表。

---

## 3. 产物 → 图表对应

| 论文图/表 | 数据来源 | 生成命令 |
|---|---|---|
| F3 cJSON coverage curves | E1a + E1b 的 coverage.csv | `plots.py f3` |
| F4 time-to-recover boxplot | E1a + E1b 的 events.jsonl plateau 段 | `plots.py f4` |
| F5 mutator microbench bars | E3 的 microbench/*.json | `plots.py f5` |
| T1 per-run summary | E1a/E1b/E2a/E2b 的 fuzzpilot.sqlite + fuzzer_stats | `aggregate.py t1` |
| T2 case study | E4 的输出 | `aggregate.py case-study` |
| T3 related-work matrix | 手填 markdown | 无 |

一键全跑：

```bash
FUZZPILOT_DOCKER_PLATFORM=linux/amd64 scripts/fuzzpilot_docker.sh aggregate all
FUZZPILOT_DOCKER_PLATFORM=linux/amd64 scripts/fuzzpilot_docker.sh plots all
```

---

## 4. 验收 checklist（按这个顺序勾）

- [ ] `preflight.sh` 全绿
- [ ] E1a 5 个 run 全部 `status=completed`
- [ ] E1b 5 个 run 全部 `status=completed` 且 `schema_valid_rate ≥ 0.7`
- [ ] E2a / E2b 各 3 个 run 全部 `status=completed`
- [ ] E3 15 个微基准 JSON 齐全；`fp-empty` 与 `vanilla` 差距 ≤ 5%
- [ ] E4 case study 命中候选
- [ ] `aggregate.py` 产出 `T1.md`、`case_study.json`、coverage 聚合 CSV
- [ ] `plots.py` 产出 F3/F4/F5 三个 PDF
- [ ] T3 related-work 表手填完毕
- [ ] `results/paper01_ai_recipe_mutation/validity_report.md` 重新生成且无缺失
- [ ] Git tag `paper01-arxiv-v1`
- [ ] 所有 `agent_decisions.jsonl` 经过 API-key 扫描脚本（`grep -E 'sk-[A-Za-z0-9]{20,}'` 必须为空）

全部勾上，就可以进入 arxiv 提交链路（见 §5）。

---

## 5. 从验收到提交 arxiv

1. 把 `results/paper01_ai_recipe_mutation/` 整目录打包：
   `tar czf paper01_artifacts_$(date +%Y%m%d).tar.gz results/paper01_ai_recipe_mutation/`
2. 把打包文件 SHA256 写进论文 §6 footnote
3. 把 figures/*.pdf 嵌入论文 tex 源
4. 把 tables/*.md 转成 LaTeX 表格
5. `arxiv` 提交：`cs.SE` 主类、`cs.CR` 交叉、license 选 CC-BY 4.0
6. 提交后立刻在 GitHub 打 tag `paper01-arxiv-v1` 并附上 arxiv ID
7. 把 arxiv ID 写回 README.md 顶部

---

## 6. 常见失败模式与处置

| 症状 | 可能原因 | 处置 |
|---|---|---|
| AFL++ 启动后立刻退出 | seed 解析失败 / 二进制不可执行 | `afl-fuzz -i seeds -o /tmp/out -- ./binary @@` 单独验证 |
| `schema_valid_rate` 很低（< 0.5） | prompt 漂移 / model temperature 过高 | 把 temperature 固定为 0.0；查 `agent_decisions.jsonl` 看 raw response |
| `plateau` 从不触发 | window/threshold 太宽松 | 调小 `plateau.window_sec` 或 `threshold_paths`，重跑 1 个 E1b run 试 |
| micro-campaign 总是 reward ≤ 0 | snapshot 损坏 / recipe op 集合太弱 | 检查 `snapshot-corpus` 输出是否完整；用 `lookup-recipe` 验证 |
| `coverage.csv` 行数过少 | telemetry collector 卡死 | 看 `events.jsonl` 末尾的 `telemetry_error`；通常重跑可解 |
| `fp-empty` 比 `vanilla` 慢 > 5% | mutator 派发路径有性能 bug | 先用 `perf` 抓火焰图再改；论文里只能改 throughput claim 措辞 |

---

## 7. 后续

E1–E5 全部通过后，进入 Paper 2 的基础设施开发（见 `paper02_when_to_intervene_plan.md` §"What needs to be built"）。

Paper 1 跑过的所有 run 在 Paper 3 中可作为「回归健康检查」复用——不要删除 `results/paper01_ai_recipe_mutation/` 目录。
