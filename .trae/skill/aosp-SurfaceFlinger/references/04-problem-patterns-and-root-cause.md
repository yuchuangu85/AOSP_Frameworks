# 问题模式与根因
<!-- source: 42-116.md -->

# 11.6 第六步：形成根因闭环

最终结论必须回答：

- 为什么表现为当前现象
- 根因发生在哪个阶段
- 根因如何从源码逻辑上成立
- 有哪些运行时证据支持
- 如何修复与验证

------


<!-- source: 49-131-layer.md -->

# 13.1 Layer 可见但屏幕黑

排查顺序：

1. Layer 是否存在
2. Layer 是否 visible
3. parent 是否 visible
4. alpha / crop / transform 是否正常
5. 是否有 active buffer
6. buffer 是否成功 latch
7. acquire fence 是否 ready
8. 是否真的参与 composition
9. HWC / GPU composition 是否输出成功
10. present 是否成功

典型根因：

- Layer 可见但无 buffer
- buffer 未 latch
- crop 把内容裁没
- transform 导致移出屏幕
- parent hide
- HWC fallback/compose 异常
- present 失败或延迟

------


<!-- source: 65-155-present.md -->

# 15.5 present 开销

- HWC present 是否慢
- present fence 是否异常晚
- refresh rate 切换是否引起抖动

------


<!-- source: 66-16.md -->

# 16. 常见异常模式库

以下为 SurfaceFlinger 分析中的高频异常模式。


<!-- source: 83-8.md -->

# 8. 根因闭环
- 根因链路：
- 为什么会出现该现象：
- 为什么在这个场景触发：
- 为什么会表现成当前症状：
