# 补充专题
<!-- source: 25-115-requestthread.md -->

# 11.5 RequestThread

职责：

- 将 CaptureRequest 持续转化为 HAL request
- 管理 repeating request 和 burst request
- 控制 pipeline 深度与 request 节奏

常见问题：

- request 无法继续下发
- pipeline 被 inflight/backpressure 限制
- request queue 堵塞

------


<!-- source: 29-12.md -->

# 12. 状态机分析模型

Camera 分析必须关注状态机，而不是只看单个函数。


<!-- source: 31-122-session.md -->

# 12.2 Session 生命周期

```
create session
  -> configure streams
  -> session ready
  -> submit repeating request
  -> active streaming
  -> single capture / burst / mode switch
  -> stop repeating / abort / close
```

必须分析：

- session 创建是否成功
- session 是否真的进入 ready
- request 是否在 session valid 期间提交
- close 与 app 资源释放是否一致

------


<!-- source: 32-13.md -->

# 13. 预览问题专项分析

预览问题是 Camera 中最高频的问题之一。


<!-- source: 36-14.md -->

# 14. 拍照问题专项分析


<!-- source: 40-15.md -->

# 15. 录像问题专项分析


<!-- source: 42-152-stoprecording.md -->

# 15.2 stopRecording 慢或卡住

排查点：

- encoder drain 是否完成
- camera flush 是否等待过长
- outstanding buffer 是否未返回
- muxer close 是否阻塞
- app stop 顺序是否正确


<!-- source: 45-17-buffer-surface.md -->

# 17. Buffer / Surface 专项分析

Camera 问题经常不是 camera 算法问题，而是 buffer 流转问题。


<!-- source: 49-18.md -->

# 18. 多摄 / 逻辑摄像头分析


<!-- source: 50-181.md -->

# 18.1 关注点

- logical camera id 与 physical ids 的映射
- physical stream request 是否正确设置
- zoom / crop / fusion 策略
- metadata 中 activePhysicalId 变化
- 切主摄 / 超广角 / 长焦 时 session 是否重建
- concurrent camera 支持是否存在


<!-- source: 51-182.md -->

# 18.2 常见问题

- 逻辑摄像头能力声明不完整
- physical request key 设置错误
- zoom 切换点逻辑与 HAL 实现不一致
- 多路流带宽不足
- stream combination 在 multi-camera 下失效

------


<!-- source: 52-19-offlinesession-reprocess-zsl.md -->

# 19. OfflineSession / Reprocess / ZSL 专项


<!-- source: 53-191-offlinesession.md -->

# 19.1 OfflineSession

适用场景：

- 拍照后将处理移到 offline pipeline
- 降低前台 session 阻塞

分析点：

- session 切 offline 的触发时机
- offline request/result 生命周期
- app 是否正确等待最终结果


<!-- source: 58-203.md -->

# 20.3 性能瓶颈归类

- provider / HAL open 慢
- configureStreams 慢
- 3A warmup 慢
- first request 发出慢
- first result 回来慢
- preview buffer 被 consumer 卡住
- UI 可见链路滞后
- camera 切换时旧链路释放慢

------


<!-- source: 71-1.md -->

# 1. 问题描述

- 设备：
- Android 版本：
- Camera ID：
- 场景：
- 现象：
- 复现步骤：


<!-- source: 74-4.md -->

# 4. 关键源码

- 文件：
- 类：
- 函数：
- 作用：


<!-- source: 79-9.md -->

# 9. 回归验证

- 基础功能：
- 性能指标：
- 边界场景：

------
