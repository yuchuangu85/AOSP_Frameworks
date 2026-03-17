# 架构与核心机制
<!-- source: 02-1.md -->

# 1. 目标定位

该 Skill 专门用于分析 Android / AOSP 中各类 **Transaction（事务）** 机制，重点不是“数据库事务”，而是系统框架与图形栈里的“状态批处理提交模型”。  
在 Android 里，Transaction 是多个关键子系统的核心组织方式之一，常用于：

- **窗口状态变更批量提交**
- **图层属性更新合并提交**
- **应用生命周期消息分发**
- **Shell / WM 层窗口组织与转场编排**
- **SurfaceFlinger 图层树状态原子更新**
- **BLAST / BufferState 模型下的 buffer + state 同步提交**

该 Skill 的目标是：  
当用户要求分析 AOSP Transaction 相关源码、架构、时序、异常行为或性能问题时，能够自动建立完整的事务模型，给出**源码依据、跨层调用链、设计思想、时序图、关键类职责、问题定位路径与优化建议**。

---


<!-- source: 03-2.md -->

# 2. 适用场景

当出现以下需求时应调用本 Skill：

1. 分析 AOSP 中 Transaction 的设计思想与实现机制
2. 分析以下任一事务体系：
   - **ClientTransaction**
   - **SurfaceControl.Transaction**
   - **WindowContainerTransaction**
   - **SurfaceFlinger Transaction**
   - **LayerState / ComposerState / DisplayState**
   - **BLASTBufferQueue Transaction**
3. 分析窗口切换、转场、层属性更新为何未立即生效
4. 分析事务为何被合并、延迟、覆盖、丢弃或重排序
5. 分析事务提交过程中 binder、handler、vsync、sf commit 的参与关系
6. 分析应用界面更新异常，如：
   - 位置/尺寸变化未生效
   - alpha/crop/matrix/z-order 异常
   - layer 突变、闪屏、黑屏
   - transition 卡住
   - reparent 后显示异常
7. 分析事务性能问题：
   - 频繁 apply 导致卡顿
   - 小事务过多导致 binder / sf 压力大
   - sync transaction 等待过久
   - BLAST 提交与 buffer latch 不一致
8. 构建事务相关源码知识图谱与学习路径

---


<!-- source: 04-3.md -->

# 3. 分析范围

本 Skill 覆盖以下核心 Transaction 体系：

### 3.1 App Framework / 生命周期事务
- `ClientTransaction`
- `ClientTransactionItem`
- `TransactionExecutor`
- `LifecycleStateRequest`
- `ActivityLifecycleItem`
- `LaunchActivityItem`
- `ResumeActivityItem`
- `PauseActivityItem`
- `TopResumedActivityChangeItem`

### 3.2 WindowManager / Shell 事务
- `WindowContainerTransaction`
- `WindowOrganizer`
- `TaskOrganizer`
- `DisplayAreaOrganizer`
- `Transition`
- `TransitionController`
- `SyncTransactionQueue`
- `WindowContainer`
- `Task`
- `TaskFragment`

### 3.3 SurfaceControl 图形事务
- `SurfaceControl.Transaction`
- `SurfaceControl`
- `AttachedSurfaceControl`
- `SurfaceView`
- `BLASTBufferQueue`
- `SurfaceSyncGroup`
- `TransactionCommittedListener`
- `TransactionCompletedListener`

### 3.4 SurfaceFlinger 事务
- `SurfaceComposerClient::Transaction`
- `layer_state_t`
- `ComposerState`
- `DisplayState`
- `TransactionState`
- `TransactionHandler`
- `SurfaceFlinger::setTransactionState`
- `SurfaceFlinger::flushTransactions`
- `commitTransactions`
- `applyTransactions`
- `LatchUnsignaledConfig`
- `FrameTimeline`

### 3.5 Buffer / BLAST 协同事务
- `BufferStateLayer`
- `BufferQueueLayer`
- `BLASTBufferQueue`
- buffer + state 同步提交
- acquire fence / present fence 与 transaction 生效关系

---


<!-- source: 05-4.md -->

# 4. 分析输出要求

每次执行本 Skill 时，输出内容必须尽量覆盖以下五个维度：

### 4.1 架构设计思想
说明该 Transaction 机制为什么存在、解决了什么问题、采用什么抽象模型、相对直接调用的优势是什么。

### 4.2 核心角色与职责划分
明确关键类、关键数据结构、关键线程、关键 binder 接口、关键锁、关键时机。

### 4.3 跨层调用链
从发起者到接收者建立完整链路，标明：
- 谁创建 transaction
- 谁追加 operation / state
- 谁 apply / schedule
- 谁接收 binder
- 谁在主线程 / sf 线程 / binder 线程执行
- 谁最终使状态生效

### 4.4 时序图
至少输出一份事务时序图，必要时分场景输出：
- App lifecycle transaction
- Surface transaction
- WM transition transaction
- SF commit transaction

### 4.5 源码证据与问题定位
必须引用关键类、关键方法、关键字段、关键状态位，说明“从哪里可以验证结论”。

---


<!-- source: 09-62-windowcontainertransaction.md -->

# 6.2 WindowContainerTransaction 分析模型

### 6.2.1 设计目标

`WindowContainerTransaction` 用于 **Shell / WM 对 Task / DisplayArea / TaskFragment / WindowContainer 做批量结构变更**。

适用于：

- 多窗口组织
- 分屏/自由窗变更
- task reparent / reorder / bounds 更新
- Shell Transition 中结构调整

### 6.2.2 关键类

- `WindowContainerTransaction`
- `WindowOrganizer`
- `TaskOrganizer`
- `TaskFragmentOrganizer`
- `WindowOrganizerController`
- `WindowContainer`
- `Transition`
- `SyncEngine`
- `BLASTSyncEngine`

### 6.2.3 核心调用链

```
Shell / Organizer
  -> build WindowContainerTransaction
  -> WindowOrganizer.applyTransaction()
  -> binder to system_server
  -> WindowOrganizerController.applyTransaction()
  -> applyHierarchyOp / applyChange
  -> WindowManagerService / ATMS mutate hierarchy
  -> request traversal / transition / sync
```

### 6.2.4 典型操作

- setBounds
- setHidden
- reorder
- reparent
- setWindowingMode
- setFocusable
- setAdjacentRoots
- task fragment operation

### 6.2.5 时序图

```
Shell             WindowOrganizer      system_server            WMS/ATMS             Transition
 |                       |                  |                      |                     |
 | create WCT            |                  |                      |                     |
 | applyTransaction()    |                  |                      |                     |
 |---------------------->| binder           |                      |                     |
 |                       |----------------->|                      |                     |
 |                       |                  | applyHierarchyOp     |                     |
 |                       |                  |--------------------->|                     |
 |                       |                  | requestTransition?   |-------------------->|
 |                       |                  | sync/traversal       |                     |
```

### 6.2.6 重点分析项

- transaction 中包含哪些 `Change` 与 `HierarchyOp`
- 是否触发 transition
- 是否进入 sync group
- bounds 改动与 surface transaction 是否一并收敛
- 结构变化是否先于显示变化生效

### 6.2.7 常见异常

- Task bounds 改了但画面未同步更新
- reparent 后短暂黑屏
- transition finish 前状态不一致
- sync transaction 卡住导致界面不刷新
- 多个 WCT 相互覆盖

------


<!-- source: 10-63-surfacecontroltransaction.md -->

# 6.3 SurfaceControl.Transaction 分析模型

### 6.3.1 设计目标

`SurfaceControl.Transaction` 用于 **批量更新一个或多个 Surface/Layer 的可见属性**，例如：

- position
- alpha
- crop
- matrix
- reparent
- show/hide
- z order
- buffer
- color
- frame rate vote
- metadata

其核心思想是：
 **把 Layer 状态修改从“立即调用立即生效”改为“先收集、后统一提交”。**

### 6.3.2 关键类

- `SurfaceControl`
- `SurfaceControl.Transaction`
- `SurfaceComposerClient::Transaction`
- `layer_state_t`
- `SurfaceControlRegistry`
- `AttachedSurfaceControl`
- `TransactionCompletedListener`

### 6.3.3 Java -> Native -> SF 调用链

```
App / ViewRootImpl / SurfaceView / Shell
  -> SurfaceControl.Transaction
  -> nativeApplyTransaction()
  -> SurfaceComposerClient::Transaction::apply()
  -> ISurfaceComposer::setTransactionState()
  -> SurfaceFlinger::setTransactionState()
  -> transaction queue
  -> commit/apply at SF phase
  -> layer tree updated
```

### 6.3.4 时序图

```
App Thread          JNI/Native            Binder             SurfaceFlinger
 |                     |                    |                       |
 | new Transaction     |                    |                       |
 | setPosition/Alpha   |                    |                       |
 | setCrop/Reparent    |                    |                       |
 | apply()             |                    |                       |
 |-------------------->| nativeApply        |                       |
 |                     | setTransactionState|                       |
 |                     |------------------->|                       |
 |                     |                    | enqueue transaction   |
 |                     |                    |---------------------->|
 |                     |                    |  commit/apply         |
 |                     |                    |---------------------->|
 |                     |                    |  layer state visible  |
```

### 6.3.5 必看方法

Java 层常见入口：

- `Transaction.setPosition`
- `Transaction.setAlpha`
- `Transaction.setMatrix`
- `Transaction.setWindowCrop`
- `Transaction.reparent`
- `Transaction.show`
- `Transaction.hide`
- `Transaction.setBuffer`
- `Transaction.apply`
- `Transaction.apply(boolean sync)`

Native / SF 关键路径：

- `SurfaceComposerClient::Transaction::apply`
- `ComposerState`
- `layer_state_t::what`
- `ISurfaceComposer::setTransactionState`
- `SurfaceFlinger::setTransactionState`
- `applyTransactions`
- `commitTransactionsLocked`

### 6.3.6 重点分析项

- 一个 transaction 内改了哪些 layer state
- `what` 位图记录了哪些字段变更
- apply 是同步还是异步
- 是否 merge 到其他 transaction
- 与 buffer latch 的相对顺序
- 是否等待 fence / vsync / transaction callback

### 6.3.7 常见异常

- setPosition 了但画面未动
- setAlpha / hide 生效延迟一帧或多帧
- reparent 后层级异常
- crop 与 matrix 组合导致内容显示不正确
- 事务过多造成 binder 压力
- apply 顺序竞争导致最终状态不是预期值

------


<!-- source: 12-65-blast-bufferstate.md -->

# 6.5 BLAST / BufferState 事务模型

### 6.5.1 设计目标

BLAST 的核心价值是将：

- buffer 提交
- 尺寸/裁剪/位置等 surface state 更新
   统一纳入更稳定的一致性提交模型中，减少 resize、旋转、窗口动画时的中间态问题。

### 6.5.2 关键类

- `BLASTBufferQueue`
- `BufferStateLayer`
- `SurfaceControl.Transaction`
- `SurfaceSyncGroup`
- `TransactionReadyCallback`

### 6.5.3 重点关注

- dequeue/queue buffer 与 apply transaction 是否配套
- resize transaction 是否与新 buffer 同步
- frame 到达晚于 transaction 时如何处理
- blast sync 是否等待所有 participant
- callback 是 committed 还是 presented

### 6.5.4 常见异常

- resize 先到了，buffer 没到，导致拉伸/黑边
- buffer 先到了，crop/position 晚到，导致跳变
- sync group 卡住，transition finish 不返回
- buffer/state 不一致造成闪烁

------


<!-- source: 13-7-transaction.md -->

# 7. Transaction 架构图


<!-- source: 14-71.md -->

# 7.1 总体架构图

```
+--------------------------------------------------------------+
|                          App / Shell                         |
|--------------------------------------------------------------|
| ClientTransaction | WindowContainerTransaction | SC.Transaction |
+-----------------------------|-------------------------------+
                              |
                              v
+--------------------------------------------------------------+
|                    Framework / System Server                 |
|--------------------------------------------------------------|
| ActivityThread / TransactionExecutor                         |
| ATMS / WMS / WindowOrganizerController / TransitionController |
+-----------------------------|-------------------------------+
                              |
                              v
+--------------------------------------------------------------+
|                    Native / Surface Composer                 |
|--------------------------------------------------------------|
| SurfaceComposerClient::Transaction                           |
| ComposerState / DisplayState / layer_state_t                 |
+-----------------------------|-------------------------------+
                              |
                              v
+--------------------------------------------------------------+
|                        SurfaceFlinger                        |
|--------------------------------------------------------------|
| Transaction queue | merge | commit | latch | compose | present |
+-----------------------------|-------------------------------+
                              |
                              v
+--------------------------------------------------------------+
|                       HWC / DRM / Display                    |
+--------------------------------------------------------------+
```

------


<!-- source: 16-81-surface.md -->

# 8.1 Surface 属性更新调用链

```
ViewRootImpl / SurfaceView / Shell
  -> SurfaceControl.Transaction.setXxx()
  -> Transaction.apply()
  -> JNI nativeApplyTransaction
  -> SurfaceComposerClient::Transaction::apply
  -> ISurfaceComposer::setTransactionState
  -> SurfaceFlinger::setTransactionState
  -> queue transaction
  -> applyTransactions / commit
  -> Layer state updated
  -> composition
  -> HWC present
```

------


<!-- source: 20-10.md -->

# 10. 必查源码清单

以下是分析 AOSP Transaction 时建议优先阅读的源码入口。

### 10.1 Framework / App

- `android/app/servertransaction/ClientTransaction.java`
- `android/app/servertransaction/TransactionExecutor.java`
- `android/app/ActivityThread.java`

### 10.2 Window / Shell

- `android/window/WindowContainerTransaction.java`
- `android/window/WindowOrganizer.java`
- `com/android/server/wm/WindowOrganizerController.java`
- `com/android/server/wm/Transition.java`
- `com/android/server/wm/TransitionController.java`

### 10.3 SurfaceControl Java

- `android/view/SurfaceControl.java`
- `android/view/AttachedSurfaceControl.java`
- `android/view/SurfaceView.java`
- `android/window/SurfaceSyncGroup.java`

### 10.4 Native / SF

- `frameworks/native/libs/gui/SurfaceComposerClient.cpp`
- `frameworks/native/libs/gui/include/gui/LayerState.h`
- `frameworks/native/services/surfaceflinger/SurfaceFlinger.cpp`
- `frameworks/native/services/surfaceflinger/FrontEnd/*`
- `frameworks/native/services/surfaceflinger/BufferStateLayer*`

### 10.5 BLAST

- `frameworks/native/libs/gui/BLASTBufferQueue.cpp`
- `frameworks/base/core/java/android/view/BLASTBufferQueue.java`（若版本存在）
- `SurfaceSyncGroup` 相关实现

------


<!-- source: 24-131-apply.md -->

# 13.1 “apply 了但没生效”

可能原因：

- 后续 transaction 覆盖
- transaction 只提交到 SF，尚未 present
- 相关 layer 不可见
- parent 改变导致实际显示树不同
- buffer 未同步到位

------


<!-- source: 33-17.md -->

# 17. 推荐回答风格

在实际回答中，应尽量采用以下风格：

- 先给结论，再展开论证
- 先给事务分类，再给链路
- 先给架构，再讲代码细节
- 先解释“为什么这样设计”，再解释“代码怎么实现”
- 所有关键判断都要指出源码位置与运行时验证点

------


<!-- source: 34-18.md -->

# 18. 一份标准示例结论模板

```
这是一个 SurfaceControl.Transaction + SurfaceFlinger transaction 的联动问题，不是 WCT 或 ClientTransaction 问题。
根因在于应用侧 transaction 已经 apply，但 SurfaceFlinger 侧真正 commit/present 晚于预期，同时后续 transaction 覆盖了前一个位置更新，导致表现为“setPosition 未生效”。
关键证据应从三处确认：
1. Java 层 Transaction.setPosition / apply 调用路径
2. Native 层 SurfaceComposerClient::Transaction::apply 提交内容
3. SurfaceFlinger 侧 transaction queue、commit 时机以及最终 layer state
```

------


<!-- source: 36-20.md -->

# 20. 结束语

`Transaction` 是 Android 系统里最核心的“批处理状态变更模型”之一。
 无论是 Activity 生命周期、窗口组织、图层更新，还是 SurfaceFlinger 提交，本质上都离不开 transaction 思维：

- **先描述变更**
- **再批量提交**
- **在正确时机统一生效**
- **尽可能保持原子性与一致性**

理解 Transaction，基本就打开了理解 Android Framework、WindowManager、SurfaceControl、SurfaceFlinger、BLAST、Transition 等核心机制的大门。
