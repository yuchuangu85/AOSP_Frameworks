# 概览与范围

<!-- source: 00-overview.md -->

# aosp-anr


<!-- source: 18-101-input.md -->

# 10.1 Input 超时

关注：

- InputDispatcher 的事件分发与等待完成逻辑
- 焦点窗口/应用解析逻辑
- timeout 计算来源
- `AnrTracker` / input timeout record（版本相关）
- WMS 对焦点与窗口状态的影响

关键分析点：

1. 事件有没有真正投递给 app
2. 是卡在“找不到目标窗口”还是“目标线程没处理”
3. 目标线程是否实际在运行
4. system_server 是否先被卡住


<!-- source: 30-132.md -->

# 13.2 找检测点

确定是哪个模块报的超时：

- InputDispatcher
- AMS
- ActiveServices
- BroadcastQueue
- Provider 管理路径


<!-- source: 35-141-input-anr.md -->

# 14.1 Input ANR 时序图

```
User Touch
   |
   v
InputReader
   |
   v
InputDispatcher -----> WMS / Focus check
   |
   +---- dispatch to app input channel -----> App main thread
                                                |
                                                +---- handle input
                                                |
                                                +---- finishInputEvent
   |
   +---- timeout monitor
   |
   v
ANR detected if no timely response
```


<!-- source: 38-15.md -->

# 15. 线程栈分析规范

分析线程栈时必须至少覆盖以下线程：

### 15.1 App 进程

- `main`
- `RenderThread`
- `binder:*`
- `FinalizerDaemon`
- `FinalizerWatchdogDaemon`
- 业务线程池
- `Signal Catcher`（用于 traces 抓取时参考）

### 15.2 system_server

- `main`
- `android.fg`
- `android.ui`
- `android.io`
- `binder:*`
- `InputDispatcher`
- `InputReader`
- `SurfaceAnimationThread`
- `WindowManager`
- `ActivityManager`
- 可能的 watchdog 相关线程

### 15.3 Native / kernel state

重点关注线程状态：

- `Blocked`
- `Waiting`
- `TimedWaiting`
- `Native`
- `Runnable`
- `D`
- `S`
- futex wait
- binder wait
- epoll wait

------


<!-- source: 46-167-system-server.md -->

# 16.7 system_server 放大

特征：

- app 只是表象
- 真正卡住的是 system_server 某服务
- 多个应用同时受影响
- 可能伴随 WMS/AMS/InputDispatcher 超时扩散


<!-- source: 47-168-anr.md -->

# 16.8 图形栈级联导致假性输入 ANR

特征：

- Input ANR 前已有严重掉帧或窗口切换阻塞
- 焦点窗口建立慢、首帧迟迟未到
- Surface / relayout / transaction / BLAST / SF 卡顿拖长可交互时间

------


<!-- source: 48-17-input-anr.md -->

# 17. 重点专项：Input ANR 深度模型

Input ANR 是系统中最常见也最容易误判的一类。


<!-- source: 50-172.md -->

# 17.2 必查点

- 当前 focused application
- 当前 focused window
- InputDispatcher 等待原因文本
- app main thread 栈
- system_server 中 InputDispatcher 线程栈
- WMS 是否卡在窗口切换 / relayout / focus update
- 是否存在 binder 调用链阻塞
- 是否伴随首帧慢 / 启动慢 / draw 卡住


<!-- source: 51-173.md -->

# 17.3 典型日志语义解释

### `Input dispatching timed out`

含义：输入分发链路中有对象未在时限内完成响应。

### `Waiting because no window has focus but there is a focused application`

含义：焦点应用已确定，但焦点窗口尚未准备好。常见于：

- 启动切换中
- 窗口未 add 完成
- relayout/draw/first frame 慢
- WMS/SF/应用首帧链路卡住

### `Waiting to send non-key event because the touched window has not finished processing`

含义：前一个输入事件尚未完成处理，输入系统在等待目标窗口消费。

------


<!-- source: 61-21-perfetto.md -->

# 21. Perfetto 联合分析规则

当用户提供 Perfetto 时，必须联动分析以下轨道：

### 21.1 CPU 调度

- app main thread 是否 runnable 却未运行
- system_server 关键线程是否 runnable 却未运行
- 是否被高负载线程压制
- runqueue 是否异常长

### 21.2 Binder

- app -> system_server 同步 Binder 是否超长
- system_server -> app 回调是否迟迟不返
- binder thread pool 是否饱和
- 是否存在 transaction 串联阻塞

### 21.3 Looper / Main thread

- 主线程消息执行时长
- Choreographer / input / traversal / binder callback 的顺序
- 是否出现长任务阻塞输入

### 21.4 图形栈

- 首帧是否迟迟未出
- relayout / draw / buffer dequeue / SF present 是否异常
- Input ANR 前是否已有严重掉帧

### 21.5 锁与 wait

- futex wait 长段
- Java lock contention
- native mutex wait
- I/O wait / page fault / reclaim

------


<!-- source: 63-221-dumpsys.md -->

# 22.1 必查 dumpsys

- `dumpsys activity`
- `dumpsys window`
- `dumpsys input`
- `dumpsys SurfaceFlinger`
- `dumpsys cpuinfo`
- `dumpsys meminfo`
- `dumpsys package`
- `dumpsys binder_calls_stats`（若可用）
- `dumpsys procstats`（视场景）


<!-- source: 64-222.md -->

# 22.2 必查关键点

### activity

- 前台进程状态
- 组件生命周期
- ANR record
- 广播/service/provider 执行记录

### window

- focused window / app
- 是否无焦点窗口
- 转场、draw、relayout 状态

### input

- 当前等待目标
- dispatch timeout 描述
- 事件积压情况

### cpuinfo

- system_server / app CPU 异常
- 是否出现 CPU 饥饿

------


<!-- source: 89-step-2.md -->

# Step 2：锁定超时检测模块

确定超时是由谁判定：

- InputDispatcher
- AMS
- ActiveServices
- BroadcastQueue
- Provider 路径


<!-- source: 99-30-skill.md -->

# 30. 建议配套 Skill

该 Skill 可以与以下 Skill 组合使用：

- `aosp-graphics`：分析图形栈级联到输入超时
- `aosp-wms`：分析窗口焦点、转场、首帧慢导致 Input ANR
- `aosp-input`：分析 InputReader / InputDispatcher / 触摸延迟
- `aosp-ams`：分析进程拉起、广播、服务、provider 生命周期
- `aosp-surfaceflinger`：分析首帧未到、事务阻塞、显示路径延迟
- `aosp-binder`：分析跨进程阻塞链
- `aosp-perfetto`：分析 CPU / Binder / 调度 / 锁 / 帧时间线

------
