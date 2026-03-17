# 输出模板与检查清单
<!-- source: 16-topic-16.md -->

# 必须输出

- 播放状态机
- prepare/start/seek 时序图
- Source → Decoder → Renderer 数据流
- 音视频同步策略
- 首帧生成路径

------

# 7.2 MediaCodec / Codec2 / OMX 分析模板


<!-- source: 20-topic-20.md -->

# 必须输出

- codec 选择决策链
- buffer 生命周期图
- 状态机图
- surface 模式输出路径
- codec reset / error recovery 时序

------

# 7.3 Stagefright / Extractor 分析模板


<!-- source: 21-topic-21.md -->

# 关注点

- 容器格式如何识别
- sniff 逻辑如何决定 extractor
- 本地文件 / http / rtsp / dash 等 source 区别
- metadata 如何传递到 decoder
- seek map / sample table 如何工作
- 大文件 / 网络流 seek 限制在哪里
- DRM 容器如何处理


<!-- source: 23-topic-23.md -->

# 必须输出

- sniff → extractor 选择流程
- sample 读取链路
- 时间戳获取方式
- seek 的索引依赖
- extractor 与 codec 的接口边界

------

# 7.4 AudioTrack / AudioFlinger 分析模板


<!-- source: 26-topic-26.md -->

# 重点方法

- `AudioTrack::set`
- `AudioTrack::start`
- `AudioTrack::write`
- `AudioTrack::obtainBuffer`
- `AudioFlinger::createTrack`
- `PlaybackThread::threadLoop`
- `MixerThread::prepareTracks_l`
- `AudioPolicyManager::getOutputForAttr`


<!-- source: 27-topic-27.md -->

# 必须输出

- App → AudioTrack → AudioFlinger → HAL 完整链路
- Track 创建与路由决策
- 播放线程模型
- latency 分布图
- underrun / no sound 归因路径

------

# 7.5 AudioRecord / 录音链路模板


<!-- source: 30-topic-30.md -->

# 必须输出

- 录音数据流图
- RecordThread 线程模型
- mic → HAL → AudioFlinger → App 链路
- read 延迟与无数据根因分析

------

# 7.6 视频输出 / Surface / BufferQueue 模板


<!-- source: 31-topic-31.md -->

# 关注点

- MediaCodec configure(surface) 后 output buffer 去向
- GraphicBuffer 如何分配
- dequeueBuffer / queueBuffer 卡顿原因
- consumer acquireBuffer 阻塞条件
- Surface disconnect/reconnect 的影响
- 解码输出尺寸/色彩格式问题
- 黑屏是否因为没有成功 queueBuffer
- queue 成功但未显示是否在 SF/HWC 层丢失


<!-- source: 33-topic-33.md -->

# 必须输出

- codec output → surface queue → sf acquire → display 时序图
- buffer 生命周期图
- fence 等待点
- 黑屏问题定位分层结论

------

# 7.7 AV Sync 专项模板


<!-- source: 35-topic-35.md -->

# 必须输出

- clock source 定义
- 音视频时钟关系图
- 同步算法说明
- 不同步的具体偏差来源
- 可验证时间线

------

# 7.8 DRM / Secure Playback 模板


<!-- source: 37-topic-37.md -->

# 必须输出

- DRM session → decoder → secure buffer → display 链路
- secure surface 依赖
- 失败点分类
- 厂商实现边界说明

------


<!-- source: 39-9.md -->

# 9. 线程模型分析模板

执行分析时，必须识别：

### 9.1 线程清单

- 调用线程（App 主线程/业务线程）
- binder 线程
- NuPlayer looper 线程
- decoder 回调线程
- renderer 线程
- AudioFlinger PlaybackThread / RecordThread
- SurfaceFlinger 合成线程
- codec 内部工作线程
- vendor codec 线程

### 9.2 每个线程必须回答

- 谁创建
- 谁销毁
- 事件来源
- 阻塞点
- 共享数据
- 锁
- 是否可能产生优先级反转

### 9.3 常见阻塞点

- `dequeueInputBuffer`
- `dequeueOutputBuffer`
- `queueBuffer`
- `obtainBuffer`
- binder transact
- fence wait
- condition variable wait
- codec flush / reset

------


<!-- source: 59-16.md -->

# 16. 分析输出模板


<!-- source: 66-21-prompt.md -->

# 21. 专项 Prompt 模板
