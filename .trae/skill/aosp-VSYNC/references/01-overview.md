# 概览与范围
<!-- source: 00-overview.md -->

# aosp-VSYNC


<!-- source: 10-52.md -->

# 5.2 输入到显示延迟链路

```
InputReader
  ↓
InputDispatcher
  ↓
App main thread input callback
  ↓
Choreographer scheduleVsyncLocked
  ↓
doFrame
  ↓
ViewRootImpl traversal
  ↓
RenderThread / GPU
  ↓
queueBuffer
  ↓
SurfaceFlinger latch
  ↓
compose + present
  ↓
panel refresh
```

需要特别注意：

- 输入事件到达并不等于马上显示
- 常常要等待下一次 `VSYNC-app`
- 如果 App 未赶上 deadline，可能延后一帧甚至多帧
- 即使 queueBuffer 完成，也还要等待 SF latch 和 display present

------


<!-- source: 12-61-app.md -->

# 6.1 App 层

关键组件：

- `Choreographer`
- `ViewRootImpl`
- `ThreadedRenderer`
- `RenderThread`
- `Surface`
- `BLASTBufferQueue`（新架构常见）

职责：

- 接收 VSYNC-app
- 驱动 input / animation / traversal / commit 阶段
- 生成图形内容并提交给 Surface

关键问题：

- 主线程繁忙导致 doFrame 晚启动
- measure/layout/draw 耗时过长
- RenderThread 或 GPU completion 晚
- queueBuffer 被 backpressure 阻塞

------


<!-- source: 17-71-vsync-app-vsync-sf.md -->

# 7.1 VSYNC-app 与 VSYNC-sf

Android 常见会区分两类节奏：

- `VSYNC-app`
  - 用于通知应用开始准备下一帧
  - 目标是让 App 尽量在 SF 消费前完成生产
- `VSYNC-sf`
  - 用于通知 SurfaceFlinger 进行合成调度
  - 目标是让 SF 在 present 窗口内完成合成

两者通常存在偏移（offset），不是同一个时间点直接广播给所有人。

分析重点：

- App 是否在 `VSYNC-app` 后及时启动 doFrame
- SF 是否在 `VSYNC-sf` 节奏下按时 latch / compose
- offset 设置是否合理
- 高刷切换后 offset 是否发生变化

------


<!-- source: 21-8.md -->

# 8. 关键源码索引

以下为 AOSP 常见关键源码入口。不同 Android 版本路径和细节会有调整，分析时必须以目标版本源码为准。


<!-- source: 27-91-app-sf.md -->

# 9.1 App + SF 单帧标准路径

```
App/MainThread         RenderThread        BufferQueue        SurfaceFlinger        HWC/Display
     |                      |                   |                    |                  |
     |--- VSYNC-app ------->|                   |                    |                  |
     | doFrame              |                   |                    |                  |
     | input/anim/traversal |                   |                    |                  |
     | draw                 |---- GPU work ---->|                    |                  |
     |                      | queueBuffer ----->|                    |                  |
     |                      |                   |---- available ---->|                  |
     |                      |                   |                    |-- VSYNC-sf ----->|
     |                      |                   |                    | latchBuffer      |
     |                      |                   |                    | compose          |
     |                      |                   |                    | presentDisplay ->|
     |                      |                   |                    |<-- presentFence -|
     |                      |                   |                    |                  |
     |                      |                   |                    |      shown       |
```

------


<!-- source: 41-123-dumpsys-input.md -->

# 12.3 dumpsys input 与输入到显示问题

如果问题从触摸开始，需联合：

- input dispatch 时间
- 应用消费时间
- 第一次关联 VSYNC-app
- 最终 present 时间

判断：

- 高延迟到底是输入分发慢，还是渲染显示慢

------


<!-- source: 47-135.md -->

# 13.5 输入到显示专项异常

1. 输入分发正常但首帧 doFrame 晚
2. 输入后第一帧仅更新逻辑未提交 buffer
3. 首帧 buffer 到达但 SF 错过 latch 窗口
4. 动画开始帧与输入响应帧被混淆
5. 手势连续帧 supply 不稳定导致跟手差

------


<!-- source: 49-141.md -->

# 14.1 基本概念

分析每帧至少要识别：

- 目标帧：系统希望它在哪个 refresh 上屏
- deadline：该帧最晚何时必须完成
- actual present：实际何时显示
- jank type：哪一类超时/错过

------


<!-- source: 55-171-app.md -->

# 17.1 App 侧优化

- 减轻主线程 traversal 负载
- 缩短单帧动画 / 输入回调逻辑
- 降低同步 binder / IO / lock
- 降低 overdraw
- 预热 shader / 纹理资源
- 分帧提交重任务
- 优化列表布局与重组策略
- 避免同一帧内多次无效 invalidate

------


<!-- source: 59-18-skill.md -->

# 18. 与其它 Skill 的协同边界

本 Skill 聚焦 **显示时序 / VSYNC / 帧节奏**，与其它 Skill 的边界如下：

- `aosp-graphics`
  - 更偏图形栈全貌、BufferQueue、SurfaceFlinger、HWC、DRM
  - 本 Skill 更强调“时序与节拍”
- `aosp-input`
  - 更偏输入采集、分发、Input ANR、触摸延迟
  - 本 Skill 在其基础上向“显示结果”延申
- `aosp-wms`
  - 更偏窗口组织、层级、Insets、Transition
  - 本 Skill 更偏窗口内容何时显示
- `aosp-anr`
  - 更偏阻塞与无响应
  - 本 Skill 更偏可交互但不流畅的帧问题
- `aosp-surfaceflinger`
  - 更偏 SF 模块本身
  - 本 Skill 更偏 SF 在 VSYNC 节奏中的作用

------
