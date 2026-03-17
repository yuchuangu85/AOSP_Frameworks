# 问题模式与根因
<!-- source: 02-1.md -->

# 1. 适用范围

本 Skill 用于 **AOSP Shell / WM Shell 子系统源码分析与问题诊断**，重点覆盖：

- **WM Shell 架构**
- **Task / Activity 容器与可视化外壳协同**
- **Shell Transition 机制**
- **SplitScreen / StageCoordinator**
- **Picture-in-Picture (PIP)**
- **Recents / RecentTasks 可视层协作**
- **Back Navigation 与 Shell 动画协同**
- **TaskOrganizer / DisplayAreaOrganizer**
- **Shell 与 ATMS / WMS / SystemUI / Launcher 的协作关系**
- **窗口切换、动画切换、分屏、浮窗、多任务异常**
- **Shell 相关 trace / dumpsys / logcat 联合分析**

该 Skill 适合以下任务：

1. 阅读和拆解 `libs/WindowManager/Shell` 相关源码
2. 理解 Shell 对窗口系统能力的“表现层编排”职责
3. 分析 Transition、分屏、PIP、Task 切换等复杂链路
4. 定位分屏失败、PIP 进入退出异常、切换动画异常、Recent 页面异常
5. 构建 Shell 与 WMS/ATMS/Launcher/SystemUI 的跨层调用链
6. 输出架构图、时序图、模块关系图、问题根因报告

---


<!-- source: 10-61-shell.md -->

# 6.1 Shell 启动与总入口

重点关注：

- `WMShell`
- `ShellInit`
- `ShellController`
- `ShellTaskOrganizer`
- `RootTaskDisplayAreaOrganizer`
- `DisplayController`
- `ShellCommandHandler`
- `ShellExecutor`

分析重点：

- Shell 何时初始化
- 依赖由谁注入
- 哪些子模块按 feature 初始化
- 初始化后监听哪些系统变化
- 如何把 task/display/transition 事件分发到具体子模块

------


<!-- source: 49-145.md -->

# 14.5 问题根因

若用户带着问题来，必须输出：

- 现象
- 证据
- 根因
- 修复建议
- 验证方式

------


<!-- source: 64-8.md -->

# 8. 根因
[根因说明]
