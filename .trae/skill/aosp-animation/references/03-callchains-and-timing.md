# 调用链与时序
<!-- source: 02-1.md -->

# 1. 目标

本 Skill 用于对 Android / AOSP 动画体系进行**系统级源码分析、时序还原、跨层调用链构建、异常归因与优化建议输出**。  
分析对象覆盖：

- App 层动画
- View / Drawable / Layout 相关动画
- WindowManager 窗口动画
- Activity / Task / TaskFragment / Recents / Shell 动画
- Shell Transition / RemoteAnimation / TransitionController
- Insets / IME / SystemUI 动画
- Surface / Leash / BLAST / Transaction 动画
- SurfaceFlinger 合成节奏
- VSYNC / Choreographer / RenderThread / FrameTimeline
- 动画引发的卡顿、掉帧、闪屏、黑白屏、不同步、动画缺失等异常

---


<!-- source: 05-4.md -->

# 4. 核心分析原则

执行动画系统分析时，必须遵守以下原则：

1. **先确定动画类型，再确定驱动层**
   - View 动画
   - 属性动画
   - 布局动画
   - WindowAnimation
   - AppTransition
   - ShellTransition
   - RemoteAnimation
   - Insets / IME 动画
   - SurfaceControl Transaction 动画

2. **必须同时看“逻辑动画”和“显示动画”**
   - 逻辑上开始 ≠ 屏幕上已呈现
   - Transaction 已提交 ≠ SF 已合成展示
   - App 收到回调 ≠ 用户实际看见动画

3. **必须构建跨层时序**
   - 触发源
   - 状态变更
   - 动画对象创建
   - 帧驱动
   - Transaction 应用
   - Buffer 准备
   - SF 合成
   - Present 到屏幕

4. **必须区分“动画未启动”“动画启动但不可见”“动画执行但不流畅”**
   - 调度问题
   - 状态机问题
   - 图层可见性问题
   - VSYNC / RenderThread / GPU / SF 性能问题

5. **必须区分“Framework 逻辑耗时”和“渲染链路耗时”**
   - Java 主线程卡顿
   - Binder/WMS/ATMS/Shell 耗时
   - RenderThread / GPU 耗时
   - SurfaceFlinger / HWC 耗时

---


<!-- source: 13-8.md -->

# 8. 完整跨层调用链


<!-- source: 14-81-view-property-animation.md -->

# 8.1 View / Property Animation 调用链

```
App code
  -> ValueAnimator/ObjectAnimator.start()
  -> AnimationHandler / Choreographer callback
  -> per-frame value update
  -> View property changed (alpha/translation/scale...)
  -> invalidate / RenderNode update
  -> ViewRootImpl scheduleTraversals
  -> ThreadedRenderer / RenderThread draw
  -> buffer queued
  -> SurfaceFlinger latch
  -> HWC/GPU compose
  -> display present
```

### 典型分析点

- 动画值是否每帧推进
- invalidate 是否触发
- Traversal 是否积压
- RenderThread 是否慢
- buffer 是否持续提交
- SF 展示是否跟上

------


<!-- source: 15-82-windowanimation-apptransition.md -->

# 8.2 WindowAnimation / AppTransition 调用链

```
Activity launch / finish / task switch
  -> ATMS/WMS state transition
  -> AppTransition / TransitionController decide animation type
  -> SurfaceAnimator create animation leash
  -> AnimationAdapter / SurfaceAnimationRunner start animation
  -> Transaction updates alpha/matrix/crop/position
  -> SF receives layer state
  -> composed frames shown on display
  -> animation finish callback
  -> leash removed / surface reparent back
```

### 典型分析点

- 过渡类型识别是否正确
- leash 是否创建成功
- 动画资源/adapter 是否加载成功
- startTransaction / finishTransaction 是否执行
- 结束时是否回收 leash
- 是否出现 finish 但画面未恢复

------


<!-- source: 17-84-insets-ime.md -->

# 8.4 Insets / IME 动画调用链

```
IME show/hide request
  -> InsetsSourceProvider / InsetsControlTarget
  -> control granted
  -> WindowInsetsAnimation / InsetsAnimationControlImpl
  -> surface position/alpha change
  -> app relayout / insets dispatch
  -> SF compose
  -> final insets state commit
```

### 典型分析点

- Insets control 是否授予
- IME target 是否正确
- app 是否消费 insets
- 动画与 relayout 是否冲突
- IME surface 与 app 内容是否同步移动

------


<!-- source: 18-9.md -->

# 9. 动画时序模型


<!-- source: 19-91.md -->

# 9.1 一帧动画的标准生命周期

```
1. 触发动画请求
2. 创建/选择动画对象
3. 注册到 Choreographer 或 WMS/Shell runner
4. 等待下一次 VSYNC
5. 执行动画帧计算
6. 更新 View 属性或 Surface Transaction
7. 提交渲染结果 / Layer state
8. SurfaceFlinger latch
9. HWC/GPU 合成
10. Present 到显示屏
11. 进入下一帧或结束动画
```

------


<!-- source: 20-92.md -->

# 9.2 动画“开始慢”的根因拆解

### 可能阶段

- 触发源晚
- 状态收集晚
- 动画对象创建晚
- VSYNC missed
- 主线程忙
- binder/wms/shell 处理慢
- remote animation runner 响应慢
- 首帧 buffer 准备慢
- SF 首次 latch 慢

### 判断方法

必须把“逻辑开始时间”和“用户看到首帧时间”拆开分析。

------


<!-- source: 27-11.md -->

# 11. 必须输出的分析内容

当执行本 Skill 时，输出结果至少应包含以下内容：

### 11.1 动画类型判定

明确这是哪类动画：

- View Animation
- Property Animation
- WindowAnimation
- AppTransition
- Shell Transition
- RemoteAnimation
- Insets / IME Animation
- Surface Transaction Animation
- 多种动画联动

### 11.2 触发源

明确谁发起了动画：

- App 主动调用
- 系统窗口状态变化
- Activity/Task 生命周期变化
- Shell / Launcher / SystemUI 发起
- IME / Insets 控制变化
- 配置变更 / 旋转 / 多窗口模式切换

### 11.3 跨层调用链

必须给出从触发到上屏的关键调用链。

### 11.4 时序还原

至少给出：

- 触发时间
- 首帧逻辑开始时间
- 首帧上屏时间
- 动画结束时间
- 最终状态收敛时间

### 11.5 性能判断

明确瓶颈属于：

- UI Thread
- Binder/WMS/ATMS/Shell
- RenderThread/HWUI
- GPU
- SurfaceFlinger
- Buffer/Fence
- HWC / Display

### 11.6 根因归类

必须最终归入明确根因类别，而不是停留在现象描述。

------


<!-- source: 28-12.md -->

# 12. 动画分析标准流程


<!-- source: 30-2.md -->

# 步骤 2：确认动画是否真的启动

检查：

- `start()` 是否调用
- WMS/Shell 是否进入 animation/transition play
- Choreographer animation callback 是否执行
- leash 是否创建
- transaction 是否 apply

若未启动，则优先查状态机与条件分支。

------


<!-- source: 33-5.md -->

# 步骤 5：确认结束是否收敛

检查：

- finish callback 是否触发
- leash 是否移除
- final reparent 是否正确
- 最终 bounds / alpha / position 是否恢复
- app / window / insets 最终状态是否一致

------


<!-- source: 36-132-frametimeline-jank.md -->

# 13.2 FrameTimeline / Jank 深度模型

分析动画掉帧时，必须优先构建 FrameTimeline 视角：

- Expected Timeline
- Actual Timeline
- App frame
- SF frame
- Present time
- Jank 类型

### 常见归类

- App Deadline Missed
- SF Deadline Missed
- Buffer Stuffing
- SurfaceFlinger scheduling delay
- GPU completion delay
- HWC present delay

### 输出要求

不要只说“掉帧”，必须说明：

1. 是 App jank 还是 SF jank
2. 是首帧慢还是连续帧慢
3. 是 CPU、GPU、SF、HWC 还是 Buffer/Fence 问题

------


<!-- source: 41-151.md -->

# 15.1 分析重点

- remote runner 注册是否成功
- onAnimationStart 是否触发
- target 列表是否完整
- leash 是否有效
- remote 侧是否超时
- onAnimationCancelled 是否发生
- finish callback 是否执行


<!-- source: 46-17-surface-leash-transaction.md -->

# 17. Surface / Leash / Transaction 专项体系

动画在系统层面通常最终落实为对 SurfaceControl 的 Transaction 操作。


<!-- source: 47-171.md -->

# 17.1 分析维度

- leash 创建时机
- parent/child reparent
- alpha/matrix/position/crop
- visibility/show/hide
- apply 时机
- merge transaction
- finish 后恢复原父节点


<!-- source: 50-181.md -->

# 18.1 必看轨道

- App 主线程
- RenderThread
- Choreographer / doFrame
- SurfaceFlinger
- GPU completion
- FrameTimeline
- VSYNC-app / VSYNC-sf
- binder thread
- WMS/ATMS/Shell 线程
- Transaction / BufferQueue / PresentFence（若可见）


<!-- source: 52-183.md -->

# 18.3 输出格式建议

```
动画类型：
触发源：
首帧逻辑开始：
首帧上屏：
异常帧区间：
主要瓶颈线程：
FrameTimeline 归类：
Surface / Transaction 状态：
最终根因：
优化建议：
```

------


<!-- source: 55-192.md -->

# 19.2 关注点

### dumpsys window

- 当前焦点窗口
- app transition / transition 状态
- WindowState 可见性
- Insets / IME target
- Surface shown / alpha / layer

### dumpsys activity

- ActivityRecord 状态
- resumed / paused / stopping
- task 切换时序
- 启动流程是否完成

### dumpsys SurfaceFlinger

- layer 树
- 可见层数量
- z-order
- geometry / alpha / crop
- pending transaction
- buffer state

### dumpsys gfxinfo

- janky frames
- frame time bucket
- draw/layout/sync/process 耗时

------


<!-- source: 62-4.md -->

# 4. 关键时序
- 触发时间：
- 首帧逻辑开始：
- 首帧上屏：
- 异常区间：
- 动画结束：
- 最终收敛：


<!-- source: 67-22.md -->

# 22. 专家级输出要求

执行本 Skill 时，输出必须达到以下标准：

1. **必须给出完整跨层链路，而不是只分析单类源码**
2. **必须给出时序图或文字化时序**
3. **必须说明动画何时开始、何时上屏、何时结束**
4. **必须区分逻辑执行与视觉呈现**
5. **必须指出关键对象、关键状态位、关键 transaction**
6. **必须把现象归约到明确根因**
7. **必须给出可执行优化建议**
8. **若证据不足，必须明确缺失哪些证据，而不是猜测**

------


<!-- source: 72-234-surface-sf.md -->

# 23.4 Surface / SF 类

1. Transaction apply 晚于 VSYNC
2. buffer 未及时提交导致空帧
3. SF latch 不连续导致卡顿
4. presentFence 延迟导致动画显示慢
5. 图层被遮挡误判为“动画没执行”


<!-- source: 78-243-window-shell.md -->

# 24.3 Window / Shell 优化

- 缩短 transition collect/ready 时间
- 避免 remote animation 侧做重初始化
- 保证 finish callback 及时
- 严格校验 leash 生命周期


<!-- source: 81-25-skill.md -->

# 25. 与其他 Skill 的协同策略

### 与 `aosp-wms`

当问题集中在窗口状态、可见性、层级、窗口容器流转时联动。

### 与 `aosp-graphics`

当问题集中在 HWUI / RenderThread / GPU / 合成路径时联动。

### 与 `aosp-SurfaceControl`

当问题集中在 leash、Transaction、BLAST、layer 操作时联动。

### 与 `aosp-SurfaceFlinger`

当问题集中在 SF 合成、latch、present、display path 时联动。

### 与 `aosp-VSYNC`

当问题集中在 VSYNC、帧驱动、FrameTimeline、节奏不稳时联动。

### 与 `aosp-input`

当问题集中在手势触发慢、点击后动画晚起时联动。

### 与 `aosp-ams`

当问题集中在启动流程、Activity 生命周期衔接不顺时联动。

------
