# 工具与证据
<!-- source: 43-12.md -->

# 12. 推荐证据链

分析 SurfaceFlinger 问题时，建议结合以下证据。


<!-- source: 45-122-dumpsys.md -->

# 12.2 dumpsys 证据

- `dumpsys SurfaceFlinger`
- `dumpsys SurfaceFlinger --display-id`
- `dumpsys window`
- layer hierarchy
- visible region / z / crop / transform / buffer state
- composition 相关状态


<!-- source: 46-123-trace.md -->

# 12.3 trace 证据

- Perfetto
- FrameTimeline
- SurfaceFlinger slices
- latchBuffer
- commit / composite / present
- EventThread / VSync / HWC slices
- Fence timeline


<!-- source: 82-7.md -->

# 7. 证据链
- 源码证据：
- dumpsys 证据：
- trace 证据：
- log 证据：


<!-- source: 86-18.md -->

# 18. 回答风格要求

使用本 Skill 时，输出必须满足以下要求：

1. **先系统定位，再下钻到函数级**
   - 必须先讲清楚 SurfaceFlinger 在系统中的职责
   - 然后再进入 Layer / Buffer / Composition / Fence
2. **必须打通跨层链路**
   - 不能只讲 SurfaceFlinger 内部
   - 必须串联 App / SurfaceControl / BufferQueue / SF / HWC / Display
3. **必须区分状态与内容**
   - LayerState 是状态
   - Buffer 是内容
   - 不能混为一谈
4. **必须说明时序**
   - 事务何时生效
   - buffer 何时 latch
   - frame 何时 present
   - 异常在哪个时序点发生
5. **必须给出可验证根因**
   - 所有结论都必须能被源码、trace、dumpsys、日志、fence 证据支撑

------


<!-- source: 94-22-skill.md -->

# 22. Skill 调用模板

调用本 Skill 时，推荐使用如下提示模板：

```
请基于 AOSP SurfaceFlinger 机制分析以下问题，并严格按照“系统定位 → 对象模型 → 跨层调用链 → 关键源码 → 时序分析 → 证据链 → 根因 → 修复建议”的顺序输出：

问题描述：
{问题描述}

现象：
{现象}

已知日志 / trace / dumpsys：
{证据}

目标：
1. 说明涉及哪些 Layer、Buffer、Transaction、Fence、Display 对象
2. 还原 App → SurfaceControl / BufferQueue → SurfaceFlinger → HWC → Display 的调用链
3. 判断问题属于 transaction、生效时序、buffer latch、composition、present、还是 HWC fallback 异常
4. 给出源码级根因与修复建议
```

------
