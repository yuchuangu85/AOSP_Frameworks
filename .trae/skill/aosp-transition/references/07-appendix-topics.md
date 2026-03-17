# 补充专题
<!-- source: 07-42-shell-wm-shell.md -->

# 4.2 Shell / WM Shell 侧

- `Transitions`
- `DefaultTransitionHandler`
- `RemoteTransitionHandler`
- `OneShotRemoteHandler`
- `TaskViewTransitions`
- `PipTransition`
- `SplitScreenTransitions`
- `StageCoordinator`
- `RecentsTransitionHandler`
- `UnfoldTransitionHandler`
- `ShellTaskOrganizer`


<!-- source: 09-44-systemui-launcher-app.md -->

# 4.4 SystemUI / Launcher / App 侧

- `Launcher`
- `Quickstep`
- `RecentsAnimation`
- `RemoteAnimationRunner`
- `RemoteTransition`
- `ActivityOptions`
- `overridePendingTransition`
- `Activity#startActivity`
- `Activity#finish`


<!-- source: 12-51-transition.md -->

# 5.1 为什么会有 Transition 体系

Transition 的本质不是“播放一个动画”，而是：

- **在多窗口对象状态变化时进行统一收集**
- **将逻辑状态切换与视觉切换协调起来**
- **在状态提交前后建立同步点**
- **让多个参与对象以一致方式完成可见性和层级过渡**
- **支持本地动画、远程动画、Shell 托管动画、多显示器与复杂窗口形态**

本质上它解决的是：

- 状态变化很多，但用户应该看到“一个连贯的切换”
- 多个 WindowContainer / SurfaceControl 需要成组变换
- 动画期间逻辑状态和显示状态可能暂时不一致
- 必须保证“谁收集变化、谁声明 ready、谁执行动画、谁收尾提交”边界清晰

---


<!-- source: 21-72.md -->

# 7.2 启动场景关键节点

重点关注：

- 旧 Activity 是否先 pause/stop
- 新 Activity 的 starting window 是否创建
- 首帧 buffer 到达时间
- opening / closing targets 的 leash 是否正确
- ready 时点是否早于首帧可显示
- 动画结束时 app 是否真的已经 drawn

------


<!-- source: 27-83-apptransition-apptransitioncontroller.md -->

# 8.3 AppTransition / AppTransitionController

### 职责

- 旧路径下的 app window transition 策略
- 为 activity/task 切换选择动画类型

### 分析重点

- transit 类型来源
- `goodToGo()`
- `handleAppTransitionReady()`
- 旧式 app animation 与 shell transition 的边界

### 典型问题

- overridePendingTransition 不生效
- 系统 fallback 到默认动画
- app transition 被替换或跳过

------


<!-- source: 32-9-transition.md -->

# 9. Transition 分析方法论


<!-- source: 33-91-transition.md -->

# 9.1 先判定“是哪一类 transition”

必须先识别本次切换属于哪类：

- Activity open / close
- Task open / close
- Task to front / to back
- Home / recents switch
- keyguard / unlock transition
- rotate / display change
- split / pip / unfold
- IME / Insets 伴随变化
- remote takeover

如果分类错误，后面所有分析都会偏。

------


<!-- source: 35-93.md -->

# 9.3 再确认“同步点”

Transition 问题很多不是动画本身，而是同步点问题：

- 首帧没来
- ready 太晚
- collect 不完整
- finish 早于真实绘制完成
- 某个 participant 未汇报完成
- surface 创建与 visible 更新不同步

------


<!-- source: 44-121-wms.md -->

# 12.1 WMS 侧看什么

- ActivityRecord / Task 可见性变化
- WindowState 是否创建、是否 drawn
- transition token / type / participant
- top activity / focused app
- insets / ime / rotation 附带变化


<!-- source: 49-132.md -->

# 13.2 闪屏类

1. splash/starting window 与 app 背景色不一致
2. override 动画与默认动画叠加
3. finish 时 alpha 一次性跳到 1
4. surface reparent 到新父节点后位置重算突变
5. old task snapshot 与 live content 对齐失败
6. IME/insets 伴随变化引起 layout 突变
