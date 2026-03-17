# 架构与核心机制
<!-- source: 02-1.md -->

# 1. 目标定义

本 Skill 用于对 **AOSP SurfaceControl 子系统**进行系统级源码分析与问题定位，重点覆盖以下能力：

1. **源码结构分析**
   - 理解 SurfaceControl 在 Java Framework / JNI / Native / SurfaceFlinger 中的职责边界
   - 分析 SurfaceControl、Transaction、LayerState、SurfaceComposerClient、ComposerService 的设计关系
   - 建立 SurfaceControl 与 WMS、BLASTBufferQueue、Surface、BufferQueue、SurfaceFlinger 的连接模型

2. **跨层调用链分析**
   - 还原从 App / ViewRootImpl / WindowManager / SurfaceControl 到 SurfaceFlinger 的完整路径
   - 分析窗口创建、重布局、旋转、动画、可见性切换、裁剪、变换、Z 序、Alpha、截图等事务链路
   - 分析 `Transaction.apply()` 如何转化为 SurfaceFlinger 侧的 Layer 状态变更

3. **运行机制分析**
   - 理解事务收集、合并、提交、同步、栅栏、vsync 对显示行为的影响
   - 理解 BLAST 模型下 SurfaceControl 与 Buffer 提交同步关系
   - 理解 reparent、setLayer、show/hide、setPosition、setMatrix、setCrop、setBuffer 等操作的生效机制

4. **故障定位**
   - 定位闪屏、黑屏、窗口内容不显示、层级错乱、裁剪异常、旋转异常、残影、动画错位
   - 定位 Transaction 不生效、延迟生效、覆盖生效、被回滚、被后续事务覆盖的问题
   - 定位 WMS/SurfaceFlinger 状态不一致、Layer 树错误、Buffer 与 Layer 不匹配等问题

5. **性能优化**
   - 分析频繁事务提交、过度 Layer 操作、无效变换、频繁 reparent/resize 对性能的影响
   - 分析 SurfaceControl 路径导致的掉帧、合成开销上升、事务积压、Buffer 与 Transaction 不同步
   - 给出事务合并、层级简化、减少无效变更、优化动画事务的建议

---


<!-- source: 04-3.md -->

# 3. 分析边界与核心原则

### 3.1 分析边界

本 Skill 聚焦以下范围：

- Java Framework
  - `android.view.SurfaceControl`
  - `android.view.Surface`
  - `android.window.*`（与容器事务相关部分）
  - `ViewRootImpl` 中与 SurfaceControl 相关流程
  - `SurfaceSession`
  - `ScreenCapture` / `ScreenshotHardwareBuffer` 等相关接口

- JNI / Native
  - `android_view_SurfaceControl.cpp`
  - `android_view_Surface.cpp`
  - `SurfaceComposerClient`
  - `SurfaceControl` native wrapper
  - `LayerState`
  - `ComposerService`

- SurfaceFlinger
  - `SurfaceFlinger`
  - `Layer`
  - `TransactionState`
  - `RequestedLayerState`
  - `LayerLifecycleManager`
  - `FrontEnd / Commit / Composition` 相关路径
  - `DisplayDevice / CompositionEngine` 相关生效逻辑

- 关联模块
  - WindowManagerService / WindowState / WindowContainer
  - BLASTBufferQueue
  - BufferQueue / GraphicBuffer / Fence
  - RenderEngine / HWC（仅在需要解释最终显示时延伸）

### 3.2 核心原则

进行分析时必须遵循：

1. **先建模，再看代码**
   - 先说明 SurfaceControl 在系统中的职责
   - 再分析调用关系与具体实现
   - 最后回到具体异常与根因

2. **先事务，再显示**
   - SurfaceControl 的核心是“事务化 Layer 状态更新”
   - 任何视觉异常，优先检查：
     - Layer 是否存在
     - Transaction 是否提交
     - 状态是否进入 SF
     - 是否参与合成
     - Buffer 是否到位

3. **先状态链，再时序链**
   - 状态链：对象创建 → 属性更新 → LayerState 组装 → Binder 发送 → SF 接收 → Layer 更新
   - 时序链：何时提交、何时 latch、何时 present、何时用户可见

4. **必须给出可验证证据**
   - 关键结论必须基于源码、trace、dumpsys 或日志
   - 不能凭经验直接下结论

---


<!-- source: 07-42.md -->

# 4.2 关键角色分工

### Java 层
- `SurfaceControl`
  - Java API 封装
  - Builder/Transaction 对外接口
- `SurfaceSession`
  - Surface 创建会话
- `ViewRootImpl`
  - 窗口 relayout / surface 变更的重要入口
- `WindowlessWindowManager` / `SurfaceControlViewHost`
  - 嵌套场景下使用 SurfaceControl 管理子树

### JNI 层
- `android_view_SurfaceControl.cpp`
  - Java → Native 的桥接
  - 将 Java Transaction 转为 native `SurfaceComposerClient::Transaction`

### Native 客户端
- `SurfaceComposerClient`
  - 与 SurfaceFlinger 的 Binder 客户端
- `SurfaceControl`
  - 持有 Layer handle
- `Transaction`
  - 收集 LayerState 变更并提交

### SurfaceFlinger 侧
- 接收 TransactionState
- 解析 LayerState
- 更新 Layer 树、显示属性、可见性、缓冲区绑定、裁剪与变换
- 驱动后续合成与呈现

---


<!-- source: 08-5.md -->

# 5. 核心源码入口索引

以下为建议优先阅读的关键源码入口。


<!-- source: 09-51-java-framework.md -->

# 5.1 Java Framework 入口

```text
frameworks/base/core/java/android/view/SurfaceControl.java
frameworks/base/core/java/android/view/Surface.java
frameworks/base/core/java/android/view/SurfaceSession.java
frameworks/base/core/java/android/window/ScreenCapture.java
frameworks/base/core/java/android/view/ViewRootImpl.java
frameworks/base/services/core/java/com/android/server/wm/WindowState.java
frameworks/base/services/core/java/com/android/server/wm/WindowContainer.java
frameworks/base/services/core/java/com/android/server/wm/SurfaceAnimator.java
frameworks/base/services/core/java/com/android/server/wm/WindowSurfaceController.java
```


<!-- source: 10-52-jni-native.md -->

# 5.2 JNI / Native 入口

```
frameworks/base/core/jni/android_view_SurfaceControl.cpp
frameworks/base/core/jni/android_view_Surface.cpp

frameworks/native/libs/gui/SurfaceComposerClient.cpp
frameworks/native/libs/gui/SurfaceControl.cpp
frameworks/native/libs/gui/BLASTBufferQueue.cpp
frameworks/native/libs/gui/Surface.cpp
frameworks/native/libs/gui/BufferQueue*.cpp
frameworks/native/libs/gui/include/gui/SurfaceComposerClient.h
```


<!-- source: 11-53-surfaceflinger.md -->

# 5.3 SurfaceFlinger 入口

```
frameworks/native/services/surfaceflinger/SurfaceFlinger.cpp
frameworks/native/services/surfaceflinger/Layer.cpp
frameworks/native/services/surfaceflinger/LayerState.cpp
frameworks/native/services/surfaceflinger/LayerLifecycleManager.cpp
frameworks/native/services/surfaceflinger/FrontEnd/RequestedLayerState.cpp
frameworks/native/services/surfaceflinger/FrontEnd/LayerSnapshot.cpp
frameworks/native/services/surfaceflinger/CompositionEngine/*
frameworks/native/services/surfaceflinger/TransactionCallbackInvoker.cpp
```

------


<!-- source: 12-6.md -->

# 6. 架构设计思想


<!-- source: 13-61-surfacecontrol.md -->

# 6.1 为什么需要 SurfaceControl

Android 显示系统需要同时满足：

- 多窗口/多图层组合
- 跨进程显示控制
- 原子地更新多个 Layer 属性
- 绘制内容与显示属性同步
- 降低客户端与合成服务之间的耦合

因此引入 SurfaceControl：

1. **把内容生产与显示控制解耦**
   - Buffer 负责“画什么”
   - SurfaceControl 负责“怎么显示”
2. **用 Transaction 保证原子更新**
   - 多个 Layer 的变更一次性提交
   - 避免中间状态闪烁
3. **跨进程统一由 SurfaceFlinger 管理 Layer 树**
   - App 侧只持有控制句柄
   - SF 统一仲裁最终显示结果
4. **便于动画、旋转、截图、录屏、过渡等系统级操作**
   - 系统服务可以直接操控 Layer
   - 不必侵入应用绘制逻辑

------


<!-- source: 14-62-transaction.md -->

# 6.2 Transaction 化设计思想

`SurfaceControl.Transaction` 的核心价值：

- 批量收集属性更新
- 延迟提交
- 原子 apply
- 减少 Binder 往返
- 对一帧内多个属性更新形成一致状态

适合操作的状态包括：

- 可见性 show/hide
- layer / relative layer
- alpha
- position
- matrix
- crop / window crop
- buffer / damage region
- reparent
- corner radius
- background blur
- trusted overlay
- frame rate / metadata（视版本而定）

------


<!-- source: 16-71.md -->

# 7.1 窗口创建链路

```
App / SystemServer
  ↓
WMS / WindowState / WindowContainer
  ↓
创建 SurfaceControl.Builder
  ↓
Java SurfaceControl
  ↓ JNI
android_view_SurfaceControl.cpp
  ↓
SurfaceComposerClient::createSurfaceChecked()
  ↓ Binder
SurfaceFlinger::createLayer()
  ↓
Layer 创建并加入 Layer 树
  ↓
返回 native handle / SurfaceControl
  ↓
上层持有 Java SurfaceControl 句柄
```

------


<!-- source: 18-73-buffer-transaction-blast.md -->

# 7.3 Buffer + Transaction 同步链路（BLAST 模型）

```
App 渲染
  ↓
dequeueBuffer / queueBuffer
  ↓
BLASTBufferQueue 接收新 buffer
  ↓
关联 SurfaceControl.Transaction
  ↓
Buffer 与 Layer 属性变更一起提交
  ↓
SurfaceFlinger latch buffer
  ↓
Layer 内容 + 属性一致生效
  ↓
present 到屏幕
```

------


<!-- source: 19-74.md -->

# 7.4 截图/录屏链路

```
SystemUI / Shell / framework API
  ↓
ScreenCapture / SurfaceControl.captureLayers / screenshot APIs
  ↓ JNI / native screenshot path
  ↓
SurfaceFlinger 捕获 Layer 树或 Display 输出
  ↓
生成 GraphicBuffer / HardwareBuffer
  ↓
返回给调用方
```

------


<!-- source: 21-81-surfacecontrol.md -->

# 8.1 SurfaceControl

代表一个可被客户端控制的 Layer 句柄，关注：

- Layer identity
- parent/child 关系
- 生命周期
- native handle 有效性
- 是否由 WMS / App / Shell 持有

重点分析：

- 何时创建
- 何时销毁
- 何时 reparent
- 是否发生 handle 泄漏或过早释放

------


<!-- source: 22-82-surfacecontrolbuilder.md -->

# 8.2 SurfaceControl.Builder

用于创建 Layer 的构造器，常涉及：

- name
- parent
- format
- flags
- buffer size
- effect layer / color layer / container layer / blast layer 等类型

重点分析：

- parent 选取是否正确
- 创建出的 Layer 类型是否符合场景
- 是否创建了多余层级

------


<!-- source: 24-84-layerstate.md -->

# 8.4 LayerState

是 Transaction 传输给 SF 的核心状态载体。

重点分析：

- 哪些字段被设置了
- 哪些变更 mask 生效
- 是否因未设置 change flags 导致不生效
- 是否被后续事务覆盖
- SF 侧是否正确消费

------


<!-- source: 25-85-surfacecomposerclient.md -->

# 8.5 SurfaceComposerClient

客户端访问 SF 的核心 native 封装。

重点分析：

- create surface
- transaction apply
- callback 注册
- client-side 合并逻辑
- 与 Binder 服务通信路径

------


<!-- source: 26-86-layer-requestedlayerstate.md -->

# 8.6 Layer / RequestedLayerState

SF 侧 Layer 的真实状态实体。

重点分析：

- 客户端请求状态与当前状态的差异
- snapshot 是否更新
- 可见性判定
- crop / transform / alpha / z-order 是否进入最终合成状态

------


<!-- source: 28-91-show-hide.md -->

# 9.1 show/hide 不生效

排查顺序：

1. Java 层是否确实执行 `show()` / `hide()`
2. Transaction 是否 `apply()`
3. JNI 是否下发到 native transaction
4. Binder 是否到达 SF
5. LayerState change mask 是否包含 visibility 变化
6. SF 是否更新 Layer 可见状态
7. Layer 是否有 buffer
8. 父 Layer 是否隐藏
9. alpha / crop / matrix 是否导致“看起来像没显示”
10. 是否被后续事务覆盖

常见根因：

- 事务未 apply
- handle 已失效
- parent 不可见
- z-order 被压住
- alpha 为 0
- crop 把内容裁没了
- buffer 未提交

------


<!-- source: 33-96.md -->

# 9.6 截图/录屏异常

重点看：

- capture 的对象是 display 还是 layers
- secure layer / protected content 是否被过滤
- exclude layers / crop 参数是否正确
- 截图时机是否早于 buffer latch
- 是否抓到 leash 而非真实内容层
- 旋转矩阵是否正确应用

------


<!-- source: 34-10-wms.md -->

# 10. 与 WMS 的关系模型

SurfaceControl 在 Android 中大量由 WMS 持有和操作。

关键理解：

1. **WindowState 不直接等于最终显示 Layer**
   - 中间可能有 animation leash
   - 可能有 parent container layer
   - 可能有 blast layer / buffer layer 分层
2. **WMS 是 Layer 组织者**
   - 决定窗口层级、父子关系、可见性、过渡动画
   - 通过 SurfaceControl.Transaction 把窗口管理意图下发到 SF
3. **SurfaceAnimator / Transition 是高频操作者**
   - 动画期间大量属性作用在 leash 上而不是内容层上
   - 误判对象是 SurfaceControl 分析的常见错误来源
4. **relayout 是重要时机**
   - 尺寸变化
   - surface 重建
   - 可见性变化
   - blast 事务更新
   - 输入/绘制同步调整

------


<!-- source: 44-152-layer.md -->

# 15.2 Layer 生命周期类

1. Layer 已销毁但 Java handle 仍被使用
2. parent 销毁导致子层不可见
3. reparent 到错误节点
4. animation leash 销毁后引用未切换
5. orphan layer 遗留
6. Surface 重建后旧 SurfaceControl 仍被操作


<!-- source: 45-153.md -->

# 15.3 可见性类

1. show 生效但无 buffer
2. hide 被后续 show 覆盖
3. alpha=0 导致误判为 hide 失败
4. parent 不可见导致子层不可见
5. crop 为 0 区域
6. 被其他高层级 layer 遮挡
7. 变换后移出屏幕


<!-- source: 46-154.md -->

# 15.4 几何变换类

1. matrix 设置错误导致缩放/翻转异常
2. crop 与 transform 组合错误
3. relative layer 基准对象错误
4. rotation 后 position/crop 未同步更新
5. leash 和内容层双重变换


<!-- source: 47-155-blast-buffer.md -->

# 15.5 BLAST/Buffer 同步类

1. buffer 到达晚于 show
2. transaction 到达晚于 buffer
3. resize 与新 buffer 尺寸不同步
4. latch 前 layer state 未更新
5. buffer release/latch 节奏异常导致闪屏


<!-- source: 49-157.md -->

# 15.7 性能类

1. 高频小事务造成 jank
2. 高频 reparent 造成层树重排
3. 频繁 setBuffer / setGeometry 导致额外合成压力
4. 不必要的中间 container layer 过多
5. 大量不可见层仍频繁更新事务

------


<!-- source: 55-3.md -->

# 3. 涉及对象
- SurfaceControl：
- Parent Layer：
- 是否存在 animation leash：
- 相关 Transaction：
- 对应 Buffer / Surface：


<!-- source: 56-4.md -->

# 4. 跨层调用链
- Java 调用链：
- JNI 调用链：
- Native 调用链：
- SurfaceFlinger 生效链：


<!-- source: 57-5.md -->

# 5. 关键源码分析
- 关键类：
- 关键方法：
- 关键状态字段：
- 关键设计意图：


<!-- source: 63-18.md -->

# 18. 回答风格要求

使用本 Skill 时，输出必须符合以下风格：

1. **先系统后细节**
   - 先说职责和架构位置
   - 再说调用链
   - 最后说实现细节
2. **必须有跨层链路**
   - 不能只讲 Java
   - 不能只讲 SurfaceFlinger
   - 必须打通 Java → JNI → Native → SF
3. **必须说明设计思想**
   - 为什么这么设计
   - 解决什么问题
   - 带来了什么代价
4. **必须给出可验证结论**
   - 结论要能被源码和运行证据验证
   - 不能只给经验性判断
5. **必须指出对象层级**
   - 当前操作的是内容层还是 leash
   - parent 是谁
   - 最终显示路径是什么

------


<!-- source: 67-211-surfacecontrol.md -->

# 21.1 用户问：SurfaceControl 是什么？

应回答：

- 它是客户端控制 SurfaceFlinger 中 Layer 的句柄
- 用于创建/组织/修改图层状态
- 不负责直接绘制内容
- 与 Surface、BufferQueue、BLAST、SurfaceFlinger 构成完整显示链


<!-- source: 70-22-skill.md -->

# 22. Skill 调用模板

当调用本 Skill 时，推荐使用如下提示模板：

```
请基于 AOSP SurfaceControl 机制分析以下问题，并严格按照“架构定位 → 对象模型 → 跨层调用链 → 关键源码 → 时序分析 → 证据链 → 根因 → 修复建议”的顺序输出：

问题描述：
{问题描述}

现象：
{现象}

已知日志/trace/dumpsys：
{证据}

目标：
1. 说明涉及哪些 SurfaceControl / Layer / Transaction 对象
2. 还原 Java → JNI → Native → SurfaceFlinger 调用链
3. 判断问题属于事务不生效、层级异常、buffer不同步，还是 WMS/SF 状态不一致
4. 给出源码级根因和修复建议
```

------


<!-- source: 71-23.md -->

# 23. 最终交付标准

一个合格的 SurfaceControl 分析结果，必须满足：

- 能说清楚 SurfaceControl 在系统中的位置
- 能指出问题发生在哪一层
- 能还原完整调用链
- 能区分 Layer 属性问题与 Buffer 内容问题
- 能识别 leash / parent / z-order / crop / alpha / transform 影响
- 能给出至少一个明确根因
- 能提出工程可执行的修复方案
- 能给出验证路径

------


<!-- source: 72-24.md -->

# 24. 一句话总结

**SurfaceControl 的核心不是“画图”，而是“以事务方式控制 SurfaceFlinger 中 Layer 的结构与显示状态”；分析任何 SurfaceControl 问题，本质上都是在还原“哪个 Layer 被谁在什么时候以什么事务改成了什么状态，并最终为什么显示成现在这样”。**
