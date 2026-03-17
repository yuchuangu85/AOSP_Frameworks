---
name: aosp-handler
description: 用于分析 AOSP 中 Handler / Looper / MessageQueue / Message 的源码实现、线程消息循环机制、同步屏障、异步消息、IdleHandler、消息分发时序与性能问题。适用于 Framework、SystemServer、App 主线程、后台线程中的消息驱动逻辑分析、调用链还原、调度延迟排查与架构设计理解。
---


# AOSP Handler 源码分析 Skill

## 统一输出要求

- 所有输出文档必须保存到仓库根目录的 `docs/` 目录下。
- 文档文件必须使用 Markdown 格式，文件扩展名为 `.md`。
- 如果用户未指定文件名，使用与任务主题相关的语义化文件名。
- 每次执行本子 Skill，必须单独输出 1 个独立的 Markdown 分析文件，禁止只把内容并入总控回答而不落独立文件。
- 独立文件名必须包含当前子 Skill 名称或其对应问题域，便于总控汇总时直接引用。
- 独立输出文件名必须遵循统一模式：`docs/<skill-slug>-<topic>.md`。
- 其中 `<skill-slug>` 必须使用当前 Skill 的规范英文标识，例如 `aosp-ams`、`aosp-wms`、`aosp-surfaceflinger`。
- 其中 `<topic>` 必须是当前分析主题的语义化短名，使用小写英文和连字符，禁止空格。
- 如果用户未提供主题名，则默认使用 `analysis` 作为 `<topic>`。


## Skill 目标

该 Skill 用于对 Android AOSP 中 **Handler 消息机制**进行系统级源码分析，输出可验证、可落地、可复用的分析结果。分析目标包括但不限于：

- 理解 `Handler / Looper / MessageQueue / Message` 的职责边界与协作关系
- 还原消息从发送到执行的完整调用链
- 分析主线程与子线程消息循环模型
- 分析同步消息、异步消息、同步屏障（Sync Barrier）机制
- 分析 `IdleHandler` 的执行时机与使用场景
- 分析 `post` / `sendMessage` / `sendMessageDelayed` / `postAtFrontOfQueue` 等行为差异
- 分析消息延迟、队列阻塞、消息堆积、优先级错配等性能问题
- 分析 Framework/SystemServer 中基于 Handler 的模块化调度设计
- 输出源码证据、架构图、时序图、关键类关系、风险点与优化建议

---

## 适用场景

当用户有以下需求时调用本 Skill：

- 分析 Android Handler 机制原理
- 分析主线程 Looper 为什么不会退出
- 分析 MessageQueue 如何按时间顺序调度消息
- 分析消息为何延迟执行或不执行
- 分析某个系统服务为何使用 HandlerThread / Handler
- 分析同步屏障对 UI 刷新、输入、动画的影响
- 分析 `Handler.post(Runnable)` 与 `sendMessage(Message)` 的底层差异
- 分析 `IdleHandler` 执行时机
- 分析 Handler 消息队列卡顿、堆积、饿死、乱序问题
- 分析 Java 层 Handler 与 Native Looper / epoll 的关系
- 构建某条消息处理链的完整时序和架构视图
- 对 AOSP 中任意依赖 Handler 机制的模块做专项源码分析

---

## 分析范围

### 1. Java Framework 层核心类

重点分析以下类：

- `android.os.Handler`
- `android.os.Looper`
- `android.os.MessageQueue`
- `android.os.Message`
- `android.os.HandlerThread`
- `android.os.TestLooperManager`（如需要）
- `android.view.Choreographer`（涉及消息调度时）
- `android.view.ViewRootImpl`（涉及 UI 主线程消息时）
- `com.android.server.*` 中典型 Handler 使用者

---

### 2. Native / JNI 关联层

用于理解消息循环底座：

- `android_os_MessageQueue.cpp`
- `android_os_Looper.cpp`
- `utils/Looper.cpp`
- `libutils`
- `epoll_wait`
- `eventfd`
- `nativeWake`
- `nativePollOnce`

---

### 3. 运行时协同对象

根据分析对象扩展到：

- `ActivityThread`
- `ViewRootImpl`
- `Choreographer`
- `InputEventReceiver`
- `DisplayEventReceiver`
- `SystemServer` 中的服务线程
- `AMS / WMS / PMS / Input / SurfaceFlinger` 上层调度入口

---

## 必须产出的分析结果

每次执行该 Skill，必须尽量输出以下内容：

### 1. 模块职责与设计思想

说明：

- Handler 机制解决的核心问题是什么
- 为什么 Android 采用单线程消息循环模型
- 为什么消息驱动比直接跨线程调用更安全
- Looper / Handler / MessageQueue 的职责如何拆分
- 为什么 MessageQueue 不是真正意义上的简单 FIFO 队列
- 为什么存在同步屏障与异步消息
- 为什么 Java 层消息机制需要 Native poll/wake 支撑

---

### 2. 核心类关系图

必须输出清晰的类关系说明，例如：

- `Thread` 持有一个 `Looper`
- `Looper` 持有一个 `MessageQueue`
- `Handler` 绑定一个 `Looper` 和对应 `MessageQueue`
- `Message` 持有 target（Handler）
- `MessageQueue` 以时间排序维护消息链表
- `Looper.loop()` 持续从 `MessageQueue.next()` 取消息并分发

---

### 3. 完整调用链

至少还原以下一种或多种调用链：

#### `post(Runnable)` 调用链
`Handler.post`  
→ `getPostMessage`  
→ `sendMessageDelayed`  
→ `sendMessageAtTime`  
→ `enqueueMessage`  
→ `MessageQueue.enqueueMessage`  
→ `Looper.loop`  
→ `MessageQueue.next`  
→ `Handler.dispatchMessage`  
→ `handleCallback`  
→ `Runnable.run`

#### `sendMessage(Message)` 调用链
`Handler.sendMessage`  
→ `sendMessageDelayed`  
→ `sendMessageAtTime`  
→ `enqueueMessage`  
→ `MessageQueue.enqueueMessage`  
→ `Looper.loop`  
→ `MessageQueue.next`  
→ `Handler.dispatchMessage`  
→ `handleMessage`

#### `HandlerThread` 启动链
`HandlerThread.start`  
→ `Thread.run`  
→ `Looper.prepare`  
→ `Looper.loop`  
→ 外部 `getLooper`  
→ 创建绑定该 Looper 的 `Handler`

#### Native poll/wake 调用链
`MessageQueue.next`  
→ `nativePollOnce`  
→ `Looper::pollOnce`  
→ `epoll_wait`  
→ 被 `nativeWake` / fd event / timeout 唤醒  
→ 返回 Java 层继续处理消息

---

### 4. 时序图

至少给出文字版时序图，必要时可用 Mermaid。

#### 发送普通消息
- 调用线程构造/复用 Message
- Handler 将 Message 插入 MessageQueue
- MessageQueue 按 `when` 排序
- Looper 轮询队列
- 到期后取出消息
- 通过 `dispatchMessage` 分发
- 执行 `Runnable` 或 `handleMessage`
- Message 回收到池中

#### 延迟消息
- 消息带 `when=now+delay`
- 队列按触发时间排序
- `next()` 未到期时进入 poll timeout
- 到期后苏醒并执行

#### 同步屏障 + 异步消息
- 插入 barrier 后，同步消息被阻塞
- 异步消息可跳过 barrier 被优先执行
- 移除 barrier 后同步消息恢复调度

---

## 分析方法论

---

### 一、先看整体，再看局部

先回答以下问题：

- 该 Handler 所属线程是谁
- 该线程何时 `prepare` Looper
- 消息从哪里发送
- 消息由谁处理
- 队列是否可能存在延迟、屏障或阻塞
- 是否与 UI 刷新、输入、动画、Binder 回调有关

再深入源码细节：

- 消息入队逻辑
- 队列排序逻辑
- 消息提取逻辑
- 分发逻辑
- 回收复用逻辑
- 线程唤醒机制

---

### 二、抓 6 个关键点

分析 Handler 源码时，必须优先抓住这 6 个关键点：

1. **线程归属**
   - 当前 Handler 绑定哪个 Looper
   - Looper 属于哪个线程
   - 是否是主线程/HandlerThread/Binder线程/自定义线程

2. **消息来源**
   - 是 `post(Runnable)` 还是 `sendMessage`
   - 谁发送消息
   - 是否存在延迟、前插、异步标记

3. **消息队列结构**
   - `MessageQueue` 如何存储消息
   - 是否按 `when` 排序
   - 是否存在 barrier
   - 是否存在大量未来消息

4. **消息分发策略**
   - `dispatchMessage` 优先级顺序：
     - `msg.callback`
     - `mCallback`
     - `handleMessage`

5. **阻塞与唤醒机制**
   - 队列无消息或未到期时如何休眠
   - 新消息入队后是否唤醒 poll
   - Native 层如何通过 fd / epoll 唤醒

6. **性能与风险**
   - 单条消息执行是否过重
   - 是否造成后续消息堆积
   - 是否产生 starvation
   - 是否因 barrier 导致同步消息迟迟不执行

---

## 核心源码分析要求

---

### 1. Handler 分析要求

分析 `Handler` 时必须覆盖：

- 构造流程
- 与 `Looper` / `MessageQueue` 的绑定关系
- `post` 与 `sendMessage` 差异
- `dispatchMessage` 分发优先级
- `executeOrSendMessage`（若版本涉及）
- `removeMessages/removeCallbacks` 删除机制
- `hasMessages` / `hasCallbacks` 的复杂度和风险
- `asExecutor` 适配能力（若版本涉及）
- 异步 Handler 的能力与适用场景

必须说明：

- 为什么 `Runnable` 最终也被封装成 `Message`
- 为什么 `Message.target` 就是目标 Handler
- 为什么同一个 Handler 默认只能切换到它绑定的线程执行

---

### 2. Looper 分析要求

分析 `Looper` 时必须覆盖：

- `prepare`
- `prepareMainLooper`
- `myLooper`
- `myQueue`
- `loop`
- `setObserver`（如版本涉及）
- `setMessageLogging`
- `quit`
- `quitSafely`

必须说明：

- `ThreadLocal<Looper>` 的作用
- 一个线程为什么通常只能有一个 Looper
- 主线程 Looper 为什么特殊
- `loop()` 是如何无限循环但又不空转烧 CPU 的
- `quit()` 与 `quitSafely()` 对未处理消息的影响差异

---

### 3. MessageQueue 分析要求

分析 `MessageQueue` 时必须覆盖：

- `enqueueMessage`
- `next`
- `quit`
- `postSyncBarrier`
- `removeSyncBarrier`
- `addIdleHandler`
- `removeIdleHandler`
- `nativeInit`
- `nativePollOnce`
- `nativeWake`
- 文件描述符监听机制（若版本涉及）

必须说明：

- 为什么 MessageQueue 底层是按时间排序的单链表而不是简单队列
- `next()` 如何找到当前可执行消息
- 为什么没有消息时不会 busy loop
- barrier 节点为什么 target 为 null
- 异步消息为什么可以越过 barrier
- IdleHandler 什么时候触发、什么时候不触发

---

### 4. Message 分析要求

分析 `Message` 时必须覆盖：

- 主要字段：`what / arg1 / arg2 / obj / callback / target / when / flags`
- obtain/recycle 复用池机制
- `isAsynchronous/setAsynchronous`
- `markInUse`
- `recycleUnchecked`

必须说明：

- 为什么 Message 采用池化复用
- 池化带来的收益和限制
- 为什么 Message 不能在错误时机重复使用
- `callback` 与 `target` 分别承担什么职责

---

### 5. HandlerThread 分析要求

分析 `HandlerThread` 时必须覆盖：

- 生命周期
- `run` 中如何建立 Looper
- `getLooper` 等待机制
- `quit/quitSafely`
- 典型使用模式

必须说明：

- 为什么 `HandlerThread` 适合串行后台任务
- 为什么不适合长耗时阻塞任务泛滥
- 与线程池、Executor、协程模型相比的边界

---

## 重点专项分析模型

---

### 一、同步消息 / 异步消息 / 同步屏障模型

分析时必须建立以下判断框架：

#### 1. 普通同步消息
- 默认消息类型
- 遇到 barrier 会被阻塞

#### 2. 异步消息
- `msg.setAsynchronous(true)` 或异步 Handler 发送
- 可穿透同步屏障
- 常用于 UI 刷新优先路径

#### 3. 同步屏障
- `postSyncBarrier`
- 自身不是可执行消息
- 用于阻塞同步消息，让异步消息优先执行
- 常见于 `Choreographer` / `ViewRootImpl` 等渲染调度路径

必须说明：

- barrier 不会执行，只起调度控制作用
- 屏障若未及时移除，会导致同步消息饥饿
- 分析 UI 卡顿时必须检查 barrier 生命周期

---

### 二、IdleHandler 模型

分析 `IdleHandler` 时必须说明：

- 触发条件：队列空闲或下一条消息尚未到执行时间
- 返回 `true/false` 的行为差异
- 适合做什么：
  - 低优先级清理
  - 预加载
  - 缓存回收
  - 延后初始化
- 不适合做什么：
  - 重 CPU 任务
  - 长耗时 I/O
  - 高频关键路径逻辑

必须指出：

- “空闲”不代表线程系统层面无事可做，而是队列当前没有立即可执行消息
- IdleHandler 运行过久同样会影响后续消息执行

---

### 三、主线程消息模型

主线程分析必须覆盖：

- `ActivityThread.main`
- `Looper.prepareMainLooper`
- `Looper.loop`
- 主线程 Handler 的消息来源类型：
  - 生命周期调度
  - 窗口事件
  - 绘制调度
  - 输入事件
  - Binder 结果回调
  - 广播/服务切换
  - 系统UI/窗口更新

必须说明：

- Android 主线程本质是消息循环线程
- UI 线程安全依赖单线程串行模型
- 主线程卡顿通常不是 Looper 本身的问题，而是消息处理过重或消息耦合设计不合理

---

### 四、Native Looper 底座模型

必须建立 Java Handler 与 Native Looper 的联动理解：

- Java `MessageQueue.next()` 在无可执行消息时进入 native poll
- Native Looper 通过 `epoll_wait` 等待：
  - timeout
  - wake event
  - fd event
- 新消息入队且需抢占等待时调用 `nativeWake`
- 这使消息循环具备低功耗、低空转特性

必须说明：

- Java 消息机制并非纯 Java while 死循环
- 其阻塞等待依赖 Native 事件机制
- 这也是主线程可长期运行而不持续占满 CPU 的关键原因之一

---

## 典型问题分析模板

---

### 模板 1：某消息为什么没有及时执行

从以下维度分析：

- 是否真正入队
- `when` 是否设置过晚
- 前面是否有更早到期消息
- 当前线程是否正在执行耗时消息
- 是否有同步屏障阻塞
- 是否 Looper 已退出
- 是否消息被 remove 掉
- 是否队列所在线程根本未启动 loop

输出应包括：

- 发送点源码
- 入队逻辑
- 目标线程
- 队列状态
- 前置阻塞消息
- 最终未执行根因

---

### 模板 2：为什么主线程会消息堆积

从以下维度分析：

- 单条消息执行耗时是否过长
- 是否大量连续 post
- 是否高频重复消息未去重
- 是否输入/动画/布局/业务逻辑混在同一线程
- 是否存在 barrier 导致某类消息推迟
- 是否存在 Binder 回调转主线程后雪崩

输出应包括：

- 堆积消息类型
- 典型发送源
- 关键耗时消息
- 时序阻塞链
- 优化建议（拆分、去重、异步化、后移）

---

### 模板 3：post 与 sendMessage 在具体模块中如何选型

分析维度：

- 仅需执行逻辑闭包 → `post(Runnable)`
- 需要 `what/arg/obj` 结构化消息 → `sendMessage`
- 需要协议化状态机 → `sendMessage`
- 需要轻量任务切换 → `post`
- 需要跨模块命令定义 → Message 更可维护

输出应说明：

- 可读性
- 可维护性
- 调试性
- 扩展性
- 池化与对象分配影响

---

### 模板 4：同步屏障导致的行为异常

分析步骤：

- 查找 barrier 插入点
- 查找 barrier 移除点
- 判断是否成对出现
- 判断被阻塞的是哪些同步消息
- 判断异步消息是否持续穿透执行
- 判断是否造成消息饥饿或时序错乱

---

## Framework / SystemServer 常见分析切入点

分析 AOSP 中具体模块时，优先关注这些典型使用模式：

### 1. ActivityThread.H
- App 主线程核心消息分发器
- 生命周期、组件切换、服务、广播等消息入口

### 2. ViewRootImpl
- 窗口与绘制调度中大量依赖 Handler / Choreographer

### 3. Choreographer
- 与 VSYNC / callback / barrier 紧密相关

### 4. AMS / ATMS / WMS / PMS
- 服务线程中大量使用 Handler 串行化状态处理

### 5. Input 体系
- 输入结果回调、超时处理、重试调度

### 6. SystemUI
- 状态栏、通知、动画、交互调度

### 7. 蓝牙/网络/传感器/电源管理
- 使用 HandlerThread 进行串行后台控制

---

## 代码阅读顺序建议

建议按以下顺序阅读源码：

1. `Handler.java`
2. `Looper.java`
3. `MessageQueue.java`
4. `Message.java`
5. `HandlerThread.java`
6. `android_os_MessageQueue.cpp`
7. `utils/Looper.cpp`
8. 具体业务模块中的 Handler 使用点
9. 与 `Choreographer / ViewRootImpl / ActivityThread` 的联动代码

---

## 输出格式规范

每次分析时，建议按以下结构输出：

### 1. 问题定义
- 要分析哪个 Handler 机制或哪个模块中的 Handler 使用

### 2. 结论先行
- 直接给出根因、机制结论或设计总结

### 3. 架构说明
- 涉及哪些类
- 各自职责是什么

### 4. 调用链
- 从消息发送到执行的完整链路

### 5. 时序分析
- 关键时间点
- 阻塞点
- 唤醒点
- 执行点

### 6. 核心源码证据
- 类
- 方法
- 字段
- 关键分支
- 关键条件

### 7. 风险与问题点
- 延迟
- 堆积
- 屏障
- 饥饿
- 线程退出
- remove 导致丢消息
- 使用方式不当

### 8. 优化建议
- 设计优化
- 线程模型优化
- 消息协议优化
- 性能优化
- 调试观测手段优化

---

## 分析质量要求

输出必须满足以下要求：

- 不停留在“Handler 是消息机制”这种表面描述
- 必须说明每个核心类的职责边界
- 必须给出至少一条完整调用链
- 必须解释 why，而不只是 what
- 必须区分同步消息、异步消息、同步屏障
- 必须说明 Java Looper 与 Native poll/wake 的关系
- 必须给出关键源码入口
- 必须能回答“为什么会这样设计”
- 必须能落到实际问题定位或架构设计上

---

## 禁止事项

- 不要只给概念定义，不给源码落点
- 不要只讲单个类，不讲协作关系
- 不要把 MessageQueue 误说成简单 FIFO 队列
- 不要忽略 `when` 时间排序机制
- 不要忽略同步屏障对消息调度的影响
- 不要把 `IdleHandler` 当作后台线程机制
- 不要脱离线程上下文分析 Handler
- 不要把主线程卡顿简单归因于 Handler 本身
- 不要输出未经源码验证的推断
- 不做 ANR 专项分析，本 Skill 聚焦 Handler 机制本身与调度行为分析

---

## 推荐附加输出

在复杂分析中，建议附加输出以下内容：

- 类图
- 时序图
- 线程关系图
- 消息生命周期图
- barrier 前后队列示意图
- Java/Native 分层关系图
- 某模块 Handler 使用模式总结表

---

## 结果验收标准

一次合格的 Handler 源码分析，至少应满足：

- 能说清 Handler / Looper / MessageQueue / Message 的关系
- 能说清消息如何发送、存储、唤醒、分发、回收
- 能说清同步消息 / 异步消息 / 同步屏障区别
- 能说清 IdleHandler 的执行条件
- 能说清主线程消息循环为什么长期运行但不空转
- 能说清实际业务模块如何基于 Handler 构建串行调度
- 能基于源码解释延迟、堆积、屏障、乱序等问题

---

## 一句话使用说明

当需要分析 Android AOSP 中的 Handler、Looper、MessageQueue、Message、HandlerThread 或任何基于消息循环的模块实现时，使用本 Skill，从 **架构职责、源码调用链、消息时序、同步屏障、Native 底座、性能问题与优化建议** 六个维度进行系统化输出。

## 统一输出模板（必须）
每次分析必须按以下章节输出，章节名保持一致：

```markdown
## 一、问题定义与范围
- 现象、影响、触发条件、版本范围、设备与构建信息。

## 二、主调用链
- 给出主调用链（可附关键分支）。
- 标注线程/进程、IPC 或 JNI 边界、关键等待点（锁/fence/binder/message queue）。

## 三、设计思想与架构权衡
- 解释关键机制为什么这样设计。
- 说明收益与代价：稳定性、性能、复杂度、可维护性。

## 四、架构图（Mermaid）
- 图中必须体现问题相关模块边界与依赖方向。

## 五、时序图（Mermaid）
- 图中必须体现关键事件顺序与阻塞传播路径。
- 标注关键时间点或阶段（如输入、调度、合成、显示）。

## 六、关键代码详细分析
- 时序图完成后，必须紧接着对时序图中最关键的类、方法、分支和状态变化做详细源码分析。
- 至少解释 3 处关键代码：入口、关键分支或状态机、收敛点或返回路径。
- 说明每段代码在时序图中的位置、线程或进程上下文、输入输出与设计意图。

## 七、证据链（源码 + 运行时）
- 源码证据：文件路径 + 方法 + 关键条件。
- 运行时证据：logcat/dumpsys/Perfetto/Winscope/traces 等。
- 每个关键结论至少绑定 1 条源码证据和 1 条运行时证据。

## 八、根因结论与置信度
- 根因分层：App / Framework / Native / HAL / Kernel。
- 置信度：Confirmed / Highly Likely / Possible / Speculative。

## 九、修复建议
- 建议必须直连根因，说明影响面与潜在副作用。

## 十、验证计划
- 功能回归、性能回归、稳定性回归、边界场景验证。

## 十一、证据缺口与后续采集
- 列出当前缺口与补采方案（采集工具、场景、目标信号）。
```

## 图示规范（统一）
- 架构图与时序图必须使用 Mermaid。
- 图只保留与当前问题直接相关的节点与链路，避免“大而全”。
- 时序图至少包含：发起方、system_server 关键角色、图形/服务关键节点（按问题域选择）。
- 时序图之后必须紧接“关键代码详细分析”章节，逐段解释图中关键代码，禁止只给图不给代码解读。
- 图中命名与正文术语保持一致。

## 置信度分级（统一）
- `Confirmed`：源码与运行时证据闭环，结论可复现。
- `Highly Likely`：证据链完整度高，但存在单点缺口。
- `Possible`：存在方向性证据，但缺关键验证。
- `Speculative`：仅假设，必须显式标注不可直接下结论。
