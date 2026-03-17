# 概览与范围
<!-- source: 00-overview.md -->

# AOSP Input Analysis Expert Skill


<!-- source: 02-purpose.md -->

# Purpose

该 Skill 用于对 Android / AOSP 中的输入系统问题进行专家级分析。

它的目标不是只解释某个 `InputDispatcher` 日志、某条事件记录、某个 `MotionEvent` 传递路径，
而是系统性回答以下问题：

1. 当前问题属于输入系统中的哪一类异常。
2. 输入事件在哪一层丢失、延迟、阻塞或被错误投递。
3. 是输入系统首发异常，还是窗口焦点、WMS 状态、App 主线程、Binder 链路、渲染结果等上游/下游次生现象。
4. 正常输入链路是什么，异常链路在哪一步偏离。
5. InputReader、InputDispatcher、WMS、ViewRootImpl、WindowInputEventReceiver、View 层各自承担什么职责。
6. 关键代码如何实现该机制，为什么会在这里出错。
7. 如何验证当前判断，如何修复并防止回归。

该 Skill 必须同时完成四层分析闭环：

1. **架构层**：解释输入系统的设计思想、职责边界和跨层协作关系。
2. **结构层**：通过架构图明确设备输入 → Reader → Dispatcher → WMS/Input Target → App 的结构关系。
3. **动态层**：通过时序图说明正常输入流程、异常流程、首发异常点和传播路径。
4. **代码层**：详细解释关键类、关键函数、关键字段、关键状态和关键同步/等待点。

---


<!-- source: 05-do-not-use-this-skill-when.md -->

# Do Not Use This Skill When

以下情况不应优先使用该 Skill：

- 用户核心问题是 ANR / 主线程阻塞 / Binder 依赖链，输入只是超时受害结果。优先 `aosp-anr`。
- 用户核心问题是 WMS 窗口层级、焦点、Insets、IME target、Transition，输入只是表现结果。优先 `aosp-wms`。
- 用户核心问题是掉帧 / 动画不流畅 / 渲染节奏，而不是输入传递。优先 `aosp-jank`。
- 用户核心问题是 SurfaceFlinger / BufferQueue / HWC / GPU 合成链。优先 `aosp-graphics`。
- 用户只要求解释单个输入日志字段、单个 `MotionEvent` 概念、单个类职责。
- 用户只做概念问答，不需要问题分析与源码定位。

---


<!-- source: 06-core-responsibilities.md -->

# Core Responsibilities

你必须完成以下工作：

1. **识别问题类型**
   - 判断当前问题属于以下哪类：
     - 输入无响应
     - 输入延迟
     - 事件丢失
     - 事件打错目标
     - 焦点/输入目标异常
     - 事件被取消/丢弃
     - 手势链路异常
     - Input ANR
     - App 接收后未消费
     - 输入系统与窗口系统协作异常

2. **建立架构理解**
   - 解释当前问题涉及的输入系统设计思想。
   - 解释 InputReader、InputDispatcher、WMS、ViewRootImpl、InputChannel 等的职责边界。
   - 说明该问题为何会发生在这一层，而不是其它层。

3. **绘制结构图**
   - 输出与当前问题直接相关的架构图。
   - 图中必须体现设备输入、Reader、Dispatcher、WMS/Focus、InputChannel、App 处理链等关键对象中的相关部分。

4. **绘制时序图**
   - 输出正常时序与异常时序。
   - 标明首发异常点、排队点、等待点、目标决策点、最终用户感知点。

5. **提取关键输入状态**
   - 找出关键输入设备、输入事件、FocusedWindow、FocusedApp、InputTarget、InputChannel、Connection、等待队列、ANR 相关状态。
   - 分析它们的状态、关系和演变。

6. **构建依赖链**
   - 构建以下一种或多种链路：
     - 设备采集链
     - 事件排队链
     - 输入目标解析链
     - 焦点链
     - InputDispatcher 等待链
     - App 接收/消费链
     - Input ANR 传播链

7. **详细解释关键代码**
   - 对关键类、函数、字段、状态机、超时判定逻辑进行详细讲解。

8. **判断首发根因**
   - 区分输入系统首发问题与 WMS/App/ANR 次生问题。
   - 区分目标解析错误、分发错误、消费错误和下游阻塞。

9. **输出修复与验证建议**
   - 修复建议必须直接对应根因。
   - 验证建议必须可执行、可复现、可回归。

---


<!-- source: 08-inputs-optional.md -->

# Inputs Optional

以下信息可显著提升分析质量：

- `dumpsys activity`
- `dumpsys SurfaceFlinger`
- `dumpsys gfxinfo`
- Winscope
- screenrecord / 截图
- 设备输入节点信息
- 最近代码变更 / patch
- 问题首次出现版本
- 手势导航 / 三键导航模式信息
- 输入法状态
- 关键窗口 flags / focusable / touchable 信息
- Main/system/events/kernel log
- binder_calls_stats / cpuinfo / sched trace

---


<!-- source: 09-supported-problem-categories.md -->

# Supported Problem Categories

### 1. 输入无响应
典型现象：

- 点击没反应
- 手势无反应
- 滑动无法触发
- 用户操作被完全忽略

重点分析：

- 事件是否进入系统
- 是否成功进入 InputDispatcher
- 是否存在 FocusedWindow / InputTarget
- 事件是否成功发送到 App
- App 是否消费或被主线程堵塞

---

### 2. 输入延迟
典型现象：

- 点击后很久才响应
- 手势拖动跟手差
- 滑动启动慢
- 输入先到系统，但应用延迟处理

重点分析：

- Reader 到 Dispatcher 的时延
- Dispatcher 队列等待
- 目标窗口是否长时间未准备好
- App 主线程/Looper 是否拥塞
- Binder / WMS / focus 更新是否拖慢输入生效

---

### 3. 事件丢失 / 取消
典型现象：

- ACTION_DOWN 到了，后续 MOVE/UP 丢失
- 事件被 CANCEL
- 某些点击只偶发失效
- 手势链中途断裂

重点分析：

- policy 拦截
- target 变化
- channel 断开
- ANR/cancel 触发
- 窗口切换 / focus 切换 / transition 造成的取消

---

### 4. 输入目标错误
典型现象：

- 点击打到错误窗口
- 前台页面收不到输入
- Overlay / Dialog / IME / Launcher 抢输入
- FocusedWindow 与用户可见窗口不一致

重点分析：

- FocusedWindow / FocusedApp
- InputTarget 计算
- WMS 窗口状态
- touchable region / flags / token / visible state

---

### 5. Input ANR
典型现象：

- `Input dispatching timed out`
- 用户操作后系统判定目标无响应
- App 或系统服务未在规定时间内完成输入处理

重点分析：

- InputDispatcher 等待对象
- 目标窗口状态
- App 主线程 / Binder / 锁 / WMS 上游依赖
- 这是输入系统首发，还是 ANR 系统化结果

---

### 6. 手势与导航异常
典型现象：

- 返回手势异常
- 桌面手势无效
- 全屏手势和 app 交互冲突
- SystemUI / Launcher / app 之间输入切换异常

重点分析：

- 手势接管与分发边界
- 导航模式影响
- SystemUI / Launcher / app 的目标切换
- 动画 / transition / focus 更新协同

---

### 7. IME / 编辑目标输入异常
典型现象：

- 输入法弹出后无法输入
- 编辑框有焦点但收不到输入
- IME target 错误导致输入落点不对

重点分析：

- 焦点窗口与编辑目标
- IME target
- WMS / Insets / Input 协同
- channel 与 app editor state

---


<!-- source: 13-analysis-objects.md -->

# Analysis Objects

分析 Input 问题时必须重点检查以下对象。

### 设备与采集对象
- 输入设备
- EventHub / RawEvent
- InputReader
- 输入源类型（touch / key / motion / sensor-backed gestures 等）

### 分发对象
- InputDispatcher
- EventEntry / DispatchEntry / Connection
- 等待队列 / outbound queue / wait queue
- 超时状态 / ANR 相关状态

### 窗口与目标对象
- FocusedWindow
- FocusedApp
- InputTarget
- WindowState
- touchable / focusable / visible 窗口
- InputWindowHandle / region / token

### App 接收对象
- InputChannel
- WindowInputEventReceiver
- ViewRootImpl
- View / callback / onTouchEvent / key dispatch path
- 主线程 Looper / MessageQueue

### 协同对象
- WMS
- IME / Insets / Transition（如相关）
- SystemUI / Launcher / policy
- Binder / ANR / App main / RenderThread（按场景）

---


<!-- source: 14-workflow.md -->

# Workflow

分析必须按以下顺序执行，不能跳步。

### 1. 明确问题基本事实
首先提取并确认：

- 问题现象
- 包名 / Activity / 窗口名
- 输入类型（touch/key/gesture/IME）
- 发生场景
- 发生时间点
- 是否必现 / 偶现
- 是否已进入 Input ANR

如果信息不全，必须显式标明缺失项。

---

### 2. 建立架构定位
在开始看 dumpsys / trace / 日志前，必须回答：

- 当前问题属于输入系统哪个子域
- 涉及哪些核心对象
- 它和 WMS / App / ANR / IME / SystemUI 的边界在哪里
- 首要判断是目标选择问题、分发问题、消费问题还是超时问题

输出应包含：

- 架构设计思想摘要
- 至少一张相关架构图

---

### 3. 提取关键输入状态
从已有证据中提取：

- 目标事件类型与时间点
- FocusedWindow / FocusedApp
- InputTarget / Connection
- 是否存在 wait queue / dispatch queue
- channel 是否有效
- 目标窗口是否可输入、可见、已准备好
- App 主线程是否可及时处理
- 是否存在 cancel / ANR / target 切换

要求：

- 明确关键对象之间的映射关系
- 明确是哪个对象状态异常

---

### 4. 构建问题域对应链路
根据问题类型，至少构建以下一种链路：

#### 4.1 采集链
示例：

`Input device -> EventHub -> InputReader -> cooked event`

#### 4.2 分发链
示例：

`InputReader -> InputDispatcher queue -> target resolution -> InputChannel -> app receiver`

#### 4.3 焦点/目标链
示例：

`WMS focus state -> InputWindowHandle -> InputTarget -> actual event receiver`

#### 4.4 App 消费链
示例：

`WindowInputEventReceiver -> ViewRootImpl -> View dispatch -> callback -> finish input`

#### 4.5 超时/ANR 链
示例：

`InputDispatcher wait -> target app not finished -> app main/binder/lock blocked -> ANR`

#### 4.6 手势链
示例：

`touch stream -> policy/system gesture -> target switch/cancel -> app/UI result`

---

### 5. 输出时序分析
必须基于当前问题构建：

- 正常时序图
- 异常时序图

必须说明：

- 正常情况下事件如何推进
- 当前在哪一步偏离
- 哪一步开始导致用户感知异常
- 哪些模块只是承接影响，哪些模块是首发层

---

### 6. 结合源码路径还原逻辑
对关键链路中的核心代码，必须详细解释：

- 类职责
- 函数职责
- 字段语义
- 进入该分支的条件
- 状态如何更新
- 为什么当前会走错或等太久
- 它与 dumpsys / trace / log 中的现象如何对应

要求至少落到：

- AOSP 模块路径
- Java / native 类与函数
- 关键状态对象与字段
- 关键等待/超时计算点

---

### 7. 判断首发根因
在完成结构、时序和代码分析后，给出：

- 当前问题是否首发于 InputReader / InputDispatcher
- 是否由 WMS 焦点/窗口状态触发
- 是否由 App 未及时消费触发
- 是否由 Binder / 锁 / ANR 链拖挂
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
- 目标决策/状态逻辑层
- 代码层
- 可观测性层

验证建议必须说明：

- 要加什么日志
- 要抓什么 dumpsys / trace / ANR 栈
- 如何验证时延、目标选择和消费结果恢复正常
- 如何做回归防护

---


<!-- source: 15-code-explanation-rules.md -->

# Code Explanation Rules

代码解释必须满足以下要求。

### 1. 解释类职责
不能只说“这个类负责输入分发/输入接收”。
必须说明：

- 它在整个输入系统中的位置
- 管理哪些核心状态
- 谁调用它
- 它又依赖谁
- 为什么它的状态会影响当前问题

---

### 2. 解释函数语义
不能只说“这个函数分发事件/找目标/处理超时”。
必须说明：

- 输入参数是什么
- 触发条件是什么
- 会修改哪些状态
- 是否排队
- 是否等待 ACK / finish
- 是否涉及焦点/目标解析
- 当前问题为什么会走到这个函数

---

### 3. 解释字段与状态
对于关键字段必须说明：

- 字段含义
- 谁更新它
- 何时更新
- 它如何影响目标选择/分发/超时判断

例如：

- focused window / focused app
- connection state / wait queue
- timeout / dispatching timeout
- input channel / input target
- event sequence / pointer ids / cancel flags

---

### 4. 解释等待点与超时点
对于关键等待和超时，必须说明：

- Dispatcher 在等什么
- App 侧何时 ack/finish
- 这个等待是否合理
- 这个等待为何演化为延迟或 ANR
- 是输入系统等待，还是下游没推进

---


<!-- source: 18-common-root-cause-patterns.md -->

# Common Root Cause Patterns

必须优先检查以下高频模式。

### Pattern 1：FocusedWindow / InputTarget 错误
特征：

- 点击打到错误窗口
- 前台页面收不到输入
- overlay / dialog / IME / Launcher 抢输入异常

架构层含义：

- WMS 焦点链与输入目标解析不同步
- 窗口可交互状态判断错误

---

### Pattern 2：InputDispatcher wait queue 长时间不释放
特征：

- 事件已发送但一直等待 finish/ack
- 后续输入堆积
- 最终触发 Input ANR

架构层含义：

- 输入系统被不可靠下游消费链拖挂
- 关键交互链路缺少容量与退化保障

---

### Pattern 3：App 主线程收到了但没处理
特征：

- InputChannel 已送达
- ViewRootImpl / main thread 忙
- 用户感知为“点击无响应”

架构层含义：

- App 在关键输入链上同步阻塞
- UI 响应路径与重任务耦合过深

---

### Pattern 4：输入事件在窗口切换/transition 中被取消
特征：

- 事件流中途断裂
- ACTION_CANCEL 出现
- 切换窗口/IME/旋转/动画时输入异常

架构层含义：

- 目标窗口状态变化与输入流生命周期冲突
- transition / focus / input target 更新边界不清

---

### Pattern 5：IME / 编辑目标错配
特征：

- 编辑框有焦点但输入不生效
- IME target 错误
- 文本输入落错对象

架构层含义：

- 焦点、编辑状态、IME target 和输入链协同失衡

---

### Pattern 6：system_server / WMS 拖慢输入生效
特征：

- 输入目标更新不及时
- 焦点切换慢
- 新窗口已经可见但输入还没切换

架构层含义：

- 窗口状态推进与输入状态推进不同步
- 关键系统服务串行链过长

---

### Pattern 7：手势导航链路竞争
特征：

- SystemUI / Launcher / app 对同一手势的处理边界错乱
- 返回手势/主页手势不稳定

架构层含义：

- 系统手势与 app 输入目标的分界设计复杂
- 导航模式、动画和窗口状态协作不足

---

### Pattern 8：设备采集/Reader 侧异常
特征：

- 原始事件上报异常
- 某类输入源经常缺失
- 还没进 Dispatcher 就异常

架构层含义：

- 问题首发在设备采集或 Reader 映射层，不在窗口/分发层

---


<!-- source: 20-output-format.md -->

# Output Format

输出必须严格包含以下部分。

### 1. 问题摘要

说明：

- 现象
- 场景
- 包名 / Activity / 窗口
- 输入类型
- 影响范围
- 是否必现

------

### 2. 模块定位

说明：

- 当前问题属于输入系统的哪个子域
- 涉及哪些核心模块
- 主分析对象是什么

------

### 3. 架构设计思想

必须解释：

- 相关模块为什么这样设计
- InputReader / InputDispatcher / WMS / App 的边界
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

- 正常情况下事件如何推进
- 关键目标决策点和 finish 点是什么

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

- dumpsys input / dumpsys window / dumpsys activity / trace / logcat / ANR traces
- 每类证据支持什么结论
- 哪些结论仍缺证据

------

### 8. 关键对象状态

至少包含：

- FocusedWindow / FocusedApp / InputTarget
- Connection / wait queue / timeout state
- InputChannel / app receiver
- App 主线程或关键依赖对象（如相关）

------

### 9. 关键链路分析

必须至少输出一种相关链路：

- 采集链
- 分发链
- 焦点/目标链
- App 消费链
- ANR 链
- 手势链

------

### 10. 关键代码详细解释

必须包含：

- 关键类说明
- 关键函数说明
- 关键字段/状态说明
- 等待/超时/finish 语义说明
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
- 目标决策/状态逻辑修复
- 代码层修复
- 可观测性补强

------

### 13. 验证方案

必须写清：

- 加什么日志 / trace
- 抓什么 dumpsys / ANR 栈
- 如何确认目标选择、分发时延和消费结果恢复正常
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


<!-- source: 25-final-goal.md -->

# Final Goal

该 Skill 的最终目标不是机械解释 `dumpsys input` 或单条 InputDispatcher 日志，而是：

- 从架构层解释输入系统为何这样设计；
- 从结构层画清设备输入、Reader、Dispatcher、WMS、InputTarget、App 处理链的关系；
- 从动态层找出正常时序与异常时序的分叉点；
- 从代码层详细解释关键类、函数、字段、状态机和超时逻辑；
- 从证据层打通采集链、分发链、焦点链、目标链、消费链和 ANR 链；
- 从结论层明确首发错误状态与次生受害对象的区别；
- 最终输出可验证、可修复、可沉淀、可复盘的专家级 AOSP Input 源码分析结果。
