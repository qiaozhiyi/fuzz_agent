# FuzzPilot 完整实验方案
## 目标：验证 full-agent 在非饱和目标上有效

**制定日期**：2026-05-24  
**预计完成时间**：6-8 周  
**目标**：解决论文的 6 个关键问题，使其适合投稿顶会

---

## 一、当前问题分析

### 论文现状的 6 个关键问题

| 问题 | 当前状态 | 影响 | 解决方案 |
|------|---------|------|---------|
| **P1: 单一饱和目标** | 只测试了 cJSON（饱和） | 无法证明架构在非饱和目标上的价值 | 增加 libxml2/sqlite3/openssl 三个非饱和目标 |
| **P2: LLM 层零贡献** | 0/20 提升率，gate 未被测试 | 论文主打 LLM 控制，但 LLM 完全没用 | 在非饱和目标上证明 LLM 层有正向贡献 |
| **P3: 归因是假设** | 缺少 controller-only 消融 | 核心结论"controller 是主要驱动力"无法验证 | 补充 controller-only 消融实验 |
| **P4: 统计功效不足** | N=5 主实验，N=3 消融 | p=0.42，无法声称显著性 | 增加到 N≥20 主实验，N≥10 消融 |
| **P5: 泛化性缺失** | 只有 1 个目标 | 无法声称架构适用于其他目标 | 至少 3-4 个不同复杂度的目标 |
| **P6: 公平性基线不足** | cmplog 比 FuzzPilot 好（270 vs 269 边） | 负面结果削弱贡献 | 在非饱和目标上证明 FuzzPilot 的优势轴 |

---

## 二、完整实验矩阵

### 2.1 目标选择（4 个目标，覆盖不同复杂度）

| 目标 | 类型 | 复杂度 | 预期饱和时间 | 选择理由 |
|------|------|--------|------------|---------|
| **cJSON** | JSON parser | 低（269 边） | ~2500s | 已有数据，作为饱和目标对照 |
| **libxml2** | XML parser | 中（~6000+ 边） | >14400s | 非饱和，结构化文本，适合 LLM |
| **sqlite3** | SQL engine | 高（~20000+ 边） | >14400s | 非饱和，复杂语法，适合 LLM |
| **openssl_x509** | X.509 parser | 中（~8000+ 边） | >14400s | 非饱和，二进制格式，测试边界 |

### 2.2 实验模式（7 个模式）

#### 主实验（E1）：验证 full-agent 有效性
| 模式 | 描述 | 样本量 | 时长 | 目的 |
|------|------|--------|------|------|
| **baseline-afl** | 纯 AFL++，无任何增强 | N=20 | 14400s | 基线对照 |
| **full-agent** | 完整 FuzzPilot（controller + mutator + LLM + Ghidra） | N=20 | 14400s | 验证完整系统 |

#### 消融实验（E2）：验证归因假设
| 模式 | 描述 | 样本量 | 时长 | 目的 |
|------|------|--------|------|------|
| **controller-only** | 只有 controller（高原检测 + 语料重组），无 mutator/LLM/Ghidra | N=10 | 14400s | **最关键**：验证 controller 是主要驱动力 |
| **rule-only** | controller + mutator（默认规则配方），无 LLM | N=10 | 14400s | 验证 LLM 层的增量贡献 |
| **no-mutator** | controller + LLM，无 recipe-guided mutator | N=10 | 14400s | 验证 mutator 的贡献 |
| **no-static-analysis** | controller + mutator + LLM，无 Ghidra | N=10 | 14400s | 验证 Ghidra 的贡献 |

#### 公平性基线（E4）：与现有技术对比
| 模式 | 描述 | 样本量 | 时长 | 目的 |
|------|------|--------|------|------|
| **baseline-afl-cmplog** | AFL++ + cmplog 插桩 | N=10 | 14400s | 公平对比：覆盖率轴 |
| **baseline-afl-dict** | AFL++ + Ghidra 字典 | N=10 | 14400s | 公平对比：静态分析轴 |

### 2.3 完整实验矩阵

**总实验数**：4 目标 × (2 主实验 + 4 消融 + 2 公平性) = **32 个实验组**

**总 run 数**：
- 主实验：4 目标 × 2 模式 × 20 重复 = 160 runs
- 消融实验：4 目标 × 4 模式 × 10 重复 = 160 runs
- 公平性基线：4 目标 × 2 模式 × 10 重复 = 80 runs
- **总计：400 runs**

**总机时**：400 runs × 14400s = 5,760,000s ≈ **1600 小时 ≈ 67 天**

---

## 三、分阶段执行计划

### Phase 1：验证非饱和目标（2 周）
**目标**：证明 libxml2 不是饱和目标，full-agent 有提升

| 实验 | 目标 | 模式 | N | 优先级 |
|------|------|------|---|--------|
| 1.1 | libxml2 | baseline-afl | 5 | P0（已在跑，4/5 完成） |
| 1.2 | libxml2 | full-agent | 5 | P0 |
| 1.3 | libxml2 | controller-only | 3 | P0 |
| 1.4 | sqlite3 | baseline-afl | 5 | P1 |
| 1.5 | sqlite3 | full-agent | 5 | P1 |

**成功标准**：
- libxml2 或 sqlite3 的 full-agent 在覆盖率或高原时间上**显著优于** baseline（p<0.05）
- LLM gate 提升率 > 0（至少有 1 个 LLM 提案被提升）
- controller-only 的性能介于 baseline 和 full-agent 之间

**如果失败**：
- 如果 full-agent 还是没用 → 论文改成"负面结果论文"（方案C）
- 如果 controller-only 和 full-agent 一样好 → 论文改成"controller-centric"（方案A）

---

### Phase 2：完成主实验（4 周）
**目标**：增加样本量到 N=20，达到统计显著性

| 实验组 | 目标 | 模式 | 当前 N | 目标 N | 需补充 |
|--------|------|------|--------|--------|--------|
| E1 主实验 | cJSON | baseline-afl | 5 | 20 | +15 |
| E1 主实验 | cJSON | full-agent | 5 | 20 | +15 |
| E1 主实验 | libxml2 | baseline-afl | 5 | 20 | +15 |
| E1 主实验 | libxml2 | full-agent | 5 | 20 | +15 |
| E1 主实验 | sqlite3 | baseline-afl | 5 | 20 | +15 |
| E1 主实验 | sqlite3 | full-agent | 5 | 20 | +15 |
| E1 主实验 | openssl_x509 | baseline-afl | 0 | 20 | +20 |
| E1 主实验 | openssl_x509 | full-agent | 0 | 20 | +20 |

**总 runs**：4 目标 × 2 模式 × 20 = 160 runs  
**预计时间**：160 runs ÷ 4 并行 = 40 批次 × 4 小时 = **160 小时 ≈ 7 天**

---

### Phase 3：完成消融实验（2 周）
**目标**：验证归因假设，达到 N=10

| 实验组 | 目标 | 模式 | 当前 N | 目标 N | 需补充 |
|--------|------|------|--------|--------|--------|
| E2a | cJSON | rule-only | 3 | 10 | +7 |
| E2b | cJSON | no-mutator | 3 | 10 | +7 |
| E2c | cJSON | no-static-analysis | 3 | 10 | +7 |
| **E2d（新）** | cJSON | **controller-only** | 0 | 10 | +10 |
| E2a-d | libxml2 | 4 消融模式 | 0 | 10 | +40 |
| E2a-d | sqlite3 | 4 消融模式 | 0 | 10 | +40 |
| E2a-d | openssl_x509 | 4 消融模式 | 0 | 10 | +40 |

**总 runs**：4 目标 × 4 消融 × 10 = 160 runs  
**预计时间**：160 runs ÷ 4 并行 = 40 批次 × 4 小时 = **160 小时 ≈ 7 天**

---

### Phase 4：公平性基线（1 周）
**目标**：与 cmplog 和 dict 对比

| 实验组 | 目标 | 模式 | N |
|--------|------|------|---|
| E4a | 4 目标 | baseline-afl-cmplog | 10 |
| E4b | 4 目标 | baseline-afl-dict | 10 |

**总 runs**：4 目标 × 2 模式 × 10 = 80 runs  
**预计时间**：80 runs ÷ 4 并行 = 20 批次 × 4 小时 = **80 小时 ≈ 3.5 天**

---

## 四、资源需求

### 4.1 计算资源
- **服务器**：fuzz-server (154.201.73.67)
- **并行度**：4 个 AFL 实例同时运行（当前配置）
- **总机时**：1600 小时 ≈ 67 天墙钟时间（4 并行）
- **存储**：每个 run ~500MB，400 runs ≈ 200GB

### 4.2 时间线（6-8 周）
```
Week 1-2:  Phase 1 - 验证非饱和目标（关键里程碑）
Week 3-4:  Phase 2 - 完成主实验（N=20）
Week 5-6:  Phase 3 - 完成消融实验（N=10）
Week 7:    Phase 4 - 公平性基线
Week 8:    数据分析 + 论文改写
```

### 4.3 关键里程碑
| 里程碑 | 时间 | 成功标准 | 风险缓解 |
|--------|------|---------|---------|
| **M1: 验证 full-agent 有用** | Week 2 | libxml2 full-agent > baseline (p<0.05) | 如果失败，改论文定位 |
| **M2: 验证 controller 归因** | Week 2 | controller-only 性能介于 baseline 和 full-agent 之间 | 如果失败，改归因结论 |
| **M3: 达到统计显著性** | Week 4 | N=20，p<0.05 | 如果不显著，增加 N 或改用效应量 |
| **M4: 多目标泛化** | Week 6 | 至少 2/3 非饱和目标上 full-agent 有提升 | 如果失败，限定适用范围 |

---

## 五、验证 full-agent 有效性的标准

### 5.1 主要指标（Primary Metrics）
| 指标 | 测量方法 | 成功标准 |
|------|---------|---------|
| **覆盖率提升** | final_edges (full-agent vs baseline) | Δ > 5% 且 p<0.05 |
| **高原缩短** | plateau_time (full-agent vs baseline) | Δ > 20% 且 p<0.05 |
| **LLM 提升率** | promoted_recipes / total_proposals | > 10% (至少 2/20 被提升) |

### 5.2 次要指标（Secondary Metrics）
| 指标 | 测量方法 | 成功标准 |
|------|---------|---------|
| **吞吐量持平** | execs_per_sec (full-agent vs baseline) | > 0.9x (不超过 10% 损失) |
| **崩溃发现** | unique_crashes (full-agent vs baseline) | ≥ baseline |
| **Gate 准确率** | promoted_good / (promoted_good + promoted_bad) | > 70% |

### 5.3 归因验证（Attribution Validation）
| 假设 | 验证方法 | 成功标准 |
|------|---------|---------|
| **H1: controller 是主要驱动力** | controller-only vs baseline | controller-only 显著优于 baseline |
| **H2: LLM 层有增量贡献** | full-agent vs rule-only | full-agent 显著优于 rule-only |
| **H3: mutator 有正向贡献** | full-agent vs no-mutator | full-agent ≥ no-mutator |
| **H4: Ghidra 有正向贡献** | full-agent vs no-static-analysis | full-agent ≥ no-static-analysis |

---

## 六、实验执行清单

### 6.1 Phase 1 立即启动（本周）
- [ ] **P0-1**: libxml2 baseline-afl r05（补齐到 N=5）
- [ ] **P0-2**: libxml2 full-agent r01-r05（新实验）
- [ ] **P0-3**: libxml2 controller-only r01-r03（新实验）
- [ ] **P1-1**: sqlite3 baseline-afl r01-r05（新实验）
- [ ] **P1-2**: sqlite3 full-agent r01-r05（新实验）

**预计完成**：2026-06-07（2 周后）

### 6.2 Phase 2 主实验扩展（Week 3-4）
- [ ] cJSON 主实验补充到 N=20（+30 runs）
- [ ] libxml2 主实验补充到 N=20（+30 runs）
- [ ] sqlite3 主实验补充到 N=20（+30 runs）
- [ ] openssl_x509 主实验 N=20（+40 runs）

**预计完成**：2026-06-21（4 周后）

### 6.3 Phase 3 消融实验（Week 5-6）
- [ ] cJSON 消融补充到 N=10（+21 runs）
- [ ] cJSON controller-only N=10（+10 runs）
- [ ] libxml2/sqlite3/openssl 消融 N=10（+120 runs）

**预计完成**：2026-07-05（6 周后）

### 6.4 Phase 4 公平性基线（Week 7）
- [ ] 4 目标 × cmplog N=10（+40 runs）
- [ ] 4 目标 × dict N=10（+40 runs）

**预计完成**：2026-07-12（7 周后）

---

## 七、数据分析计划

### 7.1 统计检验
- **主实验**：Mann-Whitney U test (N=20, α=0.05)
- **消融实验**：Kruskal-Wallis + post-hoc Dunn test (N=10, α=0.05)
- **效应量**：Vargha-Delaney A12（报告所有对比）

### 7.2 可视化
- **覆盖率曲线**：4 目标 × 7 模式的时间序列图
- **高原时间箱线图**：4 目标 × 7 模式的分布对比
- **消融实验瀑布图**：展示每个组件的增量贡献
- **LLM 提升率热力图**：4 目标 × 20 提案的提升矩阵

### 7.3 关键表格
- **Table 1**：4 目标的基本特征（边数、饱和时间、复杂度）
- **Table 2**：主实验结果（覆盖率、高原时间、p 值、A12）
- **Table 3**：消融实验结果（验证 4 个假设）
- **Table 4**：LLM gate 统计（提升率、准确率、奖励分布）

---

## 八、论文改写计划（Week 8）

### 8.1 如果 Phase 1 成功（full-agent 在非饱和目标上有用）
**标题保持**：FuzzPilot: An Architecture for Off-Hot-Path LLM Control...

**Abstract 改写**：
- 删除"Critical scope limitation"段落
- 改为："We evaluate on four targets (cJSON, libxml2, sqlite3, openssl_x509) with N=20 repetitions per mode."
- 强调："On non-saturated targets (libxml2, sqlite3), full-agent shows significant improvements..."

**贡献不变**：C1-C4 保持，但删除所有"未被测试"的警告

**实验章节**：
- RQ1：报告 4 目标的结果，强调非饱和目标的提升
- RQ3：报告 LLM gate 在非饱和目标上的提升率
- RQ4：报告完整消融实验，验证归因假设

**Limitations 缩减**：
- 删除"LLM 层未被测试"
- 删除"归因是假设"
- 保留"需要更多目标验证泛化性"

### 8.2 如果 Phase 1 失败（full-agent 还是没用）
**改用方案 C**：负面结果论文

**标题改为**：When LLM Control Fails: Lessons from Fuzzing Saturated and Non-Saturated Targets

**重点**：
- 强调这是重要的负面结果
- 分析为什么 LLM 层在多个目标上都失败
- 提出 controller-only 作为替代方案

---

## 九、风险与缓解

| 风险 | 概率 | 影响 | 缓解措施 |
|------|------|------|---------|
| **R1: full-agent 在非饱和目标上还是没用** | 中 | 高 | 改论文定位为负面结果或 controller-centric |
| **R2: 服务器资源不足** | 低 | 高 | 租用云服务器或延长时间线 |
| **R3: 实验中途崩溃** | 中 | 中 | 每天备份结果，使用 tmux 保持会话 |
| **R4: 统计功效还是不足** | 低 | 中 | 增加 N 或改用贝叶斯分析 |
| **R5: 时间超出预期** | 高 | 中 | 优先完成 Phase 1，其他阶段可延后 |

---

## 十、立即行动项（本周）

### 今天（2026-05-24）
1. ✅ 确认 libxml2 baseline r01-r04 正在运行
2. [ ] 准备 libxml2 full-agent 配置文件
3. [ ] 准备 libxml2 controller-only 配置文件
4. [ ] 准备 sqlite3 baseline 和 full-agent 配置文件

### 明天（2026-05-25）
1. [ ] 启动 libxml2 full-agent r01-r05
2. [ ] 启动 libxml2 controller-only r01-r03

### 本周末（2026-05-26）
1. [ ] 检查 libxml2 baseline 是否完成
2. [ ] 启动 sqlite3 baseline r01-r05

### 下周一（2026-05-27）
1. [ ] 分析 libxml2 baseline 结果
2. [ ] 如果 full-agent 还在跑，检查中间结果
3. [ ] 准备 Phase 1 里程碑报告

---

## 十一、成功标准总结

**Phase 1 成功标准（2 周后）**：
- ✅ libxml2 或 sqlite3 的 full-agent 覆盖率 > baseline（p<0.05）
- ✅ LLM gate 提升率 > 0（至少 1/20 被提升）
- ✅ controller-only 性能介于 baseline 和 full-agent 之间

**如果 Phase 1 成功 → 继续 Phase 2-4，6-8 周后投稿顶会**  
**如果 Phase 1 失败 → 改论文定位，2 周内投稿 arXiv**

---

## 附录：实验配置模板

### A.1 controller-only 模式配置
```yaml
mode: controller-only
components:
  plateau_detector: enabled
  corpus_snapshot: enabled
  recipe_mutator: disabled
  llm_agent: disabled
  ghidra_channel: disabled
  validation_gate: disabled
controller:
  plateau_window: 300
  plateau_threshold: 0.1
  snapshot_trigger: on_plateau
```

### A.2 实验命令模板
```bash
# libxml2 full-agent r01
python3 orchestrator.py \
  --target libxml2 \
  --mode full-agent \
  --duration 14400 \
  --run-id r01 \
  --output results/paper01_venue/libxml2_full-agent_r01

# libxml2 controller-only r01
python3 orchestrator.py \
  --target libxml2 \
  --mode controller-only \
  --duration 14400 \
  --run-id r01 \
  --output results/paper01_venue/libxml2_controller-only_r01
```

---

**总结**：这是一个 6-8 周的完整实验计划，核心是 Phase 1（2 周）验证 full-agent 在非饱和目标上有用。如果成功，继续完成 Phase 2-4；如果失败，立即改论文定位投稿 arXiv。
