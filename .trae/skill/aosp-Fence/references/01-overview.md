# 概览与范围
<!-- source: 00-overview.md -->

# aosp-Fence


<!-- source: 14-13.md -->

# 13. 根因定位决策树

```
出现掉帧 / 显示延迟
  ↓
先看 actual present 是否晚
  ├─ 否 → 更多是上层感知或输入节奏问题
  └─ 是
      ↓
      看 acquireFence 是否晚
      ├─ 是 → App / GPU / producer 问题
      └─ 否
          ↓
          看 SF/HWC 合成是否晚
          ├─ 是 → SurfaceFlinger / HWC 问题
          └─ 否
              ↓
              看 presentFence / retireFence / releaseFence 是否晚
              ├─ 是 → DRM / display / panel 问题
              └─ 否 → 进一步检查 trace 对齐、统计口径、FrameTimeline 映射
```

------


<!-- source: 20-4.md -->

# 4. 根因定位
- 责任层:
- 直接原因:
- 深层原因:


<!-- source: 22-topic-22.md -->

# 一、问题概述
- 场景：
- 现象：
- 影响范围：
- 发生频率：


<!-- source: 28-topic-28.md -->

# 七、源码定位建议
- 重点文件：
- 重点函数：
- 重点日志点：
