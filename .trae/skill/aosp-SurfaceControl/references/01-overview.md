# 概览与范围
<!-- source: 00-overview.md -->

# AOSP SurfaceControl Analysis Skill


<!-- source: 03-2.md -->

# 2. 适用问题范围

当用户出现以下需求时，应优先调用本 Skill：

### 2.1 SurfaceControl 源码分析
- “分析 SurfaceControl 的实现原理”
- “Transaction.apply 是怎么到 SurfaceFlinger 的”
- “SurfaceControl 和 Surface / SurfaceView / BLASTBufferQueue 的关系是什么”
- “LayerState 在哪里定义，怎么传输”

### 2.2 显示异常定位
- “为什么窗口已经创建但内容不显示”
- “为什么 show 了还是黑屏”
- “为什么动画过程中 Layer 层级错乱”
- “为什么截图/录屏拿不到预期画面”
- “为什么旋转后 crop / matrix 异常”

### 2.3 WMS / 图形跨层问题
- “relayout 后 SurfaceControl 发生了什么”
- “WindowContainerTransaction 和 SurfaceControl.Transaction 的关系”
- “App 侧事务与 SurfaceFlinger 侧事务状态如何对应”
- “为什么 WMS 认为可见，但 SF 没显示”

### 2.4 性能问题
- “某个动画期间频繁 apply Transaction 导致卡顿”
- “窗口频繁 resize / reparent 是否会导致掉帧”
- “如何优化 SurfaceControl 的使用方式”
- “BLAST 与 SurfaceControl 同步问题如何分析”

---


<!-- source: 05-4-surfacecontrol.md -->

# 4. SurfaceControl 系统定位


<!-- source: 53-1.md -->

# 1. 问题现象
- 现象描述：
- 出现场景：
- 影响范围：
- 是否稳定复现：
