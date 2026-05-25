# 论文逻辑最终审计报告

## 审计日期
2026-05-24

## 审计范围
系统检查论文的逻辑一致性、声明与证据的匹配、以及潜在的过度声称问题。

---

## ✅ 已修复且逻辑一致的部分

### 1. **Abstract 与实验结果一致** ✅
- 明确说明 cJSON 是饱和目标
- 承认 LLM 层未被测试（0/20 提升率）
- 说明统计显著性缺失（p=0.42）
- 强调这是"架构提案 + proof-of-concept"
- 披露负面结果（cmplog 达到 270 边）

### 2. **Introduction 与后文一致** ✅
- 贡献列表（C1-C4）明确说明每个组件的价值"取决于目标特性"
- C2（controller）被提升为核心贡献
- C3（gate）明确说明"intended property 未被测试"
- "Scope and limitations" 段落前置了所有关键限制

### 3. **RQ1 归因逻辑修复** ✅
- 添加了"Attribution caveat: controller vs mutator"段落
- 明确说明消融实验的矛盾（no-mutator 比 full-agent 更好）
- 承认这是"假设"而非"测量效果"
- 强调需要 controller-only 消融验证

### 4. **RQ3 诚实披露** ✅
- 明确说明 0/20 LLM 提案被提升
- 解释原因：cJSON 饱和导致 R=0
- 承认 gate 的核心功能"未被测试"

### 5. **RQ4 消融实验逻辑重写** ✅
- 标题改为"Ablation ordering reveals controller as primary driver"
- 用 bullet points 列出三个逻辑矛盾
- 添加"Critical caveat"段落强调这是假设
- E2c 双峰分布问题被明确标记为"Critical limitation"

### 6. **Limitations 章节完整** ✅
- 添加了"Summary of critical limitations"段落
- 5 个编号列表覆盖所有关键问题
- 总结段落明确定位为"架构提案 + proof-of-concept"

### 7. **Conclusion 与前文一致** ✅
- 正确总结了所有发现
- 明确说明统计显著性缺失
- 承认 LLM 层未被测试
- 强调归因是"假设"而非"测量效果"

---

## ⚠️ 发现的潜在逻辑问题

### 问题 1：Conclusion 中的归因表述不够谨慎

**位置**：第 2700-2702 行

**问题**：
```latex
The plateau-duration delta is therefore attributable to the 
recipe-guided mutator running the controller's default 
DictionaryAgent recipe rather than to LLM-promoted recipes
```

**逻辑矛盾**：
- 这句话说"高原缩短归因于 recipe-guided mutator"
- 但 RQ4 消融实验明确显示：移除 mutator（E2b）性能**更好**（930s < 1384s）
- 如果 mutator 是驱动力，移除它应该让性能变差，而不是变好

**正确的表述应该是**：
- "归因于 controller 的高原检测和语料重组机制"
- 而不是"归因于 recipe-guided mutator"

**建议修复**：
将第 2700-2702 行改为：
```latex
The plateau-duration delta is therefore attributable primarily to 
the controller's plateau-detection and corpus-reorganization 
machinery rather than to the recipe-guided mutator or LLM-promoted 
recipes
```

---

### 问题 2：Conclusion 中缺少对 E2c 双峰分布的警告

**位置**：第 2677-2725 行（整个 Conclusion）

**问题**：
- Conclusion 提到了 E2a（rule-only）和 E2b（no-mutator）
- 但完全没有提到 E2c（no-static-analysis）的双峰分布问题
- E2c 的中位数 234s 看起来"最好"，但实际上有一个 run 是 4454s（比 baseline 还差）

**风险**：
- 读者可能误以为"移除 Ghidra 性能最好"
- 但实际上这个结果不可靠（N=3，双峰分布）

**建议修复**：
在 Conclusion 的消融实验总结部分添加：
```latex
The E2c (no-static-analysis) ablation shows a bimodal distribution 
[187, 234, 4454]s at N=3, with the median dominated by cluster 
structure rather than a stable effect; interpretation is deferred 
to higher-N evaluation.
```

---

### 问题 3：贡献 C1 的表述可能过度

**位置**：第 197-206 行（C1 贡献）

**问题**：
```latex
C1 A data-as-recipe intermediate representation that replaces 
LLM-generated code with a schema-validated JSON description...
```

**潜在问题**：
- 这个表述暗示"data-as-recipe 是一个贡献"
- 但实验显示：在 cJSON 上，recipe-guided mutator 实际上**拖慢**了性能
- E2b（no-mutator）的性能比 full-agent 更好

**当前缓解措施**：
- C1 的描述中没有声称"提升性能"
- 只是说"side-steps runtime-safety concern"和"makes proposals cacheable"
- 这些是架构属性，不是性能声明

**结论**：
- 当前表述可以接受，因为没有过度声称
- 但需要确保读者理解：C1 是"架构设计选择"，不是"性能优化"

---

### 问题 4：Abstract 中的"three pieces of infrastructure"表述

**位置**：第 97-106 行

**问题**：
```latex
We build three pieces of infrastructure on top of AFL++: 
(i) a recipe-guided custom mutator
(ii) a micro-campaign validation gate
(iii) a plateau-triggered controller
```

**潜在混淆**：
- Abstract 说"三个组件"
- 但贡献列表（C1-C4）有"四个贡献"
- C4（Ghidra）在 Abstract 中没有被列为"infrastructure"

**当前状态**：
- Abstract 后面有一句："An LLM can be hooked into this pipeline as a proposal source, though the architecture does not require it."
- 这暗示 LLM 是可选的，不是核心基础设施

**建议**：
- 保持当前表述（三个基础设施组件）
- Ghidra 作为"context channel"是辅助组件，不是核心架构
- 这个分类是合理的

---

## 🔍 需要特别注意的表述

### 1. **"primarily by the controller"的一致性**

**检查点**：
- Abstract（第 124-127 行）：✅ "suggest the plateau reduction is driven primarily by the controller"
- RQ1（第 1088-1090 行）：✅ "controller's plateau-detection and corpus-reorganization machinery is the primary driver"
- RQ4（第 1902-1908 行）：✅ "controller's plateau detector and corpus-snapshot reorganization are the primary drivers"
- Conclusion（第 2700-2708 行）：❌ "attributable to the recipe-guided mutator" ← **需要修复**

### 2. **"hypothesis, not measured effect"的一致性**

**检查点**：
- Abstract（第 126-127 行）：✅ "a controller-only ablation to test this hypothesis is deferred"
- RQ1（第 1095-1096 行）：✅ "the attribution remains a hypothesis rather than a measured effect"
- RQ4（第 1910-1920 行）：✅ "This interpretation is a hypothesis, not a measured effect"
- Conclusion（第 2707-2709 行）：✅ "remains a descriptive hypothesis at N=3"

### 3. **"LLM layer untested"的一致性**

**检查点**：
- Abstract（第 128-131 行）：✅ "promoted zero, because cJSON's saturation leaves reward R=0"
- Introduction（第 258-264 行）：✅ "The gate's intended property...remains empirically undemonstrated"
- RQ3（第 1732-1750 行）：✅ "Every single one of the 20 candidates returned Δ_edges=0"
- Conclusion（第 2697-2699 行）：✅ "promoted zero, because cJSON's 269-edge ceiling is reached well before plateau time"

---

## 📋 修复建议优先级

### 🔴 高优先级（必须修复）

**1. Conclusion 中的归因错误（第 2700-2702 行）**
- 当前：归因于 recipe-guided mutator
- 应改为：归因于 controller 的机制
- 理由：与 RQ4 消融实验结果矛盾

### 🟡 中优先级（建议修复）

**2. Conclusion 中缺少 E2c 双峰分布警告**
- 添加一句话说明 E2c 结果不可靠
- 理由：避免读者误解"移除 Ghidra 性能最好"

### 🟢 低优先级（可选）

**3. 考虑在 Abstract 中提及 Ghidra（C4）**
- 当前 Abstract 只提到三个组件
- 可以考虑在某处提及"static-analysis context channel"
- 理由：与贡献列表（C1-C4）更一致

---

## ✅ 总体评价

### 优点
1. **诚实透明**：所有限制都被明确披露
2. **逻辑自洽**：大部分表述与实验结果一致
3. **统计严谨**：明确说明所有发现都是"描述性的"，不声称显著性
4. **负面结果披露**：cmplog 更好的结果被明确说明

### 需要改进
1. **Conclusion 归因错误**：需要修复第 2700-2702 行
2. **E2c 双峰分布**：Conclusion 应该提及这个问题

### 适用场景
- ✅ **arXiv 预印本**：修复上述两个问题后完全适合
- ❌ **顶会投稿**：需要补充 controller-only 消融和非饱和目标实验

---

## 🎯 最终建议

**立即修复**：
1. 修改 Conclusion 第 2700-2702 行的归因表述
2. 在 Conclusion 中添加 E2c 双峰分布警告

**修复后状态**：
- 论文逻辑完全一致
- 所有声明都有证据支持
- 所有限制都被明确披露
- 适合作为 arXiv 预印本发布

**长期工作**（venue 版本）：
1. 运行 controller-only 消融实验（最优先）
2. 在非饱和目标上验证（libxml2/sqlite3/openssl）
3. 增加样本量到 N≥20（主实验）和 N≥10（消融）
4. 重新评估归因假设
