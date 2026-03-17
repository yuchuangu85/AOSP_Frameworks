# 概览与范围
<!-- source: 00-overview.md -->

# AOSP SurfaceFlinger Analysis Skill


<!-- source: 03-2.md -->

# 2. 适用问题范围

当用户出现以下需求时，应优先调用本 Skill：

### 2.1 SurfaceFlinger 源码分析
- “分析 SurfaceFlinger 的工作原理”
- “Buffer 是怎么被 SurfaceFlinger latch 的”
- “Transaction 到 SurfaceFlinger 后如何生效”
- “CompositionEngine 是怎么工作的”

### 2.2 显示异常定位
- “为什么已经 queueBuffer 了屏幕还是黑的”
- “为什么 Layer 可见但画面没显示”
- “为什么动画时会闪屏/残影”
- “为什么旋转后图层位置不对”
- “为什么截图拿到的是旧帧或空画面”

### 2.3 图形栈跨层问题
- “App 的 buffer 到 SurfaceFlinger 后经历了什么”
- “SurfaceControl.Transaction 为什么没有立即生效”
- “BufferQueue / BLAST 和 SurfaceFlinger 是怎么协同的”
- “HWC 和 SurfaceFlinger 的分工是什么”

### 2.4 性能问题
- “SurfaceFlinger 导致掉帧如何定位”
- “为什么 sf 线程很忙”
- “为什么 HWC 没接管导致 GPU 合成变重”
- “如何分析 present 延迟和 FrameMissed”

---


<!-- source: 05-4-surfaceflinger.md -->

# 4. SurfaceFlinger 的系统定位


<!-- source: 24-82-layer.md -->

# 8.2 Layer

Layer 是 SurfaceFlinger 中最关键的显示实体。

它承载的信息包括：

- 层级关系
- 可见性
- crop / transform / alpha / z
- buffer 状态
- region / damage
- 输入和显示元数据

重点分析：

- 当前 Layer 是否存在
- 是否有 buffer
- parent/child 是否正确
- 是否在 snapshot 中可见
- 是否被纳入当前 composition

------


<!-- source: 54-136.md -->

# 13.6 截图 / 录屏异常

重点看：

- capture 的对象是 display 还是 layer tree
- snapshot 时机是否晚于目标帧
- secure/protected layer 是否被过滤
- transform / crop 是否正确应用
- 目标 layer 是否是 leash 还是内容层
- buffer 是否已 latch 到预期帧

------


<!-- source: 76-1.md -->

# 1. 问题现象
- 现象描述：
- 触发场景：
- 影响范围：
- 是否稳定复现：


<!-- source: 78-3.md -->

# 3. 涉及对象
- 目标 Layer：
- Parent / Child 关系：
- 对应 SurfaceControl：
- 对应 BufferQueue / BLAST：
- 对应 Display：
- 关键 Fence：
