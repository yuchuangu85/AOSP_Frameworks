# 补充专题
<!-- source: 11-52-bufferslot.md -->

# 5.2 BufferSlot

每个 BufferQueue 中维护一组固定上限的 slot，slot 内可能绑定一个 `GraphicBuffer`。

slot 包含：
- buffer 引用
- frameNumber
- fence
- buffer state
- crop / transform / dataspace / timestamp 等元数据

slot 不是简单数组元素，而是 buffer 生命周期管理单元。

---


<!-- source: 13-54-fence.md -->

# 5.4 Fence

Fence 用于同步：
- 渲染完成
- buffer 可读
- buffer 可复用
- 显示完成

常见 fence：
- acquire fence
- release fence
- present fence

---


<!-- source: 14-55-bufferitem.md -->

# 5.5 BufferItem

消费者 acquire 时拿到的是 `BufferItem` 语义对象，包含：
- slot
- frameNumber
- timestamp
- crop
- transform
- dataspace
- acquireFence
- scalingMode
- surfaceDamage 等

---


<!-- source: 22-8-buffer.md -->

# 8. Buffer 生命周期模型


<!-- source: 23-81.md -->

# 8.1 典型生命周期

```
FREE
  -> DEQUEUED
  -> QUEUED
  -> ACQUIRED
  -> FREE
```

扩展理解：

- `FREE`：可供 producer 使用
- `DEQUEUED`：producer 已拿到，正在写
- `QUEUED`：producer 已提交，等待 consumer 获取
- `ACQUIRED`：consumer 正在使用
- `RELEASED -> FREE`：consumer 用完，slot 返回可复用

------


<!-- source: 24-82.md -->

# 8.2 生命周期细节

### 1. dequeue

生产者申请一个可写 slot。若无可用 slot，则可能阻塞或失败。

### 2. requestBuffer

如 slot 尚未绑定 buffer，可能触发 GraphicBuffer 分配。

### 3. attach 渲染内容

CPU/GPU 向该 buffer 写入内容。

### 4. queue

buffer 携带 acquire fence 与元数据进入队列。

### 5. acquire

消费者取走最新或下一个 buffer。

### 6. release

消费者处理完后释放 slot，生产者后续可继续复用。

------


<!-- source: 25-9.md -->

# 9. 关键源码入口

> 不同 Android 版本路径可能有差异，分析时应以当前源码树为准。


<!-- source: 34-111-producer.md -->

# 11.1 Producer 侧分析

必须回答：

1. 谁在生产 buffer？
2. 是 CPU 渲染还是 GPU 渲染？
3. `dequeueBuffer` 是否阻塞？
4. slot 是否不足？
5. buffer 是否频繁重分配？
6. queue 是否按预期发生？
7. queue 后 fence 是否正确传递？
8. producer 速率是否过快导致 backlog？

------


<!-- source: 37-114-fence.md -->

# 11.4 Fence 分析

必须回答：

1. acquire fence 是否迟迟不 signal？
2. release fence 是否返回过慢？
3. 是否 GPU/HWC 导致 fence 长等待？
4. queue 正常但 buffer 不可读，是否 fence 原因？
5. present 正常但 slot 未释放，是否 releaseFence 卡住？

------


<!-- source: 39-12-bufferqueue.md -->

# 12. BufferQueue 关键问题模型


<!-- source: 45-13.md -->

# 13. 完整跨层分析链路


<!-- source: 48-133-surfaceview-camera-video.md -->

# 13.3 SurfaceView / Camera / Video 常见链路

```
Camera/Codec producer
  -> Surface
    -> BufferQueue
      -> SurfaceFlinger or GLConsumer
        -> compose/display or texture sampling
```

------


<!-- source: 52-15.md -->

# 15. 与相关模块的边界


<!-- source: 55-153-bufferqueue-vs-surfaceflinger.md -->

# 15.3 BufferQueue vs SurfaceFlinger

- BufferQueue 只保证帧从 producer 到 consumer
- 具体能否显示到屏幕，要看 SF latch / compose / present


<!-- source: 56-154-bufferqueue-vs-hwc.md -->

# 15.4 BufferQueue vs HWC

- buffer 能被 acquire 不代表一定已显示
- releaseFence/presentFence 往往把 HWC 延迟反映回 BufferQueue 侧

------


<!-- source: 61-17.md -->

# 17. 自动分析规则


<!-- source: 76-191.md -->

# 19.1 第一步：确认现象

必须先明确：

- 是黑屏还是晚显示？
- 是 producer 卡住还是 consumer 不消费？
- 是偶现还是稳定复现？
- 是首帧问题还是稳态问题？

------


<!-- source: 84-21.md -->

# 21. 优化建议库


<!-- source: 85-211-producer.md -->

# 21.1 Producer 侧优化

- 减少无意义高频生产
- 避免 resize 抖动
- 控制 buffer count 合理值
- 减少过大 buffer 分配
- 避免 queue 频率远高于显示消费频率
