# 工具与证据
<!-- source: 34-11-perfetto-trace.md -->

# 11. Perfetto / Trace 分析框架


<!-- source: 38-12-dumpsys.md -->

# 12. dumpsys 分析框架


<!-- source: 61-20.md -->

# 20. 最终输出质量标准

优秀输出必须达到：

- 能解释清楚 **VSYNC 为什么存在**
- 能讲明白 **App 与 SF 是如何被两个时序源协同驱动**
- 能说明 **一帧从开始到上屏经过哪些阶段**
- 能用源码定位 **谁控制调度、谁生成 buffer、谁消费 buffer、谁最终 present**
- 能用 trace / dumpsys 证据证明根因
- 能把问题归属到：
  - App
  - RenderThread / GPU
  - BufferQueue
  - SurfaceFlinger
  - HWC / Driver
  - RefreshRate Policy
- 能给出工程上可执行的优化建议

------
