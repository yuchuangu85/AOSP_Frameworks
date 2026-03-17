# 架构与核心机制
<!-- source: 05-4.md -->

# 4. 核心架构设计思想


<!-- source: 07-42.md -->

# 4.2 架构本质

VSYNC 机制的本质不是“一个中断通知”，而是一个 **跨层时序控制系统**：

- 上游：Display/HWC 提供刷新节奏
- 中游：SurfaceFlinger Scheduler 生成/分发时序事件
- 下游：App / RenderThread / SF 依据节奏生产和消费图形 buffer
- 校正：Fence / Present Time / FrameTimeline 用来验证每帧是否按预期到达

它解决的是三个核心问题：

1. **何时开始生产一帧**
2. **何时必须完成这帧**
3. **何时这帧真正显示到屏幕上**

---


<!-- source: 11-6.md -->

# 6. 核心模块分层


<!-- source: 15-64-hwc-hal-driver.md -->

# 6.4 HWC / HAL / Driver 层

关键组件：

- `Composer HAL`
- `HWComposer`
- `validateDisplay`
- `presentDisplay`
- display fences
- DRM/KMS driver（设备相关）

职责：

- 决策哪些 layer 走设备合成 / client composition
- 实施 present
- 返回 present fence / release fence
- 报告 vsync / refresh 支持能力

关键问题：

- HWC present 晚
- device composition 能力不足回退到 GPU
- refresh rate 切换迟滞
- driver VSYNC source 抖动

------


<!-- source: 16-7-vsync.md -->

# 7. VSYNC 关键机制总览


<!-- source: 19-73-surfaceflinger-scheduler-eventthread.md -->

# 7.3 SurfaceFlinger Scheduler / EventThread

核心思路：

- SurfaceFlinger 并不简单“收到硬件中断就立即做事”
- 它通过 Scheduler 进行事件建模、预测和分发
- EventThread 给 app / sf 等消费者派发显示节奏事件
- DispSync / VSyncPredictor 类机制用于平滑/预测 VSYNC 节奏

分析时要关注：

- VSYNC source 是否稳定
- Scheduler 是否在切换 refresh rate
- EventThread 是否派发正常
- app / sf 的 deadline 模型是否匹配当前 refresh period

------


<!-- source: 22-81-app.md -->

# 8.1 App 侧

```
frameworks/base/core/java/android/view/Choreographer.java
frameworks/base/core/java/android/view/ViewRootImpl.java
frameworks/base/core/java/android/view/ThreadedRenderer.java
frameworks/base/core/java/android/view/Surface.java
frameworks/base/libs/hwui/
```

重点关注：

- `Choreographer#postFrameCallback`
- `Choreographer#doFrame`
- `Choreographer#scheduleVsyncLocked`
- `ViewRootImpl#scheduleTraversals`
- `ViewRootImpl#doTraversal`
- `ViewRootImpl#performTraversals`

------


<!-- source: 23-82-bufferqueue-blast.md -->

# 8.2 BufferQueue / BLAST

```
frameworks/native/libs/gui/
frameworks/native/libs/gui/BufferQueueProducer.cpp
frameworks/native/libs/gui/BufferQueueConsumer.cpp
frameworks/native/libs/gui/Surface.cpp
frameworks/native/libs/gui/BLASTBufferQueue.cpp
```

重点关注：

- `dequeueBuffer`
- `queueBuffer`
- acquire/release fence
- buffer state 转换
- BLAST 事务同步

------


<!-- source: 24-83-surfaceflinger-scheduler.md -->

# 8.3 SurfaceFlinger / Scheduler

```
frameworks/native/services/surfaceflinger/SurfaceFlinger.cpp
frameworks/native/services/surfaceflinger/Scheduler/
frameworks/native/services/surfaceflinger/CompositionEngine/
frameworks/native/services/surfaceflinger/DisplayHardware/
```

重点关注：

- `SurfaceFlinger::onMessageReceived`
- `handleMessageRefresh`
- `latchBuffers`
- `commit` / `composite`
- `Scheduler`
- `EventThread`
- `FrameTimeline`
- `DispSync` / predictor 相关实现

------


<!-- source: 25-84-hwc-composer.md -->

# 8.4 HWC / Composer

```
frameworks/native/services/surfaceflinger/DisplayHardware/HWComposer.cpp
hardware/interfaces/graphics/composer/
hardware/google/graphics/common/   （设备相关可能不同）
```

重点关注：

- `validateDisplay`
- `presentDisplay`
- release/present fence 处理
- display mode / refresh rate 切换能力

------


<!-- source: 33-103-surfaceflinger.md -->

# 10.3 SurfaceFlinger 机制模板

### 要回答的问题

- SF 何时开始 latch buffer？
- 合成周期由谁驱动？
- scheduler/event thread 如何工作？
- 哪些条件会导致本次 refresh 周期使用旧 buffer？

### 重点路径

- `handleMessageRefresh`
- `latchBuffers`
- `commit`
- `composite`
- `postComposition`
- fence 追踪

------


<!-- source: 57-173-surfaceflinger.md -->

# 17.3 SurfaceFlinger / 系统侧优化

- 降低 layer complexity
- 提升 device composition 命中率
- 避免频繁 transaction storm
- 稳定 refresh rate policy
- 优化 idle / resume 首帧策略
- 检查 Scheduler offset 配置

------
