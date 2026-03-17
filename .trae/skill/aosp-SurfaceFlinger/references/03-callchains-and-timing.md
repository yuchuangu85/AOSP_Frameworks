# 调用链与时序
<!-- source: 02-1.md -->

# 1. 目标定义

本 Skill 用于对 **Android AOSP SurfaceFlinger 子系统**进行系统级源码分析、运行机制建模、异常定位与性能优化，重点覆盖以下能力：

1. **架构理解**
   - 理解 SurfaceFlinger 在 Android 图形栈中的职责定位
   - 分析 SurfaceFlinger 与 App / HWUI / SurfaceControl / BufferQueue / HWC / DRM / Display 的边界关系
   - 建立事务、Layer、Buffer、VSync、Composition 的统一模型

2. **跨层调用链分析**
   - 打通 App → Surface / SurfaceControl → BufferQueue / BLAST → SurfaceFlinger → HWC → Display 的全链路
   - 还原窗口显示、动画、旋转、截图、录屏、Buffer latch、present 的完整时序
   - 分析 TransactionState 如何被 SurfaceFlinger 消费并作用到 Layer 树

3. **运行机制分析**
   - 分析 Layer 生命周期、Transaction 合并、Buffer latch、前端状态更新、合成决策、VSync 驱动机制
   - 理解 CompositionEngine、RenderEngine、HWC 协同关系
   - 理解 FrameTimeline、Fence、Present Fence、Acquire/Release Fence 的作用

4. **异常问题定位**
   - 定位黑屏、闪屏、残影、花屏、裁剪错误、图层错乱、Buffer 不一致、旋转异常、截图异常
   - 定位事务不生效、Layer 状态异常、latch 失败、present 延迟、HWC fallback、显示不同步
   - 定位 SurfaceFlinger 与 WMS / App / HWC 状态不一致问题

5. **性能分析与优化**
   - 分析 SurfaceFlinger 路径上的掉帧、卡顿、合成瓶颈、过度重组、事务积压
   - 分析 CPU composition / GPU composition / HWC composition 切换成本
   - 给出 Layer、事务、Buffer、VSync、合成策略层面的优化建议

---


<!-- source: 07-42.md -->

# 4.2 核心职责拆分

SurfaceFlinger 的核心职责包括：

1. **Layer 管理**
   - 创建、销毁、组织 Layer 树
   - 管理 parent/child、z-order、crop、alpha、transform、visible region

2. **事务处理**
   - 接收 `TransactionState`
   - 合并多来源事务
   - 更新 Layer 请求状态
   - 驱动状态快照刷新

3. **Buffer 消费**
   - 从 Layer 对应队列中 latch 新 buffer
   - 校验 acquire fence
   - 处理 buffer 尺寸、格式、dataspace、damage region

4. **合成决策**
   - 判断图层是走 HWC 还是 GPU
   - 处理 client composition / device composition 混合场景
   - 决定输出到 display 的最终 composition plan

5. **显示调度**
   - 基于 VSync 驱动更新
   - 进行 frame deadline 相关调度
   - 处理 scheduler / event thread / frame timeline

6. **帧输出**
   - 提交给 HWC
   - 等待 present fence
   - 通知事务回调和 buffer 生命周期推进

---


<!-- source: 19-72-transaction.md -->

# 7.2 Transaction 生效链路

```
App / WMS / Shell / ViewRootImpl
  ↓
SurfaceControl.Transaction
  ↓ JNI
android_view_SurfaceControl.cpp
  ↓
SurfaceComposerClient::Transaction
  ↓ Binder
SurfaceFlinger::setTransactionState()
  ↓
事务入队 / 合并
  ↓
LayerLifecycleManager / RequestedLayerState 更新
  ↓
LayerSnapshot 刷新
  ↓
进入 composition pipeline
  ↓
下一次合成周期生效
```

------


<!-- source: 23-81-surfaceflinger.md -->

# 8.1 SurfaceFlinger

系统核心服务，负责：

- 接收 transaction
- 驱动 layer 状态更新
- 管理显示设备
- 执行合成与输出

重点分析：

- 主循环
- 事务处理入口
- VSync 驱动更新点
- composition 触发路径
- callback 与 fence 生命周期

------


<!-- source: 28-86-scheduler-eventthread-vsync.md -->

# 8.6 Scheduler / EventThread / VSync

负责驱动帧更新节奏。

重点分析：

- 帧何时被唤醒
- 是否 missed frame deadline
- app / sf VSync 关系
- refresh rate 与 present 时序关系

------


<!-- source: 29-87-fence.md -->

# 8.7 Fence

Fence 是 SurfaceFlinger 时序分析的重要对象。

关键类型包括：

- acquire fence
- release fence
- present fence

重点分析：

- buffer 是否可安全读取
- frame 是否已完成显示
- 哪个阶段发生阻塞或延迟

------


<!-- source: 30-9-surfaceflinger.md -->

# 9. SurfaceFlinger 主流程建模


<!-- source: 32-92.md -->

# 9.2 关键主循环问题

分析时重点关注：

- 哪个阶段最耗时
- transaction 是否积压
- latch 是否失败或延迟
- composition strategy 是否频繁抖动
- HWC 是否频繁 fallback
- present 是否迟到
- scheduler 是否 missed deadline

------


<!-- source: 33-10.md -->

# 10. 关键时序图


<!-- source: 34-101-buffer.md -->

# 10.1 事务 + Buffer + 合成时序

```
Client/App        SurfaceControl Tx      BufferQueue/BLAST      SurfaceFlinger        HWC/Display
    |                    |                      |                     |                    |
    | set state          |                      |                     |                    |
    |------------------->|                      |                     |                    |
    | apply              |                      |                     |                    |
    |------------------->|                      |                     |                    |
    |                    |--------------------->|                     |                    |
    | queueBuffer        |                      |                     |                    |
    |------------------------------------------>|                     |                    |
    |                    |                      | new buffer ready    |                    |
    |                    |                      |-------------------->|                    |
    |                    |                      |                     | latch buffer       |
    |                    |                      |                     | update snapshots   |
    |                    |                      |                     | prepare compose    |
    |                    |                      |                     |------------------->|
    |                    |                      |                     | present            |
    |                    |                      |                     |<-------------------|
```

------


<!-- source: 35-102-vsync.md -->

# 10.2 VSync 驱动时序

```
VSync Source
   ↓
Scheduler / EventThread
   ↓
唤醒 SurfaceFlinger
   ↓
处理 pending transaction / pending buffer
   ↓
latch + build frame
   ↓
composition + present
   ↓
present fence 返回
   ↓
frame 完成
```

------


<!-- source: 37-111.md -->

# 11.1 第一步：明确问题类型

先判断问题属于哪一类：

- Layer 不存在 / 生命周期异常
- Transaction 不生效
- Buffer 未到或 latch 失败
- 几何状态错误
- HWC 合成异常
- present 延迟
- FrameTimeline / VSync 节奏异常
- 截图/录屏异常

------


<!-- source: 39-113.md -->

# 11.3 第三步：还原事务链

需要回答：

- 谁创建了 transaction
- transaction 中设置了哪些 layer state
- 何时 apply
- 是否被后续 transaction 覆盖
- 是否被 SurfaceFlinger 正确接收与消费

------


<!-- source: 40-114-buffer.md -->

# 11.4 第四步：还原 Buffer 链

需要回答：

- buffer 何时 queue
- SurfaceFlinger 是否看到了新 buffer
- acquire fence 是否 ready
- 是否 latch 成功
- latch 到的是不是预期帧
- release/present 时序是否合理

------


<!-- source: 44-121.md -->

# 12.1 源码证据

- SurfaceFlinger 主流程
- transaction 消费逻辑
- LayerState / RequestedLayerState / Snapshot 更新逻辑
- latch / composition / present 路径
- scheduler / vsync / callback 逻辑


<!-- source: 47-124.md -->

# 12.4 日志证据

- SurfaceFlinger log
- BufferQueue/BLAST log
- HWC log
- Transaction callback log
- WMS / Shell / ViewRootImpl 相关日志

------


<!-- source: 51-133-buffer-latch.md -->

# 13.3 Buffer latch 失败或延迟

排查顺序：

1. queueBuffer 是否成功
2. BufferQueue 是否发出可消费 buffer
3. acquire fence 是否 ready
4. latch 时机是否错过当前帧
5. 尺寸/格式是否变化导致额外处理
6. 是否发生 BLAST 同步等待
7. 是否因为事务时序导致视觉不同步

典型根因：

- fence 晚 ready
- buffer 到达晚于 latch 阶段
- resize 后 buffer 尺寸不一致
- BLAST 同步点未对齐
- producer 节奏不稳定

------


<!-- source: 52-134.md -->

# 13.4 闪屏 / 残影 / 花屏

重点看：

- old buffer 与 new buffer 切换时序
- Transaction 与 buffer 是否同帧一致
- Layer hierarchy 是否发生快速变动
- crop / transform / alpha 是否在帧间切换异常
- HWC 是否因某些 layer 属性不支持而 fallback
- 旋转或 transition 中 intermediate layer 是否暴露

典型根因：

- 中间态 Layer 短暂可见
- show/hide 与 buffer 提交不同步
- stale buffer 被展示
- fallback 导致某帧构图异常
- damage region 或 geometry 状态错误

------


<!-- source: 58-143-wms-shell.md -->

# 14.3 与 WMS / Shell 的关系

- WMS / Shell 决定窗口结构、显示层级与系统过渡
- SurfaceFlinger 只负责执行显示树和合成
- 但很多视觉异常本质上是 WMS/Shell 状态通过 transaction 投射到 SF 的结果

------


<!-- source: 61-151.md -->

# 15.1 事务开销

- 是否每帧大量 apply transaction
- 是否有重复无效属性更新
- 是否 transaction 合并不足
- 是否 callback 过多


<!-- source: 67-161.md -->

# 16.1 事务类

1. transaction 收到但未进入当前 frame 生效
2. 多笔 transaction 覆盖同一 layer state
3. show/hide 与 alpha/crop 组合导致误判
4. layer state change mask 不完整
5. transaction callback 延迟导致时序错觉
6. 作用对象是 leash 而非内容层
7. 事务应用顺序与预期不一致


<!-- source: 72-166.md -->

# 16.6 时序类

1. VSync 节奏异常
2. frame deadline missed
3. transaction 晚于 latch 窗口
4. buffer 晚于 composition 窗口
5. present fence 晚返回
6. callback 看似成功但实际下一帧才可见


<!-- source: 79-4.md -->

# 4. 跨层调用链
- App / HWUI 路径：
- Surface / Buffer 路径：
- Transaction 路径：
- SurfaceFlinger 处理路径：
- Composition / Present 路径：


<!-- source: 81-6.md -->

# 6. 时序分析
- transaction 到达时机：
- buffer 到达时机：
- latch 时机：
- composition 时机：
- present 时机：
- 异常发生阶段：


<!-- source: 85-10.md -->

# 10. 回归验证建议
- 功能验证：
- 时序验证：
- 性能验证：
```

------


<!-- source: 87-19.md -->

# 19. 禁止事项

执行本 Skill 时，禁止以下行为：

1. 把 SurfaceFlinger 简化为“只是把图层画出来”
2. 把 Buffer 问题和 LayerState 问题混为一类
3. 忽略 transaction、latch、composition、present 之间的阶段差异
4. 忽略 leash、parent layer、container layer 对显示结果的影响
5. 未核实 acquire/present fence 就直接判断显示时序
6. 未核对 HWC fallback 就直接判断 GPU 或 SF 有问题
7. 未核对 snapshot/visible region/crop/transform 就断言 Layer 不可见
8. 在没有源码或运行证据的情况下做确定性结论

------


<!-- source: 88-20.md -->

# 20. 专家增强分析清单

在复杂问题中，应额外执行以下检查。

### 20.1 Layer 树检查

- 当前显示对象是不是最终内容层
- 是否存在 animation leash / transition leash
- parent/child / sibling 顺序是否正确
- layer snapshot 是否与预期一致

### 20.2 Transaction 检查

- 同帧内是否有多笔 transaction 覆盖
- 最后生效的一笔是谁
- state mask 是否完整
- callback 是否反映真实可见时机

### 20.3 Buffer 检查

- 当前 active buffer 是哪一帧
- queueBuffer 与 latch 间隔是多少
- acquire fence 是否 ready
- release/present 时序是否合理

### 20.4 Composition 检查

- 哪些 layer 走 HWC
- 哪些走 GPU
- fallback 原因是什么
- 是否存在昂贵 effect/blur/transform 组合

### 20.5 VSync / Deadline 检查

- sf 是否按期被唤醒
- 是否 missed frame deadline
- present 是否晚于预期
- 刷新率切换是否影响节奏

------


<!-- source: 93-214-surfaceflinger.md -->

# 21.4 用户问：为什么 SurfaceFlinger 掉帧？

应从以下维度分析：

- transaction 频率是否过高
- layer 数量与结构是否复杂
- latch 是否等待 fence
- composition 是否退化为 GPU
- HWC present 是否慢
- scheduler/frame timeline 是否显示 missed deadline

------


<!-- source: 95-23.md -->

# 23. 最终交付标准

一个合格的 SurfaceFlinger 分析结果，必须满足：

- 能说清 SurfaceFlinger 在 Android 图形栈中的职责
- 能明确指出问题发生在哪个处理阶段
- 能还原完整跨层调用链
- 能区分 Layer 状态问题与 Buffer 内容问题
- 能识别 transaction / latch / composition / present 的时序差异
- 能结合 HWC / GPU composition 给出解释
- 能给出至少一个源码级可验证根因
- 能提出工程上可执行的修复与验证方案

------
