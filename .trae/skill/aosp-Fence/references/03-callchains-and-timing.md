# 调用链与时序
<!-- source: 03-2.md -->

# 2. 适用场景

当用户出现以下诉求时，应调用本 Skill：

- 分析 Android 图形栈中的 Fence 机制
- 分析 `acquireFence` / `releaseFence` / `presentFence` / `retireFence`
- 分析 BufferQueue 中 buffer 为什么迟迟不能复用
- 分析 SurfaceFlinger 或 HWC 为什么卡住
- 分析 Perfetto 中 Fence wait、present delay、display latency
- 分析掉帧、Jank、黑屏、闪屏、首帧慢、滑动迟滞
- 分析 `dequeueBuffer` 阻塞、`queueBuffer` 堆积
- 分析 GPU 完成慢、显示完成慢、合成超时
- 分析内核 `sync_file` / `dma_fence` / DRM fence 时序
- 构建图形栈同步时序图与调用链

---


<!-- source: 04-3.md -->

# 3. 核心分析原则

### 3.1 Fence 不是“渲染对象”，而是“同步契约”
Fence 的本质不是图像数据，而是一个 **异步完成信号**。  
它用于描述：

- 某个生产动作何时完成
- 某个消费动作何时可以安全开始
- 某块 buffer 何时可以安全复用
- 某帧何时真正显示到屏幕上

### 3.2 Fence 分析必须放在“生产者-消费者模型”里理解
Fence 永远伴随异步管线存在。分析时必须先明确：

- 谁是生产者
- 谁是消费者
- 生产的是什么资源（通常是 GraphicBuffer）
- 谁在等谁
- 等待发生在哪一层
- signal 的责任属于谁

### 3.3 Fence 问题本质上是“跨线程 / 跨进程 / 跨硬件阶段”的时序问题
Fence 不是单点问题，必须跨层分析：

- App RenderThread / GPU
- BufferQueue
- SurfaceFlinger
- HWC
- DRM/KMS
- Panel 扫描输出

### 3.4 Fence 不能脱离 FrameTimeline / VSYNC / Buffer 生命周期单独看
Fence wait 长不一定等于问题就在 Fence；Fence 往往只是 **症状承载物**。  
真正要回答的是：

- 为什么 Fence 迟迟不 signal
- 谁阻塞了 signal 的产生
- 这一阻塞是否最终转化为掉帧/Jank

---


<!-- source: 09-8-fence-frametimeline.md -->

# 8. Fence 与 FrameTimeline 的关系

Fence 是 **底层异步完成信号**，FrameTimeline 是 **上层帧时序评价模型**。
 两者关系如下：

- FrameTimeline 关注“期望帧何时显示、实际帧何时显示”
- Fence 提供“底层每个阶段何时完成”的关键证据

### 8.1 分析映射关系

#### App 阶段

- Input → UI Thread → RenderThread → GPU completion
- acquireFence 常可映射渲染完成时间

#### SF 阶段

- latch buffer
- wait acquireFence
- composition
- present

#### Display 阶段

- HWC present
- DRM commit
- presentFence / retireFence signal

### 8.2 结论

FrameTimeline 判断一帧是否 Jank，Fence 解释这帧为何 Jank。

------


<!-- source: 19-3.md -->

# 3. 时序判断
- buffer dequeue:
- queueBuffer:
- acquireFence signal:
- SF latch:
- HWC present:
- releaseFence return:


<!-- source: 24-topic-24.md -->

# 三、跨层调用链
App
→ RenderThread/GPU
→ BufferQueue
→ SurfaceFlinger
→ HWC
→ DRM/KMS
→ Panel


<!-- source: 25-topic-25.md -->

# 四、关键时序
1. dequeueBuffer：
2. GPU render start/end：
3. queueBuffer：
4. acquireFence signal：
5. SF latch：
6. compose/present：
7. presentFence signal：
8. retireFence / releaseFence：


<!-- source: 30-16.md -->

# 16. 分析约束

### 必须做的事

- 必须区分 acquire / release / present / retire 语义
- 必须给出跨层时序链路
- 必须指出 signal 责任方
- 必须明确症状与根因的区别
- 必须尽量结合源码路径说明

### 禁止做的事

- 不能把所有等待都笼统归因于 SurfaceFlinger
- 不能把 acquireFence wait 直接等价为 SF 问题
- 不能把 dequeueBuffer 卡住直接等价为 App 问题
- 不能脱离 Buffer 生命周期空谈 Fence
- 不能只给现象，不给证据链

------


<!-- source: 31-17-skill.md -->

# 17. 与其他 Skill 的协同关系

本 Skill 建议与以下 Skill 联动：

- `aosp-graphics`：图形栈全局架构与合成链路
- `aosp-SurfaceFlinger`：SF 合成、latch、present 深入分析
- `aosp-BufferQueue`：buffer 生命周期、slot、backpressure
- `aosp-VSYNC`：VSYNC/FrameTimeline/调度时序
- `aosp-HWC`：硬件合成与 Composer HAL
- `aosp-DRM`：KMS/atomic commit/out-fence/in-fence
- `aosp-jank`：掉帧与卡顿归因
- `aosp-perfetto`：trace 证据提取与时间线分析

------
