# Transaction 源码分析

## 一、问题定义与范围
- 范围：客户端事务和服务端应用入口完整，可分析 ClientTransaction 与 SurfaceControl.Transaction 双路径。
- 现状：本次输出基于目录源码静态分析，不包含运行时日志。

## 二、主调用链
- ClientTransaction/WindowContainerTransaction -> server apply -> SurfaceControl.Transaction -> SF
- 关键边界：重点关注 system_server、Binder、BufferQueue、SurfaceFlinger 等跨线程/跨进程收敛点。

## 三、设计思想与架构权衡
- 事务模型把离散状态变化打包提交，减少中间态可见性，但调试要区分多个事务平面。

## 四、架构图（Mermaid）
```mermaid
flowchart LR
    App --> ClientTransaction
    ClientTransaction --> SurfaceTransaction
    SurfaceTransaction --> SurfaceFlinger
```

## 五、时序图（Mermaid）
```mermaid
sequenceDiagram
    participant App
    participant ClientTransaction
    participant SurfaceTransaction
    participant SurfaceFlinger
    App->>ClientTransaction: 进入主链
    ClientTransaction->>SurfaceTransaction: 状态推进
    SurfaceTransaction->>SurfaceFlinger: 结果提交
```

## 六、关键代码详细分析
- ClientTransaction 负责应用组件生命周期与回调事务化。
- SurfaceComposerClient::Transaction 负责 layer/display 事务提交。
- 实际问题常来自生命周期事务和显示事务顺序不一致。

## 七、证据链（源码 + 运行时）
- 源码证据：`base/core/java/android/app/servertransaction/ClientTransaction.java`
- 源码证据：`native/libs/gui/SurfaceComposerClient.cpp`
- 运行时证据：当前目录仅含源码，无 logcat、dumpsys、Perfetto、Winscope，运行时证据未闭环。

## 八、根因结论与置信度
- 结论：当前仓库对 `aosp-transaction` 的覆盖状态为 `FULL`。
- 置信度：`Highly Likely`。

## 九、修复建议
- 若用于问题定位，优先围绕上述入口继续下钻；若为仓库缺口，则先补齐缺失源码再做闭环判断。

## 十、验证计划
- 补采与本模块对应的 `logcat`、`dumpsys`、Perfetto 或 Winscope，并回到文中主链逐段比对。

## 十一、证据缺口与后续采集
- 缺口：缺少运行时证据。
- 后续：按本模块主链采集线程栈、事务、buffer、焦点或权限状态。
