# 问题模式与根因
<!-- source: 05-4.md -->

# 4. 禁止事项

执行本 Skill 时，必须遵守以下约束：

1. **不得脱离源码和证据臆断根因。**
2. **不得把 ANR 分析混入本 Skill 主体。**
3. 不得只给结论，不给调用链和证据链。
4. 不得只停留在 Java API 表面，必须下钻到 native / service / provider / HAL。
5. 不得只罗列类名，必须解释职责、交互关系、状态变化和时序。
6. 不得将厂商私有 HAL 行为当作 AOSP 通用事实。
7. 对厂商实现未知部分，必须显式标记为“推断 / 需厂商代码验证”。
8. 性能结论必须配合日志、trace、timestamp 或 pipeline 行为解释。
9. 遇到版本差异时必须注明 Android 版本或代码分支差异。
10. 输出图示时，必须与源码实现一致，不得画概念图冒充实现图。

---


<!-- source: 21-111-cameramanager.md -->

# 11.1 CameraManager

职责：

- 枚举 camera id
- 查询 camera characteristics
- 发起 openCamera
- 监听 camera availability / torch 状态

常见问题：

- availability 状态异常
- camera id 映射理解错误
- 逻辑摄像头与物理摄像头误用

------


<!-- source: 27-117-camera-hal3-devicesession.md -->

# 11.7 Camera HAL3 DeviceSession

职责：

- 实现 configureStreams / processCaptureRequest / flush / close
- 输出 metadata、buffer、notify
- 与 sensor / ISP / memory / codec 等底层交互

常见问题：

- stream combination 不支持
- processCaptureRequest 延迟过高
- result / buffer 丢失
- metadata 错误
- flush 卡顿或 close 不完整

------


<!-- source: 30-121-device.md -->

# 12.1 Device 状态

典型状态包括：

- uninitialized
- initialized
- configuring
- configured
- active
- flushing
- error
- closed

关键原则：

1. configure 期间不能提交正常请求
2. flush 后需要正确回收 inflight request
3. error 后并非所有路径都可继续复用 session
4. close 必须确保底层资源释放干净


<!-- source: 33-131.md -->

# 13.1 预览黑屏分析路径

优先排查：

1. camera 是否 open 成功
2. configureStreams 是否成功
3. repeating request 是否成功提交
4. HAL 是否收到 request
5. HAL 是否返回 result / output buffer
6. Camera3OutputStream 是否 queue 到 surface
7. consumer 是否在消费 buffer
8. Surface 是否可见 / 有效
9. TextureView / SurfaceView 生命周期是否正确
10. SF / 应用渲染侧是否正常显示

常见根因：

- surface 未准备好即配置 session
- request 未下发成功
- HAL 不产出 buffer
- queueBuffer 失败
- consumer 未 acquire / 被阻塞
- App 层预览控件生命周期错乱
- 切前后台后 surface 重建但 session 未重配


<!-- source: 34-132.md -->

# 13.2 预览卡住

常见根因：

- ImageReader 未及时释放 image
- Surface consumer 堵塞
- HAL pipeline 持续超时或 backlog
- RequestThread 中 inflight request 堆积
- 录像/拍照共存场景下 stream 资源被吃满
- provider / HAL 某一路 result 卡住


<!-- source: 39-143-metadata.md -->

# 14.3 metadata 异常

分析点：

- request metadata 设置是否生效
- result metadata 是否缺失或异常
- partial result 与 final result 的语义
- vendor tag 支持是否一致
- AE / AF / AWB / FLASH 状态机是否符合预期

------


<!-- source: 47-172.md -->

# 17.2 典型问题

- dequeueBuffer 卡住
- queueBuffer 失败
- consumer 不消费
- max dequeued / acquired buffer 达上限
- ImageReader 图片未关闭导致死锁式背压
- preview surface 与 encoder surface 争抢 buffer 资源
- 切换场景时旧 surface 未释放


<!-- source: 55-20.md -->

# 20. 性能分析体系

本 Skill 虽然不做 ANR，但必须具备 Camera 性能分析能力。


<!-- source: 61-212-dumpsys-mediacamera.md -->

# 21.2 dumpsys media.camera 必查内容

重点关注：

- active clients
- camera device state
- stream 列表
- stream 格式 / 尺寸
- inflight requests
- last frame number
- error 状态
- torch / availability
- provider 状态
- session 活跃情况


<!-- source: 68-25.md -->

# 25. 结论表达规范

输出结论时必须使用以下格式之一：

### 25.1 确认型结论

> 根因确认：`configureStreams` 失败由 preview + jpeg + recorder 三路流组合超出 HAL 支持能力导致。证据包括：
>
> 1. `endConfigure` 返回失败；
> 2. HAL 侧 `configureStreams` 返回错误码；
> 3. 当前 stream combination 与 `StreamConfigurationMap` 不匹配；
> 4. 移除 recorder surface 后 session 可成功建立。

### 25.2 高概率结论

> 高概率根因：预览黑屏并非 camera open 失败，而是 camera 已持续产出 buffer，但 ImageReader/Surface consumer 未正常消费，导致 buffer 背压后预览停滞。该结论已被 request/result 连续返回、queueBuffer 成功而 consumer 无 acquire 行为所支撑；是否为厂商 consumer 侧异常仍需进一步验证。

### 25.3 不足以定性

> 当前证据不足以确认 HAL 是否真正处理了首帧请求。已知 open、configure、submitRequest 成功，但缺少 HAL result / buffer 返回证据；需补充 vendor HAL 日志或 trace 才能最终定性。

------


<!-- source: 72-2.md -->

# 2. 结论

- 根因：
- 结论级别：确认 / 高概率 / 待补证据


<!-- source: 77-7.md -->

# 7. 根因分析

- 直接原因：
- 深层原因：
- 为什么会触发：
- 为什么此前未暴露：
