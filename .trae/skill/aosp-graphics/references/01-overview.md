# 概览与范围
<!-- source: 00-overview.md -->

# aosp-graphics


<!-- source: 22-96.md -->

# 9.6 完整跨层分析问题模板

分析时必须逐段回答：

1. 输入事件是否按时触发帧生产
2. Choreographer 是否按预期收到 VSync
3. UI thread 是否在预算内完成 traversal
4. RenderThread 是否按时完成 drawFrame
5. dequeueBuffer 是否等待空闲 slot
6. queueBuffer 后是否及时进入 SF 消费路径
7. SF 是否及时 latch 当前 buffer
8. composition 是走 HWC 还是 client(GPU)
9. presentDisplay / atomic commit 是否延迟
10. actual present 是否晚于预期 deadline

------

# 10. 图形关键对象模型


<!-- source: 78-c.md -->

# C 级

- 仅模式匹配推断，待验证

输出时建议写：

- `根因置信度：S / A / B / C`

------

# 21. 推荐联动 Skill

建议与以下 Skill 组合：

- `aosp-wms`
  - 分析窗口、层级、transition、surface 来源
- `aosp-input`
  - 分析 InputDispatcher → App → Display 延迟
- `aosp-ams`
  - 分析启动、切换、resume 对首帧的影响
- `aosp-anr`
  - 分析图形问题与输入超时 / 主线程阻塞关系
- `perfetto-trace-analysis`
  - 深入分析 trace 与 FrameTimeline

------

# 22. 示例任务

### 示例 1

分析 Android 16 中 App 一帧从 doFrame 到 panel 显示的全链路调用链，并给出关键源码位置。

### 示例 2

分析 Perfetto 中 App deadline miss 的真正根因，是 UI 线程、RenderThread、GPU 还是 dequeueBuffer。

### 示例 3

分析 queueBuffer 成功但画面未显示，是 BLAST、SurfaceFlinger 还是 HWC/present 导致。

### 示例 4

分析高刷设备上的动画掉帧，是 FrameTimeline miss、mode switch 抖动，还是 SF/HWC 预算不够。

### 示例 5

分析 Activity 启动首帧黑屏，沿 BLASTBufferQueue → SurfaceControl → SurfaceFlinger → HWC 链路定位根因。

------

# 23. 最终目标

该 Skill 的最终目标是：

- 把 Android 图形栈问题变成可结构化分析的问题
- 把“体感卡顿/黑屏/掉帧”变成可验证的时序链
- 把“可能是图形问题”变成可落到源码和 trace 的根因
- 让分析结果具备工程可执行性、修复可行性和验证闭环
