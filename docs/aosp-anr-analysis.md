# ANR 源码分析

## 一、问题定义与范围
- 范围：当前仓库具备 AppErrors、AnrHelper、AMS 相关超时处理路径，可完成 Framework 级 ANR 根因建模。
- 现状：本次输出基于目录源码静态分析，不包含运行时日志。

## 二、主调用链
- Input/Broadcast/Service/Provider timeout -> AppErrors/AnrHelper -> traces/logging -> kill or dialog
- 关键边界：重点关注 system_server、Binder、BufferQueue、SurfaceFlinger 等跨线程/跨进程收敛点。

## 三、设计思想与架构权衡
- ANR 设计目标是用统一超时框架在系统可恢复之前尽快暴露阻塞链，代价是高并发场景下需跨线程回溯。

## 四、架构图（Mermaid）
```mermaid
flowchart LR
    Caller --> AMS
    AMS --> AnrHelper
    AnrHelper --> AppErrors
```

## 五、时序图（Mermaid）
```mermaid
sequenceDiagram
    participant Caller
    participant AMS
    participant AnrHelper
    participant AppErrors
    Caller->>AMS: 进入主链
    AMS->>AnrHelper: 状态推进
    AnrHelper->>AppErrors: 结果提交
```

## 六、关键代码详细分析
- AnrHelper 负责收集和串行化 ANR 处理任务，避免 system_server 在风暴场景下进一步拥塞。
- AppErrors.appNotResponding 挂接 trace、dropbox、对话框与杀进程策略。
- ANR 结论必须联动 Binder、Input、Handler 等阻塞源，而不是停在超时表象。

## 七、证据链（源码 + 运行时）
- 源码证据：`base/services/core/java/com/android/server/am/AnrHelper.java`
- 源码证据：`base/services/core/java/com/android/server/am/AppErrors.java`
- 运行时证据：当前目录仅含源码，无 logcat、dumpsys、Perfetto、Winscope，运行时证据未闭环。

## 八、根因结论与置信度
- 结论：当前仓库对 `aosp-anr` 的覆盖状态为 `FULL`。
- 置信度：`Highly Likely`。

## 九、修复建议
- 若用于问题定位，优先围绕上述入口继续下钻；若为仓库缺口，则先补齐缺失源码再做闭环判断。

## 十、验证计划
- 补采与本模块对应的 `logcat`、`dumpsys`、Perfetto 或 Winscope，并回到文中主链逐段比对。

## 十一、证据缺口与后续采集
- 缺口：缺少运行时证据。
- 后续：按本模块主链采集线程栈、事务、buffer、焦点或权限状态。
