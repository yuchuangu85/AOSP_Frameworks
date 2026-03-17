# 概览与范围
<!-- source: 00-overview.md -->

# AOSP Media Analysis Skill


<!-- source: 09-62.md -->

# 6.2 录制总链路

```
Mic / Camera
 → AudioRecord / CameraSource / Surface input
 → MediaCodec Encoder
 → MediaMuxer / Writer
 → output file / network stream
```


<!-- source: 17-topic-17.md -->

# 关注点

- createByCodecName / createDecoderByType 选择逻辑
- configure 时参数如何下发
- surface 模式与 bytebuffer 模式区别
- CCodec 与 ACodec 的实际选择条件
- OMX 兼容路径如何保留
- input/output buffer 生命周期
- callback mode 与 synchronous mode 区别
- flush / stop / release 的资源回收路径
- codec error / dead object / reset 处理


<!-- source: 19-topic-19.md -->

# 重点方法

- `MediaCodec::CreateByType`
- `MediaCodec::configure`
- `MediaCodec::start`
- `MediaCodec::dequeueInputBuffer`
- `MediaCodec::queueInputBuffer`
- `MediaCodec::dequeueOutputBuffer`
- `CCodec::configure`
- `ACodec::LoadedState / ExecutingState`
- `OMXNodeInstance::emptyBuffer / fillBuffer`


<!-- source: 28-topic-28.md -->

# 关注点

- AudioRecord 创建与权限检查
- input source / device / session id 选择
- RecordThread 数据采集路径
- read 阻塞点
- 录音无数据的典型原因
- 回声消除 / 降噪 / pre-processing 在哪层生效
- 音频焦点与录音并发冲突


<!-- source: 29-topic-29.md -->

# 核心类

- `AudioRecord.java`
- `android_media_AudioRecord.cpp`
- `AudioRecord.cpp`
- `AudioFlinger::openRecord`
- `RecordThread`
- `AudioPolicyManager::getInputForAttr`
