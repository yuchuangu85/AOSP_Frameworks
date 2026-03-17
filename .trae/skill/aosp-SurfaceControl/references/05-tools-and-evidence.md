# 工具与证据
<!-- source: 41-14.md -->

# 14. 推荐证据链

分析 SurfaceControl 问题时，优先结合以下证据：

### 14.1 源码证据

- Java 调用路径
- JNI 桥接
- native transaction 组装
- SF state 消费逻辑

### 14.2 dumpsys 证据

- `dumpsys SurfaceFlinger`
- `dumpsys window`
- `dumpsys activity service SurfaceFlinger`
- layer hierarchy / visible state / z / crop / alpha / transform

### 14.3 trace 证据

- Perfetto / systrace
- FrameTimeline
- SurfaceFlinger transaction / composition / present
- buffer latch / fence / VSYNC

### 14.4 日志证据

- WMS logs
- SurfaceFlinger logs
- BLASTBufferQueue logs
- Transaction apply logs
- buffer queue logs

------


<!-- source: 59-7.md -->

# 7. 证据链
- 源码证据：
- dumpsys 证据：
- trace 证据：
- log 证据：
