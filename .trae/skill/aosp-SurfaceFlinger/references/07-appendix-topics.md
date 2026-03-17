# 补充专题
<!-- source: 17-7.md -->

# 7. 完整显示链路模型


<!-- source: 36-11.md -->

# 11. 标准分析方法论


<!-- source: 48-13.md -->

# 13. 常见分析专题


<!-- source: 55-14.md -->

# 14. 与上游模块的关系模型


<!-- source: 59-144-hwc.md -->

# 14.4 与 HWC 的关系

- SurfaceFlinger 负责全局决策
- HWC 负责尽量把图层交给硬件直接合成
- 当 HWC 无法处理某些属性时，SF 会退回 GPU/client composition
- 性能和显示正确性问题常发生在这层切换边界

------


<!-- source: 60-15.md -->

# 15. 性能分析重点


<!-- source: 64-154-composition.md -->

# 15.4 composition 开销

- HWC 接管率是否低
- GPU composition 是否频繁
- region 复杂度是否高
- blur / rounded corner / transform 等效果是否昂贵


<!-- source: 84-9.md -->

# 9. 修复建议
- 短期修复：
- 中期治理：
- 长期优化：
