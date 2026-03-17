# 调用链与时序
<!-- source: 02-1.md -->

# 1. 适用范围

本 Skill 适用于以下任务：

- 分析 **SurfaceFlinger 中 LayerTree 的组织结构与设计思想**
- 分析 **App / WMS / SurfaceControl / BLAST / SurfaceFlinger 如何共同生成最终 Layer 树**
- 分析 **Layer 的父子关系、Relative Z、Mirror Layer、Container Layer、BufferStateLayer / BufferQueueLayer**
- 分析 **LayerTree 如何参与可见性判定、裁剪、变换、透明度、输入区域、显示输出映射**
- 分析 **Transaction 提交后，Layer 状态为何未按预期生效**
- 分析 **窗口显示异常**：
  - 黑屏
  - 闪屏
  - 花屏
  - 层级错乱
  - 遮挡关系错误
  - 截图正常但实显异常
  - 实显正常但截图异常
  - 分屏 / 旋转 / 折叠态下层级错乱
  - SurfaceView / Wallpaper / SystemUI / Leash / Transition 层级异常
- 分析 **dumpsys SurfaceFlinger / SurfaceFlinger Layer hierarchy / Winscope / Perfetto** 中的 LayerTree 信息
- 输出 **架构图、Layer 树图、状态流转图、事务时序图、源码调用链、异常模式库**

---


<!-- source: 03-2.md -->

# 2. 目标输出

执行本 Skill 后，必须输出以下内容：

1. **问题归类**
   - 这是 LayerTree 构建问题、事务同步问题、合成可见性问题、层级关系问题，还是显示设备映射问题

2. **核心调用链**
   - 从上层请求到 LayerTree 生效的完整链路

3. **Layer 树结构还原**
   - 关键 Layer 的父子关系、Z 序关系、输出目标、可见性状态

4. **状态与事务分析**
   - 当前状态、DrawingState、PendingState、TransactionState 的关系
   - 哪个事务改变了什么字段，何时被应用

5. **显示合成分析**
   - 哪些 Layer 参与 composition
   - 哪些被裁剪、隐藏、透明、跳过
   - 哪些下发给 HWC，哪些走 GPU

6. **根因结论**
   - 明确指出异常的最小根因点
   - 必须绑定源码位置 / dumpsys 证据 / trace 证据

7. **修复建议**
   - 从架构、事务时序、属性设置、窗口层级、Buffer 生命周期等角度给出建议

---


<!-- source: 15-53-layer.md -->

# 5.3 Layer 异常根因分类模型

### A. 树结构异常

- parent 错误
- reparent 错误
- child 挂载位置错误
- transition leash 下挂错对象
- mirror source 错误

### B. 属性异常

- z / relative z 错误
- alpha = 0
- hidden = true
- crop 错误
- transform 错误
- layerStack 错误
- color transform / dataspace / secure 标志异常

### C. buffer 异常

- 无 buffer
- buffer 未提交
- buffer 尺寸异常
- buffer transform 异常
- acquire fence 未 ready
- producer / consumer 不同步

### D. 事务异常

- transaction 未 apply
- apply 时序晚于预期
- merge 后被覆盖
- callback 返回不代表已上屏
- BLAST 同步边界误判

### E. 显示映射异常

- display 未关联
- output 过滤掉 layer
- virtual display / mirror display 选择错误
- screenshot path 与 physical display path 不一致

### F. 合成异常

- HWC 组合失败
- fallback GPU
- client composition 区域异常
- output layer 几何计算错误

------


<!-- source: 18-62.md -->

# 6.2 事务到显示的时序图

```
Client/WMS         SurfaceControl      SurfaceFlinger         LayerTree/Output
   |                    |                    |                      |
setLayer/setBuffer      |                    |                      |
   |----Transaction---->|                    |                      |
   |                    |----Binder--------->|                      |
   |                    |                    | apply transaction    |
   |                    |                    | update layer state   |
   |                    |                    | rebuild hierarchy    |
   |                    |                    | build snapshots      |
   |                    |                    | composite            |
   |                    |                    |--------display------>|
```

------


<!-- source: 20-7.md -->

# 7. 标准分析流程


<!-- source: 32-93-perfetto.md -->

# 9.3 如果输入是 Perfetto

必须重点关注：

- SurfaceFlinger main thread
- Transaction 提交与消费
- FrameTimeline
- Composition / present 周期
- layer 更新与 buffer latch 对齐关系

------


<!-- source: 40-115.md -->

# 11.5 事务类

### 模式 11：事务提交了，但本帧未进入 drawing state

现象：

- “代码已执行但画面晚一帧/多帧生效”

排查：

- applyTransaction 时机
- latchBuffer 与 commit 的相对顺序
- callback 触发含义

------

### 模式 12：多个事务 merge 后字段被覆盖

现象：

- 先 setCrop 后 setPosition，结果只生效一部分

排查：

- transaction merge 顺序
- 最终写入 RequestedState 的内容

------

### 模式 13：BLAST 同步边界导致状态与 buffer 不一致

现象：

- 几何与内容不同步
- 短暂拉伸、闪动、位置跳变

排查：

- BLASTBufferQueue
- sync transaction group
- buffer 与 geometry 原子提交

------


<!-- source: 47-13.md -->

# 13. 分析时的约束要求

### 13.1 不得只看单帧静态树

LayerTree 异常很多是时序问题。
 必须尽量比较：

- 异常前
- 异常帧
- 异常后

至少三段状态。

------

### 13.2 不得把“Layer 在树里”等价于“最终能显示”

必须继续检查：

- buffer
- crop
- transform
- output
- composition

------

### 13.3 不得把“Transaction callback 已回调”等价于“已经上屏”

必须区分：

- 事务被接收
- 状态被应用
- buffer 被 latch
- composition 完成
- present 完成

------

### 13.4 必须关注动画 / leash / BLAST 的扰动

现代 Android 中，大量 LayerTree 异常并非 Layer 本身出错，而是：

- transition leash
- blast sync
- snapshot / freeze layer
- reparent during animation

导致的短时错乱。

------
