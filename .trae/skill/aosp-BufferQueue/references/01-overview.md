# 概览与范围
<!-- source: 00-overview.md -->

# AOSP BufferQueue Analysis Skill


<!-- source: 62-171-dumpsys-surfaceflinger.md -->

# 17.1 读取 `dumpsys SurfaceFlinger` 时必须检查

1. 目标 layer 是否存在
2. 目标 layer 是否可见
3. 对应 surface 名称是否正确
4. buffer state 是否更新
5. frameNumber 是否增长
6. 是否存在 backlog
7. transform/crop/alpha/z-order 是否异常
8. 是否有 BLASTBufferQueue 相关 layer 更新异常
9. 是否有 acquire/release 异常迹象

------


<!-- source: 77-192-surface-layer.md -->

# 19.2 第二步：锁定目标 Surface / Layer

必须确认：

- 对应窗口名
- 对应 layer 名
- 对应 BufferQueue
- 对应 producer/consumer 身份

------


<!-- source: 79-194.md -->

# 19.4 第四步：定位等待点

重点看：

- dequeue wait
- acquire wait
- fence wait
- transaction wait
- present/release wait

------


<!-- source: 83-202.md -->

# 20.2 源码分析模板

```
1. 目标类/函数
2. 所属模块
3. 设计职责
4. 输入输出
5. 核心状态变量
6. 状态迁移
7. 锁与并发控制
8. 与上下游模块关系
9. 异常路径
10. 对问题现象的影响
```

------


<!-- source: 87-213-fence.md -->

# 21.3 Fence 链优化

- 定位 GPU completion 慢原因
- 检查 HWC release fence 时延
- 优化 RenderEngine / 驱动路径
- 避免异常同步链拖长


<!-- source: 91-24-skill.md -->

# 24. 推荐组合 Skill

本 Skill 建议与以下 Skill 联动使用：

- `aosp-graphics`：图形栈全局分析
- `aosp-surface`：Surface/SurfaceControl 分析
- `aosp-SurfaceFlinger`：消费端与合成链分析
- `aosp-Fence`：同步机制与 fence 等待分析
- `aosp-VSYNC`：VSYNC / 调度 / 帧节奏分析
- `aosp-wms`：窗口、layer、可见性与事务分析
- `aosp-input`：输入到显示时延联动分析
- `aosp-anr`：因 dequeue / render 阻塞引发 ANR 的联动分析

------
