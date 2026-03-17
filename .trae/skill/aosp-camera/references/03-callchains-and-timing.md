# 调用链与时序
<!-- source: 13-10.md -->

# 10. 关键调用链模板


<!-- source: 16-103-repeating-preview-request.md -->

# 10.3 repeating preview request 调用链

```
App
 └─ setRepeatingRequest()
     └─ CameraDeviceImpl.submitCaptureRequest()
         └─ ICameraDeviceUser.submitRequestList()
             └─ CameraDeviceClient::submitRequestList()
                 └─ Camera3Device::setStreamingRequestList()
                     └─ RequestThread 持续取请求
                         └─ HAL processCaptureRequest()
                             └─ processCaptureResult / notify 返回
                                 └─ Camera3OutputStream queue buffer to Surface
                                     └─ Surface consumer 显示或消费
```

分析重点：

- repeating request 是否成功建立
- RequestThread 是否运行
- in-flight requests 是否堆积
- HAL 是否持续返回 result / buffer
- preview surface consumer 是否正常消费

------


<!-- source: 17-104-still-capture.md -->

# 10.4 still capture 调用链

```
App
 └─ capture()
     └─ submit single CaptureRequest
         └─ Camera3Device / RequestThread
             └─ HAL processCaptureRequest()
                 ├─ shutter notify
                 ├─ partial / final metadata result
                 └─ JPEG / YUV buffer output
                     └─ ImageReader / app callback
```

分析重点：

- 3A 收敛是否影响拍照时延
- shutter notify 与 buffer 返回顺序
- jpeg stream 是否被正确配置
- maxImages / acquireLatestImage 是否导致阻塞
- callback 线程和 app 处理是否过慢

------


<!-- source: 19-106-switch-camera.md -->

# 10.6 switch camera 调用链

```
close old session/device
 └─ stop repeating / abort captures / close session
     └─ disconnect old device
         └─ open new camera
             └─ configure new streams
                 └─ first preview frame
```

分析重点：

- 老 session 是否完全释放
- close 与 reopen 是否串行完成
- 旧 surface 是否复用或重建
- camera provider reopen latency
- first frame latency 的主要阶段

------


<!-- source: 37-141.md -->

# 14.1 拍照慢

需要拆分时延：

- 触发拍照到 request submit
- request submit 到 shutter notify
- shutter 到 jpeg callback
- jpeg callback 到 app 保存完成

常见根因：

- 3A 未收敛
- HAL pipeline 深
- JPEG 编码慢
- 高分辨率流重配置
- ZSL / reprocess 路径未命中
- app 自身保存/后处理耗时高


<!-- source: 38-142.md -->

# 14.2 无照片 / 回调不回来

排查：

- capture request 是否提交成功
- jpeg / yuv stream 是否已配置
- shutter notify 是否存在
- final result 是否返回
- ImageReader 是否被阻塞
- app callback executor 是否卡住
- request 被 flush / close 中断


<!-- source: 56-201.md -->

# 20.1 关键性能指标

- `openCamera latency`
- `configureStreams latency`
- `first preview frame latency`
- `capture latency`
- `switch camera latency`
- `start recording latency`
- `stop recording latency`
- `request interval stability`
- `result callback jitter`
- `buffer backlog depth`


<!-- source: 62-213-perfetto-systrace.md -->

# 21.3 perfetto / systrace 分析点

关注以下线程/轨道：

- CameraService 相关线程
- binder transaction
- app camera callback thread
- render thread / UI thread
- MediaCodec / Encoder
- Surface / BufferQueue
- 摄像头 HAL 相关 vendor trace（若有）
- CPU 调度与 binder blocking

------


<!-- source: 65-24.md -->

# 24. 时序图模板


<!-- source: 69-26.md -->

# 26. 修复建议输出模板

修复建议必须按层给出：

### App 层

- 确保 Surface 生命周期稳定后再创建 session
- 避免未关闭旧 session 就切换模式
- 控制 ImageReader `maxImages` 并及时 `close()`
- 不要在 callback 中做重阻塞工作
- 正确选择 preview / capture / record 模板

### Framework 层

- 修正 session 状态切换
- 增强 close / flush 时资源回收
- 优化 request queue 和 inflight 管理
- 增补错误日志与时序埋点

### HAL 层

- 修复 stream combination 支持
- 修复 result / buffer 回调不完整
- 优化 first frame pipeline
- 修复 flush / close 清理不彻底
- 修复 metadata 返回不规范

### 验证建议

- 比较修复前后 open / configure / first frame 时延
- 验证 preview / capture / video / switch camera 四类基础场景
- 验证前后台切换、横竖屏、息屏恢复、多次开关 camera
- 验证多摄/逻辑摄像头场景
- 验证 ImageReader / MediaRecorder 并发输出

------


<!-- source: 75-5.md -->

# 5. 关键时序

- open：
- configure：
- first request：
- first result：
- first frame：
- capture / record：


<!-- source: 82-30-skill.md -->

# 30. Skill 结束语义

当执行完成后，输出应达到以下标准：

- 能说明“问题发生在哪里”
- 能说明“为什么会发生”
- 能说明“源码中是谁负责”
- 能说明“调用链如何流转”
- 能说明“证据如何支持结论”
- 能说明“如何修复与验证”

若无法达到，必须明确指出当前缺失的证据，而不是给出伪确定性结论。
