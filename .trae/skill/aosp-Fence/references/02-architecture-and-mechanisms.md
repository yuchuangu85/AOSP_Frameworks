# 架构与核心机制
<!-- source: 05-4-fence.md -->

# 4. Fence 知识体系

---

### 4.1 Fence 基本定义

Fence 是 Android/Linux 图形同步机制中的“完成信号对象”，常见实现链路为：

- Android Framework/Native 层：`Fence`
- libsync 层：`sync_file`
- Kernel 层：`dma_fence`

它通常以一个 `fd` 的形式跨进程传递。

核心语义：

- **未 signal**：资源仍在被生产/消费，不可安全使用
- **已 signal**：异步阶段完成，可进入下一阶段

---

### 4.2 常见 Fence 类型

#### 4.2.1 acquireFence
表示消费者在读取该 buffer 前，需要等待生产者完成写入。

典型场景：

- App/GPU 渲染完成前，SurfaceFlinger/HWC 不能读取该 buffer
- `queueBuffer()` 时，producer 将 acquireFence 传给 consumer

本质：

> “这个 buffer 什么时候可以被安全消费”

---

#### 4.2.2 releaseFence
表示前一个消费者使用完 buffer 后，生产者才可以再次复用该 buffer。

典型场景：

- SurfaceFlinger/HWC/display 仍在使用 buffer
- BufferQueue 返回 releaseFence 给 producer
- producer 在下次复用该 slot 对应 buffer 前必须等待

本质：

> “这个 buffer 什么时候可以被安全重写”

---

#### 4.2.3 presentFence
表示一帧何时真正被提交/显示系统接受，常用于表示 present 相关完成时刻。

典型场景：

- SurfaceFlinger 从 HWC 获取 present fence
- 用于衡量显示提交时序
- 与 FrameTimeline / present latency 强相关

本质：

> “这一帧何时进入显示呈现阶段”

---

#### 4.2.4 retireFence
表示某帧已经从显示流水线中退休，不再是当前显示帧。

典型场景：

- DRM/HWC/display pipeline 中上一帧扫描完成
- SurfaceFlinger 用于判断 frame lifecycle 完结

本质：

> “这一帧何时真正退出显示占用状态”

---

### 4.3 Fence 所保护的典型资源

- GraphicBuffer
- GPU render target
- display composition input buffer
- framebuffer / client target
- layer buffer
- output buffer

---


<!-- source: 06-5.md -->

# 5. 完整跨层调用链

---

### 5.1 App → BufferQueue → SurfaceFlinger → HWC → DRM → Panel 全链路

```text
App
  ↓
RenderThread / OpenGL / Vulkan
  ↓ 生成 GPU completion fence
ANativeWindow / Surface
  ↓
BufferQueueProducer::queueBuffer()
  ↓ 携带 acquireFence（生产完成信号）
BufferQueueCore / BufferSlot
  ↓
BufferQueueConsumer / BLASTBufferQueue / SurfaceFlinger Layer
  ↓ wait acquireFence
SurfaceFlinger composition
  ↓ 生成 client target / layer state
HWComposer / Composer HAL
  ↓ HWC accept layer buffers + fences
DRM/KMS
  ↓ atomic commit / out-fence / retire-fence
Display Engine / Panel
  ↓
scanout 完成 / frame retired
  ↓
releaseFence 逐层回传，buffer 可复用
```

### 5.2 生产与消费视角

#### 生产者侧

- App
- GPU
- Surface producer
- SurfaceControl transaction producer
- RenderEngine（某些 client composition 场景）

#### 消费者侧

- BufferQueue consumer
- SurfaceFlinger
- HWC
- DRM/KMS
- Panel scanout

------

### 5.3 Buffer 生命周期中的 Fence 关系

```
dequeueBuffer
  ↓ 获取可写 buffer（可能需要等待旧 releaseFence）
draw / GPU render
  ↓
queueBuffer(acquireFence)
  ↓ consumer 收到 buffer
wait acquireFence
  ↓
compose / present
  ↓
生成 releaseFence / presentFence / retireFence
  ↓
producer 下次复用前等待 releaseFence
```

------


<!-- source: 07-6-aosp.md -->

# 6. AOSP 关键源码模块

------

### 6.1 Framework / Native 关键路径

#### Fence 相关

- `frameworks/native/libs/ui/Fence.cpp`
- `frameworks/native/libs/ui/include/ui/Fence.h`

#### BufferQueue 相关

- `frameworks/native/libs/gui/BufferQueueProducer.cpp`
- `frameworks/native/libs/gui/BufferQueueConsumer.cpp`
- `frameworks/native/libs/gui/BufferSlot.cpp`
- `frameworks/native/libs/gui/BufferQueueCore.cpp`

#### Surface / ANativeWindow

- `frameworks/native/libs/gui/Surface.cpp`
- `frameworks/native/libs/nativewindow/`

#### SurfaceFlinger

- `frameworks/native/services/surfaceflinger/`
- `frameworks/native/services/surfaceflinger/SurfaceFlinger.cpp`
- `frameworks/native/services/surfaceflinger/Layer.cpp`
- `frameworks/native/services/surfaceflinger/CompositionEngine/`
- `frameworks/native/services/surfaceflinger/DisplayHardware/HWComposer.cpp`

#### FrameTimeline / Scheduler

- `frameworks/native/services/surfaceflinger/Scheduler/`
- `frameworks/native/services/surfaceflinger/FrameTimeline/`

------

### 6.2 HAL / HWC 关键路径

- `hardware/interfaces/graphics/composer/`
- `hardware/interfaces/graphics/composer/2.x/`
- `hardware/google/` 或厂商定制 composer 实现
- `HWC2::Layer`
- `HWC2::Display`
- `ComposerHal`

重点关注：

- layer buffer 设置时的 acquire fence 传入
- present / release fence 回传
- display present 结果与 fence 的映射

------

### 6.3 Kernel / DRM 关键路径

- `drivers/dma-buf/`
- `drivers/dma-buf/sync_file.c`
- `include/linux/dma-fence.h`
- `drivers/gpu/drm/`
- `drm_atomic*`
- `drm_crtc_commit*`
- `out_fence_ptr`
- `in_fence_fd`

重点：

- sync_file 与 dma_fence 的绑定关系
- in-fence / out-fence 的原理
- atomic commit 完成点
- retire 时刻与 panel scanout 时刻的映射

------


<!-- source: 08-7.md -->

# 7. 核心概念深度模型

------

### 7.1 acquireFence 模型

#### 含义

buffer 已经被 queue 到消费者，但其内容可能尚未被 GPU 完全写完。
 消费者必须 wait acquireFence，确保内容可读。

#### 典型来源

- OpenGL ES 提交后的完成 fence
- Vulkan queue submit 对应的同步对象转换
- CPU/GPU 混合渲染路径

#### 异常表现

- SurfaceFlinger 长时间 wait acquireFence
- HWC 长时间无法获取可消费 layer buffer
- 帧已经 queue，但迟迟不能参与 composition

#### 根因方向

- GPU 渲染慢
- RenderThread 提交慢
- GPU driver 延迟
- 大纹理/离屏渲染/过度绘制导致 GPU completion 迟滞

------

### 7.2 releaseFence 模型

#### 含义

消费者已不再使用该 buffer，生产者可安全重写。

#### 在 BufferQueue 中的意义

没有 releaseFence signal，producer 就无法高效复用 buffer slot，可能导致：

- `dequeueBuffer` 卡住
- buffer 数量耗尽
- backpressure 扩大
- 输入正常但画面更新慢

#### 异常表现

- App dequeueBuffer 阻塞
- BufferQueue 中多个 slot 长时间处于占用
- 显示链路“看上去没有卡死”，但 producer 无法推进

#### 根因方向

- HWC/display 长时间占有 buffer
- DRM commit / panel scanout 过慢
- release fence 回传异常
- 某层 buffer 生命周期未结束

------

### 7.3 presentFence 模型

#### 含义

表示一帧提交到显示管线的重要时间点，常用于衡量 present 相关时延。

#### 作用

- 分析 `actual present time`
- 对齐 FrameTimeline 的显示结果
- 判断 SF 到 display 的延迟

#### 异常表现

- present 时间晚于预期 VSYNC
- 某帧 CPU/GPU 都不慢，但屏幕显示仍滞后
- Perfetto 中 expected / actual present 偏移明显

#### 根因方向

- HWC present 慢
- DRM atomic commit 排队
- display engine 忙
- panel scanout/TE 节奏异常

------

### 7.4 retireFence 模型

#### 含义

表征某帧从显示系统中“退休”的时刻。
 它比 present 更偏向“显示生命周期完成”。

#### 常用于

- 判断 buffer 何时真正不再被显示系统占用
- 分析上一帧何时退出 scanout
- 分析 frame overlap / pipeline depth

#### 异常表现

- retire 时刻明显滞后
- 多帧同时堆积在显示管线
- release 延迟继发增长

------


<!-- source: 10-9.md -->

# 9. 源码分析方法论

------

### 9.1 第一层：先定义当前问题属于哪类 Fence 问题

先回答：

1. 是 acquireFence 等太久？
2. 是 releaseFence 回来太晚？
3. 是 presentFence 晚？
4. 是 retireFence 晚？
5. 是 Fence 本身传递错误/丢失/复用错误？
6. 是上层误把 symptom 当成 root cause？

------

### 9.2 第二层：确认阻塞点在哪一层

| 现象                 | 优先怀疑层                      |
| -------------------- | ------------------------------- |
| wait acquireFence 长 | App/GPU/RenderThread/GPU driver |
| dequeueBuffer 卡住   | releaseFence/HWC/display        |
| present 晚           | SurfaceFlinger/HWC/DRM          |
| retire 晚            | DRM/panel/display engine        |
| buffer 堆积          | producer-consumer 速率失衡      |

------

### 9.3 第三层：回溯 signal 的责任方

Fence 分析核心问题不是“谁在等”，而是：

> “谁本应 signal，却没有按时 signal”

例如：

- acquireFence 未 signal → 追溯 GPU/render producer
- releaseFence 未 signal → 追溯 display consumer
- presentFence 晚 → 追溯 HWC/DRM/panel

------

### 9.4 第四层：建立跨层时序图

分析时必须输出：

1. buffer 何时 dequeue
2. 何时开始渲染
3. 何时 queue
4. acquireFence 何时 signal
5. SF 何时 latch
6. HWC 何时 present
7. presentFence 何时 signal
8. releaseFence 何时返回
9. 下次 buffer 何时可复用

------
