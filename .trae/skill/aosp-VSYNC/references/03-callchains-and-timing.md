# 调用链与时序
<!-- source: 03-2.md -->

# 2. 适用问题范围

当用户出现以下诉求时，应调用本 Skill：

- “分析 Android VSYNC 机制”
- “分析 Choreographer 是如何驱动绘制的”
- “分析 SurfaceFlinger 的 VSYNC 调度”
- “掉帧是 App 画慢了，还是 SF 合成慢了？”
- “为什么 input 到显示延迟很高？”
- “FrameTimeline 中 App deadline missed 是什么原因？”
- “高刷场景下为何帧率不稳定？”
- “分析 DispSync / EventThread / Scheduler 工作机制”
- “PresentFence / VSYNC-sf / VSYNC-app / Expected Timeline / Actual Timeline 分别表示什么？”
- “分析 BufferQueue / fence / present 时序”
- “分析某 trace 中 jank 的真正根因”

---


<!-- source: 04-3.md -->

# 3. 强制输出原则

输出内容必须满足以下约束：

1. **必须给出架构分层**
   - App
   - ViewRootImpl / Choreographer
   - RenderThread / HWUI
   - BufferQueue / BLAST
   - SurfaceFlinger / Scheduler / EventThread
   - HWC / Composer HAL
   - DRM / Kernel Display Driver（如源码范围包含）
   - Panel / Display 刷新

2. **必须给出跨层调用链**
   - 至少从 Input / VSYNC source → App doFrame → draw → queueBuffer → SF latch/compose → present → display refresh

3. **必须给出时序图**
   - 用 ASCII 时序图表达关键阶段

4. **必须给出关键源码点**
   - 类名
   - 关键方法
   - 关键状态变量
   - 调用前后因果关系

5. **必须给出运行时证据抓手**
   - Perfetto 关键轨
   - dumpsys SurfaceFlinger
   - dumpsys gfxinfo framestats / FrameTimeline
   - fence / present / deadline / jank 类型

6. **必须明确区分责任域**
   - App 主线程慢
   - RenderThread 慢
   - GPU 完成晚
   - Buffer dequeue/queue 阻塞
   - SF 合成慢
   - HWC present 晚
   - Display / driver VSYNC 不稳定
   - 刷新率切换策略导致的节奏变化

7. **不能只讲概念**
   - 必须结合源码机制 + trace 证据 + 常见异常模式

---


<!-- source: 06-41-android-vsync.md -->

# 4.1 为什么 Android 需要 VSYNC 驱动

Android 图形系统不是“应用想画就立刻显示”，而是围绕显示设备的固定或动态刷新节奏进行协同：

- 显示屏按一定刷新周期刷新
- App 必须在合适的 deadline 前完成 UI 更新
- SurfaceFlinger 必须在合适时机收集 buffer 并合成
- HWC 必须在显示硬件允许的窗口执行 present
- 最终显示内容在下一次或下几次 refresh 才会真正可见

因此，Android 使用 VSYNC 作为全系统统一“节拍器”，让：

- App 在 `VSYNC-app` 驱动下执行 `doFrame`
- SurfaceFlinger 在 `VSYNC-sf` 驱动下执行合成
- Scheduler 在不同刷新率与负载条件下动态调度
- FrameTimeline 用于精确建模每帧的 target / deadline / present 结果

---


<!-- source: 08-5.md -->

# 5. 完整跨层调用链


<!-- source: 09-51.md -->

# 5.1 标准显示时序主链路

```text
Display Panel refresh
  ↓
Kernel display driver / HWC VSYNC source
  ↓
SurfaceFlinger Scheduler / DispSync / EventThread
  ↓
VSYNC-app 分发给应用进程
  ↓
Choreographer 收到 frame callback
  ↓
ViewRootImpl#doTraversal / measure/layout/draw
  ↓
RenderThread / HWUI 录制与执行 GPU 命令
  ↓
BufferQueue / BLASTBufferQueue queueBuffer
  ↓
SurfaceFlinger latchBuffer
  ↓
Layer prepare / composition planning
  ↓
HWC validateDisplay / presentDisplay
  ↓
present fence signal
  ↓
Display next refresh 显示该帧
```


<!-- source: 13-62-bufferqueue-blast.md -->

# 6.2 BufferQueue / BLAST 层

关键组件：

- `BufferQueueProducer`
- `BufferQueueConsumer`
- `BLASTBufferQueue`
- `Surface`

职责：

- 解耦生产者（App）与消费者（SurfaceFlinger）
- buffer 生命周期管理
- fence 协同同步
- 应对尺寸变化、事务同步、单帧原子提交

关键问题：

- dequeueBuffer 卡住
- queueBuffer 晚
- buffer 未及时被 consumer 释放
- BLAST 事务与 buffer 同步不一致

------


<!-- source: 14-63-surfaceflinger.md -->

# 6.3 SurfaceFlinger 层

关键组件：

- `SurfaceFlinger`
- `Scheduler`
- `EventThread`
- `DispSync`
- `FrameTimeline`
- `Layer`
- `CompositionEngine`

职责：

- 接收 VSYNC 源
- 维护 app/sf 两类事件分发
- 在合适时机 latch layer buffer
- 合成并提交给 HWC
- 追踪每帧目标时间、deadline、present 结果

关键问题：

- SF 主线程负载过高
- latch 时 buffer 未准备好
- composition 路径复杂
- HWC validate/present 耗时高
- refresh rate 切换带来节奏抖动

------


<!-- source: 18-72-choreographer.md -->

# 7.2 Choreographer 的阶段模型

`Choreographer#doFrame()` 通常可理解为以下阶段：

- Input
- Animation
- InsetsAnimation / Traversal
- Commit

重点不是背 API，而是理解：

- 同一帧内多个 callback phase 的执行顺序
- 任一阶段过慢都会把这一帧挤爆
- 若主线程被 binder / lock / IO / GC 占据，`doFrame` 甚至无法准时开始

常见根因：

- `scheduleVsyncLocked()` 正常，但消息线程未及时执行
- `doFrame` 启动时间比 expected VSYNC 晚很多
- traversal 太重导致 queueBuffer 太晚

------


<!-- source: 20-74-frametimeline.md -->

# 7.4 FrameTimeline 模型

FrameTimeline 用来表示一帧的关键时刻：

- expected start
- target present time
- deadline
- actual present time
- jank classification

其价值在于把“丢帧”从模糊感觉变成精确时序问题：

- App 是否错过 deadline
- SF 是否错过 deadline
- GPU / HWC 是否导致 present 推迟
- 最终这帧是 late present、dropped、stale 还是其他 jank 类型

------


<!-- source: 26-9.md -->

# 9. 关键时序图


<!-- source: 28-92-app-missed-deadline.md -->

# 9.2 App missed deadline 场景

```
VSYNC-app 到来
  ↓
主线程忙 / traversal 慢 / RenderThread 慢
  ↓
queueBuffer 晚于 SF latch 窗口
  ↓
当前 SF 周期拿不到新 buffer
  ↓
旧帧继续显示 / 新帧延后一拍
  ↓
FrameTimeline 标记 App deadline miss / jank
```

------


<!-- source: 29-93-sf-missed-deadline.md -->

# 9.3 SF missed deadline 场景

```
App 已按时 queueBuffer
  ↓
SurfaceFlinger 当周期未及时 latch / compose / present
  ↓
HWC present 推迟
  ↓
actual present 晚于 target present
  ↓
FrameTimeline 标记 SF/HWC 侧 jank
```

------


<!-- source: 31-101-choreographer.md -->

# 10.1 Choreographer 机制模板

### 要回答的问题

- VSYNC 是如何进入 App 进程的？
- `scheduleVsyncLocked()` 何时触发？
- `doFrame()` 中各 phase 顺序如何？
- 哪些情况会导致当前帧直接超时？

### 最小分析骨架

- 类职责
- 关键字段
- 关键消息 / callback
- 与 Looper / Handler 的关系
- 与 ViewRootImpl 的关系
- 与 RenderThread 的衔接点
- 对帧时序的影响

------


<!-- source: 35-111.md -->

# 11.1 必看轨道

分析 VSYNC 问题时，优先看：

- `Expected Timeline`
- `Actual Timeline`
- `VSYNC-app`
- `VSYNC-sf`
- `surfaceflinger`
- `app` 主线程
- `RenderThread`
- `GPU completion`
- `presentFence`
- `HWC release`
- `BufferTX-*`
- `FrameTimeline`
- `TransactionQueue`
- `SurfaceFlinger` 主循环
- 输入相关轨（若问题从触摸开始）

------


<!-- source: 37-113.md -->

# 11.3 典型因果判定规则

### 模式 A：App 主线程卡顿

特征：

- `VSYNC-app` 正常
- `doFrame` 启动晚
- 主线程有长任务
- queueBuffer 晚
- SF 当前周期拿不到新 buffer

结论：

- 根因在 App 主线程调度 / UI 计算 / 同步阻塞

### 模式 B：RenderThread / GPU 慢

特征：

- 主线程 traversal 结束不算太晚
- RenderThread / GPU completion 明显延后
- queueBuffer 晚于目标窗口

结论：

- 根因在 HWUI 渲染 / GPU 负载 / shader / overdraw / upload

### 模式 C：SF 合成慢

特征：

- App buffer 已按时到达
- SF latch/composite/present 明显拉长
- Actual present 晚于 target

结论：

- 根因在 SurfaceFlinger / composition / HWC 协调

### 模式 D：HWC / present 侧慢

特征：

- SF 已正常进入 present
- presentFence 返回晚
- 显示周期实际提交延迟

结论：

- 根因在 HWC / driver / display pipeline

### 模式 E：刷新率切换造成时序抖动

特征：

- deadline / frame interval 在一段时间内变化
- VSYNC 周期不再稳定为固定值
- 某些帧在切换边界处被判 late

结论：

- 需要重点分析 Scheduler refresh rate policy

------


<!-- source: 39-121-dumpsys-surfaceflinger.md -->

# 12.1 dumpsys SurfaceFlinger

重点关注：

- refresh rate / active mode
- layer 更新频率
- composition 类型
- transaction 堆积
- scheduler 状态
- frame timeline / jank 统计（版本相关）

典型问题：

- layer 频繁变更但 app 无法稳定供帧
- active mode 频繁切换
- 部分 layer 持续走 client composition
- backpressure 明显

------


<!-- source: 40-122-dumpsys-gfxinfo-framestats-frametimeline.md -->

# 12.2 dumpsys gfxinfo framestats / FrameTimeline

重点关注：

- 每帧总耗时
- input / animation / traversal / draw 的分布
- jank frame 统计
- missed vsync / slow UI / slow bitmap upload / slow issue draw commands

用途：

- 判断 App 侧是否超预算
- 与 Perfetto 对齐验证
- 看是偶发尖峰还是系统性超时

------


<!-- source: 42-13-50-vsync.md -->

# 13. 50+ VSYNC / 时序异常模式库

以下模式是本 Skill 的核心经验库。分析时必须优先进行模式匹配。


<!-- source: 44-132-bufferqueue-blast.md -->

# 13.2 BufferQueue / BLAST 异常

1. dequeueBuffer 等待空闲 buffer
2. queueBuffer 被 backpressure 限制
3. acquire fence 未及时 signal
4. release fence 迟迟未返回
5. producer-consumer 节奏不匹配
6. BLAST 事务与 buffer 原子同步失败
7. 尺寸变化期间 buffer 重建引发抖动
8. triple buffering 导致额外帧延迟
9. 单帧 queue 成功但 SF 未在本周期 latch
10. layer tree transaction 与 buffer 更新时间错位


<!-- source: 45-133-surfaceflinger.md -->

# 13.3 SurfaceFlinger 异常

1. SF 主线程负担过重
2. `latchBuffers` 时 buffer 未准备好
3. layer 数量过多导致 composition 复杂
4. transaction 合并成本高
5. client composition 比例过高
6. GPU composition 慢
7. display mode 切换过程中周期抖动
8. Scheduler 预测误差导致 deadline 不稳
9. EventThread 分发延迟
10. 某些帧因旧 buffer 被重复展示
11. 局部 layer 持续抖动触发频繁 refresh
12. 屏幕 idle/resume 切换带来第一帧异常
13. FrameTimeline 标记为 SF deadline miss
14. transaction queue 堆积导致刷新处理滞后


<!-- source: 46-134-hwc-driver-display.md -->

# 13.4 HWC / Driver / Display 异常

1. validateDisplay 时间异常
2. presentDisplay 时间异常
3. presentFence signal 晚
4. 驱动 VSYNC source 不稳定
5. 动态刷新率切换边界帧抖动
6. panel/self refresh 模式切换影响 cadence
7. HWC capability 不足导致 client comp 回退
8. DRM/KMS 提交延迟
9. 多显示设备场景调度混乱
10. 低功耗策略压缩刷新节奏
11. 显示驱动 backlog 造成帧显示滞后


<!-- source: 48-14-frametimeline-jank.md -->

# 14. FrameTimeline / Jank 深度分析模型


<!-- source: 52-15-vsync.md -->

# 15. VSYNC 专项分析决策树

```
用户现象：掉帧 / 不跟手 / 画面抖 / 帧率不稳
  ↓
先看 FrameTimeline / Expected vs Actual Timeline
  ↓
是否是 App missed deadline？
  ├─ 是 → 看主线程 / RenderThread / GPU completion / queueBuffer
  └─ 否
      ↓
是否是 SF missed deadline？
  ├─ 是 → 看 latchBuffers / composite / TransactionQueue / layer complexity
  └─ 否
      ↓
是否是 presentFence 晚？
  ├─ 是 → 看 HWC / validateDisplay / presentDisplay / driver
  └─ 否
      ↓
是否是 cadence 抖动 / refresh rate 切换？
  ├─ 是 → 看 scheduler / display mode / active refresh rate
  └─ 否
      ↓
进一步检查输入、buffer backpressure、事务同步、异步动画等特殊链路
```

------


<!-- source: 53-16.md -->

# 16. 标准分析输出模板

每次分析 VSYNC / 时序问题，输出必须使用以下模板。

# 16.1 问题定义

- 现象：
- 触发场景：
- 频率：
- 影响范围：
- 初步判断属于：
  - App供帧慢 / SF合成慢 / HWC显示慢 / 刷新率切换 / 输入到显示延迟

# 16.2 架构链路

- 从哪个事件开始
- 经过哪些模块
- 最终在哪一层失配

# 16.3 关键源码路径

- 文件路径
- 核心类
- 核心方法
- 关键状态变量
- 调用链说明

# 16.4 时序证据

- VSYNC-app
- doFrame 开始时间
- traversal / draw / RenderThread / GPU completion
- queueBuffer
- SF latch / compose / present
- presentFence
- actual present

# 16.5 根因归属

- 一级根因：
- 二级根因：
- 证据：
- 排除项：

# 16.6 优化建议

- 短期止血
- 中期治理
- 长期架构优化

------


<!-- source: 56-172-buffer.md -->

# 17.2 Buffer / 渲染链路优化

- 减少 buffer backpressure
- 合理配置 buffer 数量
- 优化 BLAST 场景下事务同步
- 避免尺寸频繁变化
- 缓解 GPU 高峰
- 减少大纹理上传与同步等待

------


<!-- source: 58-174-hwc.md -->

# 17.4 驱动 / HWC 侧优化

- 优化 validate/present 耗时
- 排查 presentFence 异常延迟
- 检查 VSYNC source 稳定性
- 优化动态刷新率切换路径
- 降低 client composition fallback

------


<!-- source: 60-19.md -->

# 19. 使用要求

在实际执行中，必须遵守以下要求：

1. 先建立完整时序链，再判断根因
2. 不允许只凭单个 trace slice 下结论
3. 不允许把 queueBuffer 等同于上屏
4. 不允许忽略 RenderThread / GPU / fence
5. 不允许脱离 refresh rate 背景讨论 frame budget
6. Android 12+ 优先结合 FrameTimeline 分析
7. 高刷设备必须明确当前刷新周期与 deadline
8. 动态刷新率设备必须检查 mode switch 对时序的影响

------


<!-- source: 62-21.md -->

# 21. 一句话工作准则

> 分析 VSYNC 问题时，永远不要只问“这一帧为什么慢”，而要问“这一帧应该在什么时候开始、什么时候完成、什么时候上屏，以及究竟是哪一层破坏了这个时间契约”。
