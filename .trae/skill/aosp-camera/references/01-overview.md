# 概览与范围
<!-- source: 00-overview.md -->

# AOSP Camera Analysis Skill


<!-- source: 06-5.md -->

# 5. 输入要求


<!-- source: 08-52.md -->

# 5.2 缺失输入时的处理

即使输入不完整，也应先基于 AOSP 通用架构给出：

1. 标准模块职责划分
2. 可能相关调用链
3. 最小排查路径
4. 需要补充的关键证据
5. 暂时不能确认的部分

---


<!-- source: 28-118-surface-bufferqueue-imagereader-mediarecorder.md -->

# 11.8 Surface / BufferQueue / ImageReader / MediaRecorder

职责：

- 作为 Camera 输出目标
- 承载 preview / jpeg / yuv / video 数据消费

常见问题：

- consumer 未消费导致 buffer 堵塞
- ImageReader maxImages 过小或未 close
- Surface 生命周期与 session 不匹配
- encoder 反压上游

------


<!-- source: 44-16-configurestreams.md -->

# 16. configureStreams 失败专项模型

这是 Camera 典型难点之一。

### 16.1 分析维度

- 输出路数是否超限
- stream 类型组合是否受支持
- size / format 组合是否合法
- input stream / reprocess 是否支持
- deferred / shared surface 是否兼容
- physical camera stream 是否支持
- dataspace / usage 是否正确
- consumer 类型是否符合预期

### 16.2 常见错误源

- Preview + JPEG + Video + ImageReader 组合超过 HAL 表能力
- Surface 尺寸与动态范围配置冲突
- 逻辑摄像头下 physical stream 组合不合法
- App 侧未正确关闭旧 session / 旧 stream
- 厂商 HAL 能力声明和实际实现不一致

### 16.3 关键证据

- `dumpsys media.camera`
- `CameraCharacteristics` 中的 stream configuration map
- service log 中 `configureStreamsLocked`
- HAL 返回码
- stream id / width / height / format / usage

------


<!-- source: 54-192-reprocess-zsl.md -->

# 19.2 Reprocess / ZSL

分析点：

- input stream 是否支持
- zsl ring buffer 是否建立
- request 是 direct capture 还是 reprocess
- latency 优化是否生效
- metadata 是否正确继承

------
