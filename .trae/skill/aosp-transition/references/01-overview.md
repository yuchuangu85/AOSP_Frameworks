# 概览与范围
<!-- source: 00-overview.md -->

# aosp-transition


<!-- source: 25-81-transitioncontroller.md -->

# 8.1 TransitionController

### 职责

- 管理 transition 生命周期
- 创建、排队、合并、推进 transition
- 协调 ready / start / finish

### 分析重点

- transition 创建条件
- collect 范围
- 是否允许 merge
- ready 判断逻辑
- finish 后 cleanup 行为

### 必看问题

- 为什么某次状态变化没有进入 transition？
- 为什么一个 transition 长时间不 ready？
- 为什么 transition finish 后窗口状态仍不对？

------


<!-- source: 43-12-winscope.md -->

# 12. Winscope 分析模板


<!-- source: 52-135.md -->

# 13.5 特殊场景类

1. split -> fullscreen 过渡 bounds 计算错误
2. PiP enter/exit 目标矩形错误
3. unfold transition 与 rotation transition 冲突
4. keyguard/unlock transition 被其他 transition merge
5. 多 display 切换时 root leash 选错 display
6. 壁纸 target 参与/不参与策略导致闪烁
7. status bar / nav bar 跟随策略不一致

------
