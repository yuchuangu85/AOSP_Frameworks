# 问题模式与根因
<!-- source: 03-2.md -->

# 2. 适用场景

当用户出现以下诉求时，应调用本 Skill：

1. 分析 AOSP 动画框架设计与实现原理
2. 分析启动动画、Activity 切换动画、Task 切换动画
3. 分析 View / Property / ObjectAnimator / AnimatorSet 执行链路
4. 分析 WindowAnimation、AppTransition、ShellTransition、RemoteAnimation
5. 分析 Insets / IME / 状态栏 / 导航栏相关动画
6. 分析 SurfaceControl / Leash / Transaction 在动画中的作用
7. 分析动画卡顿、掉帧、抖动、闪烁、撕裂、黑屏、白屏、不同步
8. 分析动画开始慢、结束慢、中途丢帧、动画被打断
9. 分析动画和 VSYNC / Choreographer / RenderThread / SF 合成的关系
10. 基于源码、trace、dumpsys、bugreport 进行动画根因定位

---


<!-- source: 21-93.md -->

# 9.3 动画“结束异常”的根因拆解

### 常见表现

- 动画结束后停在中间状态
- 结束后闪一下
- finish 后仍有残影
- leash 未释放
- 最终位置跳变

### 常见根因

- finish transaction 未应用
- 最终 surface parent 恢复错误
- App 与 Window 最终态不一致
- Insets / relayout 晚于动画结束
- SF 中旧 layer 还在显示

------


<!-- source: 32-4.md -->

# 步骤 4：确认帧是否连续

检查：

- doFrame 间隔
- skipped frames
- jank 分类
- buffer 提交间隔
- SF latch / present 是否连续
- 是否存在 burst / stall / freeze

------


<!-- source: 35-131.md -->

# 13.1 动画卡顿的五层模型

### 第一层：触发延迟

表现：

- 点击后过很久才开始动画

根因：

- Input 分发慢
- 主线程忙
- Shell / ATMS / WMS 状态流转慢

------

### 第二层：首帧延迟

表现：

- 动画开始前短暂停顿

根因：

- 首帧 transaction 未及时 apply
- 首帧 buffer 未准备好
- starting window / real content 切换慢
- remote animation runner 初始化慢

------

### 第三层：中途掉帧

表现：

- 动画过程中不顺滑

根因：

- UI Thread doFrame 超时
- RenderThread / GPU 过载
- SF / HWC 合成压力大
- transaction 合并/提交不稳定

------

### 第四层：结束抖动

表现：

- 动画收尾处闪一下或跳一下

根因：

- final state commit 晚
- leash removal 时机错误
- relayout/insets 更新和动画结束不同步

------

### 第五层：状态不一致

表现：

- 动画结束后界面位置、透明度、裁剪不对

根因：

- WMS 最终窗口状态未收敛
- App 内容与 Window 容器状态不一致
- Shell Transition finish 不完整

------


<!-- source: 48-172.md -->

# 17.2 常见异常

- leash 泄漏
- 图层残留
- 透明度没恢复
- 裁剪区域错误
- 层级错乱
- 动画结束后窗口闪一下

------


<!-- source: 51-182.md -->

# 18.2 必答问题

1. 动画从哪一帧开始异常？
2. 首帧是否延迟？
3. 异常是 App 侧还是 SF 侧？
4. 是否有连续 missed frame？
5. 期间是否有 binder/锁竞争/等待？
6. SF 是否等待 buffer / presentFence？
7. 动画结束时是否发生 reparent/visibility 异常？


<!-- source: 65-7.md -->

# 7. 根因分析
- 直接原因：
- 深层原因：
- 归属模块：


<!-- source: 68-23.md -->

# 23. 常见异常模式库

以下为高频动画异常模式。


<!-- source: 71-233-view-render.md -->

# 23.3 View / Render 类

1. ValueAnimator 正常推进但 UI 线程卡顿
2. invalidate 频率异常导致抖动
3. LayoutTransition 触发频繁 relayout
4. 动画属性更新触发重布局而非仅重绘
5. RenderThread 忙导致帧率掉到半帧


<!-- source: 83-27.md -->

# 27. 结论

`aosp-animation` 的核心价值不是“解释某个动画类怎么工作”，而是：

- **把动画当作一个跨层系统问题来分析**
- **把逻辑动画、窗口动画、Surface 动画、显示动画串成完整链路**
- **把源码调用链、状态机、运行时 trace、layer 状态统一起来**
- **最终给出可验证、可落地、可修复的根因结论**

它适合用于：

- AOSP 源码研究
- 系统动画异常定位
- 启动 / 切换 / 手势 / IME / Shell Transition 分析
- 动画性能优化
- 复杂黑屏/闪屏/跳变/掉帧问题攻坚
