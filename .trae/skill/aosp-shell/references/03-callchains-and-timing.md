# 调用链与时序
<!-- source: 12-63-transition.md -->

# 6.3 Transition 子系统

重点关注：

- `Transitions`
- `TransitionHandler`
- `DefaultTransitionHandler`
- `RemoteTransitionHandler`
- `TransitionController`（WMS侧）
- `TransitionRequestInfo`
- `TransitionInfo`
- `TransitionPlayer`
- `WindowContainerTransaction`

分析重点：

- Transition 请求如何产生
- WMS 与 Shell 如何交接 transition 控制权
- StartAnimation / MergeAnimation / Finish 的时序
- 为什么 transition 会“挂住”
- remote transition 与 default transition 的选择逻辑
- WindowContainerTransaction 与 transition 应用时机

------


<!-- source: 13-64-splitscreen-stage.md -->

# 6.4 SplitScreen / Stage 体系

重点关注：

- `SplitScreenController`
- `StageCoordinator`
- `MainStage`
- `SideStage`
- `StageTaskListener`
- `SplitLayout`
- `SplitTransitions`

分析重点：

- 分屏进入条件与根任务组织方式
- MainStage / SideStage 如何承载 task
- 分屏布局如何与 display configuration 同步
- divider/布局变化如何驱动 Surface 或 transaction
- 分屏退出、拖拽调整、焦点变化如何处理

------


<!-- source: 17-7.md -->

# 7. 推荐分析主线

分析 Shell 源码时，建议始终按以下主线展开：

### 主线 A：初始化主线

- SystemUI / shell 何时初始化
- 依赖如何装配
- 各子模块何时注册 organizer / listener / transition handler

### 主线 B：容器主线

- DisplayArea
- RootTask
- Task
- Activity
- WindowContainer
- SurfaceControl leash

目标：搞清楚 Shell 操作的对象究竟是什么。

### 主线 C：状态主线

- task appeared
- task info changed
- transition requested
- animation start
- animation finish
- task vanished

目标：搞清楚状态何时更新、何时可见、何时结束。

### 主线 D：交易主线

- WindowContainerTransaction
- SurfaceControl.Transaction
- SyncTransactionQueue
- 远端动画事务
- finish transaction

目标：搞清楚“逻辑状态变化”和“图形提交”之间如何耦合。

### 主线 E：问题主线

- 行为是否发生
- 状态是否同步
- 动画是否执行
- Surface 是否更新
- 最终层级/可见性是否符合预期

------


<!-- source: 19-9-shell.md -->

# 9. Shell 关键时序图


<!-- source: 20-91-task-shell.md -->

# 9.1 Task 出现到 Shell 接管时序

```
App/ATMS/WMS
   |
   | task created / brought to front
   v
TaskOrganizer framework callback
   |
   v
ShellTaskOrganizer.onTaskAppeared()
   |
   +--> 建立 taskId -> listener / leash / RunningTaskInfo 关系
   +--> 分发给对应 TaskListener
   |
   v
具体模块（Split/PIP/TaskView/Recents）
   |
   +--> 更新内部状态
   +--> 需要时发起 SurfaceControl.Transaction / WCT
   v
用户看到新的任务可见或动画开始
```

------


<!-- source: 21-92-transition.md -->

# 9.2 Transition 处理时序

```
状态变化（open/close/change/reparent）
   |
   v
WMS TransitionController 收集变化
   |
   v
生成 TransitionInfo / 请求 Shell 处理
   |
   v
Shell Transitions 选择 Handler
   |
   +--> DefaultTransitionHandler
   +--> RemoteTransitionHandler
   +--> Split/PIP/Recents 自定义 Handler
   |
   v
startAnimation()
   |
   +--> 操作 leash
   +--> 应用 SurfaceControl.Transaction
   |
   v
动画结束
   |
   v
finish callback
   |
   v
WMS 完成 transition 提交，进入稳定状态
```

------


<!-- source: 25-103-windowcontainertransaction.md -->

# 10.3 WindowContainerTransaction

用于向 WMS/ATMS 请求容器级状态变化，例如：

- reparent
- setBounds
- reorder
- setWindowingMode
- startTask
- setHidden

它描述的是“系统逻辑容器变更”，不是直接图形绘制。

------


<!-- source: 26-104-surfacecontroltransaction.md -->

# 10.4 SurfaceControl.Transaction

这是图形层 transaction，描述的是：

- 如何显示
- 如何变换
- 如何裁剪
- 如何排序

分析时必须区分：

- **WCT：逻辑结构变化**
- **SCT：图形显示变化**

------


<!-- source: 32-115.md -->

# 11.5 时序图

必须尽量画出该类参与的真实时序。


<!-- source: 33-116.md -->

# 11.6 风险点

- 并发问题
- 回调重入
- 状态未清理
- 动画结束回调丢失
- transaction 未提交
- listener 未注册/重复注册

------


<!-- source: 35-121.md -->

# 12.1 分屏失败

重点检查：

1. 设备/配置是否支持 split
2. task/windowingMode 是否满足进入条件
3. `StageCoordinator` 是否建立主副 stage
4. `ShellTaskOrganizer` 是否收到 task appeared/info changed
5. `WindowContainerTransaction` 是否成功提交
6. transition 是否成功启动并完成
7. final bounds / visibility / focus 是否正确

常见根因：

- task 不支持 multi-window
- stage 状态不一致
- transition 被 merge 或取消
- display configuration 更新滞后
- launcher 发起参数不合法
- task reparent 未生效

------


<!-- source: 36-122-pip.md -->

# 12.2 PIP 无法进入或退出

重点检查：

1. 目标 activity 是否支持 PIP
2. entry request 是否到达 `PipTaskOrganizer`
3. bounds algorithm 结果是否合理
4. transition 是否接管成功
5. leash 是否存在且可操作
6. menu/gesture 状态是否干扰动画
7. finish callback 是否回到稳定态

常见根因：

- activity 属性不支持 PIP
- auto-enter 与手动 enter 竞争
- bounds 计算越界
- transition finish 未回调
- task info 更新滞后
- surface transaction 被覆盖

------


<!-- source: 37-123-transition.md -->

# 12.3 Transition 卡住

重点检查：

1. transition 请求来源
2. handler 选择是否正确
3. `startAnimation()` 是否被调用
4. merge/fallback 是否发生
5. 动画结束回调是否触发
6. WMS finish 是否收到
7. 是否有 pending transition 堆积

常见根因：

- remote transition 进程异常
- animation callback 丢失
- transaction 未 apply
- handler 判断条件错误
- transition info 中对象不完整
- 某子模块持有 pending 状态未释放

------


<!-- source: 46-142.md -->

# 14.2 调用链

- 从入口到结果的完整链路
- 标出线程/回调边界
- 标出状态变化点


<!-- source: 47-143.md -->

# 14.3 时序图

至少一张关键时序图。


<!-- source: 54-172.md -->

# 17.2 分析某个问题时

必须按以下顺序：

1. 复述现象
2. 确定涉及子模块（split / pip / transition / organizer / launcher）
3. 画出理想调用链
4. 对照源码确认关键路径
5. 对照日志和 dumpsys 验证路径是否真实发生
6. 找到中断点、错误状态或时序错位点
7. 给出根因与修复建议

------


<!-- source: 55-18.md -->

# 18. 常见易错点库

### 18.1 只看 Shell，不看 WMS/ATMS

很多 Shell 问题表面在动画或状态，根因实际在 task/window/container 组织层。

### 18.2 把 WCT 与 Surface Transaction 混淆

这是 Shell 分析最常见错误之一。

### 18.3 忽略回调异步性

Shell 大量逻辑依赖异步 callback，必须追踪完整时序。

### 18.4 只看 appeared，不看 infoChanged/vanished

很多异常并非没创建，而是状态未同步或销毁未处理。

### 18.5 只看代码，不看最终容器树

最终还是要回到：

- task 在哪里
- visible/focus/bounds 对不对
- surface 层级对不对

------


<!-- source: 60-4.md -->

# 4. 完整调用链
[从入口到结果]


<!-- source: 62-6.md -->

# 6. 时序图
[文本时序图]
