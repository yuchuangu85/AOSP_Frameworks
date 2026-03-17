# 补充专题
<!-- source: 20-8.md -->

# 8. 关键对象模型


<!-- source: 27-9.md -->

# 9. 常见分析专题


<!-- source: 39-13-surfacecontrol.md -->

# 13. SurfaceControl 分析方法论


<!-- source: 61-9.md -->

# 9. 修复建议
- 短期修复：
- 中期治理：
- 长期优化：


<!-- source: 69-213.md -->

# 21.3 用户问：为什么窗口黑屏？

应从以下维度给结论：

- Surface 是否创建成功
- SurfaceControl 是否 show
- buffer 是否到位
- 是否 latch
- parent/leash/crop/alpha/z-order 是否正确
- 是否在 relayout / transition / rotation 中出现中间态

------
