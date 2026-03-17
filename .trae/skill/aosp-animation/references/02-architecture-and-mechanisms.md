# 架构与核心机制
<!-- source: 04-3.md -->

# 3. 分析边界

### 3.1 本 Skill 重点覆盖

- Framework 动画调度机制
- WMS / ATMS / Shell 动画切换机制
- SurfaceControl / Leash / Transaction 动画控制
- Choreographer / VSYNC / RenderThread 帧驱动机制
- SF / HWC 对动画可见性的影响
- FrameTimeline / Jank 分类与动画掉帧原因
- 动画状态机、生命周期、取消/中断逻辑
- 跨层同步与 fence / buffer 可见性影响

### 3.2 本 Skill 不单独替代

下列场景建议配合其他 Skill 联合分析：

- 纯图形合成链路深挖：配合 `aosp-graphics`
- SurfaceFlinger 合成细节：配合 `aosp-SurfaceFlinger`
- Surface / BLAST / Layer 深挖：配合 `aosp-SurfaceControl`、`aosp-surface`
- VSYNC / 帧同步专项：配合 `aosp-VSYNC`
- Fence / BufferQueue 专项：配合 `aosp-Fence`、`aosp-BufferQueue`
- WMS 窗口状态机：配合 `aosp-wms`
- Input 引发的动画响应延迟：配合 `aosp-input`
- 启动慢 / 冷启动动画衔接问题：配合 `aosp-ams`

---


<!-- source: 06-5.md -->

# 5. 动画系统总览

Android 动画系统不是单一模块，而是多个层次共同参与的结果：

- **应用层**
  - View Animation
  - Property Animation
  - Animator / ObjectAnimator / ValueAnimator / AnimatorSet
  - LayoutTransition
  - Transition framework

- **Framework/UI 调度层**
  - Choreographer
  - ViewRootImpl
  - Traversal
  - ThreadedRenderer / HWUI / RenderThread

- **窗口管理层**
  - WMS
  - ATMS
  - AppTransition
  - WindowState / DisplayContent / Task / ActivityRecord
  - SurfaceAnimator
  - SurfaceAnimationRunner

- **Shell 动画层**
  - Shell Transitions
  - TransitionController
  - TransitionPlayer
  - RemoteAnimationController
  - RecentsAnimation

- **Surface 控制层**
  - SurfaceControl
  - Transaction
  - Leash
  - BLAST / SurfaceSyncGroup
  - Layer reparent / alpha / crop / matrix / position

- **显示合成层**
  - SurfaceFlinger
  - CompositionEngine
  - HWC
  - PresentFence / ReleaseFence

---


<!-- source: 07-6-aosp.md -->

# 6. AOSP 动画分层架构图

```text
+--------------------------------------------------------------+
| App / SystemUI / Launcher / IME                              |
|  - View Animation / Property Animation / Transition          |
|  - Activity/Task launch & switch requests                    |
+------------------------------+-------------------------------+
                               |
                               v
+--------------------------------------------------------------+
| Framework UI Pipeline                                        |
|  - Choreographer                                             |
|  - ViewRootImpl                                              |
|  - Traversal / invalidate / draw                             |
|  - ThreadedRenderer / RenderNode / RenderThread              |
+------------------------------+-------------------------------+
                               |
                               v
+--------------------------------------------------------------+
| Window / Shell Animation Layer                               |
|  - WMS / ATMS / AppTransition                                |
|  - SurfaceAnimator / SurfaceAnimationRunner                  |
|  - TransitionController / Shell Transitions                  |
|  - RemoteAnimation / RecentsAnimation                        |
+------------------------------+-------------------------------+
                               |
                               v
+--------------------------------------------------------------+
| Surface Transaction Layer                                    |
|  - SurfaceControl / Transaction                              |
|  - Leash / BLAST / reparent / alpha / matrix / crop          |
+------------------------------+-------------------------------+
                               |
                               v
+--------------------------------------------------------------+
| SurfaceFlinger / Composition                                 |
|  - Layer state latch                                         |
|  - FrameTimeline                                             |
|  - GPU / HWC composition                                     |
|  - Present to display                                        |
+--------------------------------------------------------------+
```


<!-- source: 09-71-app-view.md -->

# 7.1 App / View 动画核心对象

- `ValueAnimator`
- `ObjectAnimator`
- `AnimatorSet`
- `ViewPropertyAnimator`
- `LayoutTransition`
- `android.transition.Transition`
- `MotionLayout`（若系统应用或业务使用）

### 重点关注

- 动画启动入口
- 时长 / 插值器 / repeat / cancel
- invalidate / requestLayout / property update
- 是否运行于 UI Thread
- 是否和 Choreographer 帧回调绑定

------


<!-- source: 10-72-framework.md -->

# 7.2 Framework 帧驱动核心对象

- `Choreographer`
- `DisplayEventReceiver`
- `ViewRootImpl`
- `ThreadedRenderer`
- `RenderNode`
- `RenderThread`

### 重点关注

- VSYNC 到达时间
- callback 类型：input / animation / traversal / commit
- doFrame 执行耗时
- UI 线程是否拥塞
- RenderThread 是否跟上帧率

------


<!-- source: 11-73-wms-shell.md -->

# 7.3 WMS / Shell 动画核心对象

- `WindowManagerService`
- `ActivityTaskManagerService`
- `AppTransition`
- `WindowState`
- `ActivityRecord`
- `Task`
- `DisplayContent`
- `SurfaceAnimator`
- `SurfaceAnimationRunner`
- `TransitionController`
- `Transition`
- `RemoteAnimationController`
- `RecentsAnimationController`

### 重点关注

- 动画由谁发起
- 动画目标窗口/Task/Activity 是谁
- leash 是否创建
- transaction 是否提交
- transition collect / ready / play / finish 时序
- animation adapter 是否正确绑定

------


<!-- source: 12-74-surface.md -->

# 7.4 Surface / 合成相关核心对象

- `SurfaceControl`
- `SurfaceControl.Transaction`
- `BLASTBufferQueue`
- `SurfaceSyncGroup`
- `Layer`
- `SurfaceFlinger`
- `CompositionEngine`

### 重点关注

- layer 可见性
- alpha / position / matrix / crop
- reparent 是否正确
- buffer 是否就绪
- SF 是否接收到 layer state
- presentFence 是否连续稳定

------


<!-- source: 16-83-shell-transition-remoteanimation.md -->

# 8.3 Shell Transition / RemoteAnimation 调用链

```
WindowContainer change collected
  -> TransitionController requestStartTransition
  -> collect participants
  -> transition ready
  -> TransitionPlayer / remote runner invoked
  -> remote side builds animation targets
  -> SurfaceControl leash animation
  -> finish transition
  -> WMS applies final state
```

### 典型分析点

- collect 阶段是否遗漏参与者
- ready 时机是否过早/过晚
- remote runner 是否超时
- target leash 是否为空或失效
- finishTransition 是否调用
- finish 后最终层级是否正确

------


<!-- source: 23-101-java-framework.md -->

# 10.1 Java / Framework

- `frameworks/base/core/java/android/animation/`
- `frameworks/base/core/java/android/view/Choreographer.java`
- `frameworks/base/core/java/android/view/ViewRootImpl.java`
- `frameworks/base/core/java/android/view/ViewPropertyAnimator.java`
- `frameworks/base/core/java/android/transition/`
- `frameworks/base/core/java/android/view/WindowInsetsAnimation*.java`


<!-- source: 24-102-window-transition-shell.md -->

# 10.2 Window / Transition / Shell

- `frameworks/base/services/core/java/com/android/server/wm/`
- `AppTransition*`
- `SurfaceAnimator`
- `SurfaceAnimationRunner`
- `WindowContainer`
- `ActivityRecord`
- `Task`
- `DisplayContent`
- `Transition*`
- `RecentsAnimation*`


<!-- source: 25-103-shell.md -->

# 10.3 Shell

- `frameworks/base/libs/WindowManager/Shell/src/com/android/wm/shell/transition/`
- `.../back/`
- `.../recents/`
- `.../pip/`
- `.../splitscreen/`
- `.../desktopmode/`（如对应版本存在）


<!-- source: 26-104-native-render.md -->

# 10.4 Native / Render

- `frameworks/base/libs/hwui/`
- `frameworks/base/libs/renderthread/`
- `frameworks/native/libs/gui/`
- `frameworks/native/services/surfaceflinger/`

------


<!-- source: 31-3.md -->

# 步骤 3：确认动画是否可见

检查：

- layer 是否 visible
- alpha 是否非 0
- crop / matrix / position 是否合理
- layer z-order 是否正确
- 是否被其他窗口遮挡
- 是否存在 black/white starting window 覆盖

------


<!-- source: 38-141.md -->

# 14.1 关键阶段

```
requestStartTransition
  -> collect changes
  -> mark ready
  -> dispatch to player
  -> build targets
  -> play animation
  -> finish transition
  -> apply final state
```


<!-- source: 39-142.md -->

# 14.2 高风险点

- collect participant 不完整
- ready 时机异常
- remote player 响应慢
- animation target leash 丢失
- finish transition 未落最终态
- 与 legacy app transition 混用产生错乱

------


<!-- source: 56-20.md -->

# 20. 代码阅读策略

分析动画源码时，必须同时回答四类问题：

### 20.1 谁触发

找到入口函数、调用方、调用条件。

### 20.2 谁持有状态

找到动画对象、宿主对象、状态字段、取消/完成标志位。

### 20.3 谁推进每一帧

找到帧驱动器：

- Choreographer
- RenderThread
- SurfaceAnimationRunner
- Shell transition player
- Remote runner

### 20.4 谁决定最终上屏

找到 Transaction 提交点、SF latch 条件和最终 layer 状态。

------


<!-- source: 61-3.md -->

# 3. 跨层调用链
- App / Framework：
- WMS / Shell：
- Surface / SF：


<!-- source: 79-244-surface-sf.md -->

# 24.4 Surface / SF 优化

- 优化首帧 buffer 就绪时间
- 保持 transaction 粒度稳定
- 减少大批量 layer 同帧变化
- 避免动画中频繁 reparent
