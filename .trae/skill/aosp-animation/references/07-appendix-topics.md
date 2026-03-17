# 补充专题
<!-- source: 08-7.md -->

# 7. 动画系统关键对象地图


<!-- source: 22-10.md -->

# 10. 关键源码模块索引

以下为建议重点阅读模块。


<!-- source: 34-13.md -->

# 13. 性能分析模型


<!-- source: 37-14-shell-transition.md -->

# 14. Shell Transition 深度模型

Android 新版本中，任务切换/启动/返回/分屏/PiP 等大量动画由 Shell Transition 统一调度。


<!-- source: 40-15-remoteanimation.md -->

# 15. RemoteAnimation 深度模型

RemoteAnimation 常用于 Launcher、Recents、手势返回、系统转场。


<!-- source: 43-16-insets-ime.md -->

# 16. Insets / IME 动画专项体系

IME 动画是高频问题区。


<!-- source: 44-161.md -->

# 16.1 关注点

- InsetsSource 是否准备完成
- control target 是否正确
- app 是否消费 WindowInsetsAnimation
- IME surface 与内容区域是否同步变化
- relayout 是否过于频繁
- 手势导航区域和 IME 动画是否冲突


<!-- source: 45-162.md -->

# 16.2 典型问题

- 键盘弹出内容跳两次
- 键盘收起后界面残留空白
- IME 动画不连贯
- 内容和键盘不同步
- 导航栏、手势条、IME 过渡抖动

------


<!-- source: 60-2.md -->

# 2. 动画类型判定
- 类型：
- 触发源：
- 参与模块：


<!-- source: 63-5.md -->

# 5. 关键源码
- 类：
- 方法：
- 状态字段：
- 关键分支：


<!-- source: 66-8.md -->

# 8. 修复建议
- 短期修复：
- 中期优化：
- 长期治理：
```

------


<!-- source: 69-231.md -->

# 23.1 启动/切换类

1. 启动动画未执行，直接跳变
2. 首帧黑屏后才开始动画
3. starting window 退场慢，真实内容晚到
4. activity 切换动画和内容加载不同步
5. task 切换时截图动画与真实窗口错位


<!-- source: 70-232-window-shell.md -->

# 23.2 Window / Shell 类

1. Shell Transition 收集对象不完整
2. remote animation 超时回退默认动画
3. leash 创建成功但未驱动
4. 动画结束后 leash 未清理
5. finish 后层级未恢复


<!-- source: 73-235-insets-ime.md -->

# 23.5 Insets / IME 类

1. IME 控制权未授予导致无动画
2. app 内容与 IME surface 不同步
3. insets dispatch 晚于动画推进
4. 收键盘时最终 insets 状态未收敛
5. 旋转 + IME 动画叠加导致跳变


<!-- source: 74-236.md -->

# 23.6 状态同步类

1. App 状态已切换但 Window 状态未切换
2. Window 已可见但 buffer 尚未 ready
3. 动画结束但最终 surface 属性未恢复
4. Shell finish 与 WMS final state 提交错序
5. 多动画叠加导致 alpha / matrix 相互覆盖

------


<!-- source: 75-24.md -->

# 24. 优化策略库


<!-- source: 77-242-view-render.md -->

# 24.2 View / Render 优化

- 避免动画期间频繁 requestLayout
- 尽量使用属性动画替代重布局动画
- 降低过度绘制
- 减少大面积 alpha 动画与模糊效果叠加


<!-- source: 80-245-insets-ime.md -->

# 24.5 Insets / IME 优化

- 保证 IME target 明确
- 降低动画期 relayout 干扰
- 内容位移和 IME surface 统一时钟推进

------
