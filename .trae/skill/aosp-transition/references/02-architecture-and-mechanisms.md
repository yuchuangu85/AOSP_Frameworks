# 架构与核心机制
<!-- source: 03-2.md -->

# 2. 适用问题范围

当用户出现以下需求时调用本技能：

- 分析 Activity 启动/切换时为什么会黑屏、闪屏、白屏
- 分析返回桌面、Recent 切换、任务切换动画的完整链路
- 分析 Android 12+ ShellTransition 的架构与执行流程
- 分析 RemoteAnimation / RemoteTransition 的实现与问题
- 分析窗口切换为什么出现卡顿、掉帧、不同步
- 分析 SurfaceControl Transaction 与动画 leash 的关系
- 分析 PiP / split-screen / rotate / fold/unfold 的切换机制
- 分析 WMS、Shell、Launcher、SystemUI 在过渡中的协同
- 分析 transition 相关 dump / trace / log 的含义
- 分析某次过渡过程里 layer、buffer、transaction、visible state 的变化
- 需要从源码角度解释 transition 的设计演进与版本差异

---


<!-- source: 04-3.md -->

# 3. 目标输出标准

输出必须满足以下强制要求：

### 3.1 必须包含的五大部分

1. **架构设计思想**
2. **完整跨层调用链**
3. **关键时序图**
4. **核心源码详细解释**
5. **问题定位结论与证据链**

### 3.2 输出结论必须回答的问题

- 这次 Transition 是谁发起的？
- 进入了哪一种 transition 路径？
- 参与方有哪些（WMS / ATMS / Shell / Launcher / SF / App）？
- 状态变化在哪些对象上发生（Task / ActivityRecord / WindowState / Transition / ChangeInfo / SurfaceControl）？
- 动画是在哪里创建、在哪里接管、在哪里提交 transaction 的？
- 图层 leash 是谁创建的？谁持有？谁释放？
- buffer、visible、alpha、position、crop、parent、layer 是如何变化的？
- 是否存在同步点未满足、收集不完整、ready 条件不满足、事务提交滞后？
- 是否存在动画结束与真实可见状态不一致？
- 根因属于：框架逻辑、Shell 逻辑、Launcher 协同、SurfaceFlinger 合成、Buffer 提交、时序竞争、应用端渲染慢，还是策略设计如此？

---


<!-- source: 05-4.md -->

# 4. 核心分析对象


<!-- source: 06-41-framework-system-server.md -->

# 4.1 Framework / System Server 侧

- `ActivityTaskManagerService`
- `ActivityTaskSupervisor`
- `RootWindowContainer`
- `WindowManagerService`
- `WindowOrganizerController`
- `TransitionController`
- `Transition`
- `AppTransition`
- `AppTransitionController`
- `DisplayContent`
- `Task`
- `TaskFragment`
- `ActivityRecord`
- `WindowState`
- `WindowContainer`
- `WindowContainerTransaction`


<!-- source: 11-5-android-transition.md -->

# 5. Android Transition 架构设计思想


<!-- source: 13-52.md -->

# 5.2 架构演进

### 早期路径
以 `AppTransition`、`WindowStateAnimator`、`SurfaceAnimator` 为中心，动画入口较分散。

### Android 12+ 路径
引入 **Shell Transitions**：

- WMS/ATMS 负责收集状态变化
- TransitionController/Transition 建立过渡对象
- Shell 统一接收 `TransitionInfo`
- 由 Shell 决定动画 handler 与 transaction 应用方式
- 更适合：
  - Recents
  - SplitScreen
  - PiP
  - Fold/Unfold
  - Rotation
  - RemoteTransition

### 设计收益
- 统一抽象过渡
- 逻辑与表现解耦
- 支持跨模块协作
- 支持复杂容器级别动画
- 便于将动画职责外移到 Shell / Launcher / SystemUI

---


<!-- source: 22-73-recent.md -->

# 7.3 Recent 切换关键节点

重点关注：

- Recents animation 是否被 Shell/Launcher 接管
- task snapshot 是否被正确使用
- home / app task 的 layer 次序是否正确
- Launcher 远程动画 finish 是否及时
- cancel path / takeover path 是否存在状态泄露

------


<!-- source: 23-74.md -->

# 7.4 旋转 / 折叠 / 分屏关键节点

重点关注：

- display change 与 window change 是否在同一个 transition 内
- freeze/unfreeze 时机
- screenshot layer / rotation leash
- bounds change、crop change、position change 是否一致
- display area、task、activity 是否都被收集

------


<!-- source: 38-102.md -->

# 10.2 推荐分析顺序

```
1. 先确定用户可见现象
2. 找到触发动作时刻
3. 看 ATMS/WMS 是否建立 transition
4. 看 collect 了哪些容器
5. 看 ready 在何时满足
6. 看 Shell/Remote 是否接管
7. 看 transaction 如何作用到 leash/layer
8. 看 SurfaceFlinger 是否按预期合成
9. 看 finish 后状态是否收敛
10. 回到源码解释根因
```

------


<!-- source: 45-122-sf.md -->

# 12.2 SF 侧看什么

- layer 创建时机
- layer parent
- relative z
- alpha
- visible
- crop / position / transform
- leash 是否残留
- old layer/new layer 切换关系


<!-- source: 46-123.md -->

# 12.3 联合分析原则

必须把 **WMS 状态变化时间** 与 **SF 图层变化时间** 对齐，否则容易误判：

- WMS 认为 visible，不代表 SF 已经呈现
- SF 有 layer，不代表 buffer 已经 ready
- 动画结束，不代表逻辑状态清理完成

------


<!-- source: 48-131.md -->

# 13.1 黑屏类

1. opening target 已创建但首帧 buffer 未到
2. starting window 提前移除，真实窗口未 ready
3. closing target 先 hide，opening target 后 show
4. remote animation 只操作 snapshot，未操作真实 layer
5. BLAST sync 等待过久，最终出现可见空窗期
6. rotation/fold 过程中 screenshot layer 释放过早
7. split/pip 切换时 parent leash 丢失


<!-- source: 51-134.md -->

# 13.4 状态错乱类

1. transition finish 后 leash 未释放
2. animation cancel 后最终状态未 reset
3. layer 顺序未恢复
4. closing window 逻辑 invisible，但 layer 仍残留
5. remote side 未调用 finish，系统 fallback 不完整
6. collect 漏掉 child container，最终画面不一致
7. task 与 activity changes 不一致
8. recents/home/app 三方 target 分类错误


<!-- source: 57-18.md -->

# 18. 对比分析要求

当涉及版本差异时，重点比较：

- Android 11 及以前：`AppTransition` 主导
- Android 12+：`Shell Transition` 主导
- Android 13/14/15/16：Shell 特化场景扩展更多
- RemoteTransition 与 Launcher / SystemUI 关系增强
- 折叠屏、多窗口、多 display、task organizer 场景更复杂

比较时必须说明：

- 架构边界变化
- 动画执行权变化
- 调试入口变化
- 常见问题模式变化

------


<!-- source: 58-19.md -->

# 19. 禁止事项

- 不要把 Transition 简化成单纯动画问题
- 不要混淆 `drawn`、`visible`、`shown`、`presented`
- 不要忽略 `SurfaceControl.Transaction` 的关键作用
- 不要忽略 leash 的 parent / lifecycle
- 不要只从 App 角度分析，不看 WMS/Shell/SF
- 不要在没有证据时断言是 GPU 或应用慢
- 不要忽略 BLAST 同步机制对首帧和切换时机的影响

------
