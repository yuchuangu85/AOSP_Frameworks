# 补充专题
<!-- source: 06-63.md -->

# 6.3 跨层优先
不得只停留在单层分析，必须尽可能串起：

- App / UI Thread
- RenderThread / HWUI
- Surface / BufferQueue / BLAST
- SurfaceFlinger / CompositionEngine / Scheduler
- HWC
- DRM/KMS
- Panel


<!-- source: 07-64.md -->

# 6.4 分类归因
必须区分以下瓶颈，不得模糊表述为“渲染慢”：

- UI 线程慢
- RenderThread 慢
- GPU 慢
- BufferQueue 背压
- SurfaceFlinger 慢
- HWC prepare/present 慢
- DRM commit/pageflip 慢
- Panel / 模式切换慢


<!-- source: 15-84-hwc-composer-hal.md -->

# 8.4 HWC / Composer HAL

- `hardware/interfaces/graphics/composer/`
- `hardware/libhardware/include_all/hardware/hwcomposer2.h`
- AIDL / HIDL composer 实现目录
- vendor composer HAL 实现目录


<!-- source: 25-103-hwui-rendernode-renderthread.md -->

# 10.3 HWUI / RenderNode / RenderThread

职责：

- 记录 DisplayList
- 将 UI thread 的绘制转移到 RenderThread / GPU
- 管理 hardware accelerated render pipeline

关键点：

- RenderNode 树
- DrawFrameTask
- RenderProxy
- Skia pipeline
- 纹理上传
- shader 编译
- command flush


<!-- source: 29-107-fence.md -->

# 10.7 Fence

职责：

- 同步 CPU/GPU/Display 各阶段完成状态

常见类型：

- acquire fence
- release fence
- present fence
- retire fence

关键判断：

- 是 producer 等 release
- 还是 consumer 等 acquire
- 还是 display present 未完成
- 还是 GPU command 尚未 completion


<!-- source: 37-121-bufferqueue.md -->

# 12.1 BufferQueue 基础模型

### Producer 侧

通常是：

- App
- HWUI
- MediaCodec
- Camera
- SurfaceControl buffer producer

职责：

- dequeue 一个空闲 buffer
- 填充内容
- queue 回队列

### Consumer 侧

通常是：

- SurfaceFlinger
- GLConsumer / SurfaceTexture
- ImageReader 等

职责：

- acquire 最新 buffer
- 消费后 release
- 返回 release fence

------


<!-- source: 38-122-bufferqueue.md -->

# 12.2 BufferQueue 状态机

```
FREE
  → DEQUEUED by Producer
  → QUEUED
  → ACQUIRED by Consumer
  → RELEASED
  → FREE
```

分析重点：

- FREE 数是否不足
- DEQUEUED 是否长时间不归还
- QUEUED 是否堆积
- ACQUIRED 是否过久不 release
- 是否 fence 未归还导致 slot 复用延迟

------


<!-- source: 48-a-ui-thread-traversal-1-12.md -->

# A. UI Thread / Traversal 类（1 ~ 12）

### 1. `performTraversals` 过长

表现：

- 主线程长 slice
- App deadline miss

### 2. measure 过重

表现：

- 深层 View 树反复测量

### 3. layout 抖动

表现：

- 多次 requestLayout 导致一帧内重复布局

### 4. draw 过重

表现：

- 自定义 View 绘制复杂

### 5. 主线程 Binder 阻塞

表现：

- doFrame 中穿插 binder 调用

### 6. 主线程锁竞争

表现：

- monitor contention 导致 traversal 拖长

### 7. 主线程 IO

表现：

- 文件 / 数据库 / 资源读取发生在帧内

### 8. 主线程 GC

表现：

- 暂停导致 missed deadline

### 9. 频繁 invalidate

表现：

- 一帧内多次重绘触发

### 10. RecyclerView bind 过重

表现：

- 滑动帧中绑定与布局压力大

### 11. 动画驱动过多属性更新

表现：

- 单帧更新对象过多

### 12. 首帧 inflate 过重

表现：

- Activity 启动首帧超时

------


<!-- source: 55-1.md -->

# 步骤 1：定义现象

明确归类：

- 掉帧
- 首帧慢
- 黑屏
- 闪屏
- 花屏
- 触摸不跟手
- queue/dequeue 卡住
- SF 高负载
- HWC/present 延迟


<!-- source: 57-3-app-miss-sf-miss.md -->

# 步骤 3：先判 App Miss 还是 SF Miss

这是第一优先级分流。


<!-- source: 61-161-app.md -->

# 16.1 App 指标

- doFrame 间隔
- traversal time
- Draw time
- RenderThread frame time
- GPU completion
- queueBuffer latency


<!-- source: 62-162-buffer.md -->

# 16.2 Buffer 指标

- dequeue wait
- queue→acquire latency
- acquire→release latency
- release fence latency
- present fence latency


<!-- source: 64-164-display.md -->

# 16.4 Display 指标

- HWC validate/present
- mode switch duration
- atomic commit duration
- pageflip latency
- actual present jitter


<!-- source: 66-171-ui-app.md -->

# 17.1 UI / App

- 减少 measure/layout 抖动
- 减少主线程 binder / IO / lock
- 减少一帧内过多 invalidate
- 优化列表 bind 与图片加载
- 预加载首帧资源
