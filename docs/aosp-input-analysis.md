# Input 源码分析

## 一、问题定义与范围
- 范围：当前仓库含 InputManagerService Java 入口，但缺少 inputflinger 主体 native 分发代码，属于部分覆盖。
- 现状：本次输出基于目录源码静态分析，不包含运行时日志。

## 二、主调用链
- InputManagerService -> native input dispatcher gap -> WMS/ViewRootImpl
- 关键边界：重点关注 system_server、Binder、BufferQueue、SurfaceFlinger 等跨线程/跨进程收敛点。

## 三、设计思想与架构权衡
- 输入系统把策略决策放在 Framework，把高频分发放在 native；当前仓库缺少后半段。

## 四、架构图（Mermaid）
```mermaid
flowchart LR
    Device --> IMS
    IMS --> WMS
    WMS --> App
```

## 五、时序图（Mermaid）
```mermaid
sequenceDiagram
    participant Device
    participant IMS
    participant WMS
    participant App
    Device->>IMS: 进入主链
    IMS->>WMS: 状态推进
    WMS->>App: 结果提交
```

## 六、关键代码详细分析
- InputManagerService 向 system_server 暴露注册、注入、策略回调入口。
- 真正的事件匹配、ANR 判定和 fd 驱动主要在缺失的 native inputflinger 中。
- 因此本仓库更适合分析策略接口，不适合单独闭环输入分发性能问题。

## 七、证据链（源码 + 运行时）
- 源码证据：`base/services/core/java/com/android/server/input/InputManagerService.java`
- 运行时证据：当前目录仅含源码，无 logcat、dumpsys、Perfetto、Winscope，运行时证据未闭环。

## 八、根因结论与置信度
- 结论：当前仓库对 `aosp-input` 的覆盖状态为 `PARTIAL`。
- 置信度：`Possible`。

## 九、修复建议
- 若用于问题定位，优先围绕上述入口继续下钻；若为仓库缺口，则先补齐缺失源码再做闭环判断。

## 十、验证计划
- 补采与本模块对应的 `logcat`、`dumpsys`、Perfetto 或 Winscope，并回到文中主链逐段比对。

## 十一、证据缺口与后续采集
- 缺口：缺少运行时证据。
- 后续：按本模块主链采集线程栈、事务、buffer、焦点或权限状态。
