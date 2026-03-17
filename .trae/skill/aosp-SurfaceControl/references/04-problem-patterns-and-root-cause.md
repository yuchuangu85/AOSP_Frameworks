# 问题模式与根因
<!-- source: 29-92-setlayer-z.md -->

# 9.2 setLayer / Z 序异常

重点看：

- 使用的是 `setLayer` 还是 `setRelativeLayer`
- parent 是否相同
- 是否存在 reparent 后 layer 关系失效
- SF 最终 Layer 树中的 sibling 顺序
- 是否被 Shell/WMS 后续事务重置
- 动画 leash 是否改变了显示层级

常见根因：

- 作用对象不是最终显示的 leash
- relativeTo 的 Layer 错了
- 同一帧多次设置层级被覆盖
- parent 变化导致层级比较基准变化

------


<!-- source: 30-93-reparent.md -->

# 9.3 reparent 异常

重点看：

- 子 Layer 是否被移到预期父节点
- 旧父节点/新父节点生命周期是否稳定
- reparent 后 relative layer、crop、transform 是否继承变化
- WMS 是否基于动画/过渡使用 leash 包裹导致层级结构变化

常见根因：

- reparent 目标错误
- 新父 Layer 不可见
- 中间过渡 leash 逻辑未考虑
- reparent 后未重新设置 layer/crop/position

------


<!-- source: 31-94-crop-matrix-position.md -->

# 9.4 crop / matrix / position 异常

重点看：

- 操作的是 buffer crop 还是 window crop
- 变换矩阵是否和旋转/缩放共同作用
- 父子 Layer 的 transform 是否叠加
- Insets / Display transform / rotation transform 是否已参与计算

常见根因：

- 坐标系理解错误
- 在错误的 Layer 上做变换
- 旋转后 crop 未重算
- leash 与内容层双重变换导致偏移

------


<!-- source: 32-95.md -->

# 9.5 黑屏 / 闪屏

排查顺序：

1. Layer 是否创建成功
2. show 是否生效
3. buffer 是否到达
4. 是否 latch 成功
5. 当前显示层是否是空 buffer
6. 是否在 resize / rotation / transition 窗口切换中出现中间态
7. 是否由于事务与 buffer 不同步出现闪烁
8. 是否 parent hide / alpha / crop 导致视觉黑屏
9. HWC/client composition 是否异常

典型根因：

- 先 show 后 buffer 晚到
- 旧层被 hide，新层 buffer 未到
- BLAST 同步点没对齐
- 过渡 leash 销毁与新层显示之间有空窗
- relayout 重建 surface 导致短暂黑屏

------


<!-- source: 42-15.md -->

# 15. 常见异常模式库

以下为 SurfaceControl 分析中高频异常模式。


<!-- source: 54-2.md -->

# 2. 结论摘要
- 根因：
- 直接触发点：
- 责任模块：
- 是否与 WMS / BLAST / SurfaceFlinger 相关：


<!-- source: 60-8.md -->

# 8. 根因闭环
- 根因链路：
- 为什么会表现为该现象：
- 为什么之前没有暴露：
- 为什么在该场景下触发：
