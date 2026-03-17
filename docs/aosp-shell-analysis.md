# Shell 源码分析

## 一、问题定义与范围
- 范围：WM Shell 源码存在，可分析 task/transition/splitscreen 等高层窗口编排。
- 现状：本次输出基于目录源码静态分析，不包含运行时日志。

## 二、主调用链
- ATMS/WMS event -> ShellTaskOrganizer/Transition -> SurfaceControl transaction
- 关键边界：重点关注 system_server、Binder、BufferQueue、SurfaceFlinger 等跨线程/跨进程收敛点。

## 三、设计思想与架构权衡
- Shell 把复杂多窗口体验从核心 WMS 中拆出，提升演进速度，代价是额外一层状态协调。

## 四、架构图（Mermaid）
```mermaid
flowchart LR
    ATMS --> WMS
    WMS --> Shell
    Shell --> SurfaceControl
```

## 五、时序图（Mermaid）
```mermaid
sequenceDiagram
    participant ATMS
    participant WMS
    participant Shell
    participant SurfaceControl
    ATMS->>WMS: 进入主链
    WMS->>Shell: 状态推进
    Shell->>SurfaceControl: 结果提交
```

## 六、关键代码详细分析
- ShellTaskOrganizer 是任务级容器接入 shell 的关键入口。
- Shell 层会把 task 级变化翻译成 transition 和 surface 事务。
- 分屏/PiP 问题往往要同时追 WMS 状态和 Shell 二次编排。

## 七、证据链（源码 + 运行时）
- 源码证据：`base/libs/WindowManager/Shell/src/com/android/wm/shell/ShellTaskOrganizer.java`
- 运行时证据：当前目录仅含源码，无 logcat、dumpsys、Perfetto、Winscope，运行时证据未闭环。

## 八、根因结论与置信度
- 结论：当前仓库对 `aosp-shell` 的覆盖状态为 `FULL`。
- 置信度：`Highly Likely`。

## 九、修复建议
- 若用于问题定位，优先围绕上述入口继续下钻；若为仓库缺口，则先补齐缺失源码再做闭环判断。

## 十、验证计划
- 补采与本模块对应的 `logcat`、`dumpsys`、Perfetto 或 Winscope，并回到文中主链逐段比对。

## 十一、证据缺口与后续采集
- 缺口：缺少运行时证据。
- 后续：按本模块主链采集线程栈、事务、buffer、焦点或权限状态。
