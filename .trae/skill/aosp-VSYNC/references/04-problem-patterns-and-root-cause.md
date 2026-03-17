# 问题模式与根因
<!-- source: 02-1.md -->

# 1. 目标定位

该 Skill 用于对 Android / AOSP 中 **VSYNC 驱动的整条显示时序链路**进行系统级源码分析与问题定位，覆盖：

- App UI 绘制节奏
- Choreographer / RenderThread 帧调度
- BufferQueue / BLASTBufferQueue 生产消费节奏
- SurfaceFlinger VSYNC 驱动的合成调度
- DispSync / Scheduler / EventThread / FrameTimeline
- HWC / Present Fence / Display Refresh
- 输入事件到最终上屏的端到端延迟分析
- 掉帧 / 卡顿 / Jank / 帧节奏漂移 / 抖动 / 不稳定刷新率问题分析

该 Skill 适用于：

- AOSP 源码阅读与架构分析
- Perfetto / systrace / FrameTimeline / dumpsys SurfaceFlinger 联合定位
- Android UI 卡顿、掉帧、延迟、时序错乱问题
- 高刷 / 动态刷新率 / variable refresh / idle refresh 异常
- SurfaceFlinger / HWC / 显示子系统时序问题
- 游戏、Launcher、SystemUI、动画场景的帧节奏分析

---


<!-- source: 36-112.md -->

# 11.2 读图顺序

建议严格按以下顺序读 trace：

### 第一步：先看掉的是哪一类帧

- 是 App missed？
- 是 SF missed？
- 是 present 晚？
- 是一帧直接没上屏？
- 是 cadence 不稳导致肉眼抖动？

### 第二步：看 Expected Timeline vs Actual Timeline

关注：

- 目标 present 时间
- 实际 present 时间
- 差值是稳定偏大还是偶发突刺
- 是连续多帧滞后还是孤立单帧

### 第三步：看 App 线程

关注：

- `doFrame` 是否准时开始
- 主线程有没有长任务
- traversal 是否超预算
- RenderThread 是否补刀拖慢

### 第四步：看 BufferQueue

关注：

- queueBuffer 是否及时
- 是否存在 dequeueBuffer 卡顿
- BufferTX 节奏是否断裂
- 当前 SF 周期是否拿到新 buffer

### 第五步：看 SurfaceFlinger

关注：

- latch 时机
- composite 时长
- TransactionQueue 是否堆积
- refresh 周期是否跳拍

### 第六步：看 HWC / fence

关注：

- presentFence 何时 signal
- HWC release 是否异常延迟
- GPU completion 是否晚于 present 窗口
- 是否存在 client composition 负担

------


<!-- source: 43-131-app.md -->

# 13.1 App 侧异常

1. 主线程 binder 调用过长导致错过 doFrame
2. 主线程锁竞争阻塞 traversal
3. measure/layout 复杂导致 traversal 超预算
4. RecyclerView 大量 bind/layout 导致一帧爆掉
5. Compose/View 层重组或重绘过重
6. 动画回调中执行重逻辑
7. 同步 IO 阻塞 UI 线程
8. GC 暂停打断 doFrame
9. Choreographer callback 堆积
10. scheduleTraversals 频繁但无法按拍完成
11. 输入回调处理过重
12. bitmap decode/upload 挤占 frame budget
13. overdraw 严重导致 GPU 负担重
14. shader 编译或 pipeline warmup 缺失
15. RenderThread 忙导致 queueBuffer 晚
16. 多 Surface 同时更新造成节奏失衡
17. SurfaceView/TextureView 双通路不同步
18. ANR 前夕 UI 线程长时间无帧提交
19. 业务线程高优抢占导致主线程调度滞后
20. CPU 降频导致固定周期超预算


<!-- source: 50-142.md -->

# 14.2 根因归属法

遇到 jank 帧，按以下规则归属：

### 规则 1：先看 App 是否按时产出 buffer

- 如果未按时 queueBuffer，优先归属 App / RenderThread / GPU

### 规则 2：若 App 已按时产出，再看 SF 是否按时 latch / compose

- 若 SF 周期处理异常，归属 SF

### 规则 3：若 SF 已按时提交，再看 presentFence

- 若 present 晚，归属 HWC / driver / display

### 规则 4：若整体链路都不重，但节奏波动明显

- 归属 refresh rate / scheduler / pacing 不稳

------


<!-- source: 51-143.md -->

# 14.3 不要犯的误判

以下误判非常常见：

- 看到掉帧就认定 App 慢
- 看到 App 耗时不长就排除 App，忽略 RenderThread/GPU
- 看到 queueBuffer 就认为已经上屏
- 看到 SF 忙就认为其为根因，忽略上游晚供帧
- 看到单帧 late 就误判为系统性问题，忽略 refresh rate 切换边界
- 只看 CPU slice，不看 fence / present 时间

------
