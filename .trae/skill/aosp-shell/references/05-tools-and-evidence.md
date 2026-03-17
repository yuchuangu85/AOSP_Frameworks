# 工具与证据
<!-- source: 04-3.md -->

# 3. 强制分析原则

执行本 Skill 时，必须遵循以下原则：

### 3.1 先整体后局部
先分析 Shell 的系统定位，再分析单类实现。  
禁止一上来只解释某个 Java 文件，而忽略整体职责边界。

### 3.2 先职责边界后调用细节
必须先明确：
- Shell 负责什么
- WMS/ATMS 负责什么
- Launcher/SystemUI 负责什么
- App 通过什么接口影响 Shell

再进入细节调用链分析。

### 3.3 先状态流后函数流
Shell 大量问题本质是：
- 状态未同步
- Transition 未完成
- 容器关系变化与动画编排脱节
- Organizer 回调与 Surface 操作时序不一致

因此必须优先分析：
- 状态机
- 生命周期
- 回调时序
- 容器变化传播路径

### 3.4 源码与运行时证据必须结合
分析结论必须同时依赖：
- 源码
- logcat
- dumpsys
- shell traces / perfetto / winscope（若可用）

禁止仅凭经验下结论。

### 3.5 必须输出图示
凡涉及源码分析，必须尽量输出：
- 架构图
- 模块依赖图
- 时序图
- 状态流转图
- 调用链表格

---


<!-- source: 50-15.md -->

# 15. 建议分析命令

实际分析时可配合：

```
adb shell dumpsys activity activities
adb shell dumpsys window
adb shell dumpsys SurfaceFlinger
adb shell dumpsys activity service SystemUIService
adb logcat -b system -b main -b events
adb shell wm size
adb shell wm density
adb shell settings get global enable_freeform_support
adb shell settings get global force_resizable_activities
```

如具备 trace：

- Perfetto
- Winscope
- shell transition trace
- wm trace
- surface trace

------


<!-- source: 51-16-shell.md -->

# 16. Shell 相关重点证据源

### 16.1 logcat 关注点

关键词示例：

```
ShellTaskOrganizer
Transitions
SplitScreenController
StageCoordinator
PipTaskOrganizer
PipTransition
RecentTasks
TaskOrganizer
WindowManager
ActivityTaskManager
RemoteTransition
```

### 16.2 dumpsys 关注点

- 当前 task 树结构
- windowingMode
- activityType
- displayId
- focused task / top resumed
- split/pip task 所在容器
- window 层级与可见性

### 16.3 trace 关注点

- transition 请求与 finish 时序
- shell 主线程是否阻塞
- app 切换与 transition 重叠关系
- Surface transaction 应用时点
- task 出现/消失与动画时点是否一致

------


<!-- source: 63-7.md -->

# 7. 运行时证据
### 7.1 log
### 7.2 dumpsys
### 7.3 trace
