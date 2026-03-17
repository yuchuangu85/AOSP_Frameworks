# BufferQueue 源码分析

## 一、问题定义与范围
- 范围：BufferQueueProducer/Consumer 主实现完整，可分析 dequeue/queue/acquire/release 主链。
- 现状：本次输出基于目录源码静态分析，不包含运行时日志。

## 二、主调用链
- Producer dequeueBuffer -> queueBuffer -> Consumer acquireBuffer -> releaseBuffer
- 关键边界：重点关注 system_server、Binder、BufferQueue、SurfaceFlinger 等跨线程/跨进程收敛点。

## 三、设计思想与架构权衡
- BufferQueue 以槽位和 fence 协调生产消费速率，用解耦换取并发显示能力。

## 四、架构图（Mermaid）
```mermaid
flowchart LR
    Producer --> BufferQueue
    BufferQueue --> Consumer
    Consumer --> SurfaceFlinger
```

## 五、时序图（Mermaid）
```mermaid
sequenceDiagram
    participant Producer
    participant BufferQueue
    participant Consumer
    participant SurfaceFlinger
    Producer->>BufferQueue: 进入主链
    BufferQueue->>Consumer: 状态推进
    Consumer->>SurfaceFlinger: 结果提交
```

## 六、关键代码详细分析
- dequeueBuffer 控制可用槽位和生产者背压。
- queueBuffer 把帧及其元数据推进到消费者可见队列。
- acquire/release 决定消费者节奏，是黑屏、卡帧、积压定位关键点。

## 七、证据链（源码 + 运行时）
- 源码证据：`native/libs/gui/BufferQueueProducer.cpp`
- 源码证据：`native/libs/gui/BufferQueueConsumer.cpp`
- 运行时证据：当前目录仅含源码，无 logcat、dumpsys、Perfetto、Winscope，运行时证据未闭环。

## 八、根因结论与置信度
- 结论：当前仓库对 `aosp-bufferqueue` 的覆盖状态为 `FULL`。
- 置信度：`Highly Likely`。

## 九、修复建议
- 若用于问题定位，优先围绕上述入口继续下钻；若为仓库缺口，则先补齐缺失源码再做闭环判断。

## 十、验证计划
- 补采与本模块对应的 `logcat`、`dumpsys`、Perfetto 或 Winscope，并回到文中主链逐段比对。

## 十一、证据缺口与后续采集
- 缺口：缺少运行时证据。
- 后续：按本模块主链采集线程栈、事务、buffer、焦点或权限状态。
