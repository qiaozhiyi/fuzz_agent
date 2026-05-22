# Paper 1 — 完整问题清单（Docker 化 / 平台无关 / 删 IDA）

写于决定"把 Paper 1 实验全扔进 Docker 跑"之前。这份清单**穷举**了所有阻塞或风险项，按对 Paper 1 跑批的紧急程度分级。每项给出：触点、根因、修法、工时。

> 上一份审计 `paper01_ghidra_and_x86_audit.md` 是初版；本文是穷举版，覆盖整个 repo。

---

## 0. 总览

| 级别 | 项数 | 说明 |
|---|---|---|
| 🔴 P0 阻塞 | 8 | 不修没法跑 Paper 1 实验，或跑出的数据失真 |
| 🟡 P1 影响一致性/可复现性 | 9 | 不修能跑但论文里要写 caveat 或 reviewer 会问 |
| 🟢 P2 清理项 | 6 | 不修不影响 Paper 1，但留着是技术债（IDA、文档、CI 等） |
| ✅ 已正确的事 | 7 | 列出来防止重复改 |

修复总工时估计：**4–6 小时工程 + 1 次端到端 Docker smoke 验证（~30 min）**。

---

## 1. 🔴 P0 阻塞项

### P0-1 — `AFL_MAP_SIZE: "4096"` 严重失真

| 项 | 内容 |
|---|---|
| 触点 | `experiments/targets/cjson/config.yaml:13`、`experiments/targets/libpng/config.yaml:13` |
| 对照 | `experiments/targets/vuln_target/config.yaml:20` 是 `65536` |
| 根因 | macOS 沙盒残留；x86_64 AFL++ 默认 65536；4096 让 cjson/libpng 这种真实目标 hash 冲突严重 |
| 后果 | `bitmap_cvg` 提前饱和、不同 edge 撞到同一 bucket、F3 曲线被压扁、F4 plateau 误判 |
| 修法 | cjson → 65536；libpng → 131072 |
| 工时 | 2 min |

### P0-2 — Seeds 极度匮乏

| 项 | 内容 |
|---|---|
| 触点 | `experiments/targets/cjson/seeds/seed1.json`（1 个文件）、`experiments/targets/libpng/seeds/seed1.png`（1 个文件） |
| 后果 | AFL++ 前 30–60 min 是 calibrate/queue-bootstrap，**4h budget 损失 25%**；baseline vs full-agent 前段差异被引导噪声主导 |
| 修法 | cjson 从 `experiments/targets/cjson/src/tests/`（submodule 内）抽 20–50 个；libpng 从 libpng 测试套件抽 ≥ 20 个不同色深/滤波器 |
| 工时 | 30 min（脚本化抽取） |

### P0-3 — `custom_mutator_path: "./build/..."` vs 实际 build dir

| 项 | 内容 |
|---|---|
| 触点 | 4 处 config（cjson/libpng/vuln_target + configs/examples/libpng.yaml）+ `include/fuzzpilot/config.hpp:47` 的 C++ 默认值，全写 `./build/...`；但本指南先前用 `build-cmake/` |
| 历史绕过 | `docker_ubuntu_smoke.sh` 用 `ln -s '$BUILD_DIR' /tmp/fuzzpilot-targets/build` |
| 修法 | **统一回归 `build/`**（影响最小）：把 `scripts/paper01/run_batch.sh` 和 `preflight.sh` 改用 `build/`；CMake 不需要改（仍可 `cmake -S . -B build`）|
| 工时 | 10 min |

### P0-4 — Submodule 没初始化

| 项 | 内容 |
|---|---|
| 触点 | `.gitmodules` 注册 cJSON + libpng；但 `experiments/targets/cjson/src/` 和 `experiments/targets/libpng/src/` 实际为空目录 |
| 后果 | `scripts/build_ubuntu_targets.sh` 在 `if [ -f "$src/cJSON.c" ]` 处 fall-through 到 `pkg-config libcjson`（apt 装的版本），就丧失了 commit 锁定能力，论文复现性受损 |
| 修法 | `Dockerfile` 里 `RUN git submodule update --init --recursive`；或在 `build_ubuntu_targets.sh` 里强制 fail |
| 工时 | 5 min |

### P0-5 — Docker 镜像不装 Ghidra/Java

| 项 | 内容 |
|---|---|
| 触点 | `docker/ubuntu/Dockerfile` |
| 后果 | `full-agent` 模式在容器里启动 Ghidra extractor 直接报错，退到 `static_analysis_error_json`，无法产出 §3 主张的 binary-aware prompt |
| 修法 | 加 `openjdk-17-jdk-headless` + 跑 `install_ghidra_ubuntu.sh`；镜像从 ~600 MB 涨到 ~3 GB，可接受 |
| 工时 | 30 min（含构建一次镜像验证） |

### P0-6 — Docker 镜像不装 Python 依赖（plot/aggregate）

| 项 | 内容 |
|---|---|
| 触点 | `docker/ubuntu/Dockerfile` 只装 `python3`，不装 `pandas`、`matplotlib`、`PyYAML` |
| 后果 | `run_batch.sh` 调 `python3 - <<PY ... yaml.safe_load ...` 会 ImportError；`plots.py` 也会 ImportError |
| 修法 | `pip install --break-system-packages pandas matplotlib pyyaml` 进 Dockerfile |
| 工时 | 5 min |

### P0-7 — Docker 镜像 AFL++ 版本不固定

| 项 | 内容 |
|---|---|
| 触点 | `docker/ubuntu/Dockerfile` 用 `apt-get install afl++` |
| 后果 | Ubuntu 22.04 apt 提供的 afl++ 是 4.04 左右，但论文 §6 必须固定到具体 commit；apt 版本随 base image 漂移 |
| 修法 | 改成 `git clone https://github.com/AFLplusplus/AFLplusplus.git && cd ... && git checkout v4.21c && make distrib && make install` |
| 工时 | 10 min（多一次构建时间） |

### P0-8 — cjson harness 没有 `__AFL_LOOP` 持久模式

| 项 | 内容 |
|---|---|
| 触点 | `experiments/targets/cjson/harness.c`（main 读 argv[1]，单次 parse）|
| 后果 | AFL++ 必须 fork-exec 每个 input，exec/sec 比 persistent mode 慢 10–100×。Paper 1 在等价 budget 下数据会非常差。 |
| 修法 | 加 `__AFL_LOOP(10000)` + stdin/`__AFL_FUZZ_INIT()` 改造，或最少用 `-Q`（qemu 模式禁用，太慢）。**推荐 persistent 模式改造**：30 行 C |
| 工时 | 20 min |

---

## 2. 🟡 P1 一致性 / 可复现性

### P1-1 — `configs/examples/libpng.yaml` 写死 macOS Homebrew 路径

```
configs/examples/libpng.yaml:14:  afl_fuzz: /opt/homebrew/bin/afl-fuzz
configs/examples/libpng.yaml:15:  afl_showmap: /opt/homebrew/bin/afl-showmap
```

**修法**：改回默认 `afl-fuzz`（依赖 PATH）。这个文件还是 CMake unit test 的输入（`fuzzpilot check-config --config configs/examples/libpng.yaml`），在 Linux CI 上必然 fail。

### P1-2 — README & docs/quickstart.md 写 `brew install`

```
README.md:120:brew install cmake ninja sqlite afl++ git
docs/quickstart.md:23:brew install cmake ninja sqlite afl++ git
```

**修法**：增加 Linux 段（apt-get / Dockerfile 一行）。保留 macOS 段作为 dev 路径。

### P1-3 — Ghidra extractor 的 x86 mnemonic 不全

```
scripts/ghidra/FuzzPilotGhidraExtract.java:184-186 cmp/test/subs/adds
```

`subs`、`adds` 是 ARM-only。x86 上的 `sub`、`add`、`and` 不在列表里 → branch_constraints 数偏少 → LLM prompt 信号密度低。

**修法**：把 mnemonic 列表按 ISA 分组，并联检测；或者只检测 `cmp/test/sub/add/and` 这几个跨 ISA 都有的根。

### P1-4 — Ghidra extractor decompiled 函数无大小限制

```
scripts/ghidra/FuzzPilotGhidraExtract.java:344  put decompiled C into JSON
```

5 个高危函数全文进 JSON。遇到大函数 prompt token 爆掉。

**修法**：每函数截到 4 KB；超过的写 `"truncated": true`。

### P1-5 — `m6_matrix` 与 `paper01_preprint.yaml` 不同步

m6 矩阵生成的模式列表包含 `no-static-analysis`、`random-recipe`、`random-reward`、`no-microcampaign`、`no-plateau`，但我的 manifest 只用了 4 个。

**修法**：把 Paper 1 manifest 加 `no-static-analysis`（×3 repeats），其余留给 Paper 2。

### P1-6 — `prepare_paper01_data.py` 与新 aggregate.py 重叠

旧的 `scripts/prepare_paper01_data.py`（550 行）和我新写的 `scripts/paper01/aggregate.py` 都从 `fuzzpilot.sqlite` 抽数据，但 schema 假设不一致（旧的假设 `paper_modes`、`micro_results` 表存在）。

**修法**：保留旧脚本（它生成 `validity_report.md`，是审稿人会问的口径），让 aggregate.py 调用它而不是自己写一遍。改用 `subprocess.run([..., "prepare_paper01_data.py", ...])`。

### P1-7 — CI 不安装 AFL++ 也不跑 ablation 烟雾测试

`.github/workflows/ci.yml` 只装 `cmake ninja sqlite3 libsqlite3-dev g++`，跑 ctest。所有 AFL++ 相关 smoke 在 CI 不会执行 → 平台移植 regression 没人挡。

**修法**：增加一个 job 用 docker buildx 构 `docker/ubuntu/Dockerfile`，跑 `scripts/docker_ubuntu_smoke.sh`。Paper 1 阶段可手动；正式版必须加。

### P1-8 — `tests/fixtures` 与新 paper 数据无关但被引用

`fuzzpilot_lookup_m2_seed_hash` 等测试是项目历史的 M2 阶段烟雾，与 Paper 1 无关。

**修法**：不动，保留作为回归测试。但写论文时要在 reproducibility 段说明 unit test 的 scope 与 paper data 的 scope。

### P1-9 — `scripts/install_ghidra_ubuntu.sh` 假设 `/opt/` 可写

容器里以 root 跑没问题；裸机非 root 用户跑会 sudo 失败。

**修法**：在 Dockerfile 里直接调；裸机用户的注意事项写在 docs/quickstart.md Linux 段。

---

## 3. 🟢 P2 清理项 / 删 IDA

按你的要求"统一 Ghidra，删 IDA"：

### P2-1 — IDA backend 代码删除清单

要删的文件：
- `scripts/ida_extractor.py`（207 行）

要改的文件：
- `include/fuzzpilot/config.hpp:98` 删 `std::filesystem::path ida_dir;`
- `src/config.cpp:220` 删 `else if (key == "ida_dir") ...`
- `src/config.cpp:297-298` 删 `if (value == "idalib") return "ida";`
- `src/config.cpp:518` 改成 `backend != "ghidra"`（拒绝任何 non-ghidra）
- `src/config.cpp:525-535` 整段 `if (backend == "ida")` 删
- `src/controller/run.cpp:113-128` 整段 IDA 分支删
- `experiments/targets/{cjson,libpng,vuln_target}/config.yaml` 删 `ida_dir: ""` 行
- `configs/examples/libpng.yaml:100` 删 `ida_dir: ""`
- `README.md:6, 112-113, 301` 改文案
- `docs/evaluation.md:14` 把 `IDA/Ghidra` 改 `Ghidra`

**工时**：30 min。**对 Paper 1 不阻塞**——IDA 路径反正在 Linux 上没人会启用——但你说要统一就一起做了。

### P2-2 — 文档 README.md 里的 `./build/` 全量统一

20+ 处引用，统一成 `build/`（与 CMakeLists 默认一致）。

### P2-3 — `docker/ubuntu/Dockerfile` 加 healthcheck / 测试入口

容器没有 ENTRYPOINT；当前完全靠 `docker_ubuntu_smoke.sh` 喂 bash -lc 长命令。**Paper 1 跑批 entrypoint** 应该是：

```dockerfile
ENTRYPOINT ["/usr/local/bin/fuzzpilot-paper01-entrypoint.sh"]
CMD ["preflight"]
```

加一个 `docker/ubuntu/entrypoint.sh` 包装 `preflight | run-batch | aggregate | plots` 三个动词。

### P2-4 — `m6_matrix` 与 Paper 1 manifest 双写

m6_matrix.cpp 用 C++ 生成矩阵；Paper 1 用 YAML manifest。是两套并行的事实源。Paper 3 时统一；Paper 1 不动。

### P2-5 — 历史 results/ 目录占了 git 空间

`results/ghidra_guidance_20260512_152038/`、`results/m6_arxiv_prelim/`、`results/paper01_ai_recipe_mutation/`（部分）已在 git 里。

**修法**：`results/*/runs/*/work/*` 加进 `.gitignore`，保留 metadata 文件。Paper 1 跑批前必须做，否则会污染提交。

### P2-6 — `.idea/` 目录被跟踪

JetBrains 项目元数据进了 git。Linux 容器跑批用不到。

**修法**：加进 `.gitignore`。

---

## 4. ✅ 已经正确的事

防止我们重复改坏：

- `src/runner/process_posix.cpp` 用 `posix_spawnp` + `sigaction` + `POSIX_SPAWN_SETPGROUP`，**Linux/macOS 共通**，不需要改
- `src/config.cpp:317-322` 的 `.dylib/.so/.dll` 后缀解析按平台自动选，**正确**
- `src/config.cpp:498` 有 `looks_like_macho_binary` 检测，**防呆**——在 Linux 上启动时会拒绝 macOS 编出的 binary
- `mutators/fuzzpilot/CMakeLists.txt:17` `if(APPLE) SUFFIX ".dylib"`，**正确**
- `mutators/fuzzpilot/CMakeLists.txt:24-37` 给非 Windows 创建 `libfuzzpilot_mutator`（无扩展名）符号链接，**AFL++ 自动加 `.so`/`.dylib` 都能命中**
- ablation 模式已全部在 `src/controller/run.cpp:411-461` 实现：`baseline-afl / rule-only / no-static-analysis / no-mutator / no-microcampaign / no-plateau / random-recipe / random-reward / edges-only / full-agent` —— **比我之前根据 `validity_report.md` 推断的多得多**，Paper 2 RQ1/RQ2 大部分轴不需要再开发 CLI
- `scripts/install_ghidra_ubuntu.sh` 把 Ghidra 装到 `/opt/ghidra`，与 yaml 配置一致

---

## 5. 修复执行顺序（按依赖排）

把所有 P0 修完，Paper 1 就能在 Docker 里一键跑。

```
1. P0-3  统一 build/ 路径（脚本 + preflight）              10 min
2. P0-1  改 AFL_MAP_SIZE（cjson 65536 / libpng 131072）    2 min
3. P0-2  扩 cjson / libpng seeds 到 ≥ 20                  30 min
4. P0-4  Dockerfile 加 submodule init                      5 min
5. P2-5/P2-6 .gitignore                                     5 min
6. P0-5  Dockerfile 加 JDK + Ghidra                       30 min
7. P0-6  Dockerfile 加 Python deps                         5 min
8. P0-7  Dockerfile 固定 AFL++ commit                     10 min
9. P0-8  cjson harness 改 persistent mode                  20 min
10. P1-1 修 configs/examples/libpng.yaml 路径              5 min
11. P1-3 Ghidra extractor x86 mnemonic                    15 min
12. P1-4 Ghidra extractor decompiled 截断                 10 min
13. P2-1 删 IDA 全部代码 + 文档                           30 min
14. P2-3 Dockerfile entrypoint                             20 min
15. P1-5 manifest 加 no-static-analysis ablation           5 min
16. P1-2 README / quickstart 加 Linux 段                  10 min
17. P1-6 aggregate.py 调 prepare_paper01_data.py 整合     20 min
                                                       =========
                                                       ~4 h 工程
```

最后 + 1 次 Docker smoke 验证（30 min）。

## 6. 推荐的 Docker 跑批新形态

Paper 1 跑批从 host 移到 Docker，host 只做 build & 启动。终态：

```bash
# host 上一次性 build 镜像
docker build -t fuzzpilot:paper01 -f docker/ubuntu/Dockerfile .

# 跑实验（mount 出 results）
docker run --rm \
  -v "$PWD/results:/work/fuzz_agent/results" \
  -e FUZZPILOT_MODEL_API_KEY="$FUZZPILOT_MODEL_API_KEY" \
  fuzzpilot:paper01 \
  paper01 --exp E3 E1a E1b E2a E2b
```

`paper01` 是新的 docker entrypoint，包装 preflight + run_batch + aggregate + plots。`--exp` 接受多个，串行跑完。

**这样跨平台是真正解决的**：host 只要有 docker 就够（macOS / Linux / Windows-WSL2 全部一样）。Ghidra/Java/AFL++/Python deps 全在镜像里固定。

---

## 7. 接下来要不要直接动手

我建议按本清单的"修复执行顺序" 1–10 项**全部修了**（约 1.5 小时实际工程），然后 11–17 是延后项。

如果你说"开干"，我会按上述顺序依次改文件并报进度。你只需要在最后看 docker smoke 通过就行。
