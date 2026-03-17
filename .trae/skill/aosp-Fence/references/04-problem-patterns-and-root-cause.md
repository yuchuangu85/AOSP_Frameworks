# 问题模式与根因
<!-- source: 11-10-perfetto-trace.md -->

# 10. Perfetto / Trace 分析规则

------

### 10.1 必看轨道

分析 Fence 时，重点检查：

- App 主线程
- RenderThread
- GPU completion
- SurfaceFlinger
- HWComposer
- FrameTimeline
- VSYNC-app / VSYNC-sf
- buffer queue 相关轨道
- present fence / release fence 相关轨道
- DRM / display commit（若有）
- kernel sync/dma_fence/drm trace（若开启）

------

### 10.2 Fence 分析四步法

#### 第一步：定位异常帧

找到：

- Jank frame
- missed vsync frame
- 大于 16.6ms / 11.1ms / 8.3ms 的帧
- expected present 与 actual present 偏移大的帧

#### 第二步：判断慢在哪一段

拆解：

- Input/App 逻辑慢
- RenderThread/GPU 慢
- acquireFence wait 长
- SurfaceFlinger 合成慢
- present 晚
- release 返回晚

#### 第三步：验证 Fence 证据

确认：

- acquireFence signal 时刻
- SF latch 时刻
- presentFence 时刻
- releaseFence 时刻

#### 第四步：回推根因

将 Fence 时序映射回：

- Render pipeline
- composition pipeline
- display pipeline

------

### 10.3 典型 Perfetto 判断模板

#### 模式 A：App/GPU 慢

特征：

- App 提交 buffer 晚
- acquireFence signal 晚
- SF 等 acquireFence
- present 连锁延迟

结论：

> Fence 只是承接 GPU 渲染慢的表现，根因在生产端。

#### 模式 B：SF/HWC 慢

特征：

- acquireFence 很快 signal
- SF/HWC 执行耗时长
- presentFence 晚
- actual present 晚

结论：

> buffer 内容准备好了，但系统合成/显示阶段推进慢。

#### 模式 C：Display pipeline 慢

特征：

- SF 提交及时
- HWC present 后 releaseFence/retireFence 仍然很晚
- 下游 scanout 深度堆积

结论：

> 问题在 HWC/DRM/panel 侧。

------


<!-- source: 13-12.md -->

# 12. 常见异常模式库

------

### 模式 1：acquireFence 长时间未 signal

**现象**

- SF 无法及时 latch
- 某 layer 帧迟迟不上屏

**根因方向**

- GPU 渲染长尾
- RenderThread 卡顿
- driver backlog

------

### 模式 2：releaseFence 返回过晚

**现象**

- producer dequeueBuffer 卡住
- triple buffer 仍不够用

**根因方向**

- display pipeline 长时间占用 buffer
- release 回传链路异常

------

### 模式 3：presentFence 晚于预期 VSYNC

**现象**

- FrameTimeline actual present 晚
- 明显掉帧但 App 阶段不慢

**根因方向**

- SF/HWC/DRM present 迟滞

------

### 模式 4：retireFence 连续滞后

**现象**

- 帧在显示队列堆积
- 帧新鲜度下降，出现“慢半拍”

**根因方向**

- panel/DRM 扫描链路拥塞
- pipeline depth 增加

------

### 模式 5：Fence fd 传递错误或重复使用

**现象**

- wait 永久阻塞
- signal 状态异常
- 某些机型偶发黑屏/闪屏

**根因方向**

- HAL/驱动实现 bug
- fence 生命周期管理错误

------

### 模式 6：BufferQueue slot 耗尽

**现象**

- dequeueBuffer 阻塞
- queue 端突增延迟

**根因方向**

- releaseFence 太晚
- producer 过快、consumer 过慢
- buffer 数量配置不足只是表象

------

### 模式 7：首帧显示慢

**现象**

- App 已启动但界面晚显示
- First frame metrics 差

**根因方向**

- 首帧 acquireFence 慢
- SF 首次 latch/present 慢
- HWC/client composition 初始化开销

------

### 模式 8：滑动时持续轻微卡顿

**现象**

- 每帧都不极端慢，但整体不跟手

**根因方向**

- acquireFence 稍晚 + presentFence 稍晚叠加
- pipeline 每帧均轻微漂移
- 帧虽未完全丢失，但实际显示滞后

------


<!-- source: 15-14.md -->

# 14. 标准输出要求

调用本 Skill 时，输出必须尽量包含以下结构：

### 14.1 问题定义

- 用户观察到的现象
- 是否是卡顿/掉帧/黑屏/显示延迟/Buffer 堆积
- 涉及的帧、窗口、layer、buffer

### 14.2 Fence 类型识别

- 当前分析涉及哪类 Fence
- 哪个 Fence 是主要证据
- 哪个 Fence 是结果，哪个是根因线索

### 14.3 完整时序图

至少给出：

- dequeueBuffer
- draw/render
- queueBuffer
- acquireFence signal
- SF latch
- HWC present
- presentFence
- retireFence / releaseFence

### 14.4 跨层调用链

必须说明：

- App/GPU 是否慢
- BufferQueue 是否堆积
- SF 是否阻塞
- HWC/DRM/display 是否延迟

### 14.5 根因结论

结论必须落到明确责任层，例如：

- App RenderThread 过慢
- GPU completion 延迟
- SurfaceFlinger latch/present 迟滞
- HWC present 回调延迟
- DRM atomic commit 过慢
- panel scanout 造成 release 滞后

### 14.6 证据清单

必须列出：

- 源码依据
- trace 依据
- dumpsys/log 依据
- 如果有不确定性，明确说明缺失证据

------


<!-- source: 26-topic-26.md -->

# 五、异常模式匹配
- 命中模式：
- 模式解释：
- 与当前日志/trace 对应关系：


<!-- source: 27-topic-27.md -->

# 六、根因分析
- 表层现象：
- 直接阻塞方：
- 根因责任层：
- 为什么不是其他层：
