# 问题模式与根因
<!-- source: 03-2.md -->

# 2. 适用问题范围

当用户提出以下问题时应启用本 Skill：

- “帮我分析这个 ANR”
- “为什么这个应用发生 Input dispatching timed out”
- “BroadcastReceiver 超时根因是什么”
- “Service ANR 从源码怎么走”
- “ContentProvider publish timeout 发生在哪里”
- “ANR traces 如何结合 AOSP 分析”
- “System Server 卡死为什么会导致应用 ANR”
- “Binder 阻塞怎么引发 ANR”
- “卡顿、锁等待、死锁、主线程阻塞与 ANR 的关系”
- “想分析 Android 14/15/16 的 ANR 机制”
- “给我输出 ANR 的架构图、时序图、源码调用链、定位方法”

---


<!-- source: 06-5.md -->

# 5. 必备输入

用户若提供以下信息，应优先利用：

### 5.1 必选信息（至少其一）
- ANR traces / traces.txt
- bugreport
- logcat
- tombstone
- Perfetto trace
- 关键源码路径
- 具体 ANR 报错文本

### 5.2 强烈建议信息
- Android 版本（尤其 Android 12/13/14/15/16）
- 机型/SoC/厂商定制情况
- 前后台状态
- 是否系统应用
- 是否复现稳定
- 复现步骤
- 时间点
- 是否伴随卡顿、黑屏、掉帧、binder timeout、watchdog、system_server load 高

### 5.3 最佳输入组合
最佳实践输入为：

1. ANR traces
2. 对应时刻 logcat
3. bugreport
4. Perfetto trace
5. 涉及模块的 AOSP 源码

---


<!-- source: 08-7-aosp-anr.md -->

# 7. AOSP ANR 体系总览


<!-- source: 100-31.md -->

# 31. 一句话执行准则

> 对每一个 ANR，都要从“谁超时了”继续追到“谁真正卡住了”，再继续追到“为什么它会卡住”，最终形成跨层、可验证、可修复的根因结论。

------


<!-- source: 102-33.md -->

# 33. 结束语

该 Skill 的目标不是“解释 ANR 是什么”，而是把 **AOSP ANR 分析** 变成一种标准化工程能力：

- 可复用
- 可交付
- 可审查
- 可验证
- 可持续沉淀

当输入源码、trace、日志、bugreport 等证据后，应能稳定产出高质量根因分析，而不是停留在经验判断层面。


<!-- source: 13-91-input-anr.md -->

# 9.1 Input ANR 跨层链路

```
InputReader
  -> InputDispatcher::dispatchOnce()
  -> find focused window / app
  -> dispatch event to app input channel
  -> ViewRootImpl / WindowInputEventReceiver
  -> Looper / main thread
  -> app event handling
  -> finishInputEvent / consume
  -> timeout monitor
  -> Input dispatching timed out
```

典型阻塞点：

- App 主线程正在执行耗时任务
- 焦点窗口异常，事件无法正常投递
- Window 未完成 relayout / add / resume
- system_server 锁竞争导致 input 分发卡住
- Binder 回调阻塞主线程
- CPU 被抢占，线程长期得不到运行

------


<!-- source: 14-92-broadcast-anr.md -->

# 9.2 Broadcast ANR 跨层链路

```
AMS send/dispatch broadcast
  -> BroadcastQueue / dispatcher select receiver
  -> scheduleReceiver / process start if needed
  -> app process binder attach
  -> ActivityThread / receiver dispatch
  -> BroadcastReceiver.onReceive()
  -> finishReceiver()
  -> timeout monitor
  -> broadcast ANR
```

典型阻塞点：

- onReceive 主线程耗时
- receiver 内同步 Binder / DB / 网络 / I/O
- 冷启动过程太长
- receiver 之前已有主线程 backlog
- ordered broadcast 前序 receiver 卡住

------


<!-- source: 16-94-provider-anr.md -->

# 9.4 Provider ANR 跨层链路

```
caller requests provider
  -> AMS / provider lookup / process launch if absent
  -> target app attach
  -> install providers
  -> ContentProvider.onCreate()
  -> publish provider
  -> caller continues
  -> timeout if provider not ready
```

典型阻塞点：

- 应用冷启动慢
- provider 初始化重
- DB open/upgrade 慢
- 锁、磁盘、SELinux、I/O、解密、文件系统延迟

------


<!-- source: 22-11.md -->

# 11. 关键源码模块索引

以下为分析 AOSP ANR 时的重点源码区域。不同 Android 版本路径和类名可能有调整，但总体结构相近。


<!-- source: 29-131.md -->

# 13.1 先定类型

首先根据日志或 traces 明确是哪一类 ANR：

- Input
- Broadcast
- Service
- Provider
- 其它策略超时


<!-- source: 33-135.md -->

# 13.5 找上游原因

例如：

- 主线程卡在 Binder 调用
   -> system_server 某服务卡住
   -> system_server 某锁被占用
   -> 上游某线程死锁
   -> 最终根因不是 app，而是 system_server

------


<!-- source: 39-16-anr.md -->

# 16. ANR 根因分类模型


<!-- source: 43-164.md -->

# 16.4 冷启动级联

特征：

- ANR 发生于首次启动/拉起组件
- 栈包含 class loading / provider install / dex / app init
- provider/service/receiver 首次执行慢


<!-- source: 44-165-cpu.md -->

# 16.5 CPU 饥饿 / 调度延迟

特征：

- 线程理论上 runnable，但长时间未执行
- Perfetto 显示 runqueue 长、切换少、CPU 被高优线程占用
- top-app / foreground 线程调度异常


<!-- source: 49-171-input-anr.md -->

# 17.1 Input ANR 不等于“主线程卡死”

可能有以下几类根因：

1. 事件已送达，App 主线程未及时处理
2. 事件尚未送达，因为焦点窗口异常
3. WMS 焦点解析/窗口状态异常
4. system_server 自身卡住，InputDispatcher 无法推进
5. App 已经在处理，但 finish signal 未及时返回
6. CPU 调度异常导致输入处理延迟
7. 窗口切换/启动慢导致“有 focused app 无 focused window”


<!-- source: 52-18-broadcast-anr.md -->

# 18. 重点专项：Broadcast ANR 深度模型


<!-- source: 58-20-provider-anr.md -->

# 20. 重点专项：Provider ANR 深度模型


<!-- source: 65-23.md -->

# 23. 分析决策树

```
发现 ANR
  |
  +--> 第一步：识别类型
  |       |
  |       +--> Input
  |       +--> Broadcast
  |       +--> Service
  |       +--> Provider
  |
  +--> 第二步：找到系统在等谁
  |       |
  |       +--> app main thread
  |       +--> binder reply
  |       +--> window ready
  |       +--> service finish
  |       +--> provider publish
  |
  +--> 第三步：检查被等待对象为何不前进
  |       |
  |       +--> 自身执行重活
  |       +--> 等锁
  |       +--> 等 binder
  |       +--> 等 I/O
  |       +--> CPU 抢不到
  |       +--> 上游 system_server 卡住
  |
  +--> 第四步：继续追上游
  |
  +--> 第五步：确定最终根因与放大链
```

------


<!-- source: 66-24-50-anr.md -->

# 24. 50+ 常见 ANR 异常模式库

以下模式库用于提高归因准确率。


<!-- source: 73-247-input-window.md -->

# 24.7 Input / Window 模式

1. 焦点应用存在但焦点窗口未就绪
2. 启动/转场期间窗口首帧过慢
3. InputDispatcher 自身被 system_server 锁阻塞
4. WMS 焦点更新迟滞
5. ViewRootImpl 主线程未及时消费输入
6. 窗口切换时 SF / draw 卡顿级联成 Input ANR


<!-- source: 74-248.md -->

# 24.8 系统级模式

1. system_server CPU 100%
2. system_server 某关键线程死锁
3. 低内存回收/频繁 GC 放大延迟
4. I/O 抖动导致多链路超时
5. 热插拔/存储异常引发 D 状态
6. 内核调度异常导致关键线程长期 runnable 不运行

------


<!-- source: 76-251.md -->

# 25.1 标准正式分析模板

```
# ANR 分析报告


<!-- source: 77-1.md -->

# 1. 结论摘要
- ANR 类型：
- 直接超时点：
- 真正阻塞点：
- 上游诱因：
- 最终根因：


<!-- source: 78-2.md -->

# 2. 现象
- 发生时间：
- 前后台状态：
- 用户可见现象：
- 伴随现象（卡顿/黑屏/掉帧/重启等）：


<!-- source: 83-7.md -->

# 7. 根因分析
- 为什么报 ANR：
- 为什么没有及时完成：
- 为什么真实根因是该点而不是表面现象：


<!-- source: 88-step-1-anr.md -->

# Step 1：识别 ANR 类型

从日志、traces、bugreport 中明确 ANR 类型，禁止一上来泛泛地谈“主线程卡顿”。


<!-- source: 96-27-12.md -->

# 27. 分析时必须回答的 12 个问题

每次 ANR 分析至少要回答以下问题：

1. 这是哪一类 ANR？
2. 谁报的超时？
3. 系统在等谁？
4. 等待持续了多久？
5. 被等待对象当时在做什么？
6. 它为什么没完成？
7. 它在等谁？
8. 上游哪一层先卡住？
9. 是 app 根因还是 system 根因？
10. 有没有锁/Binder/I/O/调度问题？
11. 哪些证据支持这个结论？
12. 如何验证与修复？

------
