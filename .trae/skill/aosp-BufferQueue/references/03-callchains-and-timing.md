# 调用链与时序
<!-- source: 18-7.md -->

# 7. 完整调用链


<!-- source: 20-72-surfacecontrol-blast.md -->

# 7.2 SurfaceControl / BLAST 路径

```
App
  -> SurfaceControl / BLASTBufferQueue
    -> BLASTBufferItemConsumer
    -> queue buffer + transaction
    -> SurfaceFlinger
      -> apply transaction
      -> latch buffer
      -> compose
      -> present
```

------


<!-- source: 30-101.md -->

# 10.1 基础队列时序图

```
Producer                          BufferQueue                       Consumer
   |                                   |                               |
   | dequeueBuffer()                   |                               |
   |---------------------------------->|                               |
   | <----------- slot ----------------|                               |
   |                                   |                               |
   | render buffer                     |                               |
   |                                   |                               |
   | queueBuffer(slot, fence)          |                               |
   |---------------------------------->|                               |
   |                                   |                               |
   |                                   | acquireBuffer()               |
   |                                   |<------------------------------|
   |                                   | -------- BufferItem --------> |
   |                                   |                               |
   |                                   |        consume                |
   |                                   |                               |
   |                                   | releaseBuffer(slot)           |
   |                                   |<------------------------------|
   |                                   |                               |
```

------


<!-- source: 31-102-fence.md -->

# 10.2 带 fence 的时序图

```
Producer                GPU/Fence                BufferQueue             Consumer/SF
   |                        |                         |                        |
   | dequeue                |                         |                        |
   |----------------------->|                         |                        |
   | render                 |                         |                        |
   | submit GPU cmd         |                         |                        |
   |-------------- acquireFence --------------------->|                        |
   | queueBuffer(slot)                                |                        |
   |------------------------------------------------->|                        |
   |                                                  | acquireBuffer          |
   |                                                  |----------------------->|
   |                                                  | wait on acquireFence   |
   |                                                  |                        |
   |                                                  | compose/display        |
   |                                                  |                        |
   |<----------------------- releaseFence -------------------------------------|
```

------


<!-- source: 32-103-blast.md -->

# 10.3 BLAST 路径时序图

```
App
  -> BLASTBufferQueue::dequeueBuffer
  -> render
  -> BLASTBufferQueue::queueBuffer
  -> transaction merge
  -> SurfaceFlinger transaction apply
  -> latch buffer
  -> composition
  -> present
```

BLAST 的关键价值在于：

- 将 buffer 与 transaction 更紧密绑定
- 降低 geometry/state 与内容不同步概率
- 改善窗口变更、尺寸变更、旋转等场景的一致性

------


<!-- source: 41-122-queue.md -->

# 12.2 queue 了但没显示模型

### 现象

- App 认为已经提交帧
- queueBuffer 成功
- 屏幕内容不更新

### 常见原因

1. SurfaceFlinger 未 acquire 到该 buffer
2. acquire fence 未 signal
3. layer 不可见或被遮挡
4. transaction 未同步提交
5. BLAST state 与 buffer 时序错位
6. HWC 合成未生效
7. queue 的不是目标 layer 对应的 surface

------


<!-- source: 49-14-bufferqueue-frametimeline-jank.md -->

# 14. BufferQueue 与 FrameTimeline / Jank 的关系

BufferQueue 问题本质上是帧流转问题，因此和 FrameTimeline 强相关。


<!-- source: 59-162-trace-perfetto.md -->

# 16.2 trace / perfetto

关注：

- SurfaceFlinger
- FrameTimeline
- Choreographer
- HWUI / RenderThread
- BufferQueue
- GPU completion
- present fence
- transaction
- HWC events

------


<!-- source: 63-172-perfetto.md -->

# 17.2 读取 Perfetto 时必须检查

1. RenderThread 是否卡在 `dequeueBuffer`
2. SurfaceFlinger 是否未及时 latch
3. GPU completion 是否延迟
4. present fence 是否拖后
5. transaction 与 queueBuffer 的先后关系
6. FrameTimeline 中 app/sf/jank 归因位置
7. 是否存在周期性 backlog

------


<!-- source: 64-173.md -->

# 17.3 读取源码时必须检查

1. slot 状态转换路径
2. buffer 分配触发条件
3. queue/acquire/release 的锁与状态条件
4. fence 传递路径
5. BLAST 合并 transaction 的逻辑
6. consumer latch 条件
7. 异常路径和 fail-fast 分支

------


<!-- source: 69-184-blast.md -->

# 18.4 BLAST 类

1. buffer 已 queue，transaction 未同步
2. 几何状态先变、内容后到
3. 内容先到、事务后应用
4. resize 过程中持续 buffer 重建
5. rotation 期间显示错位
6. reparent 后目标 layer 不一致
7. leash layer 误判
8. BLAST 队列 backlog
9. merge transaction 时序偏移
10. 首帧依赖 BLAST 事务而延迟


<!-- source: 74-189.md -->

# 18.9 复合链路类

1. CPU 慢 + queue 延迟
2. GPU 慢 + acquire fence 延迟
3. SF 忙 + release 晚
4. HWC 卡 + dequeue 阻塞
5. BLAST 时序偏移 + 首帧黑
6. 多级 BufferQueue 链式累积延迟
7. transaction 风暴引发消费滞后
8. VSYNC 错过导致“BufferQueue 看起来卡”
9. layer 生命周期切换导致 buffer orphan
10. 根因在显示链但表象在 producer 卡住

------


<!-- source: 75-19.md -->

# 19. 标准分析流程


<!-- source: 78-193.md -->

# 19.3 第三步：还原时序

按以下顺序分析：

1. dequeue 是否正常
2. render 是否完成
3. queue 是否成功
4. acquire 是否及时
5. latch 是否发生
6. compose/present 是否发生
7. release 是否返回

------


<!-- source: 88-214-blast.md -->

# 21.4 BLAST 优化

- 保证 buffer 与 transaction 同步性
- 降低 resize/rotation 期间多余事务
- 避免 state/content 提交错位

------


<!-- source: 89-22.md -->

# 22. 分析时的高频误区

1. 看到 `dequeueBuffer` 卡住，就误判是 producer bug。
2. 看到 queue 成功，就误判 buffer 一定已经显示。
3. 不看 fence，只看 queue/acquire。
4. 不区分 BufferQueueLayer 与 BufferStateLayer。
5. 把 layer 不可见误判成 BufferQueue 无输出。
6. 把 GPU/HWC 慢误判成 SF acquire 慢。
7. 只看 App，不看 SurfaceFlinger。
8. 只看源码，不看运行时证据。
9. 只看单帧，不看连续帧节奏。
10. 忽略 BLAST transaction 时序。

------
