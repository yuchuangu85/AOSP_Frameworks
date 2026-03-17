# 概览与范围
<!-- source: 00-overview.md -->

# aosp-animation


<!-- source: 29-1.md -->

# 步骤 1：确认动画场景

先回答：

1. 动画对象是谁？
2. 动画目标是谁？
3. 动画由谁触发？
4. 动画类型是什么？
5. 动画时长和节奏是什么？
6. 是否存在系统参与者（WMS/Shell/SF/IME）？

------


<!-- source: 42-152.md -->

# 15.2 典型异常

- 动画直接消失
- 目标窗口空白
- 只动了截图没动真实窗口
- 动画结束卡住
- fallback 到默认动画

------


<!-- source: 54-191.md -->

# 19.1 常用命令

```
dumpsys window
dumpsys activity
dumpsys SurfaceFlinger
dumpsys gfxinfo
dumpsys input
dumpsys display
```


<!-- source: 59-1.md -->

# 1. 问题现象
- 现象：
- 复现路径：
- 影响范围：


<!-- source: 76-241.md -->

# 24.1 触发层优化

- 缩短 input 到动画启动路径
- 减少主线程同步阻塞
- 减少动画启动前的 binder 往返
- 避免在动画触发点做重逻辑
