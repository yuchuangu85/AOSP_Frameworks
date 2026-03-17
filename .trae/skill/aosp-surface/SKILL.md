---
name: aosp-surface
description: 面向 Android AOSP Surface / SurfaceControl / BufferLayer / BLAST / BufferQueue / SurfaceFlinger 相关问题的系统级分析 Skill。用于分析应用绘制到 Surface 建立、Layer 组织、Buffer 生产消费、 Transaction 提交、同步栅栏、显示合成链路中的行为与异常。适用于黑屏、白屏、首帧不出、 SurfaceView/TextureView 异常、窗口有但内容不显示、Buffer 不流动、Layer 不更新、Transaction 不生效、BLAST 异常、Surface 生命周期异常、截图与实际显示不一致等场景。输出要求必须基于源码、 trace、dumpsys、log 与可验证证据，构建跨层调用链与根因结论。
---


# aosp-surface

## 统一输出要求

- 所有输出文档必须保存到仓库根目录的 `docs/` 目录下。
- 文档文件必须使用 Markdown 格式，文件扩展名为 `.md`。
- 如果用户未指定文件名，使用与任务主题相关的语义化文件名。
- 每次执行本子 Skill，必须单独输出 1 个独立的 Markdown 分析文件，禁止只把内容并入总控回答而不落独立文件。
- 独立文件名必须包含当前子 Skill 名称或其对应问题域，便于总控汇总时直接引用。
- 独立输出文件名必须遵循统一模式：`docs/<skill-slug>-<topic>.md`。
- 其中 `<skill-slug>` 必须使用当前 Skill 的规范英文标识，例如 `aosp-ams`、`aosp-wms`、`aosp-surfaceflinger`。
- 其中 `<topic>` 必须是当前分析主题的语义化短名，使用小写英文和连字符，禁止空格。
- 如果用户未提供主题名，则默认使用 `analysis` 作为 `<topic>`。


## 1. 目标

本 Skill 用于执行 **AOSP Surface 系统级源码分析**，聚焦以下核心问题：

1. **Surface 如何被创建、关联、显示与销毁**
2. **App 如何通过 Surface / SurfaceControl / BLASTBufferQueue 把内容送入 SurfaceFlinger**
3. **BufferQueue 如何在生产者与消费者之间流动**
4. **Transaction / LayerState 为什么不生效**
5. **为什么窗口存在但画面不显示**
6. **为什么 SurfaceView / TextureView / 普通 Window 内容显示异常**
7. **为什么首帧迟迟不出来、黑屏、白屏、闪屏**
8. **为什么截图正常但屏幕不正常，或屏幕正常但截图异常**
9. **为什么 buffer 在 dequeue/queue/acquire/present 某一阶段卡住**
10. **为什么 Surface 链路引发卡顿、掉帧、同步问题**

该 Skill 的核心任务不是泛泛解释概念，而是：

- 建立 **App → ViewRootImpl / Surface → BLAST / BufferQueue → SurfaceFlinger → HWC** 的跨层调用链
- 从 **源码、运行时状态、trace、fence、layer 树、buffer 流动** 中提取证据
- 输出 **可验证、可复现、可修复** 的根因分析

---

## 2. 适用场景

当用户出现以下诉求时，应调用本 Skill：

### 2.1 显示内容异常
- 页面已启动但内容黑屏
- 首帧不出 / 白屏时间长
- Window 已创建但界面不可见
- SurfaceView 区域黑块 / 透明 / 不刷新
- TextureView 不显示或显示旧帧
- Dialog / Popup / Activity 有窗口但没有内容
- 截图与真实显示不一致

### 2.2 Surface 生命周期异常
- `surfaceCreated/surfaceChanged/surfaceDestroyed` 时序异常
- relayout 后 Surface 频繁重建
- 旋转、分屏、切后台后 Surface 丢失
- SurfaceControl 泄漏或 Layer 未销毁

### 2.3 Buffer 流动异常
- `dequeueBuffer` 卡住
- `queueBuffer` 之后不显示
- SurfaceFlinger 没有 acquire 到新 buffer
- buffer 持续滞留在某个队列阶段
- fence 长时间 unsignaled
- producer/consumer 尺寸、格式、transform 不匹配

### 2.4 Transaction / LayerState 异常
- `SurfaceControl.Transaction.apply()` 后无效果
- alpha / crop / position / visibility 改动无效
- reparent / setLayer / show-hide 不生效
- leash / child layer 关系异常
- ShellTransition / WindowContainerTransaction 间接导致 Layer 状态异常

### 2.5 性能与时序问题
- 首帧路径慢
- Layer 更新频率异常
- BufferQueue backlog
- SurfaceFlinger 合成延迟
- 多 Surface 同步异常
- FrameTimeline 中 app / sf / present 某段异常

---

## 3. 分析边界

本 Skill 聚焦以下模块：

### 3.1 App / Framework 侧
- `ViewRootImpl`
- `Surface`
- `SurfaceControl`
- `SurfaceSession`
- `SurfaceHolder`
- `SurfaceView`
- `TextureView`
- `ThreadedRenderer / HWUI`
- `BLASTBufferQueue`
- `BufferQueueProducer`
- `SurfaceControl.Transaction`

### 3.2 System Server / WM 关联侧
- `WindowManagerService`
- `WindowState`
- `WindowContainer`
- `WindowSurfaceController`
- `ActivityRecord`
- `Insets / IME / Transition` 对 Surface 的间接影响

### 3.3 Native / SF 侧
- `SurfaceComposerClient`
- `SurfaceControl` native
- `BufferQueueCore`
- `BufferLayer / BufferStateLayer / EffectLayer / ContainerLayer`
- `Layer`
- `LayerState`
- `TransactionState`
- `SurfaceFlinger`
- `FrameTimeline`
- `Fence / Sync`
- `RenderEngine`
- `CompositionEngine`

### 3.4 HAL / Display 侧
- HWC 合成参与情况
- present fence / release fence
- 显示设备状态对 Surface 最终可见性的影响

### 3.5 不直接覆盖
以下问题可联动其他 Skill，但不作为本 Skill 主责：
- 输入分发异常：建议联动 `aosp-input`
- 纯窗口可见性策略异常：建议联动 `aosp-wms`
- 纯卡顿/Jank：建议联动 `aosp-graphics`
- AMS 生命周期异常：建议联动 `aosp-ams`

---

## 4. 核心分析原则

### 4.1 先证据，后结论
任何判断必须尽量基于以下证据之一或多者组合：
- AOSP 源码调用链
- `dumpsys SurfaceFlinger`
- `dumpsys window`
- `dumpsys gfxinfo`
- `perfetto / systrace`
- `logcat`
- `Surface trace / transaction trace`
- `Winscope`
- 屏幕录制 / 截图对比
- fence / buffer 状态

### 4.2 必须构建“对象映射”
分析时必须明确：
- **Window 是谁**
- **对应 Surface 是谁**
- **对应 Layer 是谁**
- **对应 BufferQueue 是谁**
- **producer / consumer 分别是谁**
- **父子 Layer 关系是什么**
- **显示目标 display / transform / crop 是什么**

### 4.3 必须定位“卡点阶段”
任何显示问题都应尽量落到以下阶段之一：
1. **没创建出来**
2. **创建了但不可见**
3. **可见但没有 buffer**
4. **有 buffer 但没有被 SF acquire**
5. **被 acquire 但没有参与有效合成**
6. **参与合成但最终没 present 到屏**
7. **present 了但显示结果与预期不一致**

### 4.4 必须区分“状态异常”和“时序异常”
- 状态异常：layer hidden、alpha=0、crop 错误、z 错误、parent 错误
- 时序异常：首帧晚、transaction 晚、buffer queue 堵塞、fence 晚

---

## 5. 输入要求

用户提供的信息越全，分析越准确。优先需要：

### 5.1 最低输入
- 问题现象描述
- 发生场景
- 是否稳定复现
- 目标窗口 / 页面 / 应用名
- Android 版本 / 设备平台

### 5.2 推荐输入
- `bugreport`
- `dumpsys SurfaceFlinger --list`
- `dumpsys SurfaceFlinger`
- `dumpsys window`
- `dumpsys activity top`
- `perfetto trace`
- `surface trace / layers trace`
- 关键 logcat
- 截图、录屏、现象时间点

### 5.3 最佳输入
- 现象发生前后 5~10s 的 Perfetto
- 对应时刻 layer trace
- 窗口名、Activity 名、Surface 名
- 相关模块源码路径
- 复现步骤
- 期望行为 vs 实际行为

---

## 6. 输出要求

输出必须严格包含以下部分：

1. **问题定义**
2. **对象映射**
3. **跨层调用链**
4. **关键源码路径**
5. **运行时证据**
6. **异常卡点定位**
7. **根因判断**
8. **修复建议**
9. **验证方案**
10. **风险与回归点**

禁止输出：
- 无证据的拍脑袋结论
- 只讲概念不落地
- 没有对象映射的泛泛分析
- 没有调用链的“猜测式”定位

---

## 7. 标准分析流程

## 7.1 第一步：定义现象
先回答：
- 是整个窗口不可见，还是窗口可见但内容不可见？
- 是首帧不出，还是后续不刷新？
- 是某个 SurfaceView 黑屏，还是整个应用黑屏？
- 是静态状态错，还是动态时序错？

输出形式：
- 现象开始时间
- 影响范围
- 是否只影响某一类 Surface
- 是否与旋转/切后台/Transition/分屏/息屏唤醒相关

---

## 7.2 第二步：做对象映射
必须建立下列映射关系表：

| 对象类型       | 名称/标识 | 说明                                      |
| -------------- | --------- | ----------------------------------------- |
| App/Activity   | xxx       | 业务页面                                  |
| Window         | xxx       | WindowState 对象                          |
| Surface        | xxx       | Java/Native Surface                       |
| SurfaceControl | xxx       | layer 控制句柄                            |
| Layer          | xxx       | SF 中的 Layer 节点                        |
| 父 Layer       | xxx       | parent/reparent 关系                      |
| BufferQueue    | xxx       | producer-consumer 对                      |
| Producer       | xxx       | App / BLAST / CPU / GPU                   |
| Consumer       | xxx       | SurfaceFlinger / ImageReader / GLConsumer |
| Display        | xxx       | 输出目标屏幕                              |

如果这一步做不出来，后续分析通常不可靠。

---

## 7.3 第三步：还原创建链路
重点看：
- Surface 是何时创建的
- 谁发起创建
- 创建时挂在哪个 parent 下
- 生命周期是否发生重建
- relayout / resize 是否导致 Surface 重新分配

典型链路：

### App 普通窗口 Surface 创建主链
```text
ActivityThread.handleResumeActivity
  -> ViewRootImpl.setView
  -> requestLayout / performTraversals
  -> relayoutWindow
  -> IWindowSession.relayout
  -> WMS.relayoutWindow
  -> create / update WindowSurface
  -> SurfaceControl / BLASTBufferQueue 建立
  -> Surface 回传 App
```

### SurfaceView 链路

```
SurfaceView.updateSurface
  -> ViewRootImpl / WindowSession.relayout
  -> SurfaceControl 创建或更新
  -> BLASTBufferQueue / BufferQueue 建立
  -> surfaceCreated / surfaceChanged 回调
  -> App/RenderThread/EGL 开始生产 buffer
```

### TextureView 链路

```
TextureView
  -> SurfaceTexture
  -> BufferQueueProducer
  -> GLConsumer 消费
  -> 最终内容由宿主 ViewRoot 的 RenderNode/HWUI 合成
```

------

## 7.4 第四步：还原 Buffer 流动

关键问题：

1. producer 是否成功 `dequeueBuffer`
2. 是否成功绘制并 `queueBuffer`
3. consumer 是否 `acquireBuffer`
4. SF 是否在对应 frame 使用了该 buffer
5. present fence 是否完成

标准流动链：

```
App Render / CPU Canvas / EGL SwapBuffers
  -> dequeueBuffer
  -> draw
  -> queueBuffer
  -> BufferQueueCore 入队
  -> SurfaceFlinger acquireBuffer
  -> Layer latchBuffer
  -> CompositionEngine 参与合成
  -> HWC/GPU 合成
  -> presentDisplay
```

排查重点：

- buffer 数量是否耗尽
- producer 是否被 backpressure 阻塞
- queue 后是否没有 acquire
- acquire 后是否被旧事务/可见性状态挡住
- release fence 是否迟迟不回收，导致生产端阻塞

------

## 7.5 第五步：检查 Layer 状态

显示异常通常最终都能落到 Layer 状态上。必须检查：

- `visibleRegion`
- `alpha`
- `z-order`
- `transform`
- `crop`
- `buffer size`
- `dataspace`
- `position`
- `parent`
- `relative layer`
- `shadow / rounded corner / blur` 等特殊 effect
- `hidden` / `pending state`
- `buffer transform`
- `active buffer` 是否存在

判断原则：

- **Layer 不存在**：创建/销毁链路有问题
- **Layer 存在但 hidden**：可见性/transaction 有问题
- **Layer visible 但无 buffer**：producer 没供帧
- **Layer 有 buffer 但被裁剪/移出屏幕**：几何状态错误
- **Layer 有 buffer 且几何正确但仍不显示**：合成/display 阶段继续查

------

## 7.6 第六步：检查 Transaction 提交流程

重点分析：

```
App / WM / Shell / SystemUI
  -> SurfaceControl.Transaction
  -> native SurfaceComposerClient::Transaction
  -> set layer state
  -> apply / merge
  -> Binder to SurfaceFlinger
  -> TransactionState 入队
  -> SurfaceFlinger 处理事务
  -> commit to Layer state
  -> 下一个 frame 生效
```

要确认：

- apply 是否真的调用
- 是否被 merge 覆盖
- 是否是 one-way 异步导致观察时机错误
- 是否在下一帧才生效
- 是否被 parent 状态覆盖
- 是否被 WM / Shell 的后续事务打回
- BLAST 是否改写了提交时序

------

## 7.7 第七步：检查首帧路径

首帧问题要特别还原“谁先谁后”：

```
Window visible
  -> Surface ready
  -> App first draw
  -> queue first buffer
  -> SF latch first buffer
  -> composition
  -> first present
```

首帧慢通常发生在：

- Window 已 visible，但 App 还没 draw
- App 已 draw，但 buffer 没 queue 成功
- queue 了但 SF 没及时 latch
- latch 了但 transaction / geometry 尚未就绪
- 全链路完成但被 Transition / starting window 遮挡

------

## 8. 完整跨层调用链模型

## 8.1 普通应用窗口显示链

```
Activity.resume
  -> ViewRootImpl.setView
  -> performTraversals
  -> relayoutWindow
  -> WMS.relayoutWindow
  -> 创建/更新 SurfaceControl
  -> 返回 Surface 给应用
  -> HWUI/EGL 在 Surface 上渲染
  -> queueBuffer
  -> BufferQueue
  -> SurfaceFlinger acquire
  -> Layer latch
  -> CompositionEngine
  -> HWC/GPU
  -> Display present
```

## 8.2 SurfaceView 显示链

```
App Window
  -> SurfaceView.updateSurface
  -> 请求独立 SurfaceControl / BLAST
  -> 创建独立 Layer
  -> producer 向独立 BufferQueue 提交 buffer
  -> SurfaceFlinger 单独合成该 Layer
  -> 与宿主 Window 共同输出到屏幕
```

## 8.3 TextureView 显示链

```
App producer -> SurfaceTexture(BufferQueueProducer)
  -> GLConsumer 消费成纹理
  -> TextureView 参与宿主 ViewRoot/HWUI 渲染
  -> 最终 buffer 进入宿主 Window Surface
  -> SurfaceFlinger 合成宿主 Layer
```

## 8.4 BLAST 链路

```
App / ViewRootImpl / SurfaceView
  -> BLASTBufferQueue
  -> 事务与 buffer 更新协调
  -> 提交 buffer 对应几何状态
  -> SurfaceFlinger 在事务同步点 latch
```

BLAST 的关键价值：

- 将 buffer 与几何 transaction 更紧密同步
- 减少 resize / sync 场景中的错帧
- 但也可能让“buffer 已提交但几何未同步”类问题更隐蔽

------

## 9. 重点源码索引

> 以下为分析时优先关注的典型源码入口；不同 Android 版本路径与实现细节可能有差异，应以实际分支为准。

## 9.1 Framework Java

```
frameworks/base/core/java/android/view/Surface.java
frameworks/base/core/java/android/view/SurfaceControl.java
frameworks/base/core/java/android/view/SurfaceView.java
frameworks/base/core/java/android/view/TextureView.java
frameworks/base/core/java/android/view/ViewRootImpl.java
frameworks/base/graphics/java/android/graphics/SurfaceTexture.java
```

## 9.2 Framework JNI / Native Glue

```
frameworks/base/core/jni/android_view_Surface.cpp
frameworks/base/core/jni/android_view_SurfaceControl.cpp
frameworks/base/core/jni/android_graphics_SurfaceTexture.cpp
```

## 9.3 Native libs/gui

```
frameworks/native/libs/gui/Surface.cpp
frameworks/native/libs/gui/SurfaceControl.cpp
frameworks/native/libs/gui/SurfaceComposerClient.cpp
frameworks/native/libs/gui/BLASTBufferQueue.cpp
frameworks/native/libs/gui/BufferQueueProducer.cpp
frameworks/native/libs/gui/BufferQueueConsumer.cpp
frameworks/native/libs/gui/BufferQueueCore.cpp
frameworks/native/libs/gui/ConsumerBase.cpp
```

## 9.4 SurfaceFlinger

```
frameworks/native/services/surfaceflinger/SurfaceFlinger.cpp
frameworks/native/services/surfaceflinger/Layer.cpp
frameworks/native/services/surfaceflinger/BufferLayer.cpp
frameworks/native/services/surfaceflinger/BufferStateLayer.cpp
frameworks/native/services/surfaceflinger/ContainerLayer.cpp
frameworks/native/services/surfaceflinger/EffectLayer.cpp
frameworks/native/services/surfaceflinger/TransactionState.cpp
frameworks/native/services/surfaceflinger/FrontEnd/
frameworks/native/services/surfaceflinger/FrameTimeline/
frameworks/native/services/surfaceflinger/DisplayHardware/
frameworks/native/services/surfaceflinger/CompositionEngine/
```

## 9.5 WindowManager 关联

```
frameworks/base/services/core/java/com/android/server/wm/WindowState.java
frameworks/base/services/core/java/com/android/server/wm/WindowContainer.java
frameworks/base/services/core/java/com/android/server/wm/WindowSurfaceController.java
frameworks/base/services/core/java/com/android/server/wm/Session.java
frameworks/base/services/core/java/com/android/server/wm/ActivityRecord.java
```

------

## 10. 运行时证据清单

## 10.1 必看 dumpsys

```
adb shell dumpsys SurfaceFlinger --list
adb shell dumpsys SurfaceFlinger
adb shell dumpsys window
adb shell dumpsys activity top
adb shell dumpsys gfxinfo <package>
```

## 10.2 建议抓取 trace

- Perfetto:
  - SurfaceFlinger
  - FrameTimeline
  - gfx
  - sched
  - freq
  - binder
  - wm
  - view
- layers trace / transactions trace
- winscope 关联查看

## 10.3 关键日志关键词

```
Surface
SurfaceControl
SurfaceView
BLASTBufferQueue
BufferQueueProducer
BufferQueueConsumer
SurfaceFlinger
Layer
Transaction
dequeueBuffer
queueBuffer
acquireBuffer
presentFence
releaseFence
first frame
```

------

## 11. 标准排查决策树

## 11.1 黑屏 / 不显示

```
窗口是否存在？
  否 -> WMS / 启动链问题
  是 -> Layer 是否存在？
        否 -> Surface / SurfaceControl 创建链问题
        是 -> Layer 是否 visible？
              否 -> Transaction / WM 可见性问题
              是 -> 是否有 active buffer？
                    否 -> producer 未出帧 / BufferQueue 堵塞
                    是 -> 是否被 crop / alpha / z / parent 影响？
                          是 -> LayerState 错误
                          否 -> 是否参与 composition？
                                否 -> SF/HWC 路径问题
                                是 -> display/present 阶段问题
```

## 11.2 首帧慢

```
Window ready 时间
  -> Surface ready 时间
  -> App first draw 时间
  -> first queueBuffer 时间
  -> SF first latch 时间
  -> first present 时间
```

谁最晚，谁优先背锅；但还要继续查它为什么晚。

## 11.3 Transaction 不生效

```
apply 是否调用？
  -> Binder 是否发到 SF？
  -> SF 是否处理该 transaction？
  -> Layer state 是否更新？
  -> 更新是否被后续事务覆盖？
  -> 父 Layer/relative layer 是否导致观察结果不符？
```

## 11.4 Buffer 卡住

```
dequeue 卡住 -> 空闲 buffer 不足 / release fence 不回
queue 后不显示 -> consumer 未 acquire / Layer 不可见
acquire 后不显示 -> latch/transaction/display 阶段继续查
```

------

## 12. 高频异常模式库

## 12.1 Surface 创建与生命周期类

1. `ViewRootImpl` 尚未完成 relayout，Surface 未返回
2. `SurfaceView.updateSurface` 反复重建导致瞬时黑屏
3. Activity 切换过程中旧 Surface 已销毁，新 Surface 尚未 ready
4. 旋转触发 resize，Surface 重建与首帧错位
5. 多窗口/分屏切换时 parent layer 重挂载异常
6. 进程切后台后 producer 停止，但 layer 仍可见
7. leash/transition layer 存在，真实内容 layer 被包在中间层

## 12.2 BufferQueue 类

1. producer 没有真正开始绘制
2. `dequeueBuffer` 因 buffer exhaustion 阻塞
3. `queueBuffer` 成功但 consumer 迟迟不 acquire
4. acquire 后旧 buffer 一直被 latch，新 buffer 不上屏
5. release fence 长时间不回导致生产侧阻塞
6. producer/consumer size 不一致导致显示裁剪异常
7. buffer transform 与 layer transform 叠加后方向错误
8. dataspace / pixel format 异常导致显示异常
9. `max acquired buffer count` 相关堵塞
10. shared buffer / auto refresh 行为被误判

## 12.3 BLAST 类

1. BLAST 同步 transaction 未在预期帧生效
2. resize 与 buffer 更新未对齐，出现闪烁/拉伸
3. buffer 已 queue 但等待配套几何 transaction
4. 应用侧观察到提交完成，但 SF 侧尚未 commit
5. SurfaceView 的 BLAST 子 layer 已更新，但宿主窗口状态未匹配

## 12.4 Layer/Transaction 类

1. alpha=0 导致完全透明
2. hidden=true 或 parent hidden 导致不可见
3. z-order 太低被遮挡
4. crop 把内容裁没
5. position 移出屏幕
6. reparent 到错误父节点
7. relative layer 关系异常
8. corner / shadow / effect layer 干扰判断
9. transaction 被后续系统事务覆盖
10. 多源事务竞争，最终状态不是预期状态
11. merge 顺序导致前一笔事务失效
12. leash 存在，业务 layer 在 leash 下但 leash 不可见

## 12.5 首帧 / 时序类

1. Window 先 visible，首帧 buffer 后到
2. starting window 移除与真实首帧切换不平滑
3. app 首绘完成但 SF 下一拍才 latch
4. SF 已 latch，但 present 受 HWC/GPU 节奏影响
5. 首帧 draw 触发太晚，本质是上游生命周期/渲染慢
6. 首帧已出但被 transition 动画层遮住

## 12.6 SurfaceView / TextureView 专项

1. SurfaceView 独立 layer 在宿主之下或被错误遮挡
2. Z-order onTop/mediaOverlay 使用不当
3. SurfaceView 区域变动后 crop 未同步
4. TextureView producer 正常，但宿主 HWUI 未重绘
5. SurfaceTexture 有 buffer，但 transform matrix 异常
6. TextureView 截图正常、屏显异常，需区分最终合成链
7. SurfaceView 黑屏但宿主 UI 正常，重点查独立 layer
8. 宿主 Window 正常但 SurfaceView 子 layer 未跟上 resize

## 12.7 截图与屏显不一致

1. 截图路径抓到的是 layer 合成前结果
2. 特定 overlay / HWC plane 没被普通截图捕获
3. secure / protected layer 行为导致截图差异
4. 屏幕上被其他 layer 遮挡但截图采样顺序不同
5. 实际显示受显示设备链路影响而截图正常

------

## 13. 首帧与显示时序分析模板

分析首帧问题时，必须输出如下时间线：

| 阶段                       | 时间 | 说明           |
| -------------------------- | ---- | -------------- |
| Activity resume 开始       | T0   | 生命周期入口   |
| Window add / relayout 完成 | T1   | WMS 已准备窗口 |
| Surface 返回 App           | T2   | 可用于绘制     |
| App first draw 开始        | T3   | 首次渲染       |
| first queueBuffer          | T4   | 首 buffer 提交 |
| SF first acquire/latch     | T5   | SF 接收到首帧  |
| first present              | T6   | 屏幕真正显示   |

然后计算：

- `T1-T0`：窗口建立耗时
- `T2-T1`：Surface ready 耗时
- `T4-T2`：应用首绘耗时
- `T5-T4`：SF 接帧耗时
- `T6-T5`：显示合成耗时

------

## 14. 推荐回答模板

## 14.1 标准版

```
一、问题定义
- 现象：
- 影响范围：
- 触发条件：
- 是否稳定复现：

二、对象映射
- Activity：
- Window：
- Surface：
- SurfaceControl：
- Layer：
- BufferQueue：
- Producer / Consumer：

三、跨层调用链
- App ->
- Framework ->
- Native ->
- SurfaceFlinger ->
- HWC/Display ->

四、关键源码定位
- 文件：
- 方法：
- 关键状态：
- 关键分支：

五、运行时证据
- dumpsys SurfaceFlinger：
- dumpsys window：
- trace：
- log：

六、异常卡点
- 卡在：
- 原因说明：
- 排除项：

七、根因结论
- 根因：
- 触发机制：
- 为什么会表现为当前现象：

八、修复建议
- 修复点 1：
- 修复点 2：
- 修复点 3：

九、验证方案
- 验证步骤：
- 预期日志：
- 预期 trace 变化：

十、风险与回归点
- 风险：
- 回归场景：
```

## 14.2 简洁结论版

```
结论：
这是一个发生在【阶段】的 Surface 显示异常，根因是【根因】。

证据：
1. 【证据1】
2. 【证据2】
3. 【证据3】

调用链：
【关键调用链】

修复建议：
【建议】
```

------

## 15. 分析时必须回答的关键问题

每次分析至少尝试回答以下问题：

1. 目标 Window 是否真的存在？
2. 目标 Surface 是否真的创建成功？
3. 对应 Layer 是否存在于 layer tree 中？
4. Layer 当前是否可见？
5. 当前是否持有 active buffer？
6. producer 是否持续 queue 新 buffer？
7. consumer 是否正常 acquire？
8. geometry/transform/crop 是否正确？
9. transaction 是否按预期应用？
10. 首帧问题发生在 app、sf 还是 display 阶段？
11. 是否有 parent/leash/transition layer 干扰？
12. 是否与 BLAST 的同步策略相关？
13. 是否与 release fence / present fence 异常相关？
14. 当前现象是单纯状态错误还是时序错位？
15. 是否能给出唯一主根因，而不是罗列一堆可能性？

------

## 16. 与其他 Skill 的协同关系

### 16.1 联动 `aosp-wms`

当问题表现为：

- 窗口本身没显示
- 可见性/层级由窗口策略主导
- 转场/多窗口/Insets/IME 影响 Surface

### 16.2 联动 `aosp-graphics`

当问题表现为：

- 掉帧、Jank、FrameTimeline 异常
- SF 合成性能问题
- GPU/HWC 性能瓶颈

### 16.3 联动 `aosp-input`

当问题表现为：

- 输入已到但界面不刷新
- Surface 更新和输入反馈时延相关

### 16.4 联动 `aosp-ams`

当问题表现为：

- 生命周期未推进导致首帧不出
- 进程/Activity 状态影响窗口建立

------

## 17. 使用约束

本 Skill 执行时必须遵循以下约束：

1. **不得脱离源码路径谈机制**
2. **不得脱离对象映射谈现象**
3. **不得脱离证据谈根因**
4. **不得把 WM 问题误判成 SF 问题**
5. **不得把 producer 不出帧误判成 SF 不显示**
6. **不得忽略 parent/leash/transition 中间层**
7. **不得只看截图，不看实际 layer/buffer 状态**
8. **不得只看 log，不看 trace 与 dumpsys**
9. **不得把“偶现”当作“无规律”，必须还原触发时序**
10. **结论必须能指导修复与验证**

------

## 18. Skill 执行提示词

可作为该 Skill 的内部执行提示：

```
你是 Android AOSP Surface 专家，负责分析 Surface / SurfaceControl / BLAST / BufferQueue / SurfaceFlinger
相关的显示与时序问题。

你的目标不是泛泛解释概念，而是：
1. 明确问题现象与影响范围
2. 建立 Window / Surface / SurfaceControl / Layer / BufferQueue 对象映射
3. 构建 App -> Framework -> Native -> SurfaceFlinger -> HWC 的跨层调用链
4. 结合源码、dumpsys、trace、log、截图等证据定位异常卡点
5. 判断问题处于“创建 / 可见性 / buffer 生产 / buffer 消费 / transaction / composition / present”中的哪一阶段
6. 输出唯一主根因、修复建议和验证方案

输出时必须包含：
- 问题定义
- 对象映射
- 跨层调用链
- 关键源码路径
- 运行时证据
- 异常卡点
- 根因结论
- 修复建议
- 验证方案
- 风险与回归点

如果证据不足，必须明确指出缺失证据以及最小补充抓取项；
如果可以排除某些方向，要明确写出“已排除项及理由”；
禁止只给可能性列表，不给主判断。
```

------

## 19. 最小可执行示例

### 输入示例

```
现象：
应用启动后主界面 Window 已经出现，但 SurfaceView 区域一直黑屏，偶现恢复。
Android 14，某 MTK 设备，复现概率 30%。

已提供：
- dumpsys SurfaceFlinger
- dumpsys window
- 10 秒 perfetto
- 关键 logcat

要求：
分析黑屏是在 Surface 创建、BufferQueue、Transaction 还是 SF 合成阶段。
```

### 期望输出示例

```
结论：
该问题不是 Window 未显示，而是 SurfaceView 对应独立 Layer 已创建但首帧 buffer 未及时进入稳定显示状态。
主卡点位于 BLASTBufferQueue + 首次 transaction 同步阶段。

关键证据：
1. dumpsys window 显示宿主 Window 可见，布局完成。
2. SurfaceFlinger 中目标 Layer 存在，parent 关系正常，但早期 active buffer 缺失。
3. trace 中 App 首次 queueBuffer 晚于 Window visible，且首个 buffer latch 又滞后一拍。
4. BLAST 相关 transaction 与 resize 同步发生，导致首帧在可见窗口出现后仍未及时上屏。

根因：
SurfaceView 首帧 buffer 提交与几何 transaction 对齐延迟，导致用户看到黑屏窗口。

修复建议：
1. 检查 SurfaceView 初始化时首帧生产触发点，提前 producer 启动。
2. 检查 resize / relayout 是否导致 BLAST 同步事务延后。
3. 增加首帧关键日志：surfaceCreated、first dequeue、first queue、first latch。
```

## 统一输出模板（必须）
每次分析必须按以下章节输出，章节名保持一致：

```markdown
## 一、问题定义与范围
- 现象、影响、触发条件、版本范围、设备与构建信息。

## 二、主调用链
- 给出主调用链（可附关键分支）。
- 标注线程/进程、IPC 或 JNI 边界、关键等待点（锁/fence/binder/message queue）。

## 三、设计思想与架构权衡
- 解释关键机制为什么这样设计。
- 说明收益与代价：稳定性、性能、复杂度、可维护性。

## 四、架构图（Mermaid）
- 图中必须体现问题相关模块边界与依赖方向。

## 五、时序图（Mermaid）
- 图中必须体现关键事件顺序与阻塞传播路径。
- 标注关键时间点或阶段（如输入、调度、合成、显示）。

## 六、关键代码详细分析
- 时序图完成后，必须紧接着对时序图中最关键的类、方法、分支和状态变化做详细源码分析。
- 至少解释 3 处关键代码：入口、关键分支或状态机、收敛点或返回路径。
- 说明每段代码在时序图中的位置、线程或进程上下文、输入输出与设计意图。

## 七、证据链（源码 + 运行时）
- 源码证据：文件路径 + 方法 + 关键条件。
- 运行时证据：logcat/dumpsys/Perfetto/Winscope/traces 等。
- 每个关键结论至少绑定 1 条源码证据和 1 条运行时证据。

## 八、根因结论与置信度
- 根因分层：App / Framework / Native / HAL / Kernel。
- 置信度：Confirmed / Highly Likely / Possible / Speculative。

## 九、修复建议
- 建议必须直连根因，说明影响面与潜在副作用。

## 十、验证计划
- 功能回归、性能回归、稳定性回归、边界场景验证。

## 十一、证据缺口与后续采集
- 列出当前缺口与补采方案（采集工具、场景、目标信号）。
```

## 图示规范（统一）
- 架构图与时序图必须使用 Mermaid。
- 图只保留与当前问题直接相关的节点与链路，避免“大而全”。
- 时序图至少包含：发起方、system_server 关键角色、图形/服务关键节点（按问题域选择）。
- 时序图之后必须紧接“关键代码详细分析”章节，逐段解释图中关键代码，禁止只给图不给代码解读。
- 图中命名与正文术语保持一致。

## 置信度分级（统一）
- `Confirmed`：源码与运行时证据闭环，结论可复现。
- `Highly Likely`：证据链完整度高，但存在单点缺口。
- `Possible`：存在方向性证据，但缺关键验证。
- `Speculative`：仅假设，必须显式标注不可直接下结论。
