# 问题模式与根因
<!-- source: 03-2.md -->

# 2. 适用问题域

### 2.1 播放类问题
- 视频无法播放
- 黑屏但有声音
- 有画面但无声音
- 首帧慢
- seek 卡顿或 seek 后花屏
- 播放中断 / 卡住 / 一直 buffering
- 切后台后播放异常
- 直播流播放不稳定
- 某些编码格式无法解码
- 硬解失败回退软解
- secure 视频播放失败

### 2.2 音频类问题
- 无声
- 爆音 / 杂音 / 破音
- 音频延迟大
- 焦点抢占异常
- 路由切换错误（扬声器/耳机/Bluetooth）
- 音量策略异常
- offload 不生效
- A2DP / SCO 行为异常
- 录音失败 / 权限正常但无数据
- 低时延播放未生效

### 2.3 同步与性能类问题
- 音视频不同步
- 视频掉帧
- 音频 underrun / overrun
- 首帧时间过长
- MediaCodec dequeueBuffer 卡顿
- Surface queueBuffer 阻塞
- Binder 调用慢导致播放卡顿
- 解复用 / 解码 / 渲染各阶段瓶颈定位
- 录制丢帧 / 编码吞吐不足

### 2.4 稳定性问题
- mediaserver / audioserver / media.codec 崩溃
- native crash / tombstone
- binder transaction failed
- 死锁 / 长时间等待 fence / buffer / codec callback
- ANR 与媒体操作相关联
- service 重启导致播放中断

---


<!-- source: 49-13.md -->

# 13. 常见问题模式库

# 13.1 黑屏有声

可能原因：

1. video decoder 未输出 buffer
2. codec 输出为 bytebuffer 模式，未接 surface
3. queueBuffer 失败
4. consumer 未 acquire
5. Surface 已失效或被销毁
6. secure video 无 secure path
7. timestamp 异常导致 renderer 丢帧
8. SurfaceFlinger/HWC 合成异常
9. 尺寸/裁剪/transform 错误导致不可见
10. 首帧未触发 present

# 13.2 有图无声

可能原因：

1. AudioTrack 创建失败
2. AudioPolicy 路由错误
3. mute / volume / focus 异常
4. offload thread 不工作
5. HAL write 失败
6. track underrun
7. 音频格式与设备不兼容
8. 音频时钟异常导致 renderer 不推进

# 13.3 首帧慢

可能原因：

1. data source 打开慢
2. extractor sniff/metadata 解析慢
3. codec create/configure 慢
4. DRM session 建立慢
5. first I-frame 距离远
6. surface 未就绪
7. queueBuffer 被 fence 阻塞
8. 音频设备启动慢
9. binder 调用链阻塞
10. vendor codec 首次启动开销大

# 13.4 seek 卡顿

可能原因：

1. seek map 不精确
2. source seek 后读取慢
3. decoder flush/reset 成本高
4. 音视频队列未及时清空
5. 关键帧距离大
6. network buffering 重新建立
7. surface pipeline backlog 未清掉

# 13.5 音视频不同步

可能原因：

1. PTS 错误
2. audio clock 漂移
3. video decode 太慢
4. renderer drop 策略异常
5. seek 后 clock 未重建
6. rate change / speed 调整处理不一致
7. HAL timestamp 不可靠

# 13.6 无声

可能原因：

1. route 到错误 device
2. focus 被占用
3. track 未 start
4. mixer 未混入
5. HAL 静音
6. 蓝牙设备状态异常
7. 音量策略被压制
8. PCM 数据为空

# 13.7 录音无数据

可能原因：

1. input route 错误
2. mic 被占用
3. read 阻塞
4. 权限/隐私开关限制
5. pre-processing 失败
6. HAL read 异常
7. source 选择错误

# 13.8 MediaCodec configure/start 失败

可能原因：

1. mime/caps 不匹配
2. color format 不支持
3. secure/non-secure 混用
4. surface 不合法
5. vendor codec 崩溃
6. resource manager 拒绝
7. 参数 bundle 不兼容

------


<!-- source: 56-151.md -->

# 15.1 播放卡顿分析

重点看：

- App 发起播放到 first frame 的总时长
- codec 创建与 configure 时长
- 首个 output buffer 时间
- queueBuffer → SF acquire → present 时间
- AudioTrack start 到首个 HAL write 时间
- binder 是否长时间阻塞
