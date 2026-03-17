# 架构与核心机制
<!-- source: 03-2.md -->

# 2. 明确范围

### 2.1 包含范围

本 Skill 包含但不限于以下分析主题：

- Camera App 到 Framework 到 Native 到 HAL 的跨层链路
- Camera1 / Camera2 / CameraX 背后的 AOSP 核心实现映射
- CameraService / CameraDeviceClient / Camera3Device / RequestThread
- CameraProviderManager / CameraProvider / ICameraProvider / ICameraDeviceSession
- HAL3 Request / Result / Buffer / Metadata / Stream 配置
- Surface / BufferQueue / ImageReader / SurfaceTexture / MediaRecorder
- Preview / Still Capture / Video Recording / Reprocess / ZSL / OfflineSession
- 多摄协同、逻辑摄像头、物理摄像头、扩展能力
- Session 生命周期、流配置、状态机、错误恢复
- dumpsys media.camera / logcat / systrace / perfetto 联合分析
- Camera 性能与稳定性问题定位
- AOSP 设计思想与版本演进分析

### 2.2 排除范围

本 Skill **不包含 ANR 专项分析**，包括但不限于：

- Camera 导致的 Input ANR
- App 主线程卡死引发的 ANR
- system_server Camera 相关 ANR
- binder 调用超时引发的 ANR 归因专项

若问题核心是 “ANR 是否由 Camera 触发” 或 “ANR 归因链路分析”，应切换到独立的 **AOSP ANR Skill**，本 Skill 仅可提供 Camera 侧辅助证据，不承担 ANR 定性结论。

---


<!-- source: 04-3.md -->

# 3. 使用场景

当用户出现以下需求时触发本 Skill：

- “分析 AOSP Camera 源码架构”
- “梳理 CameraService 到 HAL 的调用链”
- “分析拍照流程 / 预览流程 / 录像流程”
- “为什么 Camera 打不开”
- “为什么 configureStreams 失败”
- “为什么预览黑屏 / 花屏 / 不出图”
- “为什么拍照慢 / 首帧慢 / 切摄像头慢”
- “分析 Camera metadata / Request / Result 流转”
- “分析多摄 / 逻辑摄像头”
- “分析 Camera 和 Surface / BufferQueue 的关系”
- “根据 logcat / dumpsys / trace 定位 camera 问题”
- “解释 camera provider / camera hal / framework 交互机制”
- “输出 camera 模块架构图 / 时序图 / 源码解释”

---


<!-- source: 10-7.md -->

# 7. 标准分析总流程

执行 Camera 分析时，遵循以下总流程：

### Step 1：界定问题类型

先判定属于哪一类：

- 打开失败（open failed）
- Session 配置失败（configureStreams failed）
- 预览异常（黑屏 / 花屏 / 卡住 / 不出图）
- 拍照异常（快门慢 / 无图 / 保存失败 / metadata 异常）
- 录像异常（起录失败 / 掉帧 / 音画问题 / stop 卡住）
- 模式切换异常（前后摄切换 / 拍照转录像 / session rebuild）
- 性能问题（首帧慢 / capture latency 高 / stream restart 慢）
- 多摄问题（logical / physical stream / sync）
- 资源争用问题（camera in use / eviction / policy）
- HAL / provider 通信异常
- buffer / surface 链路异常

### Step 2：确定入口层

判断问题从哪一层切入最合适：

- App / Camera2 API 层
- Framework Java 层
- Framework Native / CameraService 层
- Provider / HAL 适配层
- Surface / Buffer / Consumer 层
- MediaRecorder / Encoder / ImageReader 层

### Step 3：建立完整调用链

按场景建立跨层调用链，不允许只分析单层。

### Step 4：还原关键时序

还原以下关键时间点：

- openCamera 起点
- connect / initialize 完成
- configureStreams 起止
- first repeating request 发出
- first result 返回
- first preview buffer 到达
- shutter / jpeg callback / image available
- startRecording / stopRecording 时序
- session close / reopen / switch camera

### Step 5：提取关键证据

从以下证据中形成闭环：

- logcat 中服务端与 HAL 交互日志
- dumpsys media.camera 中的 client / device / stream / request 信息
- trace 中 camera / binder / buffer / app 渲染线程时间线
- 源码中的状态转换与返回码
- metadata / stream config / usage flags / format / size

### Step 6：根因分类

将根因归入以下类型之一：

- 权限 / 资源占用 / policy 冲突
- open / initialize 失败
- stream 配置不兼容
- surface / producer-consumer 关系错误
- request 未下发或结果未返回
- HAL pipeline 堵塞
- buffer backlog / dequeueBuffer 卡住
- metadata 设置错误
- 多摄能力声明与实际实现不一致
- session 生命周期切换错误
- app 使用方式错误
- 厂商 HAL 私有实现异常

### Step 7：给出修复建议

修复建议必须分层给出：

- App 层修复
- Framework 层修复
- HAL 层修复
- 参数配置修复
- 性能优化修复
- 验证与回归建议

---


<!-- source: 11-8-camera.md -->

# 8. Camera 核心架构总览

---

### 8.1 总体分层

```text
App
 └─ CameraX / Camera2 / Camera API
     └─ Framework Java
         └─ Framework JNI / Native glue
             └─ CameraService
                 ├─ CameraDeviceClient
                 ├─ Camera3Device
                 ├─ RequestThread
                 ├─ CameraProviderManager
                 └─ Torch / Status / Policy
                     └─ Camera Provider (HIDL/AIDL)
                         └─ Camera HAL3 DeviceSession
                             ├─ configureStreams
                             ├─ processCaptureRequest
                             ├─ processCaptureResult
                             └─ flush / close
                                 └─ Gralloc / BufferQueue / ISP / Sensor / Encoder / Display
```

### 8.2 核心设计思想

1. **Framework 与 HAL 分层清晰**
    Framework 负责 API 语义、状态机、权限、资源仲裁、流配置组织；HAL 负责具体 sensor / ISP / buffer pipeline。
2. **请求-结果异步模型**
    Camera3 采用 `CaptureRequest -> processCaptureRequest -> processCaptureResult / notify` 异步模型。
3. **流配置先行**
    预览、拍照、录像等都要先经过 stream configuration，再进入请求循环。
4. **Metadata 驱动 pipeline**
    AE / AF / AWB、曝光、对焦、crop、flash、jpeg、video 等行为均通过 metadata 驱动。
5. **Surface / Buffer 解耦输出端**
    Camera 不直接画图，而是输出到 `Surface`，后续由 Preview、ImageReader、MediaRecorder、Codec 等消费。
6. **多路输出并发**
    一个 session 可同时配置 preview / jpeg / video / yuv 等多个 stream，但受 HAL 能力限制。

------


<!-- source: 12-9.md -->

# 9. 核心源码模块索引

以下路径以 AOSP 通用目录为主，不同版本可能略有差异。

### 9.1 Framework Java 层

- `frameworks/base/core/java/android/hardware/camera2/`
- `frameworks/base/core/java/android/hardware/camera2/impl/`
- `frameworks/base/core/java/android/hardware/camera2/params/`
- `frameworks/base/core/java/android/hardware/camera2/CameraManager.java`
- `frameworks/base/core/java/android/hardware/camera2/CameraDevice.java`
- `frameworks/base/core/java/android/hardware/camera2/CaptureRequest.java`
- `frameworks/base/core/java/android/hardware/camera2/CameraCaptureSession.java`

重点类：

- `CameraManager`
- `CameraDeviceImpl`
- `CameraCaptureSessionImpl`
- `CaptureRequest`
- `OutputConfiguration`
- `SessionConfiguration`

### 9.2 JNI / Native 桥接

- `frameworks/base/core/jni/`
- `frameworks/av/camera/`
- `frameworks/av/camera/ndk/`

重点类：

- `CameraManagerGlobal`
- `CameraMetadata`
- `ICameraService`
- `ICameraDeviceUser`

### 9.3 CameraService 服务端

- `frameworks/av/services/camera/libcameraservice/`

重点文件：

- `CameraService.cpp`
- `CameraService.h`
- `CameraDeviceClient.cpp`
- `CameraDeviceClient.h`
- `Camera3Device.cpp`
- `Camera3Device.h`
- `Camera3OutputStream.cpp`
- `Camera3InputStream.cpp`
- `Camera3Stream.cpp`
- `Camera3IOStreamBase.cpp`
- `Camera3BufferManager.cpp`
- `Camera3OfflineSession.cpp`
- `CameraProviderManager.cpp`
- `CameraFlashlight.cpp`
- `utils/`
- `common/`

### 9.4 Provider / HAL 接口层

- `hardware/interfaces/camera/`
- `frameworks/av/services/camera/libcameraservice/common/`
- `hardware/libhardware/include/hardware/camera3.h`

重点接口：

- `ICameraProvider`
- `ICameraDevice`
- `ICameraDeviceSession`
- `camera3_device_ops`
- `camera3_callback_ops`

### 9.5 相关图形与 Buffer 层

- `frameworks/native/libs/gui/`
- `frameworks/native/services/surfaceflinger/`
- `frameworks/native/libs/nativewindow/`
- `frameworks/base/media/java/android/media/ImageReader.java`
- `frameworks/av/media/libstagefright/`
- `frameworks/av/media/`

### 9.6 编码 / 录像相关

- `frameworks/av/media/libmediaplayerservice/`
- `frameworks/av/media/libstagefright/`
- `frameworks/base/media/java/android/media/MediaRecorder.java`

------


<!-- source: 14-101-opencamera.md -->

# 10.1 openCamera 调用链

```
App
 └─ CameraManager.openCamera()
     └─ CameraManagerGlobal / CameraDeviceImpl
         └─ ICameraService.connectDevice()
             └─ CameraService::connectDeviceImpl()
                 └─ makeClient / CameraDeviceClient
                     └─ CameraDeviceClient::initialize()
                         └─ Camera3Device::initialize()
                             └─ CameraProviderManager / openSession
                                 └─ ICameraDeviceSession 建立
```

分析重点：

- 权限校验
- camera id 合法性
- camera 是否已被占用
- policy / user / uid 限制
- provider 是否存在
- HAL session 是否成功创建

------


<!-- source: 20-11.md -->

# 11. 核心对象职责说明


<!-- source: 22-112-cameradeviceimpl.md -->

# 11.2 CameraDeviceImpl

职责：

- App 侧 camera device 控制核心
- 管理 session 创建、request 提交、callback 分发
- 跟踪 device 状态和错误

常见问题：

- close 与 callback 竞态
- session recreate 时机错误
- callback executor/thread 使用不当

------


<!-- source: 23-113-cameradeviceclient.md -->

# 11.3 CameraDeviceClient

职责：

- CameraService 中 app client 的服务端实体
- 连接 app 请求与 Camera3Device
- 负责 stream 管理、request 提交、错误映射

常见问题：

- client 生命周期管理
- beginConfigure / endConfigure 顺序问题
- app surface 失效后的恢复问题

------


<!-- source: 24-114-camera3device.md -->

# 11.4 Camera3Device

职责：

- Camera HAL3 的 Framework 侧核心封装
- 维护 request pipeline、stream、in-flight request、状态机
- 管理 RequestThread、Result 处理、Buffer 回流

常见问题：

- 状态机异常
- flush / close / reconfigure 时机错误
- in-flight request 回收不完整
- buffer 未归还导致 pipeline 堵塞

------


<!-- source: 26-116-cameraprovidermanager.md -->

# 11.6 CameraProviderManager

职责：

- 管理 provider 注册和 camera device 信息
- 协调 framework 与 provider / device session 的连接

常见问题：

- provider 未注册
- 设备状态同步异常
- vendor tag / metadata 支持不一致

------


<!-- source: 46-171.md -->

# 17.1 核心链路

```
Camera HAL
 └─ output buffer
     └─ Camera3OutputStream
         └─ ANativeWindow / BufferQueue producer
             └─ queueBuffer
                 └─ consumer
                     ├─ SurfaceView / SurfaceTexture
                     ├─ ImageReader
                     ├─ MediaRecorder / MediaCodec
                     └─ 其他 consumer
```


<!-- source: 57-202.md -->

# 20.2 首帧慢拆解模型

将首帧慢拆成以下阶段：

```
App 调 open
 -> CameraService connect
 -> HAL open session
 -> create session
 -> configureStreams
 -> setRepeatingRequest
 -> first processCaptureRequest
 -> first processCaptureResult
 -> first preview buffer queue
 -> consumer acquire
 -> UI 可见首帧
```

任何阶段都可能成为瓶颈，必须分段定责。


<!-- source: 60-211-logcat.md -->

# 21.1 logcat 重点关键词

建议关注：

- `CameraService`
- `CameraProviderManager`
- `Camera3-Device`
- `Camera3-OutputStream`
- `CameraDeviceClient`
- `CameraDevice-JV`
- `CameraCaptureSession`
- `BufferQueue`
- `ImageReader`
- `MediaRecorder`
- `ACamera`
- 厂商 HAL tag

重点寻找：

- open / close / disconnect
- configureStreams
- processCaptureRequest
- processCaptureResult
- notify shutter / error
- stream create / delete
- request submit / fail
- flush / timeout / invalid state


<!-- source: 63-22.md -->

# 22. 源码分析时必须回答的问题

每次执行 Camera 分析时，至少回答以下问题：

1. 问题发生在哪一层首先可观测？
2. 当前场景是 preview / capture / video / switch / reconfigure 中哪一种？
3. open 是否成功？
4. session 是否创建成功？
5. configureStreams 是否成功？
6. repeating request / single request 是否真正提交？
7. HAL 是否收到请求？
8. HAL 是否返回 metadata / buffer？
9. 输出 Surface 是否正常消费？
10. 是否存在 backlog / inflight 卡住？
11. 是否涉及多摄 / 特殊流组合 / reprocess / offline？
12. 根因属于 app、framework、HAL 还是 consumer 链路？
13. 需要怎样修复与验证？

------


<!-- source: 64-23.md -->

# 23. 架构图输出规范

当用户要求架构图时，至少输出以下一种或多种：

### 23.1 模块架构图

```
App
 ├─ CameraX / Camera2
 ├─ Preview UI
 ├─ ImageReader / MediaRecorder
 └─ Callbacks

Framework
 ├─ CameraManager
 ├─ CameraDeviceImpl
 ├─ CameraCaptureSessionImpl
 └─ JNI / Binder

System Server / Native
 ├─ CameraService
 ├─ CameraDeviceClient
 ├─ Camera3Device
 ├─ RequestThread
 └─ CameraProviderManager

Provider / HAL
 ├─ ICameraProvider
 ├─ ICameraDevice
 ├─ ICameraDeviceSession
 └─ camera3 HAL

Output / Consumer
 ├─ SurfaceView / TextureView
 ├─ ImageReader
 ├─ MediaRecorder / Codec
 └─ BufferQueue / Gralloc
```

### 23.2 时序图

必须贴合具体场景，而不是抽象化泛图。

例如：open + preview 首帧时序图、capture 时序图、switch camera 时序图、recording 时序图。

------


<!-- source: 66-241-open-preview.md -->

# 24.1 open + preview 首帧时序

```
App              Framework            CameraService         Camera3Device/HAL      Surface Consumer
 | openCamera()      |                     |                       |                     |
 |------------------>|                     |                       |                     |
 |                   | connectDevice()     |                       |                     |
 |                   |-------------------->| initialize/open       |                     |
 |                   |                     |---------------------->| session ready       |
 | createSession()   |                     |                       |                     |
 |------------------>| configureStreams    |                       |                     |
 |                   |-------------------->| configureStreamsLocked|                     |
 |                   |                     |---------------------->| configureStreams    |
 | setRepeating()    |                     |                       |                     |
 |------------------>| submitRequest       |                       |                     |
 |                   |-------------------->| setStreamingRequest   |                     |
 |                   |                     |---------------------->| processCaptureRequest
 |                   |                     |<----------------------| result/buffer       |
 |                   |<--------------------| callback dispatch     |                     |
 | preview visible   |                     |                       |----queueBuffer----->|
```


<!-- source: 67-242-still-capture.md -->

# 24.2 still capture 时序

```
App              Framework            CameraService         HAL / ISP             ImageReader
 | capture()          |                    |                    |                      |
 |------------------->| submitRequest      |                    |                      |
 |                    |------------------->| queue request      |                      |
 |                    |                    |------------------->| capture processing   |
 |                    |                    |<-------------------| shutter notify       |
 | shutter callback   |<-------------------|                    |                      |
 |                    |                    |<-------------------| jpeg buffer/result   |
 | image callback     |<-------------------|                    |--------------------->|
```

------


<!-- source: 73-3.md -->

# 3. 关键调用链

- App →
- Framework →
- CameraService →
- HAL →
- Surface / Consumer →


<!-- source: 78-8.md -->

# 8. 修复建议

- App：
- Framework：
- HAL：


<!-- source: 80-28.md -->

# 28. 执行原则

在执行本 Skill 时，始终遵循以下原则：

1. 先分层，再下钻。
2. 先时序，再定责。
3. 先证据，再结论。
4. 先区分 Camera 本体问题与 Surface/Buffer/Consumer 问题。
5. 先判断是否是流配置问题，再看 request/result pipeline。
6. 遇到厂商 HAL 行为不透明时，明确说明边界。
7. 不把本 Skill 扩展为 ANR 分析 Skill。

------


<!-- source: 81-29.md -->

# 29. 一句话触发提示词

可用以下提示快速触发本 Skill：

- 分析 AOSP Camera 源码架构与设计思想
- 分析 CameraService 到 HAL3 的调用链
- 分析 Camera 预览黑屏 / 卡住 / 花屏
- 分析 Camera 拍照慢 / 首帧慢 / 切摄慢
- 分析 Camera configureStreams 失败
- 分析 Camera preview / capture / video 时序
- 分析 Camera buffer / surface / ImageReader 问题
- 根据 logcat + dumpsys + trace 定位 camera 问题

------
