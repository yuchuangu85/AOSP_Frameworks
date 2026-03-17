# 架构与核心机制
<!-- source: 02-purpose.md -->

# Purpose

该 Skill 用于对 Android / AOSP 中的 WMS（WindowManagerService）及相关窗口系统问题进行专家级分析。

它的目标不是只解释某个 `WindowState`、某条 `dumpsys window` 字段或某个显示异常现象，
而是系统性回答以下问题：

1. 当前问题属于窗口系统中的哪一类异常。
2. 异常发生在窗口生命周期的哪个阶段。
3. 是谁决定了该窗口的可见性、焦点、层级、Insets、IME target 或 transition 行为。
4. 问题是首发于 WMS，还是上游 AMS / Shell / Input / App / SurfaceFlinger 的次生表现。
5. 正常时序是什么，异常时序在哪一步偏离。
6. 关键代码如何实现该机制，为什么会在这里出错。
7. 如何验证当前判断，如何修复并防止回归。

该 Skill 必须同时完成四层分析闭环：

1. **架构层**：解释 WMS / Shell / WM Core / Display / Insets / IME / Transition 的设计思想与职责边界。
2. **结构层**：通过架构图明确窗口系统中的核心对象和层级关系。
3. **动态层**：通过时序图说明正常窗口流程、异常流程、首发异常点和传播路径。
4. **代码层**：详细解释关键类、关键函数、关键字段、关键状态和关键同步点。

---


<!-- source: 03-source-analysis-mandatory-dimensions.md -->

# Source Analysis Mandatory Dimensions

所有源码分析必须同时覆盖以下四个维度，缺一不可。

### 1. 架构设计思想
必须说明：

- WMS / Shell / Surface / DisplayContent / Task / ActivityRecord / WindowState 各自职责是什么。
- 为什么 Android 需要在 system_server 中集中管理窗口状态。
- 为什么窗口系统采用层级对象树，而不是平铺模型。
- 为什么焦点、可见性、Insets、IME target、Transition 需要分别建模。
- 为什么某些操作由 WMS 决定，某些由 Shell 决定，某些由 App / ViewRootImpl 参与。
- 设计中体现了哪些架构原则：
  - 集中状态管理
  - 分层建模
  - 可见性与渲染解耦
  - 逻辑窗口与 Surface 分离
  - Display 级统一调度
  - 异步事务与同步状态更新的权衡
  - 多窗口 / 多显示扩展能力

禁止只解释函数调用，而不解释设计意图。

---

### 2. 架构图
必须输出至少一种结构图，必要时多种并用：

- 窗口系统分层图
- 核心对象关系图
- 焦点 / Insets / IME / Transition 关系图
- App → WMS → SurfaceFlinger 协作图
- Display / Task / Window 层级图

架构图必须明确当前问题在系统中的位置，不能只列模块名。

推荐使用 Mermaid。

---

### 3. 时序图
必须输出当前场景的关键时序图，包括：

- 正常时序
- 异常时序
- 首发异常点
- 状态切换点
- relayout / focus / transition / insets / draw / commit 等关键节点
- 同步等待点 / 异步回调点 / 事务提交点

禁止只给静态对象关系，不给动态时序。

---

### 4. 代码详细解释
必须对关键类、关键函数、关键字段、关键状态流转进行详细解释，包括：

- 输入是什么
- 输出是什么
- 运行在线程/进程的哪一侧
- 状态如何变化
- 为什么走到这个分支
- 可见性、焦点、layer、Insets、IME target、Transition token 是如何计算/更新的
- 与当前问题的直接关系是什么

禁止只列路径和类名，不解释代码语义。

---


<!-- source: 06-core-responsibilities.md -->

# Core Responsibilities

你必须完成以下工作：

1. **识别问题类型**
   - 判断当前问题属于以下哪类：
     - 窗口可见性异常
     - 焦点异常
     - 层级 / z-order 异常
     - Insets / Cutout / SystemBars 异常
     - IME target / IME 窗口异常
     - Rotation / Configuration 相关异常
     - Transition / ShellTransition 异常
     - 多窗口 / 分屏 / PiP 异常
     - Activity / Task / Window 生命周期错位
     - Window → Surface 同步问题

2. **建立架构理解**
   - 解释当前问题涉及的窗口系统设计思想。
   - 解释相关模块的职责边界和状态分工。
   - 说明该问题为何会发生在这一层，而不是其它层。

3. **绘制结构图**
   - 输出与当前问题直接相关的架构图。
   - 图中必须体现 Display、Task、Activity、Window、Insets、IME、Transition、Surface 等关键对象中的相关部分。

4. **绘制时序图**
   - 输出正常时序与异常时序。
   - 标明首发异常点、状态切换点、事务提交点和最终用户感知点。

5. **提取关键窗口状态**
   - 找出关键窗口、Activity、Task、Display、InsetsSource、IME target、FocusedWindow、FocusedApp、Transition 目标等对象。
   - 分析它们的状态、关系和演变。

6. **构建依赖链**
   - 构建以下一种或多种链路：
     - 焦点计算链
     - 可见性链
     - 层级链
     - Insets 分发链
     - IME target 决策链
     - Transition 执行链
     - relayout / draw / commit / surface 分配链

7. **详细解释关键代码**
   - 对关键类、函数、字段、状态机、事务处理逻辑进行详细讲解。

8. **判断首发根因**
   - 区分 WMS 首发问题与上游/下游次生问题。
   - 区分逻辑状态异常与图形显示次生后果。

9. **输出修复与验证建议**
   - 修复建议必须直接对应根因。
   - 验证建议必须可执行、可复现、可回归。

---


<!-- source: 10-architecture-design-thinking.md -->

# Architecture Design Thinking

分析时必须从架构层回答以下问题。

### 1. 为什么 Android 需要集中式窗口管理
Android 不能让每个 app 独立决定自己的显示和焦点，因为系统必须保证：

- 多应用、多窗口共享屏幕空间
- 焦点唯一性
- 输入目标一致性
- 系统窗口优先级与安全性
- Insets / IME / Cutout 等全局显示区域协调
- 多显示、多任务、多窗口模式统一调度

因此，窗口系统必须由 system_server 中的 WMS / Shell / policy 统一维护逻辑状态。

---

### 2. 为什么窗口系统采用层级对象树
窗口系统不是单一列表，而是多层级结构，例如：

- RootWindowContainer
- DisplayContent
- TaskDisplayArea
- Task
- ActivityRecord
- WindowToken
- WindowState

这样设计的原因是：

- 需要表达显示设备维度
- 需要表达任务/Activity/Window 的包含关系
- 需要在不同层级计算可见性、焦点、bounds、transition、configuration
- 需要支持分屏、PiP、freeform 等复杂模式

---

### 3. 为什么逻辑窗口与 Surface 分离
WMS 管理的是逻辑窗口状态：

- 可见性
- 焦点
- bounds
- token
- layer 规则
- Insets / IME target
- transition 参与关系

SurfaceFlinger 管理的是图形层与合成。
逻辑窗口和 Surface 分离的原因是：

- 逻辑状态与图形合成职责不同
- 状态变化可以先于图像提交
- transition / animation / relayout 等需要逻辑层统一调度
- app、WMS、SF 三方职责解耦

---

### 4. 为什么焦点、Insets、IME、Transition 要分别建模
这些问题经常同时出现，但不能混在一起建模，因为：

- 焦点解决“谁接收输入”
- Insets 解决“哪些区域被系统占用/控制”
- IME 解决“输入法窗口与编辑目标的关系”
- Transition 解决“状态切换与动画/显示时序”

它们相互关联，但各自需要独立决策与状态流转，否则复杂场景下无法维护一致性。

---

### 5. 为什么窗口问题常常是跨层问题
窗口异常的表象可能出现在：

- app：界面没显示
- input：点击打错目标
- ime：输入法不弹
- sf：surface 没更新
- shell：transition 未完成

但首发根因常在更上游逻辑层，因此分析时必须区分：

- 逻辑状态错误
- 事务提交延迟
- 图形层次生影响
- 输入层受害结果

---


<!-- source: 11-architecture-diagram-requirements.md -->

# Architecture Diagram Requirements

分析结果必须包含至少一张 Mermaid 架构图。
按问题复杂度，可以输出以下一种或多种。

### 1. 窗口系统分层图
用于说明：

- App / ViewRootImpl
- WMS / policy / Shell
- SurfaceControl / SurfaceFlinger
- Input / Insets / IME / Display

### 2. 核心对象关系图
用于说明：

- DisplayContent
- Task
- ActivityRecord
- WindowState
- WindowToken
- InsetsSource
- IME target
- Transition participants

### 3. 焦点 / Insets / IME 关系图
用于说明：

- FocusedApp
- FocusedWindow
- Input target
- IME target
- Insets control target

### 4. Transition 协作图
用于说明：

- Activity/Task 状态变化
- TransitionController
- Shell transition handler
- surface collect / start / finish

---


<!-- source: 15-code-explanation-rules.md -->

# Code Explanation Rules

代码解释必须满足以下要求。

### 1. 解释类职责
不能只说“这个类负责窗口管理/焦点/Insets”。
必须说明：

- 它在整个窗口系统中的位置
- 管理哪些核心状态
- 谁调用它
- 它又依赖谁
- 为什么它的状态会影响当前问题

---

### 2. 解释函数语义
不能只说“这个函数更新焦点/可见性/Insets”。
必须说明：

- 输入参数是什么
- 触发条件是什么
- 会修改哪些状态
- 是否会发起事务
- 是否会影响焦点、可见性、layer、Insets、IME、Transition
- 当前问题为什么会走到这个函数

---

### 3. 解释字段与状态
对于关键字段必须说明：

- 字段含义
- 谁更新它
- 何时更新
- 它如何影响判断结果

例如：

- visibility / hidden / drawn / mHasSurface
- focus flags / ime target / insets source state
- transition state / collecting state / ready state
- layout params 与 window type / flags

---

### 4. 解释同步点与事务点
对于关键事务和同步点，必须说明：

- 是谁发起状态更新
- 是谁提交 SurfaceControl 事务
- 是同步状态变更还是异步结果呈现
- 当前异常发生在逻辑状态侧还是事务结果侧

---
