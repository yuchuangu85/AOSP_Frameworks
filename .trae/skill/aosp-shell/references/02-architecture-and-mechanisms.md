# 架构与核心机制
<!-- source: 06-41-shell.md -->

# 4.1 Shell 的本质

Shell 不是传统意义上的“窗口管理内核”，它更接近：

- **建立在 WMS / ATMS 之上的窗口与任务可视化编排层**
- **承接系统级多任务表现能力的组织层**
- **把 Task/DisplayArea/WindowContainer 的变化转化为用户可见交互与动画的层**
- **连接 Launcher / SystemUI / WindowManager Core 的中间编排层**

一句话总结：

> WMS/ATMS 决定“系统中有哪些窗口和任务”，Shell 决定“这些变化如何以系统交互形态呈现给用户”。

---


<!-- source: 08-5.md -->

# 5. 重点源码目录

不同 Android 版本路径略有差异，但一般重点在：

```text
frameworks/base/libs/WindowManager/Shell/
frameworks/base/libs/WindowManager/Shell/src/com/android/wm/shell/
frameworks/base/services/core/java/com/android/server/wm/
frameworks/base/packages/SystemUI/
frameworks/base/packages/SystemUI/shared/
frameworks/base/packages/SystemUI/src/com/android/systemui/
packages/apps/Launcher3/
```


<!-- source: 18-8-shell.md -->

# 8. Shell 核心架构图（逻辑）

```
+---------------------------+
|        Launcher / UI      |
+-------------+-------------+
              |
              v
+---------------------------+
|      SystemUI / WM Shell  |
|  - WMShell                |
|  - ShellController        |
|  - ShellTaskOrganizer     |
|  - Transitions            |
|  - SplitScreenController  |
|  - PipTaskOrganizer       |
+-------------+-------------+
              |
              | Organizer / Transition / WCT
              v
+---------------------------+
|       ATMS / WMS Core     |
|  - ActivityTaskManager    |
|  - WindowManagerService   |
|  - RootWindowContainer    |
|  - Task / ActivityRecord  |
|  - TransitionController   |
+-------------+-------------+
              |
              v
+---------------------------+
|      SurfaceControl       |
|      SurfaceFlinger       |
+---------------------------+
```

------


<!-- source: 24-102-surfacecontrol-leash.md -->

# 10.2 SurfaceControl leash

Shell 并不直接操作底层窗口实现对象，而常通过 **leash** 对 task 或动画对象进行变换。

重点：

- 位移
- 缩放
- alpha
- crop
- reparent
- layer

------


<!-- source: 40-131-wms.md -->

# 13.1 与 WMS 的边界

WMS 负责：

- 窗口层级规则
- WindowContainer 树
- Transition 核心收集与应用
- Insets/DisplayPolicy/焦点等系统策略

Shell 负责：

- task 级可视化编排
- 高层交互能力封装
- transition handler 和组织
- split/pip/desktop/recents 的行为组合

------


<!-- source: 45-141.md -->

# 14.1 架构总览

- 模块职责
- 上下游边界
- 关键对象图


<!-- source: 48-144.md -->

# 14.4 源码细读

- 关键类
- 关键字段
- 关键方法
- 核心分支


<!-- source: 53-171.md -->

# 17.1 分析某个类时

必须回答：

1. 它在整个 Shell 架构中的位置是什么？
2. 它管理的是“容器状态”还是“动画状态”还是“表现状态”？
3. 它依赖的真实系统源头是谁？
4. 它失败后会表现成什么用户问题？

------


<!-- source: 59-3.md -->

# 3. 架构关系
[模块职责 + 边界]


<!-- source: 61-5.md -->

# 5. 核心源码分析
### 5.1 类 A
### 5.2 类 B
### 5.3 类 C
