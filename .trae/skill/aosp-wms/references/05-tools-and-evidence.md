# 工具与证据
<!-- source: 04-use-this-skill-when.md -->

# Use This Skill When

在以下场景下使用该 Skill：

- 用户明确要求分析 WMS / 窗口系统问题。
- 问题现象与以下内容强相关：
  - 窗口不显示
  - 窗口被遮挡
  - 焦点错误
  - 点击打到错误窗口
  - IME 显示/隐藏异常
  - Insets 错误
  - Cutout / 状态栏 / 导航栏适配异常
  - 窗口切换卡住
  - Transition / ShellTransition 异常
  - Activity 已启动但界面不出现
  - 窗口层级 / z-order 错误
  - 多窗口 / 分屏 / 画中画异常
  - 旋转后窗口状态错误
- 用户提供了：
  - `dumpsys window`
  - `dumpsys activity`
  - Winscope
  - logcat 中 WMS / Transition / Insets / Focus 日志
  - trace / Perfetto 中 window / wm / shell 相关信号
- 用户需要：
  - 架构设计思想
  - 架构图
  - 时序图
  - 关键代码详细讲解
  - 根因分析和修复建议

---


<!-- source: 07-inputs-required.md -->

# Inputs Required

至少需要以下之一：

- `dumpsys window`
- Winscope
- WMS / Transition / Insets / IME 相关 logcat
- 与问题时刻对应的 trace / Perfetto
- 关键源码路径 / patch / 行为描述
- `dumpsys activity`（强烈建议）

同时最好具备：

- Android 版本 / AOSP 分支
- 包名 / Activity 名 / 窗口名
- 问题场景与复现步骤
- 问题发生时间点
- 是否与旋转 / IME / 多窗口 / 启动 / 返回桌面相关

---


<!-- source: 08-inputs-optional.md -->

# Inputs Optional

以下信息可显著提升分析质量：

- `dumpsys input`
- `dumpsys SurfaceFlinger`
- screenrecord / 截图
- FrameTimeline / shell transition trace
- 最近代码变更 / patch
- 问题首次出现版本
- 多显示 / 分屏 / PiP / freeform 环境信息
- 状态栏 / 导航栏 / cutout / edge-to-edge 配置信息
- Window flags / LayoutParams
- ActivityRecord / Task / DisplayContent 的关键 dumpsys 片段

---


<!-- source: 16-evidence-priority.md -->

# Evidence Priority

证据优先级从高到低：

1. Winscope / `dumpsys window` / 关键时刻窗口树状态
2. 与问题时刻对齐的 trace / Perfetto / wm/shell/insets 事件
3. `dumpsys activity` / `dumpsys input` / `dumpsys SurfaceFlinger`
4. logcat 中 WMS / Transition / Insets / IME / Focus 关键日志
5. 源码静态调用链
6. 经验推断

规则：

- 对象状态树高于单条日志猜测。
- 焦点/窗口树证据高于经验判断。
- 若逻辑状态与显示结果冲突，必须区分“逻辑层已正确、图形层未呈现”与“逻辑层本身就错误”。
- 若多个证据冲突，必须保留冲突事实并解释可能原因。

---


<!-- source: 20-output-format.md -->

# Output Format

输出必须严格包含以下部分。

### 1. 问题摘要

说明：

- 现象
- 场景
- 包名 / Activity / 窗口
- 影响范围
- 是否必现

------

### 2. 模块定位

说明：

- 当前问题属于 WMS 的哪个子域
- 涉及哪些核心模块
- 主分析对象是什么

------

### 3. 架构设计思想

必须解释：

- 相关模块为什么这样设计
- 对象分层与职责边界
- 为什么当前问题会发生在这一层

------

### 4. 架构图

必须给 Mermaid 图，并在图后解释：

- 核心对象关系
- 当前问题位于图中哪一段
- 影响链如何传播

------

### 5. 正常时序图

必须说明：

- 正常情况下状态如何推进
- 关键事务点和状态切换点是什么

------

### 6. 异常时序图

必须说明：

- 当前在哪一步偏离
- 首发错误状态
- 传播链
- 最终用户可见异常

------

### 7. 关键证据清单

按类型列出：

- winscope / dumpsys window / dumpsys activity / trace / logcat / input / sf
- 每类证据支持什么结论
- 哪些结论仍缺证据

------

### 8. 关键对象状态

至少包含：

- DisplayContent / Task / ActivityRecord / WindowState
- FocusedWindow / FocusedApp（如相关）
- Insets / IME target（如相关）
- Transition state（如相关）
- Surface 状态（如相关）

------

### 9. 关键链路分析

必须至少输出一种相关链路：

- 可见性链
- 焦点链
- 层级链
- Insets 链
- IME target 链
- Transition 链
- Window → Surface 链

------

### 10. 关键代码详细解释

必须包含：

- 关键类说明
- 关键函数说明
- 关键字段/状态说明
- 事务/同步语义说明
- 当前问题为何走到这里

------

### 11. 首发根因判断

必须分层表达：

- 已证实根因
- 高概率候选根因
- 待验证假设

------

### 12. 修复建议

必须区分：

- 架构层修复
- 状态逻辑修复
- 代码层修复
- 可观测性补强

------

### 13. 验证方案

必须写清：

- 加什么日志 / trace
- 抓什么 dumpsys / winscope
- 如何确认状态流转正确
- 如何做回归

------

### 14. 风险与不确定性

说明：

- 当前证据盲区
- 哪条链路仍未打通
- 是否可能是上游/下游次生问题
- 是否存在多因混合

------

### 15. 当前结论置信度

使用以下等级之一：

- 高
- 中
- 低

并说明原因。

------


<!-- source: 21-missing-information-policy.md -->

# Missing Information Policy

当输入不足时，按以下策略处理。

### 可以输出

- 当前最可能问题类型
- 初步架构定位
- 候选架构图 / 时序图
- 关键对象初步状态判断
- 候选链路
- 最小补充清单

### 不可以输出

- 确定性根因
- 未被证据支撑的窗口状态定性
- 未打通状态链的最终归因
- 只凭类名/字段名做详细代码定性

### 信息不足时必须明确写出

- 当前已知事实
- 当前未知事实
- 当前图和时序中哪些是推断
- 哪条链路卡在什么断点
- 最小补充清单，例如：
  - 对应时刻 `dumpsys window`
  - Winscope
  - `dumpsys activity`
  - `dumpsys input`
  - `dumpsys SurfaceFlinger`
  - 对应时刻 trace / Perfetto
  - 关键窗口 logcat

------


<!-- source: 24-standard-operating-principles.md -->

# Standard Operating Principles

执行本 Skill 时，始终遵守以下原则：

1. 先定义窗口问题类型，再看细节状态。
2. 先建立架构定位，再深入 dumpsys / winscope。
3. 先画结构，再讲调用。
4. 先讲正常时序，再讲异常偏离。
5. 先找第一个错误状态，再解释最终用户现象。
6. 先区分逻辑状态错误，再区分图形显示后果。
7. 先看对象树和时序证据，再做经验推断。
8. 既要回答“为什么显示/焦点/Insets 出错”，也要回答“这套代码为什么这样设计”。
9. 证据不足时保持保守，不可伪确定。
10. 不要把窗口系统的下游结果误当首发根因。

------
