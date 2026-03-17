# 输出模板与检查清单
<!-- source: 15-102-createcapturesession-configurestreams.md -->

# 10.2 createCaptureSession / configureStreams 调用链

```
App
 └─ CameraDevice.createCaptureSession()
     └─ CameraDeviceImpl.configureStreamsChecked()
         └─ ICameraDeviceUser.beginConfigure()
         └─ createOutputStream / deleteStream
         └─ endConfigure()
             └─ CameraDeviceClient::endConfigure()
                 └─ Camera3Device::configureStreamsLocked()
                     └─ HAL configureStreams()
```

分析重点：

- OutputConfiguration / Surface 数量和类型
- format / size / dataspace / usage
- Preview + JPEG + Video 组合是否超出能力
- deferred surface / shared surface 行为
- HAL 对 stream combination 的支持

------


<!-- source: 18-105-video-recording.md -->

# 10.5 video recording 调用链

```
App
 └─ createCaptureSession(preview + recorder surface)
     └─ configureStreams
         └─ preview stream + encoder input stream
             └─ repeating request for preview/video template
                 └─ camera output to MediaRecorder / MediaCodec surface
                     └─ encoder
                         └─ muxer / file
```

分析重点：

- recorder surface 是否创建成功
- video template metadata 是否正确
- encoder backlog 是否反压 camera buffer
- stopRecording 阶段 flush / drain 是否异常

------


<!-- source: 35-133.md -->

# 13.3 预览花屏 / 尺寸错乱

常见根因：

- stream format / usage / dataspace 不匹配
- YUV / PRIVATE / IMPLEMENTATION_DEFINED 理解错误
- consumer 期望尺寸与 producer 输出尺寸不一致
- transform / crop / rotation 处理错误
- 厂商 HAL buffer layout 问题

------


<!-- source: 41-151-startrecording.md -->

# 15.1 startRecording 失败

排查点：

- recorder surface 是否创建成功
- session 是否包含 recorder stream
- stream combination 是否受限
- encoder 初始化是否成功
- request template 是否为录像模板
- audio/video 同步链路是否完整


<!-- source: 43-153.md -->

# 15.3 录像掉帧

排查点：

- camera output 帧率是否稳定
- encoder 是否过载
- buffer 队列是否持续积压
- 大分辨率 + 高码率 + 稳定防抖组合是否超能力
- app 主线程是否影响控制逻辑

------


<!-- source: 48-173.md -->

# 17.3 必查项

- 输出 Surface 类型
- gralloc usage
- BufferQueue 生产消费速度
- acquire / release 节奏
- Surface 是否断连
- ImageReader `maxImages`
- MediaCodec 是否 backlog

------


<!-- source: 70-27.md -->

# 27. 交付模板

以下为本 Skill 的标准输出模板：

# Camera 问题分析报告
