# 工具与证据
<!-- source: 02-1.md -->

# 1. 技能定位

本技能用于分析 Android/AOSP 中与 **界面切换（Transition）** 相关的系统行为，覆盖：

- **App Transition**  
  Activity 启动、关闭、切换、task 切换、recent 切换
- **Window Transition**  
  窗口可见性切换、窗口动画、Insets/IME 伴随切换
- **Shell Transition**  
  Android 12+ 的统一 Transition 框架，尤其是 Shell 侧 orchestrator
- **RemoteAnimation / RemoteTransition**
- **SurfaceControl Transaction / BLAST / Leash 动画**
- **Recents / Launcher / SystemUI 参与的过渡动画**
- **旋转、分屏、PiP、折叠屏姿态变化、Display 切换**
- **WMS / Shell / SurfaceFlinger 协同过程中的闪屏、黑屏、跳层、动画不同步、卡顿**

该技能不是泛泛解释概念，而是要求在源码与运行时证据基础上完成：

1. **架构设计思想拆解**
2. **跨层调用链还原**
3. **Transition 时序建模**
4. **关键类与关键方法精读**
5. **异常模式识别与根因定位**
6. **结合 trace / dumpsys / logcat / winscope 进行证据闭环**

---


<!-- source: 36-10.md -->

# 10. 建议的证据收集顺序


<!-- source: 37-101.md -->

# 10.1 必要证据

- `logcat`
  - `WindowManager`
  - `ActivityTaskManager`
  - `ShellTransitions`
  - `SurfaceAnimator`
  - `RemoteAnimation`
  - `Transition`
  - `BLASTBufferQueue`
  - `SurfaceFlinger`
- `dumpsys window`
- `dumpsys activity activities`
- `dumpsys SurfaceFlinger --list`
- `dumpsys SurfaceFlinger --latency`
- `perfetto trace`
- `winscope`
- `screenrecord` 或现场录屏
- 对应源码版本

------


<!-- source: 39-11-perfetto-trace.md -->

# 11. Perfetto / Trace 分析模板


<!-- source: 53-14.md -->

# 14. 分析输出模板

在实际回答中，必须尽量按以下结构输出。

# 1. 问题现象

- 描述用户看到的现象
- 描述触发路径
- 给出现象分类：黑屏 / 闪屏 / 卡顿 / 跳变 / 状态错乱

# 2. Transition 类型判定

- 判定属于哪一类 transition
- 说明依据
- 说明主导模块

# 3. 架构与设计意图

- 解释该类 transition 在 AOSP 中为何这样设计
- 说明由谁收集变化、谁执行动画、谁负责清理

# 4. 完整调用链

- 从触发入口到 SurfaceFlinger present
- 必须给出跨层路径

# 5. 关键时序

- 给出关键时间点
- 哪一步开始晚、结束晚、不同步

# 6. 核心源码解析

- 关键类
- 关键方法
- 关键字段与状态机

# 7. 运行时证据

- logcat
- dumpsys
- trace
- winscope
- 关键 layer / transaction / visible state

# 8. 根因判断

- 说明为什么是这个根因，而不是其他候选根因

# 9. 修复建议

- 框架修复
- Shell 修复
- Launcher/SystemUI 修复
- App 协同修复
- 调试验证建议

------


<!-- source: 55-16.md -->

# 16. 执行规则

当用户发来 Transition 相关问题时，按以下步骤执行：

### Step 1：识别场景

判断是：

- App open/close
- task switch
- recents/home
- split/pip
- rotate/fold
- remote animation
- 普通 window animation
- 多类叠加 transition

### Step 2：确定代码路径

判断主要看：

- `AppTransition`
- `TransitionController + Transition`
- `Shell Transitions`
- `RemoteAnimation/RemoteTransition`
- `SurfaceAnimator`
- `BLASTSyncEngine`

### Step 3：建立分析模型

至少建立：

- 状态变化对象模型
- 图层变化模型
- transaction 提交模型
- 动画执行与 finish 模型

### Step 4：证据对齐

把以下证据按时间线对齐：

- log
- trace
- winscope
- dumpsys
- 录屏现象

### Step 5：输出结论

结论必须包含：

- transition 类型
- 调用链
- 异常发生点
- 根因
- 修复建议

------


<!-- source: 56-17.md -->

# 17. 用户输入适配模板

### 模板 1：源码分析请求

用户输入：

- “分析 AOSP transition 机制”
- “分析 Android 14 ShellTransition 源码”

执行重点：

- 架构设计
- 类图/职责
- 时序图
- 关键源码方法精讲
- 新旧路径对比

### 模板 2：问题定位请求

用户输入：

- “返回桌面时闪一下黑屏”
- “Recent 切换动画卡顿”
- “分屏切全屏动画跳变”

执行重点：

- 场景归类
- transition 路径判定
- 证据链定位
- 根因分析
- 修复建议

### 模板 3：dump/trace 解读请求

用户输入：

- `winscope`
- `perfetto`
- `dumpsys window`
- `SurfaceFlinger trace`

执行重点：

- 提取 transition token / participants / layer 变化
- 构建时间线
- 对齐可见现象
- 回到源码解释

------
