# 架构与核心机制
<!-- source: 04-3.md -->

# 3. 分析边界与核心原则

### 3.1 分析边界

本 Skill 聚焦以下范围：

- SurfaceFlinger 核心
  - `SurfaceFlinger`
  - `Layer`
  - `LayerState`
  - `TransactionState`
  - `LayerLifecycleManager`
  - `Frontend` / `RequestedLayerState` / `LayerSnapshot`
  - `Scheduler`
  - `CompositionEngine`
  - `RenderEngine`
  - `TransactionCallbackInvoker`

- 图形交互核心
  - `SurfaceComposerClient`
  - `SurfaceControl`
  - `BufferQueue`
  - `BLASTBufferQueue`
  - `Surface`
  - `GraphicBuffer`
  - `Fence`

- 显示后端
  - `HWComposer`
  - `DisplayDevice`
  - `DisplayHardware`
  - `VSync / DispSync / EventThread`
  - DRM / panel 仅在需要解释到底层输出链路时延伸

- 上游关联模块
  - ViewRootImpl
  - WindowManagerService
  - Shell Transition
  - HWUI / RenderThread

### 3.2 核心原则

进行分析时必须遵循以下原则：

1. **先架构定位，再进入源码**
   - 先说明 SurfaceFlinger 的系统职责
   - 再建立事务、Layer、Buffer、Composition 模型
   - 最后解释具体函数实现

2. **先状态链，再时序链**
   - 状态链：Layer / Buffer / DisplayState 如何变化
   - 时序链：Transaction / latch / composition / present 何时发生

3. **先确认对象，再判断问题**
   - 必须先明确当前分析对象是：
     - 哪个 Layer
     - 哪个 BufferQueue
     - 哪个 Transaction
     - 哪个 Display
     - 哪个 Fence

4. **事务与 Buffer 必须分开分析，再合并闭环**
   - Transaction 决定 Layer 状态
   - Buffer 决定内容
   - 最终视觉问题必须同时检查二者

5. **所有结论必须可验证**
   - 必须基于源码、trace、dumpsys、日志、fence 时序或 FrameTimeline
   - 不能凭经验直接断言

---


<!-- source: 06-41-surfaceflinger.md -->

# 4.1 SurfaceFlinger 的本质

SurfaceFlinger 是 Android 显示系统的核心合成服务，它负责：

- 管理系统全局 Layer 树
- 接收客户端提交的 Transaction 和 Buffer
- 维护 Layer 状态与显示属性
- 根据显示设备状态进行合成决策
- 驱动 HWC / GPU composition
- 将最终帧输出到屏幕

可以将其理解为：

- **App / HWUI**：生产图形内容
- **Surface / BufferQueue / BLAST**：传输内容
- **SurfaceControl / Transaction**：控制图层显示属性
- **SurfaceFlinger**：整合内容与状态，形成最终显示结果
- **HWC / DRM / Panel**：执行最终硬件显示输出

---


<!-- source: 08-5.md -->

# 5. 核心源码入口索引

以下为建议优先阅读的关键源码入口。


<!-- source: 09-51-surfaceflinger.md -->

# 5.1 SurfaceFlinger 核心

```text
frameworks/native/services/surfaceflinger/SurfaceFlinger.cpp
frameworks/native/services/surfaceflinger/SurfaceFlinger.h
frameworks/native/services/surfaceflinger/Layer.cpp
frameworks/native/services/surfaceflinger/Layer.h
frameworks/native/services/surfaceflinger/LayerState.cpp
frameworks/native/services/surfaceflinger/LayerState.h
frameworks/native/services/surfaceflinger/TransactionCallbackInvoker.cpp
frameworks/native/services/surfaceflinger/TransactionCallbackInvoker.h
frameworks/native/services/surfaceflinger/LayerLifecycleManager.cpp
frameworks/native/services/surfaceflinger/LayerLifecycleManager.h
```


<!-- source: 10-52-frontend-snapshot-requested-state.md -->

# 5.2 Frontend / Snapshot / Requested State

```
frameworks/native/services/surfaceflinger/FrontEnd/RequestedLayerState.cpp
frameworks/native/services/surfaceflinger/FrontEnd/RequestedLayerState.h
frameworks/native/services/surfaceflinger/FrontEnd/LayerSnapshot.cpp
frameworks/native/services/surfaceflinger/FrontEnd/LayerSnapshot.h
frameworks/native/services/surfaceflinger/FrontEnd/LayerHierarchy.cpp
frameworks/native/services/surfaceflinger/FrontEnd/LayerHierarchy.h
```


<!-- source: 11-53-compositionengine-renderengine-display.md -->

# 5.3 CompositionEngine / RenderEngine / Display

```
frameworks/native/services/surfaceflinger/CompositionEngine/*
frameworks/native/libs/renderengine/*
frameworks/native/services/surfaceflinger/DisplayHardware/*
frameworks/native/services/surfaceflinger/Scheduler/*
```


<!-- source: 12-54.md -->

# 5.4 关联客户端路径

```
frameworks/native/libs/gui/SurfaceComposerClient.cpp
frameworks/native/libs/gui/SurfaceControl.cpp
frameworks/native/libs/gui/BLASTBufferQueue.cpp
frameworks/native/libs/gui/Surface.cpp
frameworks/native/libs/gui/BufferQueue*.cpp
frameworks/base/core/jni/android_view_SurfaceControl.cpp
frameworks/base/core/jni/android_view_Surface.cpp
frameworks/base/core/java/android/view/SurfaceControl.java
frameworks/base/core/java/android/view/Surface.java
```

------


<!-- source: 13-6.md -->

# 6. 架构设计思想


<!-- source: 14-61-surfaceflinger.md -->

# 6.1 为什么需要 SurfaceFlinger

Android 是多窗口、多进程、多图层系统。显示系统必须满足：

- 多进程共享屏幕输出
- 窗口和图层独立管理
- 内容与显示属性解耦
- 多图层统一仲裁
- 硬件能力差异可抽象适配
- 需要在高刷、低延迟、低功耗之间平衡

因此需要一个全局合成服务：

- 集中维护 Layer 树
- 接收来自不同进程的状态更新
- 决策显示合成方式
- 把复杂硬件显示链路封装起来

SurfaceFlinger 就是这个系统中的 **全局图层管理者与显示输出协调者**。

------


<!-- source: 15-62-transaction.md -->

# 6.2 为什么采用 Transaction 模型

Transaction 模型的设计目标：

1. **多属性原子更新**
   - 避免中间状态闪烁
   - 一次性更新多个 Layer
2. **跨进程可传输**
   - 客户端提交状态，服务端统一消费
3. **降低 Binder 往返**
   - 批量收集状态后再提交
4. **与 Buffer 更新解耦但可关联**
   - 便于实现内容与状态同步显示

------


<!-- source: 16-63-buffer-layer.md -->

# 6.3 为什么 Buffer 与 Layer 分离

Android 显示系统中：

- Buffer 表示“画了什么”
- Layer 表示“这个内容如何显示”

分离的好处：

- 内容生产逻辑不必关心系统合成树
- 系统可以在不修改内容的前提下做动画、旋转、裁剪、截图
- WMS / Shell / SF 可统一管理视觉结构
- 便于多 Layer、多窗口、嵌套显示与系统动画

------


<!-- source: 18-71-app.md -->

# 7.1 App 到屏幕的完整链路

```
App / HWUI / RenderThread
  ↓
Surface / ANativeWindow
  ↓
BufferQueue / BLASTBufferQueue
  ↓
queueBuffer
  ↓
SurfaceFlinger latch buffer
  ↓
Transaction 更新 LayerState
  ↓
CompositionEngine 生成合成计划
  ↓
HWC / GPU composition
  ↓
Display present
  ↓
Panel 显示
```

------


<!-- source: 20-73-buffer-latch.md -->

# 7.3 Buffer latch 链路

```
Producer(App/HWUI)
  ↓
dequeueBuffer / queueBuffer
  ↓
BufferQueue / BLASTBufferQueue
  ↓
Layer 持有新 buffer 可消费状态
  ↓
SurfaceFlinger 在合成前执行 latch
  ↓
检查 acquire fence
  ↓
更新 active buffer
  ↓
参与当前或下一帧 composition
```

------


<!-- source: 21-74-present.md -->

# 7.4 present 链路

```
SurfaceFlinger
  ↓
prepare frame
  ↓
choose composition strategy
  ↓
set layer state to HWC / GPU path
  ↓
commit composition
  ↓
present display
  ↓
obtain present fence
  ↓
frame complete / callbacks / release fences
```

------


<!-- source: 22-8.md -->

# 8. 核心对象模型


<!-- source: 25-83-layerstate-transactionstate.md -->

# 8.3 LayerState / TransactionState

这是客户端提交给 SurfaceFlinger 的状态载体。

重点分析：

- 哪些字段变更
- change mask 是否正确
- 哪些 layer state 被提交
- 是否被后续 transaction 覆盖
- 是否进入 frontend request state

------


<!-- source: 26-84-requestedlayerstate-layersnapshot.md -->

# 8.4 RequestedLayerState / LayerSnapshot

前端模型中：

- `RequestedLayerState`：客户端请求状态
- `LayerSnapshot`：某次合成使用的稳定快照

重点分析：

- 请求状态是否正确接收
- snapshot 是否按预期刷新
- 可见性、几何、buffer 状态是否一致

------


<!-- source: 27-85-compositionengine.md -->

# 8.5 CompositionEngine

合成决策核心，负责：

- 收集当前 display 与 layer 状态
- 判断设备合成与客户端合成策略
- 组织 RenderEngine / HWC 工作

重点分析：

- 哪些 layer 走 HWC
- 哪些 layer 走 GPU
- fallback 条件是什么
- 是否存在昂贵的 client composition

------


<!-- source: 31-91.md -->

# 9.1 核心处理阶段

可以将 SurfaceFlinger 的工作分为以下阶段：

1. **接收状态**
   - 接收 transaction
   - 接收 buffer 更新
   - 接收显示设备变化
2. **整理前端状态**
   - 更新 RequestedLayerState
   - 更新 Layer 树
   - 刷新 LayerSnapshot
3. **latch buffer**
   - 选择可用 buffer
   - 校验 fence
   - 切换当前活跃 buffer
4. **prepare composition**
   - 收集 display / layer / visible region
   - 判断 composition strategy
5. **执行 composition**
   - HWC prepare
   - client composition（必要时）
   - display present
6. **完成回调**
   - 更新 present fence
   - 触发 transaction callback
   - 推进 buffer release 生命周期

------


<!-- source: 38-112.md -->

# 11.2 第二步：锁定关键对象

必须明确：

- 哪个 Layer 出问题
- 哪个 SurfaceControl 对应它
- 是否有 animation leash / parent layer
- 对应哪个 BufferQueue / BLASTBufferQueue
- 当前 display 是哪个
- 相关 fence 是哪个

------


<!-- source: 41-115.md -->

# 11.5 第五步：还原合成链

需要回答：

- 当前帧中哪些 Layer 可见
- 哪些 Layer 走 HWC，哪些走 GPU
- 是否发生 fallback
- visible region / damage region 是否正确
- display present 是否成功

------


<!-- source: 50-132-transaction.md -->

# 13.2 Transaction 不生效

排查顺序：

1. transaction 是否真的提交
2. LayerState change mask 是否包含对应属性
3. SurfaceFlinger 是否收到 transaction
4. RequestedLayerState 是否更新
5. Snapshot 是否刷新
6. 是否被后续 transaction 覆盖
7. 是否作用在错误 Layer（例如 leash 而非内容层）

典型根因：

- apply 缺失
- 对象句柄失效
- 变更被覆盖
- 更新延迟到下一次 frame
- 分析对象选错

------


<!-- source: 53-135-frame-missed.md -->

# 13.5 掉帧 / 卡顿 / Frame Missed

重点看：

- SurfaceFlinger 主线程是否繁忙
- latchBuffer / composition / present 哪一步耗时高
- HWC prepare/set 是否耗时
- GPU composition 是否变多
- layer 数量和 region 复杂度是否过高
- transaction 是否积压
- scheduler 是否 missed frame deadline

------


<!-- source: 56-141-surfacecontrol.md -->

# 14.1 与 SurfaceControl 的关系

- SurfaceControl 是客户端操控 Layer 的句柄
- SurfaceFlinger 是 Layer 的真实持有者和执行者
- 客户端提交的是“请求状态”
- SurfaceFlinger 决定何时把请求状态变成系统可见状态

------


<!-- source: 57-142-bufferqueue-blast.md -->

# 14.2 与 BufferQueue / BLAST 的关系

- BufferQueue 负责内容流转
- BLAST 强化了 buffer 与 transaction 的同步能力
- SurfaceFlinger 在合成前 latch 新 buffer
- 表面上的显示异常通常是 buffer 状态和 layer 状态共同作用的结果

------


<!-- source: 62-152-layer.md -->

# 15.2 Layer 数量与结构

- layer 总数是否过多
- parent/child 层级是否过深
- 是否存在过多中间 container/effect layers
- 不可见层是否仍频繁更新


<!-- source: 68-162-layer.md -->

# 16.2 Layer 生命周期类

1. layer 已销毁但仍被引用
2. parent 销毁导致子 layer 不可见
3. reparent 到错误节点
4. transition leash 未及时回收
5. orphan layer 遗留
6. old layer 与 new layer 切换中间态暴露


<!-- source: 69-163-buffer.md -->

# 16.3 Buffer 类

1. queueBuffer 成功但 buffer 未及时 latch
2. acquire fence 晚 ready
3. stale buffer 被继续显示
4. resize 后 buffer 与 layer geometry 不一致
5. 没有 active buffer 但 layer 已 show
6. BLAST 同步点导致 buffer 延迟可见
7. release fence 时序异常


<!-- source: 70-164-geometry-visible.md -->

# 16.4 Geometry / Visible 类

1. crop 为零或错误
2. transform 叠加错误
3. alpha=0 或极小值
4. z-order 被错误 layer 遮挡
5. relative layer 基准错误
6. display transform 未正确参与计算
7. rotation 后 geometry 未收敛


<!-- source: 71-165-composition.md -->

# 16.5 Composition 类

1. HWC 不支持某属性导致 fallback
2. client composition 成本过高
3. composition strategy 频繁抖动
4. damage region 过大导致额外 GPU 工作
5. visible region 计算异常
6. HWC layer slot/state 不一致
7. prepare / set / present 某阶段失败


<!-- source: 73-167.md -->

# 16.7 性能类

1. sf 主线程高负载
2. 高频小事务导致 Binder 压力
3. 过多 layer 导致 region 计算重
4. blur / effect layer 开销大
5. 频繁 refresh rate 切换
6. GPU composition 退化严重
7. HWC 接管率不稳定
8. 事务和 buffer 同步链路抖动导致 jank

------


<!-- source: 77-2.md -->

# 2. 结论摘要
- 根因：
- 直接触发点：
- 责任模块：
- 涉及 Layer / Buffer / HWC / VSync：


<!-- source: 80-5.md -->

# 5. 关键源码分析
- 关键类：
- 关键函数：
- 关键状态字段：
- 关键设计意图：


<!-- source: 90-211-surfaceflinger.md -->

# 21.1 用户问：SurfaceFlinger 是什么？

应回答：

- 它是 Android 的全局显示合成服务
- 管理 Layer 树、消费 Transaction 和 Buffer
- 负责决定如何合成并输出到显示设备
- 位于 App/SurfaceControl 与 HWC/Display 之间


<!-- source: 91-212-queuebuffer.md -->

# 21.2 用户问：为什么 queueBuffer 了却没显示？

应从以下维度分析：

- buffer 是否到达 SF
- acquire fence 是否 ready
- layer 是否 visible
- 是否成功 latch
- 是否参与当前 frame composition
- present 是否成功
- 是否被 crop/alpha/z-order/parent 影响


<!-- source: 92-213-transactionapply.md -->

# 21.3 用户问：为什么 Transaction.apply 后没效果？

应从以下维度分析：

- transaction 是否到达 SF
- layer state 是否真正更新
- 作用对象是否正确
- snapshot 是否刷新
- 是否被下一笔 transaction 覆盖
- 是否是“已生效但视觉上被其他状态掩盖”


<!-- source: 96-24.md -->

# 24. 一句话总结

**SurfaceFlinger 的核心不是“简单绘制图层”，而是“在 VSync 驱动下，把来自不同进程的 Layer 状态与 Buffer 内容收敛成一个可被显示设备正确呈现的最终帧”；分析任何 SurfaceFlinger 问题，本质上都是在还原“哪个 Layer 在哪个时刻以什么状态绑定了什么内容，并为什么在当前 frame 中以当前方式被合成和显示”。**
