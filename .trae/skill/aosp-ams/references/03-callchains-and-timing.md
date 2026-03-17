# 调用链与时序
<!-- source: 08-61-activity.md -->

# 6.1 Activity 冷启动完整跨层调用链

```mermaid
sequenceDiagram
    participant Caller as 调用方App
    participant ATMS as ATMS
    participant AMS as AMS
    participant PL as ProcessList
    participant Z as Zygote
    participant App as 目标App进程
    participant TH as ActivityThread
    participant WMS as WMS
    participant SF as SurfaceFlinger

    Caller->>ATMS: startActivity()
    ATMS->>ATMS: 组件解析/启动策略/Task决策
    ATMS->>AMS: 请求目标进程状态支持
    AMS->>PL: 查询目标进程
    alt 进程不存在
        PL->>Z: fork process
        Z-->>App: 创建新进程
        App->>AMS: attachApplication()
    end
    AMS->>ATMS: 目标进程已就绪
    ATMS->>TH: scheduleLaunchActivity()
    TH->>App: handleLaunchActivity()
    App->>WMS: add window / relayout
    WMS->>SF: 创建/更新 Surface
    SF-->>WMS: 合成准备
    WMS-->>ATMS: 可见状态推进
    ATMS-->>Caller: 生命周期推进完成
```

### 关键结论

- **启动决策主导者是 ATMS**
- **进程存在性与创建由 AMS/ProcessList 负责**
- **Activity 真正执行在 App 进程 ActivityThread**
- **窗口准备由 WMS 接手**
- **最终“用户看见界面”依赖 WMS + SF，而不是 AMS 单独完成**

### 必查拆分

- ATMS 做了什么启动策略决策
- AMS 是否触发冷启动
- attachApplication 是否及时
- scheduleLaunchActivity 是否延迟
- 窗口创建是否卡在 WMS / 渲染侧

------


<!-- source: 09-62-activity.md -->

# 6.2 Activity 恢复前台完整跨层调用链

```mermaid
sequenceDiagram
    participant User as 用户操作
    participant ATMS as ATMS
    participant AMS as AMS
    participant WMS as WMS
    participant App as App进程
    participant TH as ActivityThread

    User->>ATMS: 切回前台Task
    ATMS->>ATMS: resumeTopActivity
    ATMS->>AMS: 更新前后台进程重要性
    AMS->>AMS: 更新procstate/adj候选
    ATMS->>TH: scheduleResumeActivity
    TH->>App: onResume
    App->>WMS: 窗口可见/焦点变化
    WMS-->>ATMS: 可见窗口建立
    AMS->>AMS: 触发OomAdj更新
```

### 核心关注

- resumed 不等于用户可见
- visible 不等于 top resumed
- 前台状态变化会反向影响 OOM_ADJ
- WMS 的窗口可见、焦点、屏幕状态会改变进程重要性判断

------


<!-- source: 10-63-startservice.md -->

# 6.3 startService 完整跨层调用链

```mermaid
sequenceDiagram
    participant Client as 调用方
    participant AMS as AMS
    participant AS as ActiveServices
    participant PL as ProcessList
    participant Z as Zygote
    participant App as 目标进程
    participant TH as ActivityThread

    Client->>AMS: startService
    AMS->>AS: startServiceLocked
    AS->>AS: 权限/后台限制/记录复用
    alt 进程不存在
        AS->>PL: 启动目标进程
        PL->>Z: fork
        Z-->>App: 新进程
        App->>AMS: attachApplication
    end
    AMS->>TH: scheduleCreateService
    TH->>App: handleCreateService
    AMS->>TH: scheduleServiceArgs
    TH->>App: onStartCommand
    App-->>AMS: 执行状态回传/timeout受控
```

### 关键点

- 记录对象由 AMS/ActiveServices 维护
- 组件实际执行由 App 主线程完成
- timeout 观察者在 system_server，执行者在 app
- Service 与进程优先级强绑定，FGS/绑定关系会抬升重要性

------


<!-- source: 11-64-bindservice.md -->

# 6.4 bindService 完整跨层调用链

```mermaid
sequenceDiagram
    participant Client as Client App
    participant AMS as AMS
    participant AS as ActiveServices
    participant Target as Target App
    participant TH as ActivityThread

    Client->>AMS: bindService
    AMS->>AS: bindServiceLocked
    AS->>AS: 建立ConnectionRecord/AppBindRecord/IntentBindRecord
    alt 目标进程未启动
        AS->>AMS: 启动目标进程
        Target->>AMS: attachApplication
    end
    AMS->>TH: scheduleCreateService
    TH->>Target: onCreate/onBind
    Target-->>AMS: publishService
    AMS-->>Client: connected callback
    AMS->>AMS: 绑定关系影响adj/procstate
```

### 关键点

- bind 不只是回调链问题，也是优先级传播链问题
- binding client 的重要性会通过 connection 向 server 侧传播
- 很多“后台 service 却没被杀”的根因是绑定提升 adj
- 很多“该提升但没提升”的问题本质是连接未建立、断链、或不符合传播条件

------


<!-- source: 12-65-broadcast.md -->

# 6.5 Broadcast 完整跨层调用链

```mermaid
sequenceDiagram
    participant Sender as 发送方
    participant AMS as AMS
    participant BD as BroadcastDispatcher
    participant BQ as BroadcastQueue
    participant PL as ProcessList
    participant App as Receiver进程
    participant TH as ActivityThread

    Sender->>AMS: sendBroadcast
    AMS->>AMS: resolve receivers / permission check
    AMS->>BD: enqueue
    BD->>BQ: 调度下一个receiver
    alt receiver进程不存在
        AMS->>PL: 启动进程
        App->>AMS: attachApplication
    end
    BQ->>TH: scheduleReceiver
    TH->>App: onReceive
    App-->>AMS: finishReceiver
    AMS->>BQ: 继续下一个
```

### 关键点

- ordered broadcast 是链式串行模型
- 前一个 receiver 卡住，会阻塞整个链
- 广播 ANR 的 timeout 观察在 AMS，实际阻塞常在 app 主线程或跨进程依赖
- “AMS 超时”不等于“AMS 卡住”，往往只是超时观察者

------


<!-- source: 13-66-provider.md -->

# 6.6 Provider 完整跨层调用链

```mermaid
sequenceDiagram
    participant Client as 调用方
    participant AMS as AMS
    participant PH as ContentProviderHelper
    participant PL as ProcessList
    participant ProviderApp as Provider进程
    participant TH as ActivityThread

    Client->>AMS: getContentProvider
    AMS->>PH: getContentProviderImpl
    PH->>PH: 查ProviderMap/引用计数/权限
    alt 目标进程未启动
        PH->>PL: 启动Provider进程
        ProviderApp->>AMS: attachApplication
    end
    AMS->>TH: scheduleInstallProvider / app init
    ProviderApp-->>AMS: publishContentProviders
    AMS-->>Client: 返回Provider句柄
```

### 关键点

- Provider 首次访问常处于冷启动关键路径
- Provider `onCreate()` 在主线程执行
- 数据库升级 / 文件 I/O / 同步依赖很容易把 Provider 变成启动瓶颈
- 很多冷启动慢本质是 Provider 隐式启动

------


<!-- source: 14-67.md -->

# 6.7 可见性与进程重要性联动调用链

```mermaid
sequenceDiagram
    participant ATMS as ATMS
    participant WMS as WMS
    participant AMS as AMS
    participant OA as OomAdjuster
    participant LMKD as LMKD

    ATMS->>WMS: Activity窗口状态变化
    WMS-->>ATMS: 可见性/焦点/屏幕显示状态
    ATMS->>AMS: 通知进程前后台重要性变化
    AMS->>OA: updateOomAdj
    OA->>OA: 计算procstate/adj/schedGroup
    OA->>LMKD: 写入重要性相关信息
```

### 核心结论

- “用户能看到” 与 “系统认为重要” 之间有映射层
- 这个映射层由 **ATMS/WMS → AMS/OomAdjuster** 完成
- 问题分析必须跨过 AMS、ATMS、WMS 三层，而不能只看单层 log

------

# 7. AMS ANR 深度模型

本章为专家增强版重点。

------


<!-- source: 38-105-provider-5666.md -->

# 10.5 Provider 类（56~66）

1. Provider 首次访问触发冷启动
2. Provider `onCreate()` 数据库升级过慢
3. Provider 初始化读大文件/扫目录
4. 多个 Provider 串行初始化
5. stable reference 长期不释放
6. unstable reference 抖动导致频繁重连
7. provider publish 迟到导致调用方卡住
8. provider 所在进程已启动但主线程卡死
9. ContentResolver 调用链引发隐式跨进程阻塞
10. provider 死亡后重建抖动
11. provider ANR 表层在 caller，根因在 provider 进程

------
