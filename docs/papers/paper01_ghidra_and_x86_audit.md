# Paper 1 — Ghidra 模块作用审计 & x86_64 移植风险报告

写于动手跑 Paper 1 实验之前。把两个我此前一笔带过的问题翻出来认真看了一遍。

---

## Part 1 — Ghidra 模块到底做了什么

我把 `scripts/ghidra/FuzzPilotGhidraExtract.java`、`src/controller/run.cpp` 里的 `run_static_extractor` / `generate_dict_from_static_json` / 主循环里所有 `static_context_json` 的引用都通读了一遍。结论比 README 的描述要朴素得多。

### 1.1 实际工作流

每次 `fuzzpilot run` 启动时（不是每次 plateau）：

1. 如果 `static_analysis.enabled = true`，控制器调用 `analyzeHeadless` 一次性把目标二进制喂给 Ghidra：
   - 项目目录：`<run_dir>/ghidra_project`
   - 脚本：`scripts/ghidra/FuzzPilotGhidraExtract.java`
   - 输出：`<run_dir>/static_context.json`
2. Ghidra 脚本从二进制里抽四类东西，全部受硬上限约束：
   - **危险函数 top 20**：按对 `gets/system/popen/strcpy/strcat/sprintf/malloc/free/memcpy/memmove/printf` 的调用打分（10/5/5/4/3/2 分制），排序后取前 20 个
   - **magic_tokens ≤ 150 个**：来源是 `getDefinedData()` 的 String 类型 + 4 字节 ASCII 常量（既试 little-endian 又试 big-endian），过滤掉 `afl/cmplog/forkserver/asan/...` 这类运行时噪声
   - **constants ≤ 100 个**：指令操作数里出现的 Scalar
   - **branch_constraints ≤ 150 条**：每个条件跳转往前回看 4 条指令，找紧邻的 cmp/test 对，记 `(addr, condition, comparison, op1, op2)`
3. 高危函数 top 5 还会被反编译成 C 代码塞进 JSON

### 1.2 这份 JSON 在闭环里的两个去处

**去处 A：生成 AFL++ 字典**（`generate_dict_from_static_json`）

- 从 `magic_tokens` + 兼容的 legacy `extracted_strings` 字段抽字符串
- 长度 3–32、纯 printable、过滤同样的噪声黑名单
- 上限 200 条，写成 `<run_dir>/static_generated.dict`
- 这份 dict **只喂 micro-campaign**（`build_micro_afl_spec(config, spec, static_dict_path)`），不喂主 campaign
- 也就是说：Ghidra 对主 AFL++ run 的字典 **零影响**

**去处 B：塞进 blackboard 给 LLM**

- `combined_intel` 就是 `static_context.json` 的内容（或缓存的 `base_intelligence.json`）
- 拼进 `plateau_blackboard_json`，跟最近的 telemetry / agent_memory / 之前的 decisions 一起序列化给 model gateway
- LLM 看到 top 函数名、危险函数 decompiled C 代码、constants、branch 约束这些线索后，才能"针对性"提议 recipe（比如：看到一个 `strcpy` 调用紧跟某个 `cmp r/0x7b`，提议在 mutation 时 insert `'{'`）

### 1.3 因此 Ghidra 的真实贡献是什么

- **不是** 指导主 mutator 的 op 选择（recipe-guided mutator 完全独立）
- **不是** 给主 AFL++ 喂字典
- **是** 给 LLM 一个含 binary-aware 信号的 prompt context（去处 B）
- **是** 给 micro-campaign 一份候选字典（去处 A，影响 ≤ K 个短窗口）

换言之，关掉 Ghidra 主要削弱的是 **LLM proposal 的针对性** 与 **micro-campaign 的 dict 覆盖**——而不是 mutator 的命中率。

### 1.4 这件事对 Paper 1 的影响

1. **三大 pillar 的写法要修订**。我之前 outline 写的"recipe abstraction + native mutator + audit trail"忽略了 Ghidra 实际上是 LLM prompt 的 **input source**。如果不澄清，reviewer 会问"那 no-static-analysis ablation 究竟改变了什么"。
2. **`no-static-analysis` ablation 的解释要明确**：它只是把 base_intelligence_json 锁成 `{}`，让 LLM 在没有 binary 知识的情况下"瞎猜"。这是一个合法的 ablation，但措辞要小心，不要让读者以为它影响了 mutator。
3. **Paper 1 是否需要 `no-static-analysis` ablation？** 现在 `paper01_preprint.yaml` 里没有它（我只放了 baseline / full-agent / rule-only / no-mutator）。**建议加进去**：1 个 target × 3 repeats × 4h = 12 核时，几乎零边际成本，但能填上 §3 关于"static analysis 起什么作用"的空白。
4. **arch 偏差**：`FuzzPilotGhidraExtract.java` 里的分支 mnemonic 检测对 x86 (`j*`) 和 ARM (`b.*` / `cbz` / `cbnz` / `tbz`) 都有覆盖；但紧邻 cmp 的匹配字符串混了两套（`cmp/test/subs/adds`），其中 `subs`/`adds` 是 ARM-only。**x86 上 branch_constraints 数量会偏少**，进而 LLM 在 x86 上的 prompt 信号比 macOS arm64 上更弱。这是 Paper 1 必须在 §9 提的 cross-arch 一致性 caveat。
5. **同一 prompt 在两个 arch 上不可复现**。Ghidra 反编译 ARM Mach-O vs ELF x86 出的 C 代码大不一样；LLM 看到的 prompt 也不一样；`agent_decisions.jsonl` 的 schema_valid_rate / fallback_rate 在 cross-arch 上没有可比性。**Paper 1 的所有数字必须只来自 Linux x86_64**——这条已经在 evaluation.md 写了，但我之前的 outline 没把这条放到 §6 一开始就强调。

### 1.5 给 Ghidra 模块开发的建议（独立于本次 paper）

- 把 `subs`/`adds` 拆到 ARM-specific 路径，x86 增加 `cmp/test/sub/add/and` 的检测，避免 cross-arch 偏差扩大
- branch_constraints 同时记录 ISA，方便后续诊断
- 把 decompiled C 的长度限制写进 schema（目前没限制单函数大小，对极大函数 prompt 会爆 token）

---

## Part 2 — x86_64 Linux 移植的真实坑（按严重度排序）

我比对了 `docker/ubuntu/Dockerfile`、`scripts/docker_ubuntu_smoke.sh`、`scripts/build_ubuntu_targets.sh`、三个 target 的 `config.yaml`、`results/ghidra_guidance_20260512_152038/` 下历史 Linux 跑批留下的 `main_launch.sh`，对照了 macOS 现有状态。下面是会真的让 Paper 1 数据失真的问题。

### 🔴 P0 - 必须修，否则 Paper 1 数据不能用

#### P0-1：`AFL_MAP_SIZE: "4096"` 对 cjson/libpng 太小

```text
experiments/targets/cjson/config.yaml:13:    AFL_MAP_SIZE: "4096"
experiments/targets/libpng/config.yaml:13:    AFL_MAP_SIZE: "4096"
experiments/targets/vuln_target/config.yaml:20: AFL_MAP_SIZE: "65536"   ← 对照组
```

AFL++ 在 x86_64 / ELF 上的默认 map 大小是 65536。4096 对一个一次性玩具（vuln_target 都用 65536！）也许够用，但对 cjson、libpng 这种实际目标会 **严重 hash 冲突**：

- `bitmap_cvg` 数值偏高且失真（提前饱和）
- 不同 edge 互相打到同一个 bucket，新 path 检测漏掉
- 论文里所有 coverage 曲线、F3 的曲线形状会被压扁

**修法**：cjson 改 `AFL_MAP_SIZE: "65536"`，libpng 改 `AFL_MAP_SIZE: "131072"`（大目标）。这条修完 + 重跑 AFL++ instrumentation 才有意义。

#### P0-2：seeds 极度匮乏

```text
experiments/targets/cjson/seeds/seed1.json     ← 1 个文件
experiments/targets/libpng/seeds/seed1.png     ← 1 个文件
```

AFL++ 从 1 个 seed 起步会经历漫长的 calibrate + queue 引导阶段，前 30–60 分钟数据非常嘈杂，对 4 小时 budget 是 ~25% 的开销。直接后果：

- baseline 的 coverage-over-time 曲线前半段抖动巨大
- F4 的 plateau 触发时间会被这段噪声主导
- full-agent 看似的"快速恢复"可能其实只是 seed 引导阶段差异

**修法**：

- cjson：从 `experiments/targets/cjson/src/tests/`（如果存在）或上游 cJSON repo 拿 20–50 个有代表性的 JSON 样例（包括 `{}`、`[]`、嵌套对象、字符串、数字、unicode escape）
- libpng：从 libpng 测试套件 + 几个不同色彩深度 / 滤波器的小 PNG，至少 20 个

#### P0-3：`custom_mutator_path: "./build/..."` 路径错误

```text
experiments/targets/cjson/config.yaml:36: custom_mutator_path: "./build/mutators/fuzzpilot/libfuzzpilot_mutator"
experiments/targets/libpng/config.yaml:36: 同上
experiments/targets/vuln_target/config.yaml:38: 同上
configs/examples/libpng.yaml:36: 同上
```

但我们用的 build dir 是 `build-cmake/`，不是 `build/`。docker smoke 脚本通过 `ln -s '$BUILD_DIR' build` 绕开了这个，但 **我们的 `scripts/paper01/run_batch.sh` 没做这个软链接**。直接跑会 AFL++ 报"failed to dlopen custom mutator"。

**修法**（任选其一）：

A. 改 config，把 `custom_mutator_path` 改成绝对路径或 `./build-cmake/...`
B. 在 `preflight.sh` 里自动 `ln -sfn build-cmake build`（最省事，但污染 repo 根目录）
C. 在 `run_batch.sh` 里给每个 run 用 `--work-dir` 切到 build-cmake 那条路径

我推荐 A：把 target config 改成 `${REPO_ROOT}/build-cmake/...`，并在 preflight 里检查文件存在。

### 🟡 P1 - 影响可重现性 / 跨平台一致性

#### P1-1：Docker 镜像不装 Ghidra

`docker/ubuntu/Dockerfile` 装了 afl++、cmake、libcjson-dev、libpng-dev，但没有 Java JDK 和 Ghidra。也就是说在容器里直接跑 `full-agent` 会因为 Ghidra extractor 启动失败而退到 `static_analysis_error`。

历史 Linux run（`results/ghidra_guidance_20260512_152038/`）能跑出 Ghidra context，说明用的是裸机 + `install_ghidra_ubuntu.sh`，不是 docker。

**修法**：

- 路径 A：在 `docker/ubuntu/Dockerfile` 末尾加 `RUN apt-get install -y openjdk-17-jdk-headless` + 跑 `scripts/install_ghidra_ubuntu.sh`，把镜像大小推到 ~3 GB，但论文复现链路一条命令搞定
- 路径 B：保持 docker 镜像精简，复现说明里说明"full-agent 模式需要主机 Ghidra"
- preflight.sh 增加 `[ghidra]` 检查项（我现在的版本漏了）

#### P1-2：Linux AFL++ 包版本不可控

`apt-get install afl++` 拿到的版本随发行版漂移，paper 必须固定到具体 commit/tag。

**修法**：Dockerfile 改成从源码 clone 一个 pinned commit / tag 编译 AFL++，并把版本写进 `run_metadata.json` 的 `afl_version`。capture_run_metadata.sh 已经在写这一项了，要做的是固定版本而不是相信发行版。

#### P1-3：macOS 开发产物会污染 Linux 跑批

如果开发者在 macOS 上 `cmake --build` 过，`build-cmake/mutators/fuzzpilot/libfuzzpilot_mutator.dylib` 是 Mach-O；在同一 checkout 切到 Linux 容器二次构建时如果不 `rm -rf build-cmake`，会出诡异 link 错误。

**修法**：preflight.sh 增加 `file build-cmake/fuzzpilot | grep ELF` 检查。

#### P1-4：Branch constraint 抽取在 x86 上偏弱

见 1.4 的第 4 点。这不会让程序崩，但会让 x86 上的 LLM prompt 比 ARM 上信息密度低一点。Paper 1 的 §9 里我会把它写成 known limitation。

### 🟢 P2 - 可接受但要在 README / paper 中注明

- `AFL_SKIP_CPUFREQ=1` 在 Linux 上只抑制启动告警，没坏处；保留即可
- `afl_fuzz: "afl-fuzz"`（默认）依赖 PATH；preflight 已经检查了
- `ghidra_home: "/opt/ghidra"` 是 Linux 约定路径，与 `install_ghidra_ubuntu.sh` 默认一致；macOS 上需要单独设
- `m6_matrix` 生成器的输出格式与本 paper 无关，不修

---

## Part 3 — 修复优先级 & 行动清单

按"修了能立刻进 Paper 1 实验"排序：

| # | 项 | 修法 | 工时 |
|---|---|---|---|
| 1 | cjson AFL_MAP_SIZE → 65536 | 改 yaml 1 行 | 1 min |
| 2 | libpng AFL_MAP_SIZE → 131072 | 改 yaml 1 行 | 1 min |
| 3 | cjson 扩 seeds 到 ≥ 20 个 | 从 cJSON 上游测试套件抽 | 30 min |
| 4 | libpng 扩 seeds 到 ≥ 20 个 | 从 libpng 测试套件 + Mozilla pngtest | 30 min |
| 5 | 三个 target config 的 custom_mutator_path 改绝对 / build-cmake | 改 yaml 1 行 × 3 | 5 min |
| 6 | preflight.sh 增加 ghidra / java / mutator-arch 检查 | 加 ~30 行 shell | 20 min |
| 7 | Paper 1 加 `no-static-analysis` ablation 进 manifest | 改 manifest + 跑 3 个 4h run | 12 核时 |
| 8 | docker/ubuntu/Dockerfile 装 JDK + Ghidra（可选） | 加 ~10 行 | 30 min |
| 9 | Ghidra extractor 修 x86 mnemonic（可选，对 Paper 1 不阻塞） | 改 Java 几行 | 15 min |

总工程时间 ~1.5 小时，新加 12 核时实验。**1–6 是 Paper 1 阻塞项**，没做完不要跑 E1a/E1b。

---

## Part 4 — 对此前 Paper 1 文档的勘误

需要回头改的：

1. **`paper01_arxiv_placeholder.md`** §3 "三大支柱" 句式：要在 "audit trail" 旁边补一句 "static analysis serves as LLM prompt context, not a mutator input"。否则审稿人会问 "no-static-analysis 究竟在改什么"。
2. **`paper01_arxiv_placeholder.md`** §6 RQ 列表：增加一个隐式 RQ "Does static-analysis-derived prompt context measurably affect proposal quality?"，对应新的 `no-static-analysis` ablation。
3. **`paper01_experiment_plan.md`** 和 **`paper01_experiments_runbook.md`** 的实验矩阵：加 E2c = no-static-analysis × 3 repeats × 4h。
4. **`experiments/manifests/paper01_preprint.yaml`**：加 E2c 段。
5. **`scripts/paper01/preflight.sh`**：加 Ghidra、Java、mutator ELF 检查。
6. **三个 target `config.yaml`**：改 AFL_MAP_SIZE 和 custom_mutator_path。
7. **新增 `experiments/targets/cjson/seeds/` 内容**（≥ 20 文件）。

我接下来会一项一项落地。如果你只想抓最关键的两项："改 AFL_MAP_SIZE + 扩 seeds + 改 custom_mutator_path" 是三个必修；其余可以延后。

---

## 附录 — 我读过的关键文件与行号

- `scripts/ghidra/FuzzPilotGhidraExtract.java:1-447`
- `src/controller/run.cpp:108-175` (`run_static_extractor`)
- `src/controller/run.cpp:178-300` (`generate_dict_from_static_json`)
- `src/controller/run.cpp:735-760` (initial static scan)
- `src/controller/run.cpp:881-930` (combined_intel → blackboard)
- `src/controller/run.cpp:989` (micro-campaign 消费 static_dict_path)
- `src/agents/agent_runtime.cpp:42, 238` (prompt 引用 static_context)
- `docker/ubuntu/Dockerfile`（确认无 Ghidra）
- `scripts/docker_ubuntu_smoke.sh:24-35` （`ln -s build` workaround 的来源）
- `scripts/build_ubuntu_targets.sh:1-60`
- `experiments/targets/{cjson,libpng,vuln_target}/config.yaml`
- `results/ghidra_guidance_20260512_152038/.../main_launch.sh`（确认 Linux 历史跑批配置）
