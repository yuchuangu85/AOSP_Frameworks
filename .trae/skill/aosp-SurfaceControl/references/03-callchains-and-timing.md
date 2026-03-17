# 调用链与时序
<!-- source: 06-41-surfacecontrol.md -->

# 4.1 SurfaceControl 的本质

`SurfaceControl` 本质上是 **客户端对 SurfaceFlinger 中 Layer 的控制句柄**。

它本身不直接承载绘制逻辑，而是用于：

- 创建/销毁图层
- 修改图层状态
- 控制显示属性
- 将一组 Layer 变更封装为 Transaction 原子提交
- 与 Buffer 提交协同，实现显示内容与图层属性同步更新

可以将其理解为：

- **Surface**：生产像素内容的投递接口
- **BufferQueue / BLAST**：管理 buffer 流转与同步
- **SurfaceControl**：控制“这个内容如何显示”的 Layer 控制器
- **SurfaceFlinger**：接收事务并最终决定显示合成

---


<!-- source: 15-7.md -->

# 7. 完整跨层调用链模型


<!-- source: 17-72.md -->

# 7.2 属性更新链路

```
WMS / App / ViewRootImpl / Animation
  ↓
SurfaceControl.Transaction
  ↓
setPosition / setLayer / setAlpha / setCrop / reparent / show / hide ...
  ↓ JNI
android_view_SurfaceControl.cpp
  ↓
SurfaceComposerClient::Transaction
  ↓
组装 composer state / DisplayState / LayerState
  ↓ Binder
SurfaceFlinger::setTransactionState()
  ↓
事务入队 / 合并 / 同步处理
  ↓
更新 Layer 生命周期状态
  ↓
进入 composition pipeline
  ↓
下一帧显示生效
```

------


<!-- source: 23-83-surfacecontroltransaction.md -->

# 8.3 SurfaceControl.Transaction

事务容器，负责收集变更后统一提交。

重点分析：

- 一个事务中包含哪些 LayerState
- 是否存在频繁小事务 apply
- 是否多处代码重复覆盖同一属性
- 是否调用了 sync / callback 机制
- 是否与 buffer transaction 协同

------


<!-- source: 35-11-blastbufferqueue.md -->

# 11. 与 BLASTBufferQueue 的关系模型

在现代 Android 显示路径中，很多窗口显示基于 BLASTBufferQueue。

核心关系：

- SurfaceControl 管理 Layer 属性
- BLASTBufferQueue 管理 buffer 提交与同步
- 二者通过事务关联，保证“内容 + 属性”尽量同帧生效

重点分析：

- 新 buffer 到达时是否附带事务
- transaction callback 是否按预期触发
- buffer / size / crop / transform 是否同源更新
- 是否出现“事务已生效，buffer 未到”或“buffer 已到，事务未生效”的不同步

------


<!-- source: 36-12.md -->

# 12. 关键时序图


<!-- source: 37-121.md -->

# 12.1 窗口显示时序

```
WMS                SurfaceControl(Java)      JNI/Native              SurfaceFlinger
 |                        |                      |                           |
 | createSurface          |                      |                           |
 |----------------------->|                      |                           |
 |                        | nativeCreate         |                           |
 |                        |--------------------->| createSurface            |
 |                        |                      |-------------------------->|
 |                        |                      |<--------------------------|
 |<-----------------------|                      |                           |
 | build Transaction      |                      |                           |
 | show/setLayer/setPos   |                      |                           |
 |----------------------->|                      |                           |
 | apply                  |                      |                           |
 |----------------------->| nativeApply          |                           |
 |                        |--------------------->| setTransactionState       |
 |                        |                      |-------------------------->|
 |                        |                      |                           | update Layer state
 |                        |                      |                           | compose/present
```

------


<!-- source: 38-122-buffer-transaction.md -->

# 12.2 Buffer + Transaction 同步时序

```
App Render         Surface/BLAST            SurfaceControl Tx        SurfaceFlinger
    |                    |                         |                      |
    | draw               |                         |                      |
    | queueBuffer        |                         |                      |
    |------------------->|                         |                      |
    |                    | attach buffer           |                      |
    |                    | + related transaction   |                      |
    |                    |------------------------>| apply                |
    |                    |                         |--------------------->|
    |                    |                         |                      | latch buffer
    |                    |                         |                      | apply layer state
    |                    |                         |                      | present
```

------


<!-- source: 40-131.md -->

# 13.1 标准分析流程

### 第一步：明确问题类型

先判断属于哪类问题：

- 创建失败
- 属性变更不生效
- 显示异常
- 过渡动画异常
- 截图异常
- 事务同步异常
- 性能问题

### 第二步：确定操作对象

必须搞清楚操作的是哪个 Layer：

- 真正内容层
- container layer
- animation leash
- blast layer
- parent task layer
- display root layer

### 第三步：还原事务链

需要回答：

- 谁创建了 Transaction
- 设置了哪些属性
- 何时 apply
- 是否多处重复 apply
- 是否有 callback/sync
- 是否被其他模块覆盖

### 第四步：核对 SF 侧状态

需要核对：

- Layer 是否存在
- parent/child 是否正确
- 最终 layer state 是什么
- snapshot 是否更新
- 合成可见性如何

### 第五步：结合 buffer 与 vsync 时序

确认：

- buffer 何时到
- transaction 何时到
- latch/present 何时发生
- 是否同帧一致

### 第六步：形成根因闭环

最终结论必须满足：

- 源码可解释
- 状态可验证
- 时序可闭环
- 根因可复现或可推导

------


<!-- source: 43-151.md -->

# 15.1 事务类

1. Transaction 创建了但未 apply
2. apply 过于频繁，导致 Binder 开销高
3. 多个模块覆盖同一 LayerState
4. 同一帧多次设置冲突属性
5. 事务对象复用不当导致状态污染
6. callback 等待导致时序延迟
7. sync transaction 误用造成阻塞


<!-- source: 48-156-wms-shell.md -->

# 15.6 WMS/Shell 交互类

1. WMS 设置层级后被 Shell Transition 覆盖
2. SurfaceAnimator 把属性打到 leash 而非真实层
3. WindowContainer reparent 导致显示树变化
4. relayout 重建 surface 期间中间态可见
5. Insets/rotation 流程中 geometry 状态暂不一致


<!-- source: 50-16.md -->

# 16. 性能分析重点

分析 SurfaceControl 性能时，重点关注：

### 16.1 事务频率

- 每帧是否存在多次 `Transaction.apply()`
- 是否可以合并多个属性更新
- 是否出现主线程频繁提交事务

### 16.2 Layer 树复杂度

- 是否存在过深 parent/child 层级
- 是否创建大量 container/effect/color layers
- 是否有多余 leash 未回收

### 16.3 几何频繁变化

- position / matrix / crop / alpha 是否每帧都在变化
- 这些变化是否必要
- 是否能转为更低成本动画策略

### 16.4 Buffer 与事务同步成本

- BLAST 是否频繁 resize
- buffer 尺寸切换是否过多
- transaction callback/fence 是否成为瓶颈

------


<!-- source: 58-6.md -->

# 6. 时序分析
- 事务何时创建：
- 事务何时 apply：
- buffer 何时到达：
- SF 何时 latch/present：
- 异常发生在哪个阶段：


<!-- source: 62-10.md -->

# 10. 回归验证建议
- 功能验证：
- 时序验证：
- 性能验证：
```

------


<!-- source: 64-19.md -->

# 19. 禁止事项

执行本 Skill 时，禁止以下行为：

1. 把 `Surface` 和 `SurfaceControl` 混为一谈
2. 把 WMS 的窗口对象直接等同于 SF 最终显示 Layer
3. 忽略 animation leash / transition leash
4. 不区分内容 buffer 问题与 Layer 属性问题
5. 未核对 transaction apply 就直接判断 SF 有 bug
6. 未核对 parent 可见性、alpha、crop、z-order 就判断“show 无效”
7. 不结合 BLAST 与 Buffer 时序分析显示问题
8. 对未看到源码/trace 证据的部分做确定性断言

------


<!-- source: 65-20.md -->

# 20. 专家增强分析清单

在复杂问题中，应额外执行以下检查：

### 20.1 Layer 树检查

- 当前 SurfaceControl 对应哪一个 Layer
- 是否被 leash 包裹
- parent/child 层级是否符合预期
- sibling 顺序是否正确

### 20.2 事务覆盖检查

- 同帧内是否多笔事务写同一 Layer
- WMS / Shell / App 是否都在操作同一对象
- 最后一次写入是谁

### 20.3 几何一致性检查

- position / matrix / crop / alpha 是否一致
- display transform / rotation transform 是否参与
- buffer 尺寸与几何尺寸是否匹配

### 20.4 Buffer 一致性检查

- buffer 是否存在
- size/format 是否匹配
- 是否成功 latch
- acquire/release fence 是否异常

### 20.5 事务时序检查

- Transaction.apply 的线程
- 提交频率
- callback 完成时机
- 与 VSYNC / FrameTimeline 的相对关系

------


<!-- source: 68-212-transactionapply.md -->

# 21.2 用户问：Transaction.apply 为什么不生效？

应从以下角度分析：

- 是否真的 apply
- apply 的对象是否是最终显示层
- LayerState 是否包含对应 change
- 是否到达 SF
- 是否被后续事务覆盖
- 是否虽生效但因 alpha/crop/z/parent/buffer 导致视觉上看似无效
