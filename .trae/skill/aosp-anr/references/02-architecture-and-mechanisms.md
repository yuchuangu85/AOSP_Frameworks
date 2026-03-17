# 架构与核心机制
<!-- source: 04-3.md -->

# 3. 核心目标

该 Skill 的输出必须尽量达到以下目标：

1. **识别 ANR 类型**
   - Input
   - Broadcast
   - Service
   - ContentProvider
   - ForegroundService
   - Job / provider / activity lifecycle 相关变体
2. **还原完整触发路径**
3. **定位超时发生在哪一层**
   - App
   - Framework
   - System Server
   - Native
   - Kernel
4. **识别真正阻塞线程**
   - UI/Main thread
   - Binder thread
   - system_server binder thread
   - RenderThread
   - Finalizer / GC
   - HandlerThread
5. **识别根因模式**
   - 锁竞争
   - Binder 调用链阻塞
   - 主线程耗时
   - I/O 卡顿
   - CPU 饥饿
   - 调度延迟
   - 死锁/活锁
   - 系统服务拥塞
   - 图形栈卡顿级联
6. **给出验证方式**
7. **给出修复建议**
8. **形成标准化分析报告**

---


<!-- source: 09-71-anr.md -->

# 7.1 ANR 本质

ANR 不是一个单点错误，而是一类 **“系统在规定时间内未观察到预期响应”** 的超时判定机制。

本质上是：

> 某个关键交互链路有明确的责任线程或组件需要在时限内完成处理，但由于线程阻塞、系统拥塞、资源竞争或调度问题未完成，最终被系统判定为“无响应”。

---


<!-- source: 10-72-anr.md -->

# 7.2 常见 ANR 大类

### 7.2.1 Input ANR
典型报错：

- `Input dispatching timed out`
- `Application is not responding: Window ...`
- `No focused window`
- `Waiting because no window has focus but there is a focused application that may eventually add a window`

核心检测链：

- InputReader
- InputDispatcher
- WindowManagerService / Focus
- App 主线程 / Input channel 消费
- ActivityThread / ViewRootImpl / Looper

### 7.2.2 Broadcast ANR
典型场景：

- 前台广播超时
- 后台广播超时
- ordered broadcast 卡链

核心检测链：

- ActivityManagerService
- BroadcastQueue / BroadcastProcessQueue / BroadcastDispatcher（版本差异）
- receiver 执行线程
- 应用主线程或自定义 handler / executor

### 7.2.3 Service ANR
典型场景：

- `executing service xxx`
- service 生命周期回调耗时
- bind / create / start 生命周期阻塞

核心检测链：

- AMS / ActiveServices
- app main thread
- service 生命周期回调
- binder / lock / I/O

### 7.2.4 ContentProvider ANR
典型场景：

- provider publish 慢
- provider call/query 阻塞
- 进程启动 + provider 初始化过慢

核心检测链：

- AMS / Provider helper
- app process start
- provider onCreate
- binder / DB / 磁盘

### 7.2.5 Foreground Service / 特殊超时
版本相关，通常涉及：

- 前台服务启动窗口限制
- 系统策略类超时
- 更严格的后台执行监管

---


<!-- source: 11-8-aosp-anr.md -->

# 8. AOSP ANR 核心架构图

```text
+--------------------------------------------------------------+
|                          User / System Event                 |
+------------------------------+-------------------------------+
                               |
                               v
+--------------------------------------------------------------+
|                        Timeout-sensitive Path                |
|  Input / Broadcast / Service / Provider / Activity launch    |
+------------------------------+-------------------------------+
                               |
                               v
+--------------------------------------------------------------+
|                    Framework / System Server                 |
|  InputDispatcher / AMS / ATMS / WMS / ActiveServices / etc   |
+------------------------------+-------------------------------+
                               |
                               v
+--------------------------------------------------------------+
|                        App Process / Threads                 |
|  main thread / binder thread / worker / RenderThread / GC    |
+------------------------------+-------------------------------+
                               |
                               v
+--------------------------------------------------------------+
|                   Native / Binder / Scheduler / I/O          |
|      futex / epoll / binder wait / disk wait / CPU runq      |
+------------------------------+-------------------------------+
                               |
                               v
+--------------------------------------------------------------+
|                     Timeout Detector fires ANR               |
+--------------------------------------------------------------+
```


<!-- source: 15-93-service-anr.md -->

# 9.3 Service ANR 跨层链路

```
AMS / ActiveServices
  -> realStartServiceLocked / scheduleCreateService / scheduleServiceArgs
  -> app main thread H message
  -> Service.onCreate / onStartCommand / onBind
  -> lifecycle finish callback
  -> timeout monitor
  -> service ANR
```

典型阻塞点：

- Service 生命周期里做重活
- 主线程被前序任务卡住
- bind/service connection 链路阻塞
- 锁竞争 / Binder 调用等待 system_server 返回

------


<!-- source: 17-10-anr.md -->

# 10. ANR 检测与超时机制分析框架

不同 Android 版本实现细节可能变化，但分析逻辑基本一致。


<!-- source: 20-103-service.md -->

# 10.3 Service 超时

关注：

- create/start/bind 不同生命周期点
- 是 service 自己慢，还是应用主线程已有 backlog
- ANR 文本中的 “executing service” 对应哪一阶段


<!-- source: 23-111-input-anr.md -->

# 11.1 Input ANR 相关

- `frameworks/native/services/inputflinger`
- `InputDispatcher.cpp`
- `InputReader.cpp`
- `frameworks/base/services/core/java/com/android/server/wm`
- `WindowManagerService`
- `InputMonitor`
- `frameworks/base/core/java/android/view`
- `ViewRootImpl`
- `WindowInputEventReceiver`


<!-- source: 24-112-ams-broadcast-service-provider.md -->

# 11.2 AMS / Broadcast / Service / Provider

- `frameworks/base/services/core/java/com/android/server/am`
- `ActivityManagerService`
- `ActiveServices`
- `BroadcastQueue` / `BroadcastDispatcher` / `BroadcastProcessQueue`
- `ProcessList`
- `ProcessRecord`
- `ContentProviderHelper` 或相关 provider 管理模块
- `frameworks/base/core/java/android/app/ActivityThread.java`


<!-- source: 25-113-binder-looper.md -->

# 11.3 Binder / 线程 / Looper

- `frameworks/native/libs/binder`
- `system/libhwbinder`
- `frameworks/base/core/java/android/os`
- `Looper`
- `MessageQueue`
- `Handler`
- `Binder.java`


<!-- source: 27-12.md -->

# 12. 架构设计思想分析要求

分析源码时，不能只讲“代码在哪里”，还必须讲清楚设计思想。

### 12.1 为什么 ANR 机制存在

- 防止系统交互长期无反馈
- 保护用户体验
- 给系统一个统一超时仲裁机制
- 形成 app 与 system 的行为边界

### 12.2 为什么分多类超时

不同场景有不同责任链与用户体验敏感度：

- Input：最敏感，直接影响交互
- Broadcast：影响系统消息传播与组件协同
- Service：影响后台与组件生命周期
- Provider：影响跨进程数据访问与启动链

### 12.3 为什么不能只靠 app 自己检测

因为超时判定往往需要系统全局视角：

- 当前焦点窗口是谁
- 事件是否真正下发
- 是否系统服务先卡死
- 是否是对端进程未启动完成
- 是否 ordered broadcast 前序阻塞

------


<!-- source: 31-133.md -->

# 13.3 找被等待对象

确定系统在等谁：

- app 主线程
- receiver finish
- service 生命周期完成
- provider publish
- focus window ready
- binder reply


<!-- source: 32-134.md -->

# 13.4 找真实阻塞点

查看线程栈和 trace，确定真正卡住的位置：

- Java synchronized
- native mutex
- futex wait
- binder transact
- binder thread pool exhaustion
- Looper backlog
- I/O wait
- monitor contention


<!-- source: 40-161.md -->

# 16.1 主线程直接耗时

特征：

- main thread 在执行明显耗时逻辑
- Java 栈落在业务代码 / framework callback
- 无明显等待对象

常见原因：

- 大量 JSON / XML 解析
- 数据库查询
- 文件读写
- bitmap 解码
- 同步网络调用
- 大量反射 / 类加载
- 复杂布局 / 初始化


<!-- source: 41-162.md -->

# 16.2 锁竞争

特征：

- 线程栈出现 `waiting to lock`
- monitor / mutex / lock owner 明确
- 另一个线程持锁时间过长

常见原因：

- 主线程抢锁
- system_server 全局锁争用
- package / activity / window 全局锁
- 业务单例锁设计不当


<!-- source: 42-163-binder.md -->

# 16.3 Binder 阻塞

特征：

- 栈停在 `BinderProxy.transact` / native transact
- 对端线程池饱和或服务端锁等待
- 常伴随 system_server binder thread 堵塞

常见原因：

- app 主线程同步 Binder 调用
- system_server 服务内部重活
- Binder 回调形成环路等待
- 小线程池 + 长任务导致排队


<!-- source: 45-166-i-o.md -->

# 16.6 I/O 卡死

特征：

- 线程处于磁盘 / 文件系统 / 数据库等待
- `D` 状态或 native I/O 栈明显
- 可能伴随存储介质抖动、fsync、数据库锁


<!-- source: 53-181.md -->

# 18.1 关键误区

Broadcast ANR 不一定是 `onReceive()` 本身慢，也可能是：

- receiver 进程冷启动慢
- main thread 前面积压消息太多
- ordered broadcast 前一个 receiver 卡住
- onReceive 内同步调用 system service 导致 binder 堵塞
- 系统在 finishReceiver 前的路径被卡住


<!-- source: 55-19-service-anr.md -->

# 19. 重点专项：Service ANR 深度模型


<!-- source: 56-191-service-anr.md -->

# 19.1 Service ANR 常见真因

- `onCreate()` 重活
- `onStartCommand()` 阻塞
- `onBind()` 耗时
- 主线程已被别的任务堵死
- 与 provider / receiver / activity 启动交织
- 同步等待 binder 结果


<!-- source: 57-192.md -->

# 19.2 关键判断

要区分：

1. 是 service callback 自己慢
2. 还是回调根本没机会执行
3. 还是已经执行但 finish 路径未完成
4. 还是 AMS 端记录与 app 端状态不同步

------


<!-- source: 67-241-app.md -->

# 24.1 App 主线程模式

1. 主线程执行数据库查询
2. 主线程执行大文件读取
3. 主线程 Bitmap 解码
4. 主线程 JSON 解析
5. 主线程同步网络
6. 主线程等待 CountDownLatch/Future
7. 主线程抢 Java 锁
8. 主线程等待 native 锁
9. 主线程同步 Binder 调 system_server
10. 主线程陷入递归/长循环


<!-- source: 69-243.md -->

# 24.3 锁竞争模式

1. AMS 全局锁竞争
2. WMS 全局锁竞争
3. PMS 锁竞争
4. 应用内单例锁被 worker 长持有
5. provider 初始化锁阻塞主线程
6. system_server 多服务交叉持锁
7. Java 锁与 Binder 交织形成长等待
8. native mutex 长持有


<!-- source: 71-245-service.md -->

# 24.5 Service 模式

1. onCreate 重活
2. onStartCommand 重活
3. onBind 重活
4. service 启动前主线程已 backlog
5. service 中同步初始化 SDK
6. service 调 provider / binder 形成等待链


<!-- source: 80-4.md -->

# 4. 源码调用链
- 从入口到超时检测点：
- 从被等待对象到阻塞点：
- 涉及核心类/方法：


<!-- source: 93-step-6.md -->

# Step 6：做跨层归因

把 app / framework / native / kernel 联系起来，找到真正堵塞点。
