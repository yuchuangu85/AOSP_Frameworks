# 架构与核心机制
<!-- source: 03-source-analysis-mandatory-dimensions.md -->

# Source Analysis Mandatory Dimensions

所有源码分析必须同时覆盖以下四个维度，缺一不可。

### 1. 架构设计思想
必须说明：

- 为什么 Android 需要 InputReader 与 InputDispatcher 分层。
- 为什么输入目标选择与窗口焦点/可接收输入状态要由 WMS 协作提供。
- 为什么输入链路中既有 native 层事件循环，也有 app 侧 ViewRootImpl / Looper / MessageQueue 协作。
- 为什么输入系统要对分发时延、ANR、焦点唯一性、输入安全性进行集中控制。
- 为什么某些问题首发于 InputDispatcher，而某些只是焦点、窗口、App 主线程卡顿的结果。
- 设计中体现了哪些架构原则：
  - 采集与分发解耦
  - 输入目标集中决策
  - 跨进程输入通道模型
  - 焦点唯一性
  - 事件驱动
  - 超时保护与可用性保障
  - 输入与窗口系统协同
  - 安全边界与权限控制

禁止只解释函数调用，而不解释设计意图。

---

### 2. 架构图
必须输出至少一种结构图，必要时多种并用：

- 输入系统分层图
- 核心对象关系图
- InputReader → InputDispatcher → WMS → App 协作图
- Focus / InputTarget / InputChannel 关系图
- 触摸延迟 / Input ANR 依赖图

架构图必须明确当前问题在系统中的位置，不能只列模块名。

推荐使用 Mermaid。

---

### 3. 时序图
必须输出当前场景的关键时序图，包括：

- 正常时序
- 异常时序
- 首发异常点
- 事件生成点
- 事件入队/分发点
- 焦点/目标解析点
- App 接收与消费点
- 超时判定点
- 同步等待点 / 异步回调点 / 消息处理点

禁止只给静态调用链，不给动态时序。

---

### 4. 代码详细解释
必须对关键类、关键函数、关键字段、关键状态流转进行详细解释，包括：

- 输入是什么
- 输出是什么
- 运行在哪个线程/进程
- 事件队列如何变化
- 焦点与输入目标如何解析
- ANR timeout 如何计算
- 输入通道如何建立和使用
- App 侧如何接收并分发到 View 层
- 与当前问题的直接关系是什么

禁止只列路径和类名，不解释代码语义。

---


<!-- source: 10-architecture-design-thinking.md -->

# Architecture Design Thinking

分析时必须从架构层回答以下问题。

### 1. 为什么 Android 需要 InputReader 和 InputDispatcher 分层
Android 输入系统需要同时处理：

- 设备层原始输入采集
- 统一事件标准化
- 多窗口、多应用的目标选择
- 输入安全控制
- 分发超时与无响应保护
- App 侧事件消费

因此输入系统拆分为：

- **InputReader**：负责读取和解释设备原始输入。
- **InputDispatcher**：负责把事件路由到正确目标并监控分发状态。

这种设计使设备采集逻辑与分发决策逻辑解耦，便于支持不同输入设备与复杂窗口环境。

---

### 2. 为什么输入目标必须由系统集中决策
输入不能由 app 自己决定发给谁，因为系统必须保证：

- 焦点唯一性
- 安全边界
- 窗口可见性和可交互性一致
- 系统窗口优先级
- 多窗口、多显示和导航手势统一调度

因此 InputDispatcher 必须与 WMS 协作，基于焦点、可见性、区域、flags 等信息决定输入目标。

---

### 3. 为什么输入系统天然是跨层问题
用户看到“点击没反应”，问题可能出在：

- 输入设备事件未上报
- InputReader 未正确解释
- InputDispatcher 找不到目标
- WMS 焦点/窗口状态错误
- App 主线程堵塞，收到了但没处理
- ANR 触发后输入被取消
- Surface/Transition 结果与用户看到的窗口状态不一致

所以输入分析不能只看 Dispatcher 日志，必须跨 Reader、Dispatcher、WMS、App 和 ANR 链一起看。

---

### 4. 为什么输入系统必须有超时保护
输入是用户交互最敏感的链路之一。
系统必须保证：

- 输入在有限时间内产生可见响应
- 目标窗口长期无响应时能够被检测
- system_server 不会无限等待某个 app
- 无法继续推进的输入链能够被取消或触发 ANR

因此 InputDispatcher 内建等待、监控和 ANR 判定逻辑。

---

### 5. 为什么源码分析必须从设计意图切入
输入问题常见表象是“没收到”“慢了”“打错了”，但根因往往是：

- 目标决策模型错误
- 窗口状态与输入状态不同步
- App 不该在关键链路上阻塞
- 导航/IME/Transition 参与边界不清
- 线程模型与同步等待设计失衡

所以必须从“为什么这样设计”和“设计在哪里失衡”来理解代码问题。

---


<!-- source: 11-architecture-diagram-requirements.md -->

# Architecture Diagram Requirements

分析结果必须包含至少一张 Mermaid 架构图。
按问题复杂度，可以输出以下一种或多种。

### 1. 输入系统分层图
用于说明：

- 输入设备
- EventHub / InputReader
- InputDispatcher
- WMS / Focus / InputTarget
- InputChannel
- App / ViewRootImpl / View

### 2. 核心对象关系图
用于说明：

- InputReader
- InputDispatcher
- Connection
- InputTarget
- FocusedWindow
- FocusedApp
- InputChannel
- WindowInputEventReceiver

### 3. 焦点 / 目标关系图
用于说明：

- WMS 焦点状态
- Dispatcher 输入目标
- App 实际接收窗口
- IME / overlay / system window 对目标选择的影响

### 4. Input ANR 依赖图
用于说明：

- Dispatcher wait queue
- 目标窗口
- App main
- system_server / WMS / Binder
- 首发阻塞点

---
