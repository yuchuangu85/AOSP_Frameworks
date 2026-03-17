# 问题模式与根因
<!-- source: 02-1-skill.md -->

# 1. Skill 定位

该 Skill 用于对 Android AOSP 图形栈进行**系统级、跨层、可证据化、可落地修复**分析，覆盖：

- 图形架构设计分析
- 图形关键源码分析
- 渲染 / 合成 / 显示全链路调用链分析
- FrameTimeline / Jank 深度分析
- BufferQueue / Fence / BLAST 专项分析
- Perfetto + dumpsys SurfaceFlinger 联合分析
- 黑屏 / 闪屏 / 花屏 / 掉帧 / 延迟 / 首帧慢等异常定位
- GPU / SF / HWC / DRM / Panel 级瓶颈归因
- Android 12 ~ Android 16 图形栈机制演进分析

该 Skill 的目标不是停留在“图形知识解释”，而是输出：

1. **跨层调用链**
2. **关键源码位置**
3. **关键时序**
4. **异常根因**
5. **修复建议**
6. **验证方案**
7. **优化策略**

---

# 2. 适用问题域

适用于以下问题：

- 滑动掉帧 / 动画掉帧 / 高刷下掉帧
- App 卡顿、UI 不跟手
- Input-to-Display 延迟高
- Activity 启动首帧慢 / 黑屏
- Window 切换闪屏 / 黑屏 / 错位
- SurfaceFlinger 合成耗时高
- GPU completion 晚
- HWC present 慢
- BufferQueue dequeueBuffer / queueBuffer 卡住
- Fence wait 长
- BLAST resize / relayout / rotation 异常
- 可变刷新率 / mode switch 抖动
- 多窗口 / 分屏 / PIP / IME / 截图 / 录屏导致显示异常
- 花屏、裁剪错误、transform 错误、layer 错序
- SurfaceFlinger / HWC / DRM 路径架构与实现分析

---

# 3. 激活条件

当用户请求涉及以下关键词或等价表达时激活本 Skill：

- 图形栈
- Graphics Stack
- SurfaceFlinger
- HWUI
- RenderThread
- BufferQueue
- BLASTBufferQueue
- Surface / SurfaceControl
- Fence
- FrameTimeline
- Jank
- 掉帧
- 黑屏 / 闪屏 / 花屏
- HWC / DRM / Panel
- Perfetto 图形分析
- dumpsys SurfaceFlinger 分析
- App 到显示链路分析
- Input 到显示延迟分析

---

# 4. 输入要求


<!-- source: 24-102-viewrootimpl.md -->

# 10.2 ViewRootImpl

职责：

- 驱动 View 树遍历
- 管理窗口与 Surface 交互
- 触发渲染与 relayout

关键点：

- `scheduleTraversals`
- `performTraversals`
- `relayoutWindow`
- `draw`
- `reportDrawFinished`


<!-- source: 36-115.md -->

# 11.5 输出模板

```
异常帧：
Jank 类型：
Expected vs Actual：
App Deadline 是否 miss：
SF Deadline 是否 miss：
App 侧慢点：
SF 侧慢点：
是否存在背压：
是否与刷新率/模式切换有关：
根因：
修复建议：
置信度：
```

------

# 12. BufferQueue / Fence / BLAST 专项体系

这一部分是图形异常分析的核心抓手之一。凡是黑屏、闪屏、queue 卡住、dequeue 卡住、首帧慢、resize 错乱，都必须优先检查这一体系。

------


<!-- source: 39-123-dequeuebuffer.md -->

# 12.3 dequeueBuffer 卡住模型

### 典型现象

- RenderThread / app 在 `dequeueBuffer` 上等待
- 整帧被阻塞
- 随后出现 App deadline miss

### 根因树

```
dequeueBuffer Blocked
├─ Consumer 未 release
├─ max dequeued buffer 达上限
├─ release fence 未返回
├─ SurfaceFlinger / HWC 消费慢
├─ 显示链路 present 晚
└─ producer buffer 数配置过低
```

必须判断：

- 是 queue 满还是 slot 不可复用
- 是 release 慢还是 present fence 慢
- 是 producer 太快还是 consumer 太慢

------


<!-- source: 65-165-ux.md -->

# 16.5 UX 指标

- FPS
- jank count
- missed deadline count
- touch-to-display latency
- first frame time
- animation smoothness

------

# 17. 修复建议库


<!-- source: 72-182-jank.md -->

# 18.2 掉帧/Jank 分析模板

```
一、现象
二、异常帧定位
三、FrameTimeline 结论
四、App 侧分析
五、RenderThread/GPU 分析
六、BufferQueue/Fence 分析
七、SF/HWC/Display 分析
八、根因归类
九、修复建议
十、验证方案
```


<!-- source: 73-183.md -->

# 18.3 黑屏/闪屏/首帧慢模板

```
一、现象与场景
二、首个异常 buffer / layer / transaction 定位
三、BLAST / SurfaceControl 状态分析
四、BufferQueue 生命周期分析
五、SurfaceFlinger latch/present 分析
六、根因判断
七、修复建议
八、验证方案
```


<!-- source: 74-184.md -->

# 18.4 标准结论模板

```
问题类型：
主瓶颈层：
异常阶段：
关键证据：
关键源码：
直接原因：
根因：
修复建议：
验证指标：
置信度：
```

------

# 19. 回答约束

使用该 Skill 时必须遵守：

1. 不能只给概念，必须给链路。
2. 不能只给链路，必须给源码位置。
3. 不能只说“可能”，必须给证据强弱。
4. 不能把所有问题都归因到 App。
5. 不能忽略 SF / HWC / DRM / Panel。
6. 不能忽略 BufferQueue / Fence。
7. 不能只看单帧，要判断是否形成连续背压。
8. 信息不足时，必须指出最关键缺失证据。
9. 若有多个候选根因，必须排序。
10. 必须尽量给出验证方案。

------

# 20. 证据置信度分级
