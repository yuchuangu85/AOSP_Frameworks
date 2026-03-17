# 工具与证据
<!-- source: 02-1.md -->

# 1. 目标定位

该 Skill 用于对 Android 图形系统中的 **Fence 同步机制**进行系统级分析，重点解决以下问题：

1. **Fence 是什么、为什么存在、解决什么同步问题**
2. **Fence 在 App → BufferQueue → SurfaceFlinger → HWC → DRM → Panel 全链路中的流转关系**
3. **acquireFence / releaseFence / presentFence / retireFence 的职责与区别**
4. **Fence 与 GPU 渲染、HWC 合成、显示器扫描、VSYNC、FrameTimeline 的关系**
5. **Fence wait 过长、未 signal、错误传递、重复等待、栅栏泄漏、Buffer 堆积 的根因定位**
6. **如何结合源码、dumpsys、Perfetto、logcat、kernel trace 分析显示时序问题**
7. **如何区分 App 慢、GPU 慢、SurfaceFlinger 慢、HWC 慢、DRM/Panel 慢**

---


<!-- source: 12-11-dumpsys.md -->

# 11. dumpsys / 日志分析规则

------

### 11.1 dumpsys SurfaceFlinger 重点字段

重点关注：

- layer buffer state
- queued frames
- acquire fence state
- release state
- composition state
- present timing
- frame miss / latch miss
- backpressure 状态

### 11.2 dumpsys gfxinfo / framestats

可辅助判断：

- App 是否本身慢
- 渲染阶段是否超时
- 是否与 Fence 延迟一致

### 11.3 logcat 重点关键词

```
Fence
acquireFence
releaseFence
presentFence
retireFence
waitForever
timed out
BufferQueue
dequeueBuffer
queueBuffer
SurfaceFlinger
HWC
frame missed
latch
present
```

### 11.4 Kernel log / trace 重点关键词

```
dma_fence
sync_file
drm_atomic
out_fence
in_fence
retire
commit
vblank
crtc
```

------


<!-- source: 21-5.md -->

# 5. 证据
- 源码:
- Trace:
- 日志:
```

------

### 15.2 专家版模板

```
# AOSP Fence 专项分析报告
