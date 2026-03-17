# 问题模式与根因
<!-- source: 15-71-ams-anr.md -->

# 7.1 AMS 视角下的 ANR 本质

AMS 不是所有 ANR 的直接根因，但通常是以下角色之一：

- **timeout 观察者**
- **状态持有者**
- **trace / cpu / stack 采集发起者**
- **错误对话与杀进程策略决策者**
- **受害者进程与根因进程之间的仲裁者**

因此 ANR 分析必须回答四个问题：

1. **谁发现超时**
2. **谁真的阻塞了**
3. **谁表面上无响应**
4. **谁最终触发用户可见错误**

------


<!-- source: 18-74-ams-anr-12.md -->

# 7.4 AMS ANR 分析必须回答的 12 个问题

1. ANR 类型是什么
2. timeout 从哪里开始计时
3. timeout 阈值是多少
4. timeout 观察者是谁
5. 出现问题的 ProcessRecord 是谁
6. 当时组件类型是什么
7. 该进程当时 procstate / adj 是什么
8. 是否涉及冷启动 / attachApplication
9. 是否涉及跨进程 Binder 依赖
10. App 主线程在等什么
11. system_server 当时是否持锁或被阻塞
12. 表层受害者与真实根因者是否一致

------


<!-- source: 24-85-scheduling-group-cgroup.md -->

# 8.5 scheduling group / cgroup

除了 procstate / adj，还要关注：

- 当前 scheduling group
- 是否进入 top-app / foreground / background 组
- CPU 调度优先级变化
- cpuset / uclamp / 相关调度策略

### 关键结论

很多“用户觉得卡顿但没被杀”的问题，本质不是内存优先级，而是 **CPU 调度组下降**。

------


<!-- source: 30-92-bugreport.md -->

# 9.2 bugreport 自动分析规则

### Rule-BR-01：识别进程死亡原因

**输入：**

- bugreport 中 activity/processes / event log / tombstone / dropbox / lmkd 记录

**规则：**

- 若存在 `lowmemorykiller` / `lmkd` kill 记录，优先归类为 LMK
- 若存在 `FATAL EXCEPTION` / tombstone，归类为 crash
- 若存在 force-stop / package update / user stop 痕迹，归类为外部清理
- 若存在 watchdog/system_server 异常，标为系统级关联

**输出：**

- kill 类型
- 死亡前状态
- 受影响组件

------

### Rule-BR-02：识别 ANR 类型

**匹配关键字：**

- `Input dispatching timed out`
- `Broadcast of Intent`
- `Timeout executing service`
- `ContentProvider not responding`
- `ANR in`

**输出：**

- ANR 类型
- 观察者
- 涉及进程
- 涉及组件
- timeout 起点候选

------

### Rule-BR-03：识别冷启动阻塞段

**观察区域：**

- `am_proc_start`
- `bindApplication`
- Activity launch 事件
- Provider publish
- 首屏日志
- traces

**输出：**

- 进程创建段
- 应用初始化段
- Provider 段
- Activity 生命周期段
- 首帧可见段

------

### Rule-BR-04：识别 OOM_ADJ 异常

**观察区域：**

- `dumpsys activity oom`
- `dumpsys activity processes`
- lmkd kill logs
- visible/top app 状态记录

**输出：**

- 当前 adj / procstate
- 是否符合用户感知
- 是否存在连接关系抬升
- 是否存在状态错位

------

### Rule-BR-05：识别广播阻塞链

**观察区域：**

- `dumpsys activity broadcasts`
- ordered queue
- receiver 列表
- timeout 日志

**输出：**

- 卡住的 receiver
- 前序 receiver 是否阻塞
- 是否存在冷启动 receiver
- 是否 finishReceiver 延迟

------

### Rule-BR-06：识别 Service timeout

**观察区域：**

- `dumpsys activity services`
- `Timeout executing service`
- restart log
- ANR traces

**输出：**

- timeout service
- 执行线程
- 是否主线程阻塞
- 是否频繁重启

------


<!-- source: 32-94-logcat.md -->

# 9.4 logcat 自动分析规则

### Rule-LC-01：进程启动

匹配：

- `Start proc`
- `am_proc_start`
- `HostingRecord`
- `attachApplication`

输出：

- 启动时间点
- 启动原因
- 首个宿主组件

### Rule-LC-02：进程死亡

匹配：

- `Killing`
- `has died`
- `Process ... has died`
- `lowmemorykiller`
- `lmkd`

输出：

- 死亡类型
- 死亡前上下文
- 是否伴随组件清理

### Rule-LC-03：ANR

匹配：

- `ANR in`
- `Reason:`
- `Input dispatching timed out`
- `Timeout executing service`
- `BroadcastQueue Timeout`

输出：

- ANR 类型
- 直接原因
- 受害者进程
- 根因候选

### Rule-LC-04：重启风暴

匹配：

- `Scheduling restart of crashed service`
- 频繁 `Start proc`
- 短周期进程死亡

输出：

- 是否存在 restart storm
- 主体组件
- 是否为 persistent/fgs/service/isolated 模式问题

### Rule-LC-05：OOM_ADJ 波动

匹配：

- `adj`
- `procstate`
- `setProcState`
- OOM update 相关输出

输出：

- 状态变化链
- 是否存在抖动
- 是否与窗口/绑定/FGS 相关

------


<!-- source: 33-95.md -->

# 9.5 自动分析结果标准格式

```
[问题类型]
- 冷启动慢 / ANR / LMK / 广播阻塞 / Service timeout / 状态错位

[涉及进程]
- 受害者进程
- 根因候选进程
- system_server 是否参与

[核心组件]
- Activity / Service / Receiver / Provider

[关键状态]
- procstate
- curAdj
- schedGroup
- visible/top/fgs/bound 状态

[关键证据]
- bugreport section
- dumpsys field
- logcat line
- trace event

[候选根因树]
- 现象
- 直接原因
- 机制原因
```

------

# 10. 80+ AMS 异常模式库

以下模式库用于真实工程问题快速归类与分析。

------


<!-- source: 35-102-activity-1625.md -->

# 10.2 Activity / 前后台切换类（16~25）

1. resumed 但窗口未 visible
2. visible 已建立但 top 状态未稳定
3. Activity 切前台时 procstate 更新滞后
4. 任务切换导致旧进程错误降级
5. 多窗口场景下重要性判断与用户感知不一致
6. 屏幕熄灭后 top-sleeping 状态误判
7. WMS 可见性变化未及时反映到 OOM_ADJ
8. resumed 链与窗口焦点链不同步
9. ATMS 生命周期推进完成但首帧未可见
10. Home / Recents 切换导致 LRU 异常波动

------


<!-- source: 36-103-service-2640.md -->

# 10.3 Service 类（26~40）

1. `onCreate()` 主线程阻塞
2. `onStartCommand()` 做重 I/O
3. `onBind()` 执行慢
4. `publishService()` 延迟
5. 后台启动限制导致服务行为与预期不符
6. 前台服务声明与实际启动时机不匹配
7. FGS 启动但优先级未按预期抬升
8. restart backoff 使服务长期不可用
9. bind/unbind 泄漏导致进程常驻
10. 执行中的 service 长时间不 finish
11. service timeout 表层在 app，根因在 binder 依赖
12. 短时间反复 startService 造成 system_server 压力
13. 同一进程多个 service 串行执行拖慢主线程
14. ServiceRecord 状态残留导致异常重启
15. app 主线程锁竞争触发 service ANR

------


<!-- source: 37-104-broadcast-4155.md -->

# 10.4 Broadcast 类（41~55）

1. ordered broadcast 前序 receiver 阻塞整条链
2. receiver 主线程做重活
3. receiver 中同步访问 Provider
4. receiver 中等待远端 binder 返回
5. receiver 拉起冷进程成本高
6. finishReceiver 调用延迟
7. 广播风暴导致 BroadcastQueue 堵塞
8. sticky broadcast 状态理解错误
9. parallel 广播并发过多抢占主线程
10. 同进程多个 receiver 竞争主线程
11. receiver 内启动 Activity/Service 引发链式阻塞
12. system_server 广播分发自身过慢
13. 广播队列被异常 receiver 长期占用
14. 前台广播与后台广播优先级认知错误
15. 广播超时表面在 receiver，根因在 system_server 锁

------


<!-- source: 41-108-anr-91100.md -->

# 10.8 ANR 深层根因类（91~100）

1. 主线程等待 binder 回调形成环路
2. 主线程等待 CountDownLatch / Future 永不返回
3. Binder 线程池耗尽导致关键回调排队
4. system_server 长时间持锁导致 app 表象 ANR
5. 输入 ANR 根因在 WMS 焦点链
6. 广播 ANR 根因在 Provider 初始化
7. Service ANR 根因在数据库锁
8. Provider ANR 根因在文件系统阻塞
9. 表层是 app ANR，根因是 system_server 死锁
10. 多重超时叠加导致错误归因

------

# 11. 源码入口索引


<!-- source: 47-116-anr.md -->

# 11.6 ANR / 错误

- `AnrHelper.java`
- `AppErrors.java`
- `ProcessErrorStateRecord.java`


<!-- source: 57-151-aosp-anr.md -->

# 15.1 与 `aosp-anr`

当主问题是系统级 ANR 时：

- 本 Skill 负责 AMS 侧 timeout / 组件 / 进程 / procstate / ANR 管理机制
- `aosp-anr` 负责全局 ANR 分类、trace 栈归因与跨模块证据汇总


<!-- source: 60-154-aosp-graphics-aosp-jank.md -->

# 15.4 与 `aosp-graphics` / `aosp-jank`

当“启动慢”实际上是：

- 首帧晚
- 窗口已起但渲染未完成
- Surface / Buffer / SF 问题
   必须联动图形与卡顿 Skill

------

# 16. 一句话定义

> `aosp-ams` 是一个面向 Android AOSP ActivityManagerService 体系的专家增强型源码分析 Skill，用于从跨层调用链、状态机、ANR 模型、OOM_ADJ/procstate/LMKD、以及 bugreport/dumpsys/logcat 证据链五个核心维度，系统分析 Android 进程与组件运行管理问题，并输出可验证、可复现、可落地的结论。
