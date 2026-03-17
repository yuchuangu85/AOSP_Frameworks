# 调用链与时序
<!-- source: 06-5-transaction.md -->

# 5. Transaction 的统一抽象模型

在 AOSP 中，可将 Transaction 统一建模为：

```text
事务发起方
  -> 构造事务对象
  -> 填充多项变更操作（op/state）
  -> 选择 apply / schedule / sync / merge
  -> 通过 binder / handler / queue 发送
  -> 接收方合并、校验、排序
  -> 在特定时机统一提交
  -> 下游模块原子生效
  -> 监听 committed / completed / presented 结果
```

其核心价值：

- **批量化**：多个状态一次提交，减少频繁跨进程/跨线程操作
- **原子性**：同一帧或同一提交点内统一生效，避免中间态可见
- **可合并**：重复更新可以压缩
- **可延迟**：配合 vsync / transition / sync group 在合适时间应用
- **可追踪**：便于建立 frame、transition、window change 的一致性

------


<!-- source: 07-6-transaction.md -->

# 6. 必须掌握的 Transaction 分类

------


<!-- source: 08-61-clienttransaction.md -->

# 6.1 ClientTransaction 分析模型

### 6.1.1 设计目标

`ClientTransaction` 用于 **AMS/ATMS 向应用进程调度 Activity / Client 生命周期和回调消息**。

它解决的问题是：

- 生命周期切换不是零散调用，而是可批量组织
- callback 与 lifecycle request 可组合
- 客户端统一由 `TransactionExecutor` 执行，保证顺序与状态一致性

### 6.1.2 关键类

- `ClientTransaction`
- `IApplicationThread`
- `ClientLifecycleManager`
- `TransactionExecutor`
- `ClientTransactionItem`
- `ActivityLifecycleItem`
- `ActivityThread.H`

### 6.1.3 核心调用链

```
AMS/ATMS
  -> ClientLifecycleManager.scheduleTransaction()
  -> ClientTransaction
  -> IApplicationThread.scheduleTransaction()
  -> ApplicationThread (binder)
  -> ActivityThread.scheduleTransaction()
  -> sendMessage(H.EXECUTE_TRANSACTION)
  -> TransactionExecutor.execute()
  -> executeCallbacks()
  -> executeLifecycleState()
```

### 6.1.4 时序图

```
ATMS                App Binder Thread         ActivityThread/H           TransactionExecutor
 |                         |                        |                           |
 | build ClientTransaction |                        |                           |
 |------------------------>|                        |                           |
 | scheduleTransaction     |                        |                           |
 |------------------------>|                        |                           |
 |                         | ActivityThread.scheduleTransaction()               |
 |                         |----------------------->|                           |
 |                         |                        | send H.EXECUTE_TRANSACTION|
 |                         |                        |-------------------------->|
 |                         |                        |                           | executeCallbacks
 |                         |                        |                           | executeLifecycleState
 |                         |                        |                           | lifecycle state changed
```

### 6.1.5 重点分析项

- callback 与 lifecycle request 顺序
- 事务是否包含多个 item
- 是否被 `ActivityThread` 异步转入主线程
- 是否存在状态跳跃执行
- 是否与 pause/resume/stop/relaunch 并发交错

### 6.1.6 常见异常

- 生命周期顺序异常
- launch 后立刻 pause/stop
- transaction 到达但未及时执行
- 主线程阻塞导致事务积压
- 配置变更/relaunch transaction 交织引起闪屏

------


<!-- source: 11-64-surfaceflinger-transaction.md -->

# 6.4 SurfaceFlinger Transaction 分析模型

### 6.4.1 设计目标

SurfaceFlinger 侧负责将来自多个进程的事务统一收敛、排序并在合适的提交点作用到 Layer 树与 Display 状态上。

### 6.4.2 关键对象

- `TransactionState`
- `ComposerState`
- `DisplayState`
- `Layer`
- `BufferStateLayer`
- `FrontEnd / LayerSnapshot`
- `TransactionHandler`
- `FrameTimeline`
- `Scheduler`

### 6.4.3 基本流程

```
Client process
  -> binder setTransactionState
  -> SurfaceFlinger receives TransactionState
  -> queue/merge transactions
  -> check transaction readiness
  -> latch buffer/state if ready
  -> commit layer state
  -> composition planning
  -> HWC / RenderEngine
  -> present
```

### 6.4.4 核心问题

- 事务何时进入 queue
- 是否可以立即 commit
- 是否受 buffer readiness 限制
- 是否和 frame timeline / vsync 对齐
- 是否在同一帧内和其他 layer transaction 合并
- transaction committed 与 frame presented 有什么差异

### 6.4.5 Transaction 生效的关键时机

必须区分以下几个概念：

1. **binder 收到 transaction**
2. **transaction 被 SF 接纳**
3. **transaction committed**
4. **layer state 对 composition 生效**
5. **frame presented 到屏幕**

这些不是同一时刻。很多“事务没生效”的问题，本质上是混淆了这几个时间点。

### 6.4.6 常见异常

- transaction 已提交但屏幕尚未 present
- 有 state 无 buffer，或有 buffer 无 state
- 某事务等待同步组导致整体延后
- 被后续事务覆盖，表面看像“丢失”
- 前一帧未完成导致当前 transaction 继续堆积

------


<!-- source: 15-8.md -->

# 8. 跨层调用链模板

------


<!-- source: 17-82-shell.md -->

# 8.2 Shell 转场调用链

```
Shell Transition
  -> build WindowContainerTransaction
  -> WindowOrganizer.applyTransaction
  -> WindowOrganizerController.applyTransaction
  -> WMS/ATMS mutate hierarchy
  -> collect transition participants
  -> SyncTransactionQueue / BLASTSyncEngine
  -> SurfaceControl.Transaction apply
  -> SurfaceFlinger commit
  -> transition animation frame displayed
```

------


<!-- source: 18-83-activity.md -->

# 8.3 Activity 生命周期事务调用链

```
ATMS
  -> ClientLifecycleManager.scheduleTransaction
  -> IApplicationThread.scheduleTransaction
  -> ActivityThread.scheduleTransaction
  -> H.EXECUTE_TRANSACTION
  -> TransactionExecutor.execute
  -> executeCallbacks
  -> executeLifecycleState
```

------


<!-- source: 19-9.md -->

# 9. 源码分析方法论

分析 Transaction 时，必须遵循以下顺序。

### 9.1 先识别事务类型

先判断用户说的“transaction”是哪一种：

- 生命周期事务
- 窗口结构事务
- surface 图层事务
- sf 原生事务
- blast sync 事务

不能把它们混为一谈。

### 9.2 找入口

寻找 transaction 创建点：

- `new ClientTransaction`
- `new WindowContainerTransaction`
- `new SurfaceControl.Transaction`
- native `Transaction()`

### 9.3 找操作填充点

观察事务中追加了哪些操作：

- callback item
- hierarchy op
- state change
- layer_state_t what bits

### 9.4 找提交点

识别真正提交点：

- `scheduleTransaction`
- `applyTransaction`
- `apply()`
- `setTransactionState`

### 9.5 找生效点

提交不等于生效。必须追踪到：

- `TransactionExecutor.execute`
- `WindowContainer` 结构变更完成
- `SurfaceFlinger commit/apply`
- `present fence`

### 9.6 找覆盖/合并点

若结果与预期不符，检查：

- merge
- coalesce
- overwrite
- sync wait
- frame miss
- later transaction override

------


<!-- source: 21-11.md -->

# 11. 必问问题模板

当用户请求分析 Transaction 问题时，应优先回答以下问题：

1. 这是哪一种 transaction？
2. transaction 在哪里创建？
3. transaction 内到底改了哪些状态？
4. 谁调用了 apply / schedule？
5. apply 后是否真的到达接收方？
6. 接收方是立即执行还是排队？
7. 是否发生 merge / overwrite / defer？
8. 生效点在什么线程、什么阶段？
9. 生效是否受 buffer / fence / vsync 限制？
10. 有没有更晚的 transaction 覆盖前者？
11. 用户看到的异常是“未提交”还是“已提交未显示”？
12. 性能瓶颈在构造、binder、queue、commit 还是 present？

------


<!-- source: 22-12.md -->

# 12. 标准输出模板

执行本 Skill 时，建议使用如下输出结构：

### 12.1 问题定义

- 用户要分析的 transaction 类型
- 涉及模块
- 目标问题现象

### 12.2 结论摘要

- 一句话给出本次分析结论
- 指出关键根因与关键证据点

### 12.3 架构设计思想

- 为什么存在该 transaction
- 解决什么问题
- 设计收益与代价

### 12.4 核心类职责表

| 类/模块                                  | 角色      | 关键职责         |
| ---------------------------------------- | --------- | ---------------- |
| ClientTransaction / SC.Transaction / WCT | 事务载体  | 保存待提交操作   |
| 发起方                                   | producer  | 构建事务         |
| binder/controller                        | transport | 跨进程传输       |
| executor/SF                              | consumer  | 执行事务         |
| callback/fence                           | feedback  | 通知事务完成情况 |

### 12.5 跨层调用链

按源码路径列出完整调用链。

### 12.6 时序图

画出 transaction 创建、提交、接收、生效全过程。

### 12.7 关键源码解析

逐个解释关键方法、关键字段、关键标志位。

### 12.8 问题根因定位

说明：

- 事务是否发出
- 是否到达
- 是否被合并/覆盖
- 是否被延迟
- 是否真正显示到屏幕

### 12.9 优化建议

给出可执行优化手段。

------


<!-- source: 27-134-transition.md -->

# 13.4 “transition 卡死”

可能原因：

- WCT 已下发，但 sync group 未完成
- transaction ready callback 未回调
- 参与 layer/buffer 未全部 ready
- finish transaction 被阻塞

------


<!-- source: 28-135-transaction.md -->

# 13.5 “生命周期 transaction 到了但页面没切过来”

可能原因：

- App 主线程阻塞
- execute transaction 积压
- callback 先执行，lifecycle request 后执行导致观感异常
- WMS/Surface 显示路径落后于 lifecycle

------


<!-- source: 29-136-transaction.md -->

# 13.6 “大量 transaction 导致卡顿”

可能原因：

- 高频小 transaction apply
- 每帧多次 apply 而非合并
- binder 压力大
- SF transaction queue 堆积
- 无效 state 重复下发

------


<!-- source: 30-14.md -->

# 14. 性能分析重点

Transaction 性能分析时，应重点关注：

### 14.1 事务粒度

- 是否每次只更新一个很小的状态
- 是否可以把多项更新合并到一次 apply

### 14.2 频率

- 一帧内是否多次 apply
- 是否存在动画路径重复提交无效状态

### 14.3 跨进程成本

- binder 次数是否过多
- system_server / sf 是否承压

### 14.4 合并收益

- 是否利用 transaction batch 能力
- 是否通过 sync queue 统一排程

### 14.5 显示链路耦合

- 是否与 buffer readiness 强耦合
- 是否被 vsync/frame timeline 放大延迟

------


<!-- source: 31-15.md -->

# 15. 优化建议库

### 15.1 SurfaceControl.Transaction 优化

- 合并多次 setXxx 到一次 apply
- 减少无效重复设置
- 避免频繁创建临时 transaction 对象
- 关注 callback 使用成本
- 动画过程中避免不必要的 reparent

### 15.2 WindowContainerTransaction 优化

- 结构变更集中批处理
- 减少连续多次 bounds / reorder 改动
- transition 中减少反复 patch hierarchy
- 检查 sync group 是否包含无关参与方

### 15.3 ClientTransaction 优化

- 避免主线程阻塞导致事务堆积
- 降低 launch/relaunch 频率
- 减少不必要 lifecycle 抖动

### 15.4 SF Transaction 优化

- 控制 layer state 更新频率
- 降低跨层频繁小提交
- 结合 FrameTimeline 观察延迟来源
- 检查是否存在 buffer/state 不一致导致重复补偿提交

------


<!-- source: 32-16.md -->

# 16. 分析原则

执行本 Skill 时必须遵守以下原则：

1. **必须区分不同类型的 Transaction**
2. **必须给出源码依据，不可凭空猜测**
3. **必须区分提交、commit、生效、present 四个阶段**
4. **必须构建跨层调用链，而非只看单点函数**
5. **必须解释设计思想，而非只贴代码**
6. **遇到显示问题必须把 buffer、fence、vsync、sf 一并纳入分析**
7. **遇到窗口问题必须把 WMS、Shell、Transition、SurfaceControl 一并分析**
8. **遇到生命周期问题必须把 ATMS、IApplicationThread、ActivityThread 一并分析**
9. **性能分析必须评估事务粒度、频率、合并度与线程切换成本**
10. **结论必须可验证、可复现、可落地**

------


<!-- source: 35-19.md -->

# 19. 一份标准源码分析模板

```
1. 识别 transaction 类型
2. 找构造点
3. 找 state/op 填充点
4. 找 apply/schedule 点
5. 找 binder 接收点
6. 找执行/commit 点
7. 找最终生效点
8. 检查 merge/overwrite/defer
9. 检查 buffer/fence/vsync 约束
10. 输出根因与优化建议
```

------
