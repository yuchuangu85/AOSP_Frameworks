# 问题模式与根因
<!-- source: 36-113-queue.md -->

# 11.3 Queue 状态分析

必须回答：

1. 当前 queued buffer 数量多少？
2. free slot 数量多少？
3. dequeue 是否因 queue 深度满而被阻塞？
4. 是否 single-buffer / double-buffer / triple-buffer 模式行为异常？
5. 是否存在 buffer starvation 或 backlog？

------


<!-- source: 43-124-releasebuffer.md -->

# 12.4 releaseBuffer 慢模型

### 现象

- producer 长期拿不到 free slot
- 队列深度持续升高
- dequeue 卡顿

### 常见原因

1. HWC releaseFence 返回慢
2. GPU 合成链条长
3. consumer 处理超时
4. 显示链阻塞
5. buffer 被错误持有

------


<!-- source: 50-141-bufferqueue-jank.md -->

# 14.1 BufferQueue 导致 Jank 的典型方式

1. producer dequeue 被阻塞，导致新帧无法生成
2. queue 后 acquire 过慢，导致帧未赶上期望显示时刻
3. acquire fence 未 signal，导致 latch miss
4. release 过慢，影响后续帧生成节奏
5. 多级 BufferQueue 叠加带来端到端延迟

------


<!-- source: 51-142-jank.md -->

# 14.2 Jank 归因时必须区分

- 是 CPU 渲染慢？
- GPU 完成慢？
- 还是 BufferQueue 排队慢？
- 还是 SurfaceFlinger latch/composition 慢？
- 还是 HWC/present 慢？

------


<!-- source: 53-151-bufferqueue-vs-fence.md -->

# 15.1 BufferQueue vs Fence

- BufferQueue 负责 buffer 生命周期与队列状态
- Fence 负责 buffer 何时可用/何时释放
- 很多“BufferQueue 卡住”其实根因在 fence


<!-- source: 65-18-80-bufferqueue.md -->

# 18. 80+ BufferQueue 异常模式库


<!-- source: 66-181-producer.md -->

# 18.1 Producer 类

1. dequeue 长阻塞
2. dequeue 周期性抖动
3. requestBuffer 频繁触发
4. queue 成功但 frameNumber 不增长
5. producer 过快导致 backlog
6. 单 buffer 模式导致串行阻塞
7. surface 尺寸变化导致 buffer 重建
8. usage 改变导致无法复用
9. 渲染已完成但 queue 未发生
10. queue 时 fence 异常


<!-- source: 68-183-fence.md -->

# 18.3 Fence 类

1. acquire fence 未 signal
2. release fence 过慢
3. present fence 延迟
4. GPU completion 晚
5. HWC release 晚
6. fence 链过长
7. fence 误判为 BufferQueue 问题
8. queue 后长期等待 fence
9. display 完成后 release 返回慢
10. fence 泄漏/异常持有


<!-- source: 73-188.md -->

# 18.8 资源与配置类

1. buffer count 设置不合理
2. triple buffering 失衡
3. 内存压力下 gralloc 分配慢
4. format 过重导致吞吐下降
5. usage flags 不合理
6. protected buffer 带来额外限制
7. 高刷下 queue 频率不匹配
8. 多显示设备下消费节奏异常
9. HDR / dataspace 切换引发重建
10. 宽高快速抖动导致大量重分配
