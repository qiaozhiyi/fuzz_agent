# 论文逻辑审计 - 最终报告

## 审计完成时间
2026-05-24

---

## 📊 审计结果总结

### ✅ 发现并修复的问题：2 个

#### 🔴 问题 1：Conclusion 中的归因逻辑错误（已修复）

**位置**：第 2700-2711 行

**问题描述**：
- 原文说："高原缩短归因于 recipe-guided mutator"
- 但 RQ4 消融实验显示：移除 mutator（E2b）性能**更好**（930s < 1384s）
- 这是明显的逻辑矛盾

**修复内容**：
```latex
修改前：
The plateau-duration delta is therefore attributable to the 
recipe-guided mutator running the controller's default 
DictionaryAgent recipe rather than to LLM-promoted recipes

修改后：
The plateau-duration delta is therefore attributable primarily 
to the controller's plateau-detection and corpus-reorganization 
machinery rather than to the recipe-guided mutator or 
LLM-promoted recipes; the ablation ordering shows that removing 
the mutator improves performance rather than degrading it, 
contradicting a mutator-driven attribution.
```

**影响**：
- 修复了与 RQ4 消融实验的逻辑矛盾
- 与 Abstract、RQ1、RQ4 的归因表述保持一致
- 强调这是"假设"而非"测量效果"

---

#### 🟡 问题 2：Conclusion 缺少 E2c 双峰分布警告（已修复）

**位置**：第 2720-2725 行

**问题描述**：
- Conclusion 提到了 E2a 和 E2b，但没有提到 E2c
- E2c 的中位数 234s 看起来"最好"，但实际上分布是双峰的 [187, 234, 4454]
- 缺少警告可能误导读者认为"移除 Ghidra 性能最好"

**修复内容**：
```latex
添加：
The E2c (no-static-analysis) ablation shows a bimodal plateau 
distribution [187, 234, 4,454]s at N=3, with the median dominated 
by cluster structure rather than a stable effect; interpretation 
is deferred to higher-N evaluation.
```

**影响**：
- 防止读者误解 E2c 结果
- 与 RQ4 中的"Critical limitation"标记保持一致
- 明确说明这个结果不可靠

---

## ✅ 确认无问题的部分

### 1. **Abstract 逻辑一致性** ✅
- 明确说明 cJSON 是饱和目标
- 承认 LLM 层未被测试（0/20 提升率）
- 说明统计显著性缺失（p=0.42）
- 披露负面结果（cmplog 达到 270 边）
- 强调这是"架构提案 + proof-of-concept"

### 2. **Introduction 与实验结果匹配** ✅
- 贡献列表（C1-C4）明确说明价值"取决于目标特性"
- C2（controller）被正确提升为核心贡献
- C3（gate）明确说明"intended property 未被测试"
- "Scope and limitations"段落前置了所有关键限制

### 3. **RQ1 归因逻辑** ✅
- 添加了"Attribution caveat"段落
- 明确说明消融实验的矛盾
- 承认这是"假设"而非"测量效果"
- 强调需要 controller-only 消融验证

### 4. **RQ3 诚实披露** ✅
- 明确说明 0/20 LLM 提案被提升
- 解释原因：cJSON 饱和导致 R=0
- 承认 gate 的核心功能"未被测试"

### 5. **RQ4 消融实验逻辑** ✅
- 标题："Ablation ordering reveals controller as primary driver"
- 用 bullet points 列出三个逻辑矛盾
- 添加"Critical caveat"段落
- E2c 双峰分布被标记为"Critical limitation"

### 6. **Limitations 章节** ✅
- "Summary of critical limitations"段落完整
- 5 个编号列表覆盖所有关键问题
- 总结段落明确定位为"架构提案 + proof-of-concept"

### 7. **统计方法一致性** ✅
- 所有地方都说明"descriptive only"
- 明确说明 p>0.05，不声称显著性
- 强调需要 N≥20 才能做显著性检验

---

## 🎯 关键逻辑链检查

### 链条 1：归因逻辑的一致性

| 位置 | 表述 | 状态 |
|------|------|------|
| Abstract (124-127) | "suggest...driven primarily by the controller" | ✅ 一致 |
| RQ1 (1088-1090) | "controller's...machinery is the primary driver" | ✅ 一致 |
| RQ4 (1902-1908) | "controller's...are the primary drivers" | ✅ 一致 |
| Conclusion (2700-2711) | "attributable primarily to the controller's machinery" | ✅ **已修复** |

**结论**：归因逻辑现在完全一致 ✅

---

### 链条 2："假设 vs 测量效果"的一致性

| 位置 | 表述 | 状态 |
|------|------|------|
| Abstract (126-127) | "controller-only ablation...is deferred" | ✅ 一致 |
| RQ1 (1095-1096) | "hypothesis rather than a measured effect" | ✅ 一致 |
| RQ4 (1910-1920) | "hypothesis, not a measured effect" | ✅ 一致 |
| Conclusion (2708-2711) | "descriptive hypothesis...not a measured effect" | ✅ 一致 |

**结论**：假设性质的表述完全一致 ✅

---

### 链条 3："LLM 层未被测试"的一致性

| 位置 | 表述 | 状态 |
|------|------|------|
| Abstract (128-131) | "promoted zero...saturation leaves reward R=0" | ✅ 一致 |
| Introduction (258-264) | "intended property...empirically undemonstrated" | ✅ 一致 |
| RQ3 (1732-1750) | "Every single one...returned Δ_edges=0" | ✅ 一致 |
| Conclusion (2697-2699) | "promoted zero...ceiling is reached well before" | ✅ 一致 |

**结论**：LLM 层未被测试的表述完全一致 ✅

---

### 链条 4：E2c 双峰分布的警告

| 位置 | 表述 | 状态 |
|------|------|------|
| RQ4 (1928-1942) | "Critical limitation: bimodal [187, 234, 4454]" | ✅ 详细说明 |
| Limitations (未明确提及) | （在"Deferred ablations"段落中提到） | ✅ 有提及 |
| Conclusion (2725-2729) | "bimodal...dominated by cluster structure" | ✅ **已添加** |

**结论**：E2c 警告现在在所有关键位置都有提及 ✅

---

## 📋 编译状态

✅ **编译成功**
- 文件：paper01.pdf
- 页数：40 页
- 大小：656,883 字节
- 无语法错误
- 无格式问题

---

## 🎓 论文当前状态评估

### 逻辑一致性：✅ 优秀
- 所有声明与实验结果匹配
- 归因逻辑完全一致
- 假设与事实明确区分
- 负面结果诚实披露

### 透明度：✅ 优秀
- 所有限制在 Abstract 和 Introduction 中前置
- Limitations 章节有完整总结
- 统计功效不足被明确说明
- 缺失的实验被明确列出

### 科学严谨性：✅ 优秀
- 不过度声称
- 明确区分"描述性点估计"和"显著性声明"
- 承认假设需要验证
- 明确说明需要补充的工作

### 适用场景：
- ✅ **arXiv 预印本**：完全适合，逻辑严谨，诚实透明
- ❌ **顶会投稿**：需要补充实验（controller-only 消融 + 非饱和目标 + N≥20）

---

## 🚀 下一步建议

### 立即（arXiv 提交前）
1. ✅ 逻辑问题已全部修复
2. 运行完整编译流程：`pdflatex && bibtex && pdflatex && pdflatex`
3. 检查引用是否完整
4. 准备 arXiv 提交包

### 短期（1-2 周）
1. 完成 E2b 和 E2c 的剩余重复（达到 N=10）
2. 从 fuzz-server rsync E2a 数据

### 中期（1-2 月）
1. **最优先**：实现并运行 controller-only 消融实验
2. 在非饱和目标（libxml2/sqlite3）上运行完整矩阵
3. 验证归因假设

### 长期（venue 版本）
1. 增加样本量到 N≥20（主实验）和 N≥10（消融）
2. 与 G²FUZZ 对比实验
3. 完成所有缺失的消融实验
4. 根据新实验结果更新归因结论

---

## ✅ 最终结论

**论文逻辑审计通过！**

所有发现的问题已修复：
- ✅ Conclusion 归因逻辑已修正
- ✅ E2c 双峰分布警告已添加
- ✅ 所有逻辑链条完全一致
- ✅ 编译成功，无语法错误

**论文现在的状态**：
- 逻辑严谨、诚实透明
- 所有声明都有证据支持
- 所有限制都被明确披露
- 适合作为 arXiv 预印本发布

**可以提交 arXiv！** 🎉
