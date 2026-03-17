# 调用链与时序
<!-- source: 05-62.md -->

# 6.2 时序优先
图形问题本质上通常是时序问题，必须回答：

- 异常从哪一帧开始
- 哪个阶段最慢
- 哪个阶段拖累后续链路
- 是否形成背压
- 是单点抖动还是持续性超时


<!-- source: 17-91-app.md -->

# 9.1 App 一帧从输入到显示的全链路

```
InputDispatcher
  → ViewRootImpl / View 层事件分发
  → Choreographer#doFrame
  → CALLBACK_INPUT
  → CALLBACK_ANIMATION
  → CALLBACK_TRAVERSAL
  → ViewRootImpl#performTraversals
  → measure / layout / draw
  → RecordingCanvas / RenderNode 记录 DisplayList
  → HardwareRenderer / RenderProxy
  → RenderThread 唤醒
  → Skia / OpenGL ES / Vulkan 提交渲染命令
  → ANativeWindow / Surface dequeueBuffer
  → GPU 渲染到 GraphicBuffer
  → queueBuffer
  → BufferQueue 进入队列
  → BLASTBufferQueue / SurfaceControl 同步事务提交
  → SurfaceFlinger 收到 transaction / layer update
  → latchBuffer
  → CompositionEngine 计算可见区域 / damage / composition strategy
  → HWC validate / present 或 client composition
  → DRM atomic commit / pageflip
  → panel scanout
  → 实际发光显示
```

------


<!-- source: 19-93-bufferqueue-blast.md -->

# 9.3 BufferQueue / BLAST 关键调用链

```
Producer(App)
  → dequeueBuffer
  → requestBuffer
  → attach fence / usage / transform / crop
  → queueBuffer
  → BufferQueueCore
  → Consumer(SurfaceFlinger) acquireBuffer
  → latch 到 Layer
  → releaseBuffer / release fence 返回给 Producer
```

BLAST 路径中，buffer 与 transaction 强绑定：

```
BLASTBufferQueue
  → transaction.setBuffer(...)
  → transaction.setFrameTimelineInfo(...)
  → transaction.apply(...)
  → SurfaceFlinger 原子处理几何状态 + buffer 状态
```

------


<!-- source: 23-101-choreographer.md -->

# 10.1 Choreographer

职责：

- 接收 VSync
- 组织帧阶段回调
- 构建 App 帧调度节奏

关键点：

- `doFrame`
- callback queue
- input / animation / traversal / commit
- frame deadline
- skipped frame


<!-- source: 27-105-blastbufferqueue.md -->

# 10.5 BLASTBufferQueue

职责：

- 解决 buffer 与 transaction 同步问题
- 特别用于窗口尺寸变化、rotation、启动首帧、动画过渡

关键点：

- transaction + buffer 原子提交
- 与 WMS / Shell / App 窗口生命周期耦合紧密
- 首帧黑屏、闪屏、resize 错位经常与此相关


<!-- source: 28-106-bufferqueue.md -->

# 10.6 BufferQueue

职责：

- 解耦 Producer 和 Consumer
- 管理 buffer slot
- 管理 acquire/release 生命周期
- 用 fence 做时序同步

关键点：

- dequeue
- queue
- acquire
- release
- max buffer count
- backpressure
- buffer age / damage


<!-- source: 30-108-surfaceflinger.md -->

# 10.8 SurfaceFlinger

职责：

- 处理 layer state
- 消费 buffer
- 合成 layer
- 驱动 HWC / display present

关键点：

- transaction apply
- latchBuffer
- composition strategy
- scheduler
- vsync
- display mode / refresh rate


<!-- source: 31-109-hwc-drm-panel.md -->

# 10.9 HWC / DRM / Panel

职责：

- 硬件合成
- pageflip
- 原子提交到显示硬件
- 最终显示输出

关键点：

- plane 分配
- overlay capability
- client composition fallback
- atomic commit
- refresh rate switch
- panel timing

------

# 11. FrameTimeline / Jank 深度模型

FrameTimeline 是 Android 现代图形性能分析的核心模型。分析掉帧时，必须优先从 FrameTimeline 入手。

------


<!-- source: 32-111.md -->

# 11.1 核心概念

### Expected Timeline

系统预测某帧应该在什么时候被生产、合成和显示。

### Actual Timeline

实际帧在各阶段真正完成的时间。

### App Deadline

App 必须在该时间前完成自己的生产，否则本帧将 miss。

### SF Deadline

SurfaceFlinger 必须在该时间前完成合成与提交流程，否则 SF miss。

### Present Time

帧真正被显示硬件提交并显示的时间点。

### Jank Type

系统标记该帧异常的归因类型。

------


<!-- source: 33-112-jank.md -->

# 11.2 Jank 分类深度模型

### A. App Deadline Miss

定义：
 App 未在 deadline 前完成 UI + RenderThread + queueBuffer。

常见原因：

- UI 主线程阻塞
- measure/layout/draw 过重
- RenderThread 太慢
- GPU completion 晚
- dequeueBuffer 阻塞
- 首帧资源初始化重

### B. SurfaceFlinger Deadline Miss

定义：
 App buffer 已出，但 SF 未在 deadline 前完成 latch / compose / present。

常见原因：

- layer 多
- transaction storm
- latch wait
- client composition 重
- HWC validate/present 慢
- mode switch 抖动

### C. Prediction Error

定义：
 系统对帧时序预测不准，理论上预算足够，但实际节奏偏移。

常见原因：

- VSync phase 漂移
- 可变刷新率切换
- 特殊 display mode 切换
- scheduler 不稳定
- 帧间依赖导致模型预测不稳

### D. Buffer Stuffing / Backpressure Jank

定义：
 前面帧未消费完导致后面帧堆积，生产侧不断被限流。

常见原因：

- consumer 慢
- release fence 晚
- SF 或 display 消费慢
- producer 过快

### E. SurfaceFlinger Scheduling Jitter

定义：
 SF 刷新节奏不稳定，即使 App 正常，显示链路仍抖动。

常见原因：

- EventThread / scheduler 抖动
- binder 干扰
- refresh policy 改变
- display pipeline mode 切换

------


<!-- source: 34-113-frametimeline.md -->

# 11.3 分析 FrameTimeline 的强制问题清单

必须回答：

1. 当前异常帧属于 App Miss 还是 SF Miss
2. App Miss 发生在 UI、RenderThread、GPU 还是 BufferQueue
3. SF Miss 发生在 latch、compose、present 还是 display
4. 是否存在连续 miss
5. 是否存在背压链
6. 是否与刷新率切换相关
7. 是否与窗口 transaction 风暴相关
8. 是否与首帧特殊路径相关

------


<!-- source: 35-114-frametimeline.md -->

# 11.4 FrameTimeline 归因树

```
Jank
├─ App Miss
│  ├─ UI Thread Slow
│  ├─ RenderThread Slow
│  ├─ GPU Completion Late
│  ├─ DequeueBuffer Blocked
│  └─ First Frame Init Heavy
├─ SF Miss
│  ├─ Latch Delayed
│  ├─ Composition Heavy
│  ├─ HWC Present Late
│  ├─ Client Composition Fallback
│  └─ Mode Switch / Scheduler Jitter
└─ Mixed / Backpressure
   ├─ BufferQueue Full
   ├─ Release Fence Late
   ├─ SF 消费不及时
   └─ 连续帧链式拖延
```

------


<!-- source: 41-125-fence.md -->

# 12.5 Fence 分析模型

### acquire fence 慢

表示 consumer 在等 producer 真正完成写 buffer。

### release fence 慢

表示 producer 在等 consumer/display 释放旧 buffer。

### present fence 慢

表示显示提交后，实际显示硬件完成很慢。

### retire fence 慢

表示 display pipeline 完成一次显示生命周期很慢。

### Fence 诊断规则

```
Fence Wait
├─ GPU 未完成渲染
├─ SurfaceFlinger 未完成消费
├─ HWC present 晚
├─ DRM commit/pageflip 晚
└─ panel timing / driver issue
```

------


<!-- source: 44-131-perfetto.md -->

# 13.1 Perfetto 必看轨道

### App 侧

- main thread
- RenderThread
- binder thread
- GPU completion
- FrameTimeline
- Choreographer / doFrame 相关 slice

### SurfaceFlinger 侧

- surfaceflinger main thread
- EventThread
- Scheduler
- FrameTimeline
- transaction / layer / buffer 相关轨道

### Display / Sync 侧

- VSYNC-app
- VSYNC-sf
- present fence
- HWC release
- HWC present
- Expected Timeline
- Actual Timeline

### 系统辅助轨道

- sched
- CPU freq
- idle
- binder
- IRQ
- display / drm vendor tracks

------


<!-- source: 45-132-perfetto.md -->

# 13.2 Perfetto 自动分析步骤

### 步骤 1：找到异常帧

定位：

- FrameTimeline 标红帧
- 实际明显超出 budget 的帧
- 体感卡顿对应时间点

### 步骤 2：判断 App Miss / SF Miss

根据 FrameTimeline：

- 若 App late：往 main / RenderThread / GPU / BufferQueue 深挖
- 若 SF late：往 SF / HWC / present 深挖

### 步骤 3：主线程分析

检查：

- `performTraversals`
- input callback
- animation callback
- binder / lock / GC / IO 干扰
- measure/layout/draw 耗时分布

### 步骤 4：RenderThread 分析

检查：

- DrawFrameTask
- texture upload
- flush
- shader compile
- EGL / Vulkan submit
- dequeueBuffer 等待

### 步骤 5：GPU / Fence 分析

检查：

- GPU completion 是否晚于 deadline
- acquire/release/present fence 是否拖尾

### 步骤 6：SF 分析

检查：

- handleMessageRefresh
- latchBuffers
- transaction apply
- composition
- present

### 步骤 7：Display/HWC 分析

检查：

- validate/present 是否异常长
- 是否 client composition fallback
- 是否 mode switch

### 步骤 8：形成归因结论

输出：

- 异常帧编号
- 预算值
- 最慢阶段
- 根因分类
- 连锁影响

------


<!-- source: 56-2.md -->

# 步骤 2：锁定异常帧

从用户体感时间点映射到：

- Perfetto FrameTimeline
- 主线程卡点
- RenderThread 卡点
- SF 刷新周期


<!-- source: 58-4.md -->

# 步骤 4：判定主瓶颈层

从以下六类中选择主瓶颈：

- UI
- RenderThread / GPU
- BufferQueue / Fence
- BLAST / Transaction
- SurfaceFlinger
- HWC / DRM / Panel


<!-- source: 68-173-buffer-blast.md -->

# 17.3 Buffer / BLAST

- 增加合理 buffer 深度
- 避免 resize 风暴
- 合并 transaction
- 保证 buffer 与 transaction 同步
- 减少无意义几何属性抖动
