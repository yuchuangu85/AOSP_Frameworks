# 调用链与时序
<!-- source: 08-43-surface.md -->

# 4.3 Surface / 图形侧

- `SurfaceControl`
- `SurfaceControl.Transaction`
- `TransitionInfo`
- `TransitionInfo.Change`
- `BLASTSyncEngine`
- `BLASTBufferQueue`
- `SurfaceAnimator`
- `SurfaceFreezer`
- `AnimationAdapter`
- `WindowAnimationSpec`
- `SurfaceAnimationRunner`


<!-- source: 10-45-surfaceflinger.md -->

# 4.5 SurfaceFlinger / 合成侧

- `SurfaceFlinger`
- `TransactionState`
- `Layer`
- `LayerState`
- `LayerLifecycleManager`
- `FrameTimeline`
- `CompositionEngine`

---


<!-- source: 14-6.md -->

# 6. 完整跨层调用链


<!-- source: 15-61-activity.md -->

# 6.1 Activity 启动 / 切换通用调用链

```text
App / Launcher
  ↓
startActivity / activity launch / task switch
  ↓
ATMS / ActivityStartController / RootWindowContainer
  ↓
ActivityRecord / Task / DisplayContent 状态变更
  ↓
TransitionController.requestStartTransitionIfNeeded
  ↓
Transition 创建 / collect() 收集参与容器
  ↓
WindowContainer 层级、visible、bounds、mode 变化
  ↓
Transition.setReady / BLASTSyncEngine 同步
  ↓
TransitionController.finishTransitionReady
  ↓
WindowOrganizerController / Shell Transitions 分发 TransitionInfo
  ↓
DefaultTransitionHandler / RemoteTransitionHandler 选择执行器
  ↓
创建 animation leash / SurfaceControl.Transaction
  ↓
position / crop / alpha / layer / show/hide / reparent
  ↓
SurfaceFlinger 接收 transaction
  ↓
Layer tree 更新 / 合成 / present
  ↓
动画结束回调
  ↓
Transition finish / cleanup / release leash / 最终状态收敛
```


<!-- source: 16-62-apptransition.md -->

# 6.2 旧式 AppTransition 调用链

```
Activity/Task 状态切换
  ↓
AppTransitionController.handleAppTransitionReady
  ↓
AppTransition.goodToGo
  ↓
WindowContainer.applyAnimation
  ↓
SurfaceAnimator.startAnimation
  ↓
AnimationAdapter / WindowAnimationSpec
  ↓
SurfaceAnimationRunner
  ↓
SurfaceControl.Transaction 应用
  ↓
SurfaceFlinger 合成显示
```

------


<!-- source: 17-63-shelltransition.md -->

# 6.3 ShellTransition 调用链

```
状态变化发生
  ↓
TransitionController.createTransition
  ↓
Transition.collect(windowContainer)
  ↓
Transition.start()
  ↓
Transition ready 条件满足
  ↓
Transitions.onTransitionReady(token, info, startT, finishT)
  ↓
匹配 handler:
  - DefaultTransitionHandler
  - RemoteTransitionHandler
  - RecentsTransitionHandler
  - PipTransition
  - SplitScreenTransitions
  ↓
handler.startAnimation(...)
  ↓
创建 leash / 操作 transaction / 启动 animator
  ↓
动画播放
  ↓
finishCallback.onTransitionFinished()
  ↓
TransitionController.finishTransition
  ↓
状态最终提交 / 资源清理
```

------


<!-- source: 18-64-remoteanimation-remotetransition.md -->

# 6.4 RemoteAnimation / RemoteTransition 调用链

```
调用方设置 ActivityOptions / RemoteAnimationAdapter / RemoteTransition
  ↓
系统识别本次 transition 需要远程接管
  ↓
WMS / Shell 构造 RemoteAnimationTarget 或 TransitionInfo.Change
  ↓
跨进程分发给 Launcher / SystemUI / Shell external runner
  ↓
远程侧根据 leash 执行动画
  ↓
完成后回调 finish
  ↓
WMS / Shell 执行最终 cleanup
```

------


<!-- source: 19-7.md -->

# 7. 关键时序模型


<!-- source: 20-71-transition.md -->

# 7.1 通用 Transition 时序图

```
参与者:
App / Launcher
ATMS
WMS
TransitionController
Transition
Shell Transitions
SurfaceFlinger

1. App/Launcher 发起启动、返回、切换请求
2. ATMS 改变 ActivityRecord / Task 状态
3. WMS 感知 WindowContainer 可见性/层级变化
4. TransitionController 创建 Transition
5. Transition.collect() 收集变化对象
6. 相关容器继续变更，Transition 内部记录 ChangeInfo
7. BLASTSyncEngine / ready condition 等待同步点
8. Transition ready
9. Shell 收到 TransitionInfo + startT + finishT
10. handler 创建 leash 并应用起始 transaction
11. animator 按时间推进 alpha/position/crop/matrix
12. SF 合成并 present
13. 动画结束
14. finish transaction 应用最终状态
15. Transition finish，释放 leash，状态收敛
```

------


<!-- source: 26-82-transition.md -->

# 8.2 Transition

### 职责

- 表示一次完整过渡
- 记录参与对象与变化类型
- 管理 start/finish transaction
- 协调动画执行生命周期

### 分析重点

- `collect()`
- `setReady()`
- `buildTransitionInfo()`
- `calculateTargets()`
- `finishTransition()`
- change 类型判定逻辑

### 重点看

- opening / closing / changing 如何判断
- parent / leash / root leash 如何建立
- flags 与 mode 如何映射到实际动画行为

------


<!-- source: 28-84-surfaceanimator-animationadapter-surfaceanimationrunner.md -->

# 8.4 SurfaceAnimator / AnimationAdapter / SurfaceAnimationRunner

### 职责

- 将逻辑动画映射到 SurfaceControl 操作
- 创建与管理 animation leash
- 推动每帧 transaction 应用

### 分析重点

- leash 创建与 parent 选择
- transaction 应用对象
- cancel / transfer / reset 行为
- finish callback 触发时机

### 典型问题

- leash 泄露
- 动画结束后 layer 没有恢复
- alpha 卡在中间态
- reparent 后层级错乱

------


<!-- source: 29-85-blastsyncengine-blastbufferqueue.md -->

# 8.5 BLASTSyncEngine / BLASTBufferQueue

### 职责

- 管理过渡中的同步提交
- 等待相关窗口准备完成后统一推进

### 分析重点

- sync group 创建时机
- ready 条件依赖哪些成员
- buffer 未到时是否阻塞 transition
- finish 与 commit 时序

### 典型问题

- transition ready 晚
- 动画开始晚于用户操作
- 首帧与动画不同步
- 黑屏一小段时间后才开始动画

------


<!-- source: 30-86-shell-transitions.md -->

# 8.6 Shell Transitions

### 关键类

- `Transitions`
- `DefaultTransitionHandler`
- `RemoteTransitionHandler`

### 分析重点

- 哪个 handler 接管了动画
- handler 匹配规则
- startT / finishT 的使用方式
- handler finish callback 是否可靠

### 典型问题

- 动画根本没进 DefaultTransitionHandler
- 远程 handler 未回调 finish
- handler 冲突导致 fallback 异常
- split/pip/recents 特化路径覆盖通用路径

------


<!-- source: 31-87-remoteanimation-remotetransition.md -->

# 8.7 RemoteAnimation / RemoteTransition

### 分析重点

- runner / remoteTransition 注册来源
- target 构造是否完整
- target leash 是否有效
- remote 端 finish 是否正常
- binder 断连/超时/cancel 路径

### 典型问题

- 动画卡死不结束
- 窗口已经切换但动画层还在
- 远程动画 target 缺失
- Launcher/SystemUI 与 WMS 时序竞争

------


<!-- source: 34-92.md -->

# 9.2 再确认“谁是状态源，谁是动画执行者”

Transition 里常见的两个层面：

### 状态源

- ATMS / WMS / WindowContainer 层状态变化

### 动画执行者

- SurfaceAnimator
- Shell DefaultTransitionHandler
- RemoteTransition/RemoteAnimation
- Launcher/SystemUI

要明确：

- 是 WMS 主导，还是 Shell 主导？
- 是本地动画，还是远程动画？
- 最终 transaction 是谁 apply 的？

------


<!-- source: 40-111.md -->

# 11.1 关注轨道

- `wm` / `window manager`
- `shell transitions`
- `surfaceflinger`
- `frame timeline`
- `transactions`
- `binder transactions`
- `layers`
- `app main thread`
- `render thread`
- `hwui`
- `vsync-app`
- `vsync-sf`
- `bufferqueue`
- `present fence`

------


<!-- source: 41-112.md -->

# 11.2 关键观察点

- 输入事件到 transition start 的时间
- transition ready 延迟
- 动画真正开始的时间
- app 首帧提交时间
- SF transaction 生效时间
- present 时间
- finish callback 时间
- 整体切换时长
- 是否与 jank/frame miss 对齐

------


<!-- source: 42-113-transition-trace.md -->

# 11.3 Transition 类问题 trace 判断规则

### 黑屏

通常检查：

- opening app surface 未及时 show
- starting window 提前移除
- leash 存在但无 buffer
- finish transaction 提前提交导致中间层消失

### 闪屏

通常检查：

- starting window 与真实首帧切换不平滑
- 背景色不一致
- alpha/crop 切换突变
- old/new layer z-order 短暂颠倒

### 动画卡顿

通常检查：

- 远程动画在 Launcher 线程阻塞
- app/renderthread 忙导致目标画面更新慢
- SF transaction 批量堆积
- finish callback 晚到

### 跳变

通常检查：

- 起始矩阵或 bounds 计算错误
- snapshot / live surface 切换不连续
- reparent 时机错误
- leash parent 不正确

------


<!-- source: 50-133.md -->

# 13.3 卡顿类

1. Launcher 主线程忙导致 remote animation 卡顿
2. app 首帧慢，动画播完内容还没出来
3. SF transaction 堵塞，transaction apply 晚
4. binder 回调链过长导致 startAnimation 晚
5. display change 与 task change 合并过大
6. merge transition 导致 ready 点后移
7. finish callback 晚于视觉动画结束


<!-- source: 54-15.md -->

# 15. 回答风格要求

回答必须遵循以下原则：

- **先建立结构，再解释细节**
- **所有结论必须能落到源码类、方法、状态或运行时证据**
- **禁止只讲概念，不讲调用链**
- **禁止只贴源码，不解释设计意图**
- **禁止只说“可能”，必须给出判断依据与排除依据**
- **必须区分旧式 AppTransition 与新式 ShellTransition**
- **必须区分逻辑状态变化与图形显示变化**
- **必须解释 leash / transaction / visible / draw / present 之间关系**

------


<!-- source: 59-20.md -->

# 20. 一句话能力总结

该技能用于把 Android/AOSP 的 Transition 问题，从 **“一个界面切换现象”** 拆解为 **“状态收集 → 同步就绪 → 动画接管 → Transaction 提交 → SurfaceFlinger 合成 → 最终状态收敛”** 的完整系统级模型，并基于源码与运行时证据给出可验证结论。
