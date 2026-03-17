# Graphics 总链分析

## 一、问题定义与范围
- 范围：ViewRootImpl 到 SurfaceFlinger 的关键源码在仓库中完整，可分析应用绘制到显示提交主链。
- 现状：本次输出基于目录源码静态分析，不包含运行时日志。

## 二、主调用链
- ViewRootImpl -> ThreadedRenderer/HWUI -> Surface/BLAST -> BufferQueue -> SurfaceFlinger
- 关键边界：重点关注 system_server、Binder、BufferQueue、SurfaceFlinger 等跨线程/跨进程收敛点。

## 三、设计思想与架构权衡
- 图形栈通过生产者-消费者与异步合成隔离应用绘制和显示节奏，代价是跨层同步复杂。

## 四、架构图（Mermaid）
```mermaid
flowchart LR
    App --> ViewRootImpl
    ViewRootImpl --> BufferQueue
    BufferQueue --> SurfaceFlinger
```

## 五、时序图（Mermaid）
```mermaid
sequenceDiagram
    participant App
    participant ViewRootImpl
    participant BufferQueue
    participant SurfaceFlinger
    App->>ViewRootImpl: 进入主链
    ViewRootImpl->>BufferQueue: 状态推进
    BufferQueue->>SurfaceFlinger: 结果提交
```

## 六、关键代码详细分析
- ViewRootImpl.performTraversals/scheduleTraversals 决定应用侧绘制时机。
- Surface/BLAST 将 buffer 生产与窗口状态更新拆分，提高首帧和变更吞吐。
- SurfaceFlinger::commit/composite 把全局 layer 与显示设备状态收敛为最终画面。

## 七、证据链（源码 + 运行时）
- 源码证据：`base/core/java/android/view/ViewRootImpl.java`
- 源码证据：`native/services/surfaceflinger/SurfaceFlinger.cpp`
- 运行时证据：当前目录仅含源码，无 logcat、dumpsys、Perfetto、Winscope，运行时证据未闭环。

## 八、根因结论与置信度
- 结论：当前仓库对 `aosp-graphics` 的覆盖状态为 `FULL`。
- 置信度：`Highly Likely`。

## 九、修复建议
- 若用于问题定位，优先围绕上述入口继续下钻；若为仓库缺口，则先补齐缺失源码再做闭环判断。

## 十、验证计划
- 补采与本模块对应的 `logcat`、`dumpsys`、Perfetto 或 Winscope，并回到文中主链逐段比对。

## 十一、证据缺口与后续采集
- 缺口：缺少运行时证据。
- 后续：按本模块主链采集线程栈、事务、buffer、焦点或权限状态。
