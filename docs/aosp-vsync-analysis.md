# VSYNC 源码分析

## 一、问题定义与范围
- 范围：Choreographer 与 SurfaceFlinger Scheduler 均存在，可分析应用到合成的节拍传播。
- 现状：本次输出基于目录源码静态分析，不包含运行时日志。

## 二、主调用链
- Display VSYNC -> Scheduler -> Choreographer -> traversal/render -> SurfaceFlinger
- 关键边界：重点关注 system_server、Binder、BufferQueue、SurfaceFlinger 等跨线程/跨进程收敛点。

## 三、设计思想与架构权衡
- Android 用分层 VSYNC 模型把应用绘制和系统合成锁定到统一节拍，代价是延迟分析跨多线程。

## 四、架构图（Mermaid）
```mermaid
flowchart LR
    Display --> Scheduler
    Scheduler --> Choreographer
    Choreographer --> App
```

## 五、时序图（Mermaid）
```mermaid
sequenceDiagram
    participant Display
    participant Scheduler
    participant Choreographer
    participant App
    Display->>Scheduler: 进入主链
    Scheduler->>Choreographer: 状态推进
    Choreographer->>App: 结果提交
```

## 六、关键代码详细分析
- Choreographer 决定回调批次和 UI 主线程帧推进。
- Scheduler 管理 SF 的合成节拍和显示设备同步策略。
- jank 分析要同时看应用 missed deadline 与 SF missed present。

## 七、证据链（源码 + 运行时）
- 源码证据：`base/core/java/android/view/Choreographer.java`
- 源码证据：`native/services/surfaceflinger/Scheduler/Scheduler.cpp`
- 运行时证据：当前目录仅含源码，无 logcat、dumpsys、Perfetto、Winscope，运行时证据未闭环。

## 八、根因结论与置信度

- 结论：当前仓库对 `aosp-vsync` 的覆盖状态为 `FULL`。
- 置信度：`Highly Likely`。

## 九、修复建议
- 若用于问题定位，优先围绕上述入口继续下钻；若为仓库缺口，则先补齐缺失源码再做闭环判断。

## 十、验证计划
- 补采与本模块对应的 `logcat`、`dumpsys`、Perfetto 或 Winscope，并回到文中主链逐段比对。

## 十一、证据缺口与后续采集
- 缺口：缺少运行时证据。
- 后续：按本模块主链采集线程栈、事务、buffer、焦点或权限状态。
