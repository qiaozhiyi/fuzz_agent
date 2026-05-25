# 论文逻辑问题修复总结

## 修复日期
2026-05-24

## 修复的主要问题

### 1. **重新定位论文性质**（最关键）

**问题**：原论文声称提出了三个核心基础设施组件，但实验结果显示其中两个（LLM 层和 micro-campaign gate）在 cJSON 上完全没用。

**修复**：
- 修改标题：从 "Extending Off-Hot-Path LLM Control" 改为 "An Architecture for Off-Hot-Path LLM Control"
- 重写 Abstract：
  - 明确说明这是"架构提案"而非"已验证的系统"
  - 在开头就声明 cJSON 是"饱和目标"
  - 明确说明 LLM 层和 gate 的价值"未被实证展示"
  - 强调这是"proof-of-concept"
- 修改贡献列表（C1-C4）：
  - 将 controller（高原检测器）提升为 C2，强调其重要性
  - 明确说明每个组件的价值"取决于目标特性"
  - 承认 micro-campaign gate 的核心功能"未被测试"

### 2. **在 Introduction 中前置限制说明**

**问题**：原论文把限制放在最后（§7），读者读到一半才发现实验有重大缺陷。

**修复**：
- 在 Introduction 中添加"Scope and limitations (stated upfront)"段落
- 用 bullet points 列出 5 个关键限制：
  1. 单一饱和目标
  2. LLM 层未测试
  3. 统计功效不足
  4. 归因不完整（缺少 controller-only 消融）
  5. 公平性基线警告（cmplog 更好）
- 明确说明这是"架构提案 + proof-of-concept"，不是"所有组件在所有目标上都有价值"的声明

### 3. **修正 RQ1 的归因逻辑**

**问题**：原论文声称"高原缩短归因于 recipe-guided mutator"，但消融实验显示移除 mutator 反而性能更好。

**修复**：
- 添加"Attribution caveat: controller vs mutator"段落
- 明确说明消融实验的矛盾：`no-mutator (930s) < rule-only (1165s) < full-agent (1384s)`
- 承认"如果 mutator 是驱动力，移除它应该让性能变差，而不是变好"
- 将归因改为"controller 的高原检测和语料重组是主要驱动力"
- 强调这是"假设"而非"测量效果"，因为缺少 controller-only 消融

### 4. **重写 RQ4 消融实验的解释**

**问题**：原论文试图掩盖消融实验的矛盾结果。

**修复**：
- 标题改为"Ablation ordering reveals controller as primary driver"
- 用 bullet points 明确列出三个逻辑矛盾：
  1. 移除 mutator 性能变好（不是变差）
  2. LLM 层没有贡献（full-agent 不如 rule-only）
  3. 所有消融模式都共享 controller，都比 baseline 好
- 添加"Critical caveat"段落：
  - 强调这是"假设"不是"测量效果"
  - 明确说明缺少 controller-only 消融
  - 承认"无法完全分离 controller 机制和默认规则配方"
  - 说明这是"最重要的缺失实验"

### 5. **诚实披露 E2c 的双峰分布问题**

**问题**：原论文用中位数 234s 声称 E2c 是"最好的"，但实际上有一个 run 是 4454s（比 baseline 还差）。

**修复**：
- 添加"Critical limitation"标记
- 明确说明分布是"双峰的"：`[187, 234, 4454]`
- 承认"两个 run 很短，一个 run 比 baseline 还差"
- 说明"在 N=3 下无法判断这是真实不稳定性还是采样噪声"
- 强调"中位数 234s 不是可靠的点估计"
- 推迟解释到 venue 版本

### 6. **强化 E4 公平性基线的负面结果**

**问题**：原论文轻描淡写地说 cmplog "找到了一个额外的边"。

**修复**：
- 明确标记为"负面结果"：cmplog 达到 270 边，FuzzPilot 只有 269 边
- 强调"必须披露"（用粗体）
- 说明"这是 FuzzPilot 在绝对覆盖率轴上的负面结果"
- 澄清两种技术在不同轴上：
  - FuzzPilot：墙钟时间优势（假设性的）
  - cmplog：覆盖率优势（已证实的）
- 承认"在非饱和目标上，这两个属性的相对价值是开放问题"

### 7. **加强 Limitations 章节**

**问题**：原 Limitations 章节分散，没有总结。

**修复**：
- 在章节开头添加"Summary of critical limitations"段落
- 用编号列表列出 5 个最关键的限制
- 每个限制都引用对应的章节
- 在总结后明确说明："这篇预印本应该被理解为架构提案 + proof-of-concept，而不是所有组件在所有目标上都有价值的声明"

### 8. **统计显著性的一致性处理**

**问题**：原论文在多处说"descriptive only"但没有统一强调。

**修复**：
- 在所有关键发现处添加"not statistically significant"
- 明确说明"需要 N≥20 才能做显著性检验"
- 将"点估计"改为"需要验证的点估计"
- 在 Abstract 中就说明"不是显著性声明"

## 未修改的部分

以下内容**没有修改**，因为需要补充实验数据：

1. **缺失的实验**：
   - controller-only 消融
   - ai-direct 消融
   - random-recipe 消融
   - 参数敏感性扫描
   - libxml2/sqlite3/openssl_x509 多目标矩阵
   - 与 G²FUZZ 的对比

2. **样本量**：
   - 主实验仍然是 N=5（需要 N≥20）
   - 消融实验仍然是 N=3（需要 N≥10）

3. **个人信息**：
   - 保持 "Zhiyi Yao"（没有添加 "Jhon"）
   - 如果你想用 "Zhiyi Yao (Jhon)"，需要手动修改第 78 行

## 修改后的论文定位

**修改前**：
- 声称：我们提出了三个核心组件（mutator + gate + Ghidra）
- 实验：在 cJSON 上测试
- 结论：系统有效

**修改后**：
- 声称：我们提出了一个架构（包含 4 个组件：recipe + controller + gate + Ghidra）
- 实验：在 cJSON（饱和目标）上做 proof-of-concept
- 结论：
  - 架构可以构建和运行（已证实）
  - Controller 可能有用（假设，需要验证）
  - LLM 层和 gate 的价值未被测试（诚实承认）
  - 在非饱和目标上的表现是开放问题（明确说明）

## 论文现在的状态

**适合发布为 arXiv 预印本**：
- ✅ 诚实披露了所有限制
- ✅ 明确说明这是架构提案
- ✅ 不过度声称实验结果
- ✅ 承认负面结果（cmplog 更好）
- ✅ 明确说明需要补充的工作

**不适合投稿顶会**（除非完成以下工作）：
- ❌ 需要 controller-only 消融
- ❌ 需要非饱和目标（libxml2/sqlite3/openssl）
- ❌ 需要 N≥20 主实验 + N≥10 消融实验
- ❌ 需要与 G²FUZZ 的对比

## 编译状态

✅ 论文编译成功，无语法错误
✅ 生成的 PDF：paper01.pdf (40 pages, 656487 bytes)

## 下一步建议

### 短期（arXiv 提交前）
1. 检查修改后的 Abstract 和 Introduction 是否符合你的意图
2. 如果需要，修改作者名字（添加 "Jhon"）
3. 运行 bibtex 和多次 pdflatex 确保引用正确
4. 准备 arXiv 提交包（删除辅助文件）

### 长期（venue 版本）
1. **最优先**：运行 controller-only 消融实验
2. 在非饱和目标上运行完整实验矩阵（libxml2/sqlite3/openssl）
3. 增加样本量到 N≥20（主实验）和 N≥10（消融）
4. 与 G²FUZZ 做对比实验
5. 根据新实验结果重新评估归因假设

## 关键改进点

1. **诚实性**：不再掩盖实验的局限性和矛盾
2. **逻辑一致性**：声明与实验结果匹配
3. **透明度**：在开头就说明限制，不是藏在最后
4. **科学严谨性**：承认假设是假设，不是事实
5. **负面结果披露**：明确说明 cmplog 在覆盖率上更好

这些修改让论文从"过度声称的实验论文"变成了"诚实的架构提案 + 初步评估"，这对于 arXiv 预印本是完全可以接受的。
