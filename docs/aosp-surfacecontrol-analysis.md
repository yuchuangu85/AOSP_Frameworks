# SurfaceControl 源码分析

## 一、问题定义与范围
- 范围：当前仓库覆盖 SurfaceControl Java API 与 SurfaceComposerClient native 事务实现。
- 现状：本次输出基于目录源码静态分析，不包含运行时日志。

## 二、主调用链
- SurfaceControl.java -> Transaction -> SurfaceComposerClient -> SurfaceFlinger
- 关键边界：重点关注 system_server、Binder、BufferQueue、SurfaceFlinger 等跨线程/跨进程收敛点。

## 三、设计思想与架构权衡
- SurfaceControl 将图层控制抽象为批量事务，减少跨进程状态抖动，代价是调试要理解延迟生效。

## 四、架构图（Mermaid）
```mermaid
flowchart LR
    Client --> SurfaceControl
    SurfaceControl --> SurfaceComposerClient
    SurfaceComposerClient --> SurfaceFlinger
```

## 五、时序图（Mermaid）
```mermaid
sequenceDiagram
    participant Client
    participant SurfaceControl
    participant SurfaceComposerClient
    participant SurfaceFlinger
    Client->>SurfaceControl: 进入主链
    SurfaceControl->>SurfaceComposerClient: 状态推进
    SurfaceComposerClient->>SurfaceFlinger: 结果提交
```

## 六、关键代码详细分析
- SurfaceControl.Transaction 聚合 position/alpha/reparent 等 layer 变更。
- SurfaceComposerClient::Transaction 把客户端状态编码并通过 Binder 提交给 SF。
- 问题定位要区分“事务未发送”“事务发送未应用”“事务应用但层不可见”三段。

## 七、证据链（源码 + 运行时）
- 源码证据：`base/core/java/android/view/SurfaceControl.java`
- 源码证据：`native/libs/gui/SurfaceComposerClient.cpp`
- 运行时证据：当前目录仅含源码，无 logcat、dumpsys、Perfetto、Winscope，运行时证据未闭环。

## 八、根因结论与置信度
- 结论：当前仓库对 `aosp-surfacecontrol` 的覆盖状态为 `FULL`。
- 置信度：`Highly Likely`。

## 九、修复建议
- 若用于问题定位，优先围绕上述入口继续下钻；若为仓库缺口，则先补齐缺失源码再做闭环判断。

## 十、验证计划
- 补采与本模块对应的 `logcat`、`dumpsys`、Perfetto 或 Winscope，并回到文中主链逐段比对。

## 十一、证据缺口与后续采集
- 缺口：缺少运行时证据。
- 后续：按本模块主链采集线程栈、事务、buffer、焦点或权限状态。
