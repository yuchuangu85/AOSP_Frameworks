# 补充专题
<!-- source: 17-1.md -->

# 1. 现象
-


<!-- source: 18-2-fence.md -->

# 2. 涉及 Fence 类型
- acquireFence:
- releaseFence:
- presentFence:
- retireFence:


<!-- source: 23-fence.md -->

# 二、Fence 类型判定
- 主 Fence：
- 次 Fence：
- 当前主要阻塞点：


<!-- source: 29-topic-29.md -->

# 八、优化建议
- 短期规避：
- 中期修复：
- 长期治理：
```

------


<!-- source: 32-18.md -->

# 18. 一句话总结

> Fence 是 Android 图形系统里跨 App、GPU、SurfaceFlinger、HWC、DRM、Panel 的异步同步契约；分析 Fence 的关键不是“谁在等”，而是“谁本该 signal，却没有按时 signal”。
