# 架构与核心机制
<!-- source: 02-1.md -->

# 1. 目标定位

该 Skill 用于对 Android 媒体子系统进行**体系化源码分析、调用链还原、时序建模、异常归因、性能诊断与优化建议输出**。  
关注范围覆盖：

- **应用接口层**
  - MediaPlayer
  - MediaCodec / MediaExtractor / MediaMuxer
  - AudioTrack / AudioRecord
  - MediaRecorder
  - AImageReader / ImageReader
- **Framework Java 层**
  - android.media.*
  - MediaPlayerService 接入点
  - AudioManager / AudioPolicyManager 接口映射
- **Native Framework 层**
  - libmedia
  - libmediaplayerservice
  - libstagefright
  - libstagefright_foundation
  - libaudioclient
  - libaudioprocessing
  - libaudiofoundation
- **核心服务层**
  - mediaserver（历史）
  - media.codec
  - media.extractor
  - audioserver
  - cameraserver（与媒体链路交互时）
- **播放/解码组件**
  - NuPlayer / NuPlayerDriver
  - GenericSource / RTSPSource / StreamingSource
  - MediaCodec / ACodec / CCodec
  - OMXNodeInstance（兼容路径）
  - Codec2Client / C2 组件
- **音频系统**
  - AudioTrack / AudioRecord
  - AudioFlinger
  - PlaybackThread / RecordThread / MixerThread / OffloadThread / DirectOutputThread
  - AudioPolicyService / AudioPolicyManager
  - Volume / Focus / Route / Device Selection
- **视频输出系统**
  - Surface / SurfaceControl
  - BufferQueue / BLASTBufferQueue（关联理解）
  - ANativeWindow
  - SurfaceFlinger 合成链路
  - HWC / Display pipeline（必要时向下延展）
- **时间同步与渲染控制**
  - AudioClock / MediaClock
  - timestamps / PTS / DTS
  - VSYNC 对齐
  - render scheduling
  - frame drop / late frame / AV sync
- **安全与保护**
  - DRM / Crypto / secure decoder / protected buffer
- **录制链路**
  - AudioRecord
  - Camera → MediaCodec → MediaMuxer
  - MediaRecorder / StagefrightRecorder

---


<!-- source: 05-4.md -->

# 4. 输出规范

每次执行该 Skill，输出必须尽量包含以下结构：

### 4.1 标准输出结构
1. **问题定义**
2. **现象归纳**
3. **模块边界**
4. **完整调用链**
5. **关键类与关键方法**
6. **线程/进程模型**
7. **状态机/时序图**
8. **数据流/Buffer 流**
9. **时间戳/同步机制**
10. **异常根因定位**
11. **验证方法**
12. **优化建议**
13. **相关源码索引**

### 4.2 强制输出项
- 架构设计思想
- 模块架构图
- 关键时序图
- 核心源码逐段解释
- 真实调用链
- 问题定位证据
- 可执行验证手段

---


<!-- source: 06-5-media.md -->

# 5. Media 总体分层模型

```text
App
 ├─ MediaPlayer / ExoPlayer(Java API使用MediaCodec/AudioTrack等)
 ├─ MediaCodec / MediaExtractor / AudioTrack / AudioRecord
 └─ MediaRecorder / CameraX / Camera2

Framework Java
 └─ android.media.*

JNI / Native Framework
 ├─ libmedia
 ├─ libmediaplayerservice
 ├─ libstagefright
 ├─ libaudioclient
 └─ libaudiofoundation

System Services
 ├─ media.extractor
 ├─ media.codec
 ├─ audioserver
 ├─ cameraserver
 └─ drm / resource manager 等

Codec / Audio / Render
 ├─ NuPlayer / GenericSource
 ├─ MediaCodec / CCodec / ACodec / OMX
 ├─ AudioFlinger / AudioPolicy
 ├─ Surface / BufferQueue / ANativeWindow
 └─ SurfaceFlinger / HWC

Kernel / Driver / HAL
 ├─ Audio HAL
 ├─ Codec HAL / vendor codec impl
 ├─ DRM / secure memory
 ├─ gralloc / ion / dma-buf
 └─ display/audio driver
```


<!-- source: 07-6-media.md -->

# 6. Media 核心分析地图


<!-- source: 08-61.md -->

# 6.1 播放总链路

```
App
 → MediaPlayer/MediaCodec
 → JNI
 → MediaPlayerService / NuPlayerDriver
 → NuPlayer
 → Source(Extractor/Streaming)
 → Decoder(MediaCodec/CCodec/ACodec)
 → 音频: AudioSink → AudioTrack → AudioFlinger → HAL
 → 视频: Surface/ANativeWindow → BufferQueue → SurfaceFlinger → HWC → Display
```


<!-- source: 10-63.md -->

# 6.3 音频播放总链路

```
App
 → AudioTrack Java
 → JNI
 → AudioTrack native
 → AudioFlinger
 → PlaybackThread (Mixer/Direct/Offload)
 → Audio HAL
 → DSP / codec / speaker
```


<!-- source: 11-64.md -->

# 6.4 视频解码渲染总链路

```
App
 → MediaCodec configure(surface)
 → codec output graphic buffer
 → ANativeWindow queueBuffer
 → BufferQueue
 → SurfaceFlinger acquire
 → HWC composition
 → panel present
```

------


<!-- source: 12-7.md -->

# 7. 媒体子模块专项分析模板

# 7.1 MediaPlayer / NuPlayer 分析模板


<!-- source: 13-topic-13.md -->

# 关注点

- setDataSource 如何接入
- prepare / prepareAsync / start / pause / seekTo / stop 状态变化
- NuPlayerDriver 与 NuPlayer 的关系
- Source、Decoder、Renderer 如何协同
- 音视频时钟由谁主导
- buffering 与 rebuffering 怎么触发
- seek 时 flush / resume 如何实现


<!-- source: 14-topic-14.md -->

# 核心类

- `MediaPlayer.java`
- `android_media_MediaPlayer.cpp`
- `MediaPlayerService.cpp`
- `MediaPlayerService::Client`
- `NuPlayerDriver.cpp`
- `NuPlayer.cpp`
- `NuPlayer::Source`
- `NuPlayer::DecoderBase`
- `NuPlayer::Renderer`
- `NuPlayer::GenericSource`


<!-- source: 15-topic-15.md -->

# 重点方法

- `MediaPlayer::setDataSource`
- `MediaPlayer::prepareAsync`
- `NuPlayerDriver::start`
- `NuPlayer::onMessageReceived`
- `NuPlayer::instantiateDecoder`
- `NuPlayer::Renderer::onQueueBuffer`
- `NuPlayer::performSeek`


<!-- source: 18-topic-18.md -->

# 核心类

- `MediaCodec.java`
- `android_media_MediaCodec.cpp`
- `MediaCodec.cpp`
- `ACodec.cpp`
- `CCodec.cpp`
- `Codec2Client.cpp`
- `OMXNodeInstance.cpp`
- `MediaCodecList.cpp`
- `MediaCodecInfo.cpp`


<!-- source: 22-topic-22.md -->

# 核心类

- `MediaExtractor.java`
- `MediaExtractor.cpp`
- `IMediaExtractorService`
- `NuMediaExtractor.cpp`
- `DataSource.cpp`
- `FileSource.cpp`
- `HTTPBase.cpp`
- `MPEG4Extractor.cpp`
- `MatroskaExtractor.cpp`
- `MP3Extractor.cpp`


<!-- source: 24-topic-24.md -->

# 关注点

- AudioTrack 创建流程
- shared memory / client proxy / server proxy 如何工作
- FastTrack / NormalTrack / OffloadTrack 判定条件
- write 阻塞原因
- underrun / latency 从哪里产生
- AudioFlinger 线程模型
- MixerThread 混音时机
- DirectOutput 与 Offload 区别
- timestamp 获取机制
- 应用低时延配置为何可能不生效


<!-- source: 25-topic-25.md -->

# 核心类

- `AudioTrack.java`
- `android_media_AudioTrack.cpp`
- `AudioTrack.cpp`
- `AudioSystem.cpp`
- `AudioFlinger.cpp`
- `Threads.cpp`
- `PlaybackTracks.h`
- `AudioPolicyService.cpp`
- `AudioPolicyManager.cpp`


<!-- source: 32-topic-32.md -->

# 核心类

- `Surface.cpp`
- `BufferQueueProducer.cpp`
- `BufferQueueConsumer.cpp`
- `ANativeWindow`
- `SurfaceFlinger`
- `GraphicBuffer`
- `Fence`


<!-- source: 36-topic-36.md -->

# 关注点

- DRM session 建立流程
- Crypto plugin 注入点
- secure codec 与 normal codec 的差异
- protected buffer 是否需要 secure surface
- L1/L3 行为边界
- black screen 是否因 secure path 不完整
- 截屏失败是否符合安全设计


<!-- source: 38-8.md -->

# 8. 架构设计思想分析框架

分析源码时，必须回答以下设计问题：

### 8.1 为什么媒体系统高度异步化

- 播放/录制天然是流式处理
- 网络、解复用、解码、渲染速度不同
- 需要多线程解耦避免相互阻塞
- buffer queue 是吞吐与抖动吸收核心

### 8.2 为什么大量使用状态机

- prepare/start/pause/seek/flush/stop 是离散状态转换
- codec 生命周期复杂
- 错误恢复依赖明确状态边界
- 多线程条件下需要有限状态约束

### 8.3 为什么音频和视频链路分治

- 音频实时性要求更高
- 视频更关注吞吐与渲染同步
- 音频通常作为主时钟
- 两条链路在 renderer 处重新协调

### 8.4 为什么媒体系统高度依赖 Binder

- App 与系统服务隔离
- 资源集中管理
- codec / extractor / audio policy 需要独立服务
- 安全与权限边界明确

### 8.5 为什么媒体系统经常出现“看似随机”的问题

- 时间敏感
- 缓冲区竞争
- 多线程竞态
- 外设/网络/驱动不稳定
- vendor 实现差异大

------


<!-- source: 40-10.md -->

# 10. 生命周期分析模板

### 10.1 MediaPlayer 生命周期

```
idle
 → initialized
 → preparing
 → prepared
 → started
 → paused
 → stopped / playback completed
 → end / error
```

### 10.2 MediaCodec 生命周期

```
uninitialized
 → configured
 → started
 → executing(buffer exchange)
 → flushed
 → stopped
 → released
```

### 10.3 AudioTrack 生命周期

```
create
 → set
 → start
 → write loop
 → pause/stop
 → flush
 → release
```

分析时要明确：

- 每个阶段可调用哪些接口
- 非法状态调用如何报错
- 资源在哪个阶段真正申请/释放
- stop 和 release 的差异

------


<!-- source: 42-111.md -->

# 11.1 播放问题推荐顺序

1. Java API 入口
2. JNI 桥接
3. Client / Service binder 接口
4. NuPlayerDriver
5. NuPlayer
6. Source / Extractor
7. Decoder(MediaCodec/CCodec/ACodec)
8. Renderer
9. AudioTrack / Surface 输出
10. AudioFlinger / BufferQueue / SurfaceFlinger


<!-- source: 43-112.md -->

# 11.2 音频问题推荐顺序

1. AudioTrack / AudioRecord Java
2. JNI
3. native AudioTrack / AudioRecord
4. AudioSystem
5. AudioPolicyManager
6. AudioFlinger
7. Thread / Track / HAL


<!-- source: 44-113.md -->

# 11.3 解码问题推荐顺序

1. MediaCodec API
2. MediaCodec native
3. CCodec / ACodec
4. codec list / capability
5. output surface / buffer queue
6. vendor codec logs

------


<!-- source: 46-121.md -->

# 12.1 视频播放启动时序

```
App
 → MediaPlayer.setDataSource
 → MediaPlayer.prepareAsync
 → MediaPlayerService::Client
 → NuPlayerDriver::prepareAsync
 → NuPlayer instantiate source
 → extractor ready
 → instantiate decoder
 → codec configure/start
 → audio sink ready
 → first sample decode
 → first video frame queueBuffer
 → SurfaceFlinger acquire
 → first frame shown
```


<!-- source: 48-123-seek.md -->

# 12.3 Seek 时序

```
App seekTo
 → player driver dispatch
 → source seek
 → decoder flush
 → renderer flush
 → audio/video queue clear
 → new position sample read
 → decode resume
 → AV sync re-establish
 → first frame after seek
```

------


<!-- source: 51-141-logcat.md -->

# 14.1 logcat 关键标签

```
MediaPlayer
NuPlayer
NuPlayerDriver
MediaCodec
ACodec
CCodec
Codec2Client
OMXNodeInstance
AudioTrack
AudioRecord
AudioFlinger
AudioPolicyManager
BufferQueueProducer
BufferQueueConsumer
Surface
SurfaceFlinger
AudioManager
Stagefright
MediaExtractor
Drm
Crypto
```


<!-- source: 52-142-dumpsys.md -->

# 14.2 dumpsys 关键命令

```
dumpsys media.player
dumpsys media.audio_flinger
dumpsys media.audio_policy
dumpsys media.codec
dumpsys SurfaceFlinger
dumpsys activity services | grep media
dumpsys meminfo audioserver
dumpsys meminfo media.codec
```


<!-- source: 63-18.md -->

# 18. 禁止事项

禁止输出以下低质量内容：

- 只给概念介绍，不分析源码
- 只贴类名，不解释职责
- 只讲单点，不还原完整链路
- 忽略线程模型与状态机
- 忽略 buffer 生命周期
- 忽略时间戳与同步机制
- 用“可能是”替代证据推理
- 把 vendor 私有实现当成 AOSP 通用事实

------


<!-- source: 64-19.md -->

# 19. 推荐源码目录索引

```
frameworks/base/media/java/android/media/
frameworks/av/media/libmedia/
frameworks/av/media/libmediaplayerservice/
frameworks/av/media/libstagefright/
frameworks/av/media/codec2/
frameworks/av/media/libaudioclient/
frameworks/av/services/audioflinger/
frameworks/av/services/audiopolicy/
frameworks/av/services/mediacodec/
frameworks/av/services/mediaextractor/
frameworks/native/libs/gui/
frameworks/native/libs/ui/
frameworks/native/services/surfaceflinger/
hardware/interfaces/audio/
hardware/interfaces/media/
system/media/
```

------


<!-- source: 67-211.md -->

# 21.1 黑屏有声

```
请分析 AOSP Media 视频播放“黑屏有声”问题，重点从以下路径展开：

App → MediaPlayer/MediaCodec → decoder output → Surface/ANativeWindow → BufferQueue → SurfaceFlinger → HWC

要求：
- 说明黑屏可能发生的分层位置
- 给出 buffer 生命周期
- 解释 queueBuffer / acquireBuffer / present 各阶段证据
- 区分 codec 未产出、surface 未投递、sf 未显示 三类问题
```


<!-- source: 70-214.md -->

# 21.4 音视频不同步

```
请分析 AOSP Media AV sync 机制，要求说明：

- 主时钟来源
- audio/video PTS 如何流转
- renderer 如何决策渲染/丢帧
- seek、pause、speed change 对同步的影响
- 常见不同步根因模型
```

------


<!-- source: 71-22.md -->

# 22. 质量检查表

在输出前必须自检：

- 是否给出完整分层
- 是否给出真实调用链
- 是否给出关键类与关键方法
- 是否说明线程模型
- 是否说明状态机
- 是否说明 buffer 生命周期
- 是否说明时间戳与同步
- 是否有根因证据链
- 是否给出验证方法
- 是否明确 AOSP 与 vendor 边界

------
