# Surface 源码分析

## 一、问题定义与范围
- 范围：当前仓库同时含 Java Surface 和 native Surface 实现，可分析 surface 生命周期与 buffer 生产接口。
- 现状：本次输出基于目录源码静态分析，不包含运行时日志。

## 二、主调用链
- Surface.java -> native Surface.cpp -> IGraphicBufferProducer -> BufferQueue
- 关键边界：重点关注 system_server、Binder、BufferQueue、SurfaceFlinger 等跨线程/跨进程收敛点。

## 三、设计思想与架构权衡
- Surface 把应用侧绘制接口与 BufferQueue producer 代理封装在一起，降低上层复杂度。

## 四、架构图（Mermaid）
```mermaid
flowchart LR
    App --> SurfaceJava
    SurfaceJava --> SurfaceNative
    SurfaceNative --> BufferQueue
```

## 五、时序图（Mermaid）
```mermaid
sequenceDiagram
    participant App
    participant SurfaceJava
    participant SurfaceNative
    participant BufferQueue
    App->>SurfaceJava: 进入主链
    SurfaceJava->>SurfaceNative: 状态推进
    SurfaceNative->>BufferQueue: 结果提交
```

## 六、关键代码详细分析
- Surface.java 负责 Java 层对象生命周期和 native 句柄持有。
- Surface.cpp 把 lock/unlock 或 queueBuffer 请求转给 IGraphicBufferProducer。
- surface 问题常表现为对象仍在但 producer 断链，需要结合 BLAST/BufferQueue 看。

## 七、证据链（源码 + 运行时）
- 源码证据：`base/core/java/android/view/Surface.java`
- 源码证据：`native/libs/gui/Surface.cpp`
- 运行时证据：当前目录仅含源码，无 logcat、dumpsys、Perfetto、Winscope，运行时证据未闭环。

## 八、根因结论与置信度
- 结论：当前仓库对 `aosp-surface` 的覆盖状态为 `FULL`。
- 置信度：`Highly Likely`。

## 九、修复建议
- 若用于问题定位，优先围绕上述入口继续下钻；若为仓库缺口，则先补齐缺失源码再做闭环判断。

## 十、验证计划
- 补采与本模块对应的 `logcat`、`dumpsys`、Perfetto 或 Winscope，并回到文中主链逐段比对。

## 十一、证据缺口与后续采集
- 缺口：缺少运行时证据。
- 后续：按本模块主链采集线程栈、事务、buffer、焦点或权限状态。
