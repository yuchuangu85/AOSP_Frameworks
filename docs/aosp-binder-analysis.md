# Binder 源码分析

## 一、问题定义与范围
- 范围：Java Binder、native binder 与 servicemanager 均在仓库中，可分析 IPC 基础主链。
- 现状：本次输出基于目录源码静态分析，不包含运行时日志。

## 二、主调用链
- Binder.java -> native binder -> servicemanager/service -> reply
- 关键边界：重点关注 system_server、Binder、BufferQueue、SurfaceFlinger 等跨线程/跨进程收敛点。

## 三、设计思想与架构权衡
- Binder 以对象句柄和内核驱动实现同步 IPC，兼顾安全边界与高频调用，代价是阻塞链复杂。

## 四、架构图（Mermaid）
```mermaid
flowchart LR
    Caller --> BinderJava
    BinderJava --> BinderNative
    BinderNative --> Service
```

## 五、时序图（Mermaid）
```mermaid
sequenceDiagram
    participant Caller
    participant BinderJava
    participant BinderNative
    participant Service
    Caller->>BinderJava: 进入主链
    BinderJava->>BinderNative: 状态推进
    BinderNative->>Service: 结果提交
```

## 六、关键代码详细分析
- Binder.java 是应用和 framework 常见的 Java 层入口。
- Binder.cpp/BpBinder.cpp 管理 native 事务封送与远端代理。
- servicemanager 负责服务发现，是所有系统服务建立连接的起点。

## 七、证据链（源码 + 运行时）
- 源码证据：`base/core/java/android/os/Binder.java`
- 源码证据：`native/libs/binder/Binder.cpp`
- 源码证据：`native/cmds/servicemanager/main.cpp`
- 运行时证据：当前目录仅含源码，无 logcat、dumpsys、Perfetto、Winscope，运行时证据未闭环。

## 八、根因结论与置信度
- 结论：当前仓库对 `aosp-binder` 的覆盖状态为 `FULL`。
- 置信度：`Highly Likely`。

## 九、修复建议
- 若用于问题定位，优先围绕上述入口继续下钻；若为仓库缺口，则先补齐缺失源码再做闭环判断。

## 十、验证计划
- 补采与本模块对应的 `logcat`、`dumpsys`、Perfetto 或 Winscope，并回到文中主链逐段比对。

## 十一、证据缺口与后续采集
- 缺口：缺少运行时证据。
- 后续：按本模块主链采集线程栈、事务、buffer、焦点或权限状态。
