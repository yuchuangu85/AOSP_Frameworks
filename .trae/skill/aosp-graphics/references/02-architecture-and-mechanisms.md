# 架构与核心机制
<!-- source: 08-65.md -->

# 6.5 区分生产端与消费端
Buffer 类问题必须回答：

- Producer 慢，还是 Consumer 慢
- dequeue 卡住，还是 queue 后未显示
- acquire 慢，还是 release 慢
- Fence 卡住，还是 layer state 不可见
- transaction 没同步，还是 display 没 present

---

# 7. Android 图形栈总体架构


<!-- source: 09-71.md -->

# 7.1 宏观分层

```text
App / SystemUI / Launcher
  ↓
View System / Choreographer / ViewRootImpl
  ↓
HWUI / RenderNode / RenderThread / Skia / EGL / Vulkan
  ↓
Surface / SurfaceControl / BLASTBufferQueue
  ↓
BufferQueue / Gralloc / Sync Fence
  ↓
SurfaceFlinger / CompositionEngine / Scheduler
  ↓
HWC HAL / Composer HAL
  ↓
DRM / KMS / Display Driver
  ↓
Panel / Display Hardware
```


<!-- source: 10-2.md -->

# .2 关键进程

- 应用进程
- `system_server`
- `surfaceflinger`
- `vendor composer service`
- 显示驱动相关内核线程
- GPU 驱动相关线程


<!-- source: 11-73.md -->

# 7.3 关键线程

### App 侧

- main thread
- RenderThread
- binder thread
- image decode / worker thread（视场景）
- GPU driver interaction 相关线程

### SurfaceFlinger 侧

- SF main thread
- EventThread
- Scheduler 相关线程
- binder thread
- composition / present 相关线程

### 内核 / 驱动侧

- HWC / composer 线程
- DRM/KMS worker
- GPU driver worker
- fence / irq / display pipeline worker

------

# 8. 核心模块地图


<!-- source: 12-81-framework-app-layer.md -->

# 8.1 Framework / App Layer

常见源码路径：

- `frameworks/base/core/java/android/view/ViewRootImpl.java`
- `frameworks/base/core/java/android/view/Choreographer.java`
- `frameworks/base/core/java/android/view/Surface.java`
- `frameworks/base/core/java/android/view/SurfaceControl.java`
- `frameworks/base/core/java/android/window/BLASTBufferQueue.java`
- `frameworks/base/graphics/java/android/graphics/HardwareRenderer.java`
- `frameworks/base/libs/hwui/`
- `frameworks/base/libs/hwui/renderthread/`
- `frameworks/base/libs/hwui/pipeline/`


<!-- source: 13-82-native-gui-surface-infrastructure.md -->

# 8.2 Native GUI / Surface Infrastructure

- `frameworks/native/libs/gui/`
- `frameworks/native/libs/ui/`
- `frameworks/native/libs/nativewindow/`


<!-- source: 14-83-surfaceflinger.md -->

# 8.3 SurfaceFlinger

- `frameworks/native/services/surfaceflinger/SurfaceFlinger.cpp`
- `frameworks/native/services/surfaceflinger/SurfaceFlinger.h`
- `frameworks/native/services/surfaceflinger/CompositionEngine/`
- `frameworks/native/services/surfaceflinger/Scheduler/`
- `frameworks/native/services/surfaceflinger/DisplayHardware/`
- `frameworks/native/services/surfaceflinger/Layer.cpp`
- `frameworks/native/services/surfaceflinger/BufferLayer.cpp`
- `frameworks/native/services/surfaceflinger/TransactionState.cpp`


<!-- source: 16-85-drm-kms-display-driver.md -->

# 8.5 DRM / KMS / Display Driver

- `kernel/drivers/gpu/drm/`
- SoC vendor display driver
- panel / bridge / encoder / crtc / plane 相关目录

------

# 9. 完整 App → HWUI → BufferQueue → SurfaceFlinger → HWC → DRM → Panel 跨层调用链

本节是本 Skill 的核心强制模型。分析图形问题时，必须优先尝试把问题映射到该链路中的某一段。

------


<!-- source: 18-92-app.md -->

# 9.2 App 侧关键调用链

### Java / Framework

```
Choreographer#doFrame
  → ViewRootImpl#doTraversal
  → ViewRootImpl#performTraversals
  → View#measure
  → View#layout
  → View#draw
  → ThreadedRenderer / HardwareRenderer
  → nativeSyncAndDrawFrame
```

### Native / HWUI

```
HardwareRenderer
  → RenderProxy
  → CanvasContext
  → RenderThread::queueFrame
  → DrawFrameTask
  → SkiaPipeline / OpenGLPipeline / VulkanPipeline
  → ANativeWindow_dequeueBuffer
  → GPU render
  → queueBuffer
```

------


<!-- source: 20-94-surfaceflinger.md -->

# 9.4 SurfaceFlinger 一帧关键链路

```
EventThread / Scheduler 触发 refresh
  → SurfaceFlinger::handleMessageRefresh
  → processTransaction
  → latchBuffers
  → updateLayerGeometry / visible region / damage
  → CompositionEngine::present
  → HWC validateDisplay / presentDisplay
  → 更新 present fence / retire fence
```

------


<!-- source: 26-104-surface-surfacecontrol.md -->

# 10.4 Surface / SurfaceControl

职责：

- Surface 是 buffer producer 接口
- SurfaceControl 是 layer 属性与 transaction 控制接口

关键点：

- setBuffer
- setPosition / setAlpha / setCrop / setMatrix
- reparent
- leash layer
- transaction merge / apply


<!-- source: 40-124-queuebuffer.md -->

# 12.4 queueBuffer 成功但画面不显示模型

### 典型现象

- App 已完成渲染
- queueBuffer 成功
- 但屏幕迟迟无变化

### 常见原因

- SF 未 latch 到该 buffer
- transaction 未同步 apply
- layer 被遮挡 / hidden / alpha=0
- crop / transform 错误
- BLAST 同步失败
- present 未及时执行
- HWC / DRM 未真正完成 pageflip

------


<!-- source: 42-126-blast.md -->

# 12.6 BLAST 专项模型

BLAST 的本质是把 buffer 与 transaction 同步提交，避免 resize / relayout / rotation / transition 中 layer 状态与内容不同步。

### BLAST 的重点场景

- Activity 启动首帧
- Window relayout
- rotation
- 分屏
- PIP
- IME 弹出
- recent animation / shell transition

### BLAST 常见异常

1. 首帧 buffer 已出，但 transaction 未及时 apply
2. 几何属性已更新，但内容仍是旧尺寸
3. 新旧 buffer 交替导致闪烁
4. resize 窗口时短暂黑屏
5. transition leash 与真实内容层不同步

------


<!-- source: 46-133-dumpsys-surfaceflinger.md -->

# 13.3 `dumpsys SurfaceFlinger` 自动分析规则

### 必看项 1：Layer Hierarchy

检查：

- layer 数量
- 可见性
- parent/child 关系
- leash layer
- z-order
- crop / transform / alpha

### 必看项 2：Composition Type

检查：

- 哪些 layer 走 device composition
- 哪些 layer 走 client composition
- 是否大面积 fallback 到 GPU composition

### 必看项 3：Buffer State

检查：

- 当前 layer 是否有 buffer
- 尺寸 / format / dataspace
- 更新是否符合预期
- visible region 是否异常

### 必看项 4：Refresh / Vsync / Mode

检查：

- 当前刷新率
- 是否发生 mode switch
- 是否有 variable refresh rate 变化

### 必看项 5：Latency / Present

检查：

- SurfaceFlinger latency
- present 时间
- 层级对应实际显示滞后

------


<!-- source: 49-b-hwui-renderthread-gpu-13-28.md -->

# B. HWUI / RenderThread / GPU 类（13 ~ 28）

### 13. RenderThread 被延后调度

### 14. DrawFrameTask 过长

### 15. 纹理上传过重

### 16. 大图首次上传 GPU

### 17. Shader 首次编译卡顿

### 18. blur / shadow / path 复杂

### 19. 离屏渲染过多

### 20. overdraw 高

### 21. Alpha 混合层多

### 22. saveLayer 过多

### 23. clipPath / complex path 过重

### 24. GPU completion 晚

### 25. GPU 驱动线程阻塞

### 26. Vulkan/GL submit 延迟

### 27. Surface lock/unlock 与 GPU pipeline 不匹配

### 28. 首帧 render cache 未建立

每个模式都要检查：

- RenderThread slice
- GPU completion
- 是否伴随 acquire/present fence 拖尾

------


<!-- source: 50-c-bufferqueue-fence-29-44.md -->

# C. BufferQueue / Fence 类（29 ~ 44）

### 29. dequeueBuffer 阻塞

### 30. queueBuffer 成功但未显示

### 31. acquireBuffer 延迟

### 32. releaseBuffer 延迟

### 33. release fence 长时间不返回

### 34. present fence 拖尾

### 35. 单 layer buffer 数不足

### 36. producer 速度高于 consumer

### 37. SF latch 不及时导致堆积

### 38. Surface 销毁与 buffer 生命周期竞态

### 39. buffer 尺寸频繁变化

### 40. format / dataspace 不匹配

### 41. old buffer 被重复展示

### 42. fence 链式阻塞

### 43. 截图/录屏导致额外 consumer 压力

### 44. camera/video producer 抢占 buffer 资源

------


<!-- source: 51-d-blast-surfacecontrol-transaction-45-58.md -->

# D. BLAST / SurfaceControl / Transaction 类（45 ~ 58）

### 45. BLAST transaction 与 buffer 不同步

### 46. relayout 后首帧黑屏

### 47. resize 后新旧尺寸交替闪烁

### 48. rotation 过程中 crop 错误

### 49. leash layer 与内容层不同步

### 50. transaction storm

### 51. 多 transaction merge 抖动

### 52. alpha / crop / matrix 顺序错误

### 53. reparent 期间短暂无父层可见性异常

### 54. transition 结束时 layer 切换抖动

### 55. starting window 与真实窗口切换过慢

### 56. Shell transition 中 leash 生命周期异常

### 57. IME 动画 transaction 过密

### 58. 分屏 / PIP 动画中 surface 几何状态抖动

------


<!-- source: 52-e-surfaceflinger-59-72.md -->

# E. SurfaceFlinger 类（59 ~ 72）

### 59. SF `handleMessageRefresh` 过长

### 60. latchBuffers 过长

### 61. transaction apply 过重

### 62. layer 数过多

### 63. visible region 计算过重

### 64. damage 计算收益差

### 65. client composition fallback

### 66. GPU composition 压力大

### 67. blur/effect layer 导致 SF 压力高

### 68. screen recording / mirror display 拖慢 SF

### 69. refresh 节奏不稳定

### 70. EventThread 抖动

### 71. Scheduler 预测误差大

### 72. 多显示器同步引发 SF 压力

------


<!-- source: 53-f-hwc-drm-panel-73-88.md -->

# F. HWC / DRM / Panel 类（73 ~ 88）

### 73. HWC validate 过长

### 74. HWC present 过长

### 75. overlay / plane 不足导致 fallback

### 76. protected / HDR layer 导致 client composition

### 77. scaling / rotation capability 不支持

### 78. mode switch 导致 deadline 抖动

### 79. 可变刷新率切换不稳定

### 80. DRM atomic commit 过长

### 81. pageflip 等待长

### 82. panel timing 切换慢

### 83. Display driver 中断响应慢

### 84. external display 影响主屏节奏

### 85. doze / power mode 切换引起短暂黑屏

### 86. HWC release fence 返回延迟

### 87. panel TE / VSync 节奏异常

### 88. vendor composer service 抖动

------


<!-- source: 54-g-89-102.md -->

# G. 首帧 / 启动 / 黑屏 / 花屏扩展模式（89 ~ 102）

### 89. Activity 首帧 inflate + first draw 同时过重

### 90. 首帧 queueBuffer 太晚

### 91. first buffer 已 queue 但 window 未 visible

### 92. visible 了但无有效 buffer

### 93. starting window 退出过早

### 94. rotation 后旧 buffer 被拉伸显示

### 95. black frame 来自中间态空 layer

### 96. 花屏由 stride / format 错误导致

### 97. crop / transform 错误导致内容错位

### 98. parent leash alpha 错误导致整体不可见

### 99. Display cutout / insets 导致实际显示区域异常

### 100. secure / protected content 特殊路径黑屏

### 101. 休眠唤醒首帧迟到

### 102. 多窗口切换时 buffer source 与 target 混乱

------

# 15. 图形问题标准分析主流程


<!-- source: 59-5.md -->

# 步骤 5：源码机制解释

必须落到：

- 类
- 方法
- 触发条件
- 状态变化
- 为什么会导致该现象


<!-- source: 60-6.md -->

# 步骤 6：根因归纳

必须写成：

- 现象
- 证据
- 机制
- 根因
- 修复建议
- 验证方法

------

# 16. 关键性能指标体系


<!-- source: 63-163-sf.md -->

# 16.3 SF 指标

- latch time
- transaction apply time
- composition time
- present time
- layer count
- transaction count


<!-- source: 67-172-hwui-gpu.md -->

# 17.2 HWUI / GPU

- 预热 shader
- 控制大图上传
- 减少复杂 blur / shadow / path
- 减少 saveLayer 与离屏渲染
- 降低 overdraw


<!-- source: 69-174-surfaceflinger.md -->

# 17.4 SurfaceFlinger

- 控制 layer 数
- 减少 transaction storm
- 降低 effect layer / blur layer 数量
- 排查 client composition fallback
- 稳定 refresh rate 策略


<!-- source: 71-181.md -->

# 18.1 架构分析模板

```
一、分析目标
二、系统分层
三、关键对象模型
四、完整跨层调用链
五、关键源码位置
六、核心时序
七、设计思想
八、风险点
九、结论
```


<!-- source: 77-b.md -->

# B 级

- 机制吻合，但缺少一项关键直接证据
