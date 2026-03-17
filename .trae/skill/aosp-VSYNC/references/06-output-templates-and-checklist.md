# 输出模板与检查清单
<!-- source: 30-10.md -->

# 10. 重点源码机制解读模板

在分析具体源码时，必须按以下结构输出。


<!-- source: 32-102-viewrootimpl.md -->

# 10.2 ViewRootImpl 机制模板

### 要回答的问题

- 一帧 UI traversal 从哪里进入？
- measure/layout/draw 发生在什么时机？
- `scheduleTraversals()` 如何与 VSYNC 绑定？
- 哪些阶段最容易造成 App missed deadline？

### 重点路径

- `scheduleTraversals`
- `doTraversal`
- `performTraversals`
- `draw`
- `reportDrawFinished`（如涉及）

------
