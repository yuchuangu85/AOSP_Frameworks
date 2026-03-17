# 工具与证据
<!-- source: 02-1-skill.md -->

# 1. Skill 定位

本 Skill 聚焦 Android 图形系统中的 **BufferQueue 机制**，用于完成以下任务：

1. 分析 BufferQueue 的设计目标、核心模型与跨进程共享缓冲区机制。
2. 建立 **App / Surface / BLAST / BufferQueue / SurfaceFlinger / HWC** 跨层调用链。
3. 分析 `dequeueBuffer` / `queueBuffer` / `acquireBuffer` / `releaseBuffer` / fence 同步机制。
4. 定位如下问题：
   - 黑屏
   - 花屏
   - 首帧不显示
   - Buffer 积压
   - Producer 阻塞
   - Consumer 阻塞
   - dequeue 超时
   - acquire 不及时
   - release 不及时
   - Surface 更新延迟
   - BLASTBufferQueue 异常
   - SurfaceFlinger 合成等待
   - Fence 未 signal
   - 帧延迟与掉帧
5. 结合源码、`dumpsys SurfaceFlinger`、Perfetto、Winscope、日志进行自动化分析。
6. 输出可验证、可追踪、可落地的根因结论与优化建议。

---


<!-- source: 40-121-dequeuebuffer.md -->

# 12.1 dequeueBuffer 阻塞模型

### 现象

- App 渲染线程卡住
- Trace 中卡在 `dequeueBuffer`
- 首帧或连续帧无法继续生产

### 常见原因

1. consumer 长时间不 release
2. queue backlog 太深
3. buffer 数量不足
4. single buffer 模式下消费者占用时间过长
5. HWC / SF / GPU 释放链慢
6. 异常事务导致内容消费节奏失衡

### 证据

- Perfetto 中 producer 阻塞在 dequeue
- `dumpsys SurfaceFlinger --latency` / layer 信息显示 backlog
- BufferQueue dump 中 free slot 少、acquired/queued 多

------


<!-- source: 44-125-buffer.md -->

# 12.5 Buffer 重分配抖动模型

### 现象

- 帧率抖动
- 内存波动
- 大量 gralloc 分配日志
- resize / rotation 时掉帧明显

### 常见原因

1. Surface 尺寸频繁变化
2. usage/format 改变
3. slot 绑定 buffer 无法复用
4. BLAST / Window resize 过程中持续重新申请 GraphicBuffer

------


<!-- source: 57-16.md -->

# 16. 常用命令与证据采集


<!-- source: 58-161-dumpsys.md -->

# 16.1 dumpsys

```
adb shell dumpsys SurfaceFlinger
adb shell dumpsys SurfaceFlinger --list
adb shell dumpsys SurfaceFlinger --latency
adb shell dumpsys gfxinfo <package>
adb shell dumpsys window
adb shell dumpsys activity top
```

------


<!-- source: 60-163-log.md -->

# 16.3 log 关注点

```
adb logcat -b system -b main -b events | grep -i -E "BufferQueue|Surface|SurfaceFlinger|BLAST|Fence|dequeue|queueBuffer|acquire|release"
```

------


<!-- source: 82-201.md -->

# 20.1 标准结论模板

```
一、问题现象
- 现象：
- 触发条件：
- 影响范围：

二、目标对象
- Surface：
- Layer：
- Producer：
- Consumer：

三、关键时序
- dequeueBuffer：
- queueBuffer：
- acquireBuffer：
- latchBuffer：
- present：
- releaseBuffer：

四、证据链
- 源码位置：
- dumpsys 证据：
- trace 证据：
- log 证据：

五、根因判断
- 根因分类：
- 直接阻塞点：
- 上游诱因：
- 下游表现：

六、修复建议
- 短期修复：
- 中期优化：
- 长期架构建议：
```

------
