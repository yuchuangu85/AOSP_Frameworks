# 补充专题
<!-- source: 09-6.md -->

# 6. 重点模块索引


<!-- source: 11-62-task.md -->

# 6.2 Task 组织与监听

重点关注：

- `ShellTaskOrganizer`
- `TaskOrganizer`
- `TaskAppearedInfo`
- `RunningTaskInfo`
- `TaskListener`
- `TaskView`
- `TaskViewTransitions`

分析重点：

- TaskOrganizer 如何向系统注册
- task appeared/vanished/infoChanged 的来源
- 哪类 task 交给 Shell 处理
- task leash/surface 如何交给动画或布局模块使用
- task 生命周期如何与 UI 状态同步

------


<!-- source: 22-10.md -->

# 10. 必须掌握的关键对象


<!-- source: 23-101-runningtaskinfo.md -->

# 10.1 RunningTaskInfo

代表 task 当前运行态信息，是 Shell 判断 task 属性的重要基础。

重点关注：

- taskId
- parentTaskId
- topActivity
- windowingMode
- activityType
- isVisible
- configuration
- displayId

------


<!-- source: 29-112.md -->

# 11.2 关键字段分析

- 每个关键字段存的是什么
- 生命周期由谁维护
- 是否线程敏感
- 是否会与其他状态重复或不一致


<!-- source: 31-114.md -->

# 11.4 状态机分析

- 有哪些状态
- 状态如何流转
- 状态变化依赖什么事件
- 哪些状态不一致会导致 bug


<!-- source: 34-12.md -->

# 12. 常见问题分析框架


<!-- source: 39-13-shell.md -->

# 13. Shell 与其他模块的边界


<!-- source: 41-132-atms.md -->

# 13.2 与 ATMS 的边界

ATMS 负责：

- activity/task 生命周期与调度
- task 创建、前后台、栈管理
- launch 模式与 task 组织语义

Shell 负责：

- task 变化后的视觉编排与表现形态

------


<!-- source: 43-134-systemui.md -->

# 13.4 与 SystemUI 的边界

SystemUI 既可能作为 Shell 的宿主，也可能与 Shell 共同组成系统交互框架。
 分析时要区分：

- 哪些逻辑是 Shell 内核能力
- 哪些只是 SystemUI 的呈现层适配
- 哪些由 feature controller 注入

------


<!-- source: 52-17.md -->

# 17. 专项分析策略


<!-- source: 57-1.md -->

# 1. 问题现象
[描述现象]


<!-- source: 58-2.md -->

# 2. 涉及模块
- Shell:
- WMS:
- ATMS:
- Launcher/SystemUI:


<!-- source: 65-9.md -->

# 9. 修复建议
[建议]


<!-- source: 66-10.md -->

# 10. 验证方案
[如何验证修复]
```

------
