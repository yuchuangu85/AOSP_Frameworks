# 概览与范围
<!-- source: 00-overview.md -->

# aosp-shell


<!-- source: 03-2.md -->

# 2. 分析目标

执行本 Skill 时，目标不是只解释“代码做了什么”，而是要回答以下问题：

### 2.1 架构层
- Shell 为什么存在？
- Shell 与 WMS / ATMS / SystemUI / Launcher 的边界是什么？
- Shell 为什么采用 Organizer / Transition / Coordinator 这类设计？
- 哪些能力属于“系统窗口管理内核”，哪些属于“可视化外壳编排”？

### 2.2 实现层
- 关键入口类是什么？
- 对象关系如何建立？
- 事件从哪里进入、经过哪些模块、最终如何落到 Surface/Window 行为？
- 状态机如何切换？
- 动画、容器、task 变化如何同步？

### 2.3 问题定位层
- 分屏为什么进不去？
- PIP 为什么无法进入/退出？
- Transition 为什么卡住或动画不一致？
- Recents 页面为什么与真实 task 状态不一致？
- Task 可见性、焦点、层级、裁剪为什么异常？
- Shell 是否只是表现层，还是根因在 WMS / ATMS / App / Launcher？

---


<!-- source: 05-4-shell-android.md -->

# 4. Shell 在 Android 中的定位


<!-- source: 07-42-shell.md -->

# 4.2 Shell 的核心职责

Shell 典型负责：

- 组织 Task 级别 UI 行为
- 协调分屏、PIP、桌面模式、多窗口能力
- 管理 Transition 动画编排
- 通过 Organizer 监听系统容器变化
- 维护 task/stage/pip 等高级状态
- 把窗口容器变化转化为可见动画和布局结果

不直接负责：

- Activity 生命周期
- WindowState 细粒度策略
- 输入分发核心逻辑
- SurfaceFlinger 合成策略
- AMS/ATMS 进程与调度策略

---


<!-- source: 14-65-pip.md -->

# 6.5 PIP 体系

重点关注：

- `PipTaskOrganizer`
- `PipTransitionController`
- `PipAnimationController`
- `PipBoundsAlgorithm`
- `PipBoundsState`
- `PhonePipMenuController`
- `PipSurfaceTransactionHelper`

分析重点：

- 哪些 task 可以进入 PIP
- 进入/退出 PIP 的状态流转
- bounds 算法如何计算目标位置与大小
- menu / gesture / animation 如何与 task leash 协同
- PIP 卡住、黑屏、回退失败、退出失败的常见根因

------


<!-- source: 15-66-recents-launcher.md -->

# 6.6 Recents / Launcher 协同

重点关注：

- `RecentTasks`
- `RecentsTransitionHandler`
- Launcher3 中与 task/shell 协作的入口
- SystemUI 与 Overview/Recent 页面之间的协同接口

分析重点：

- recent task 数据来源
- Shell 与 Launcher 对 task 模型是否一致
- recents 动画与真实 task 可见性如何同步
- recents 切换与 app open/close transition 如何衔接

------


<!-- source: 16-67-back-navigation.md -->

# 6.7 Back Navigation 协同

重点关注：

- 预测返回（Predictive Back）相关入口
- Shell transition 与 back 动画协作模块
- WMS/ATMS 对 back dispatch 的流转

分析重点：

- back 手势如何驱动 transition
- back 动画由谁控制
- 为什么会出现 back 动画异常、回退目标错误、状态不一致

------


<!-- source: 28-111.md -->

# 11.1 类职责分析

- 类的系统定位
- 它属于哪个子模块
- 对上提供什么能力
- 对下依赖哪些对象
- 它维护哪些核心状态


<!-- source: 30-113.md -->

# 11.3 核心方法分析

对每个关键方法说明：

- 调用入口
- 输入参数语义
- 前置条件
- 状态修改
- 下游调用
- 输出结果
- 异常路径


<!-- source: 38-124-recents.md -->

# 12.4 Recents 与实际任务不一致

重点检查：

1. recent task 数据源
2. ShellTaskOrganizer 中 task 生命周期
3. launcher overview 模型更新时间
4. transition 与 recent 页面刷新先后
5. task vanished / appeared 是否漏回调
6. task id 与容器关系是否变化

常见根因：

- recent model 更新滞后
- task 已切换但 UI 未收到同步
- animation 过程使用旧快照
- launcher 侧缓存未清理
- shell 状态已更新但 UI 渲染未更新

------


<!-- source: 42-133-launcher.md -->

# 13.3 与 Launcher 的边界

Launcher 负责：

- 用户交互入口
- recent/overview 界面
- 启动/切换交互意图

Shell 负责：

- 底层 task 组织与 transition 接管
- task 级动画和窗口行为实现

------


<!-- source: 67-20.md -->

# 20. 使用示例

### 示例 1：分析 SplitScreenController

输入：

> 请分析 AOSP Shell 中 SplitScreenController 的职责、关键调用链、与 StageCoordinator 的关系，并画出分屏进入时序图。

输出要求：

- 先讲架构定位
- 再讲对象关系
- 再讲源码细节
- 再给时序图
- 最后给常见故障点

------

### 示例 2：分析 PIP 进入失败

输入：

> 某应用调用进入 PIP 后没有缩成小窗，请基于 AOSP Shell 视角给出排查路径。

输出要求：

- 先构建理想链路
- 再给关键类
- 再列证据点
- 再给根因分类
- 再给修复建议

------

### 示例 3：分析 Transition 卡住

输入：

> Android 14 上应用切换动画偶现卡住，请从 Shell Transition 机制分析。

输出要求：

- 必须分析 transition request -> handler -> animation -> finish 全链路
- 必须指出 WMS/Shell 各自职责
- 必须列出可能的挂住点

------


<!-- source: 68-21-skill.md -->

# 21. 本 Skill 的最终目标

本 Skill 的目标不是“解释 Shell 代码”，而是：

- **从系统架构上理解 Shell 为什么这样设计**
- **从源码实现上还原 task / transition / split / pip 的真实工作过程**
- **从运行时证据上定位复杂窗口交互问题**
- **形成可复用的 Shell 问题分析框架**

当用户要求分析 AOSP Shell 相关内容时，必须优先输出：

1. 架构边界
2. 关键模块关系
3. 完整调用链
4. 状态流转
5. 时序图
6. 根因判断与验证方案
