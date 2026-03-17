# 概览与范围
<!-- source: 00-overview.md -->

# AOSP WMS Analysis Expert Skill


<!-- source: 05-do-not-use-this-skill-when.md -->

# Do Not Use This Skill When

以下情况不应优先使用该 Skill：

- 用户核心问题是 ANR / 主线程阻塞 / Binder 链阻塞，且窗口只是受影响现象。优先 `aosp-anr`。
- 用户核心问题是掉帧 / FrameTimeline / 渲染性能，而不是窗口状态错误。优先 `aosp-jank`。
- 用户核心问题是 InputReader / InputDispatcher / 事件分发时延，且焦点/窗口不是主因。优先 `aosp-input`。
- 用户核心问题是 SurfaceFlinger / BufferQueue / HWC / GPU 图形合成链，而不是窗口逻辑状态。优先 `aosp-graphics`。
- 用户只要求解释单个 `LayoutParams` 字段、单个 dumpsys 字段或单个函数。
- 用户只做概念问答，不需要问题分析与源码定位。

---


<!-- source: 09-supported-problem-categories.md -->

# Supported Problem Categories

### 1. 窗口可见性异常
典型现象：

- Activity 已启动但窗口不显示
- 窗口显示延迟
- 窗口突然消失
- 应显示窗口被标记为隐藏/不可见

重点分析：

- `WindowState` 可见性
- `ActivityRecord` / `Task` 生命周期
- 是否完成 relayout / draw / commit
- Transition 是否拦截显示
- Surface 是否已创建与提交

---

### 2. 焦点异常
典型现象：

- 输入打不到预期窗口
- 焦点落在旧窗口/错误窗口
- IME target 错误
- 弹窗/对话框/子窗口抢焦点异常

重点分析：

- FocusedWindow
- FocusedApp
- DisplayContent 焦点链
- Window token / Task / Activity 状态
- WMS 与 Input 的焦点协同关系

---

### 3. 层级 / z-order 异常
典型现象：

- 窗口被错误遮挡
- 顶层窗口没在最上层
- 系统窗口 / 应用窗口层级不对
- IME / Dialog / Overlay 层级异常

重点分析：

- Window type
- token / parent-child 关系
- layer 分配
- relative layer / z-order 规则
- SurfaceControl 层级提交链

---

### 4. Insets / Cutout / SystemBars 异常
典型现象：

- 内容被状态栏/导航栏遮挡
- Insets 不更新
- Cutout 适配错位
- edge-to-edge 显示异常
- 系统栏行为与窗口状态不匹配

重点分析：

- InsetsState / InsetsSource
- InsetsControlTarget
- policy 与 app controllable insets
- Window flags / fitInsets / decorFitsSystemWindows
- WMS / policy / app 的协同关系

---

### 5. IME 异常
典型现象：

- 输入法不弹
- 输入法弹出位置错误
- 错误窗口成为 IME target
- 输入法遮挡/动画错误

重点分析：

- IME target 计算
- InsetsSourceProvider / ImeInsetsSourceProvider
- FocusedWindow 与编辑目标关系
- IME show/hide request 时序
- Transition/Insets/Focus 协同

---

### 6. Transition / ShellTransition 异常
典型现象：

- 页面切换卡住
- Activity 已切换但画面不更新
- 动画没播完/不播放
- Shell transition 卡死或状态错乱

重点分析：

- Transition / TransitionController
- ShellTransition / legacy transition
- collect / start / play / finish 时序
- 参与者收集与目标 surface 状态
- 可见性提交与动画同步关系

---

### 7. Rotation / Configuration 异常
典型现象：

- 旋转后窗口错位
- 配置变更后窗口显示异常
- 旋转过程中焦点/Insets/IME 状态错误

重点分析：

- Display rotation
- configuration dispatch
- relayout / resize / draw
- rotation transition
- surface 尺寸与 logical bounds 更新链

---

### 8. 多窗口 / 分屏 / PiP 异常
典型现象：

- 分屏窗口显示错位
- PiP 进入/退出异常
- 多任务层级和焦点错乱

重点分析：

- RootWindowContainer / TaskDisplayArea / Task
- windowing mode
- bounds 更新链
- focus 与 visibility 规则
- transition 与 surface 目标映射

---


<!-- source: 13-analysis-objects.md -->

# Analysis Objects

分析 WMS 问题时必须重点检查以下对象。

### Display / 容器对象
- RootWindowContainer
- DisplayContent
- TaskDisplayArea
- Task / TaskFragment
- ActivityRecord

### 窗口对象
- WindowState
- WindowToken / AppWindowToken（按版本差异理解）
- 子窗口 / 对话框 / 系统窗口 / IME 窗口
- `WindowManager.LayoutParams`

### 焦点对象
- FocusedWindow
- FocusedApp
- 输入目标相关窗口
- 可能抢焦点的 overlay / dialog / system window

### Insets / IME 对象
- InsetsState
- InsetsSource
- InsetsSourceProvider
- InsetsControlTarget
- IME target / ImeInsetsSourceProvider

### Transition 对象
- Transition
- TransitionController
- ShellTransition
- 参与 transition 的 WindowContainer / Surface

### 图形协作对象
- SurfaceControl
- BLAST / Buffer 提交链（按版本和问题场景）
- 与 WMS 逻辑状态对应的 surface 状态

---


<!-- source: 14-workflow.md -->

# Workflow

分析必须按以下顺序执行，不能跳步。

### 1. 明确问题基本事实
首先提取并确认：

- 问题现象
- 包名 / Activity / 窗口名
- 发生场景
- 发生时间点
- 是否必现 / 偶现
- 是否与启动 / 切换 / IME / 旋转 / 多窗口 / Transition 相关

如果信息不全，必须显式标明缺失项。

---

### 2. 建立架构定位
在开始看 dumpsys / winscope / 代码前，必须回答：

- 当前问题属于窗口系统哪个子域
- 涉及哪些核心对象
- 它和 Input / AMS / Shell / SurfaceFlinger 的边界在哪里
- 首要判断是逻辑状态问题还是图形结果问题

输出应包含：

- 架构设计思想摘要
- 至少一张相关架构图

---

### 3. 提取关键窗口状态
从已有证据中提取：

- 目标窗口
- ActivityRecord / Task
- DisplayContent
- FocusedWindow / FocusedApp
- 是否可见 / hidden / drawn / relayout / surface exists
- Insets / IME target 状态
- transition 是否进行中
- layer / type / token / parent-child 关系

要求：

- 明确关键对象之间的映射关系
- 明确是哪个对象状态异常

---

### 4. 构建问题域对应链路
根据问题类型，至少构建以下一种链路：

#### 4.1 可见性链
示例：

`Activity lifecycle -> ActivityRecord visible request -> WindowState visibility -> relayout/draw -> surface shown`

#### 4.2 焦点链
示例：

`Activity/Task top state -> DisplayContent focus calculation -> FocusedWindow -> Input target`

#### 4.3 层级链
示例：

`Window type/token/parent -> layer assignment -> SurfaceControl z-order -> actual occlusion`

#### 4.4 Insets 链
示例：

`InsetsSourceProvider -> InsetsState -> control target -> app receive -> layout adjust`

#### 4.5 IME target 链
示例：

`Focused editor window -> IME target selection -> showIme request -> ime window/insets update`

#### 4.6 Transition 链
示例：

`Activity/Task state change -> collect participants -> start transition -> animation/play -> finish -> commit visible state`

---

### 5. 输出时序分析
必须基于当前问题构建：

- 正常时序图
- 异常时序图

必须说明：

- 正常情况下状态如何推进
- 当前在哪一步偏离
- 哪一步开始导致用户可见异常
- 哪些模块只是承接影响，哪些模块是首发层

---

### 6. 结合源码路径还原逻辑
对关键链路中的核心代码，必须详细解释：

- 类职责
- 函数职责
- 字段语义
- 进入该分支的条件
- 状态如何更新
- 为什么当前会走错
- 它与 dumpsys / winscope / log 中的现象如何对应

要求至少落到：

- AOSP 模块路径
- Java / native 类与函数
- 关键状态对象与字段
- 关键事务提交点

---

### 7. 判断首发根因
在完成结构、时序和代码分析后，给出：

- 当前问题是否首发于 WMS
- 是否由 AMS / Shell / Input / App / SF 触发
- 哪个状态是第一个错误状态
- 哪个对象是受害者
- 哪个对象是错误源

格式上必须区分：

- 已证实根因
- 高概率候选根因
- 待验证假设

---

### 8. 输出修复与验证建议
修复建议必须分别落到：

- 架构层
- 状态机/决策逻辑层
- 代码层
- 可观测性层

验证建议必须说明：

- 要加什么日志
- 要抓什么 dumpsys / winscope / trace
- 如何验证状态流转已正确
- 如何做回归防护

---


<!-- source: 17-root-cause-decision-rules.md -->

# Root Cause Decision Rules

### Rule 1：窗口不显示不等于图形问题
窗口未显示可能来自：

- Activity / Task 状态未推进
- WindowState 逻辑不可见
- Transition 未完成
- relayout / draw 未完成
- Surface 未创建或未提交
- layer 被更高层遮挡

必须先确定首发层。

---

### Rule 2：焦点错误不等于 Input 根因
Input 只是使用焦点结果。
若焦点计算错误，首发根因更可能在 WMS / DisplayContent / WindowContainer 状态，而不是输入分发本身。

---

### Rule 3：Insets/IME 异常不能只看 app
Insets 和 IME 同时受以下因素影响：

- FocusedWindow
- IME target
- InsetsSourceProvider
- WMS policy
- app controllable insets
- transition / animation 时序

不能因为 app 布局错位，就直接断定是 app 代码问题。

---

### Rule 4：Transition 异常要区分“逻辑未推进”和“动画未收尾”
页面卡住可能有两类根因：

- 状态逻辑没推进，transition 一直未准备好或未 finish
- 状态已推进，但 surface/动画结果未正确呈现

必须明确是哪一类。

---

### Rule 5：窗口树错误比单点日志更可信
单条日志可能只反映某次尝试。
窗口树 / Winscope / dumpsys 的一致状态更能说明根因所在。

---

### Rule 6：优先找第一个错误状态
不要把最终用户看到的窗口结果当作首发点。
必须回答：

- 第一个错误状态是什么
- 是哪一个对象第一次偏离正常值
- 这个错误如何传播到最终现象

---


<!-- source: 18-common-root-cause-patterns.md -->

# Common Root Cause Patterns

必须优先检查以下高频模式。

### Pattern 1：Activity 已启动但 WindowState 未进入可见链
特征：

- 生命周期已推进
- 但窗口未 visible / 未 drawn / 未 show
- 可能被 transition 或 relayout 阻塞

架构层含义：

- Activity / Window 生命周期耦合点失配
- 状态推进和 surface 提交不同步

---

### Pattern 2：FocusedWindow 错误
特征：

- 输入打到旧窗口或错误窗口
- 前台窗口未获得焦点
- IME target 随之错误

架构层含义：

- DisplayContent 焦点链或窗口可交互性判断错误
- 顶层逻辑状态与实际可见窗口不一致

---

### Pattern 3：窗口层级 / token 关系错误
特征：

- 对话框 / overlay / IME / 系统窗口遮挡异常
- 子窗口顺序不对
- 应用窗口不在预期层级

架构层含义：

- token / type / parent-child / layer 规则使用错误
- 逻辑层级与展示层级映射异常

---

### Pattern 4：InsetsSource / control target 错误
特征：

- 系统栏区域计算错误
- app 没收到正确 Insets
- edge-to-edge / cutout 适配异常

架构层含义：

- 系统区域占用状态与 app 控制权边界混乱
- policy 与 app 侧协作关系不清

---

### Pattern 5：IME target 计算错误
特征：

- 输入法不弹 / 弹错目标
- 焦点窗口正确但 IME target 不正确
- Transition / focus / editor state 时序错位

架构层含义：

- 输入法目标决策与焦点、编辑状态、可见性链未正确同步

---

### Pattern 6：Transition collect/start/finish 链异常
特征：

- 切换卡住
- 动画不结束
- 新窗口状态不生效或延迟生效

架构层含义：

- 状态切换与显示提交耦合失衡
- Shell / WMS / surface 参与者收集和提交顺序错误

---

### Pattern 7：旋转过程中的 bounds / configuration / surface 不一致
特征：

- 旋转后错位、黑边、布局异常
- focus / ime / insets 状态错乱

架构层含义：

- display 配置变更与窗口更新链未保持一致性
- transition 与 relayout 时序冲突

---

### Pattern 8：WMS 状态正确但图形结果未呈现
特征：

- dumpsys / winscope 显示逻辑状态已正确
- 但用户仍看不到预期画面

架构层含义：

- 首发不在 WMS，可能在 Surface / Buffer / SF 侧
- WMS 是逻辑正确的受害层

---


<!-- source: 25-final-goal.md -->

# Final Goal

该 Skill 的最终目标不是机械解释 `dumpsys window` 或单个 `WindowState`，而是：

- 从架构层解释窗口系统为何这样设计；
- 从结构层画清 Display / Task / Activity / Window / Insets / IME / Transition 的关系；
- 从动态层找出正常时序与异常时序的分叉点；
- 从代码层详细解释关键类、函数、字段和状态机；
- 从证据层打通焦点链、可见性链、层级链、Insets 链、IME target 链、Transition 链；
- 从结论层明确首发错误状态与次生受害对象的区别；
- 最终输出可验证、可修复、可沉淀、可复盘的专家级 AOSP WMS 源码分析结果。
