# 调用链与时序
<!-- source: 34-topic-34.md -->

# 关注点

- 音频时钟是否为主时钟
- 视频渲染是否跟随 audio clock
- PTS 是否单调
- extractor / decoder / renderer 是否篡改时间戳
- seek 后同步是否重建
- audio drift / video drift 如何修正
- late frame drop 策略是什么
- vsync 对齐与 render deadline


<!-- source: 45-12.md -->

# 12. 关键时序图模板


<!-- source: 47-122-audiotrack.md -->

# 12.2 AudioTrack 写入时序

```
App write()
 → AudioTrack.obtainBuffer
 → shared memory write
 → AudioFlinger PlaybackThread wake
 → mixer/direct/offload process
 → HAL write
 → DSP / device output
```


<!-- source: 53-143-trace-perfetto.md -->

# 14.3 trace / perfetto 关键关注项

- binder transactions
- media codec slices
- audio thread scheduling
- fast mixer / normal mixer
- bufferqueue events
- surfaceflinger frame timeline
- CPU 调度
- IRQ / driver latency（需要时）


<!-- source: 60-161.md -->

# 16.1 简版模板

```
### 问题定义
### 现象
### 相关模块
### 调用链
### 关键源码
### 时序图
### 根因判断
### 证据
### 验证方法
### 修复建议
```


<!-- source: 62-17.md -->

# 17. 回答风格要求

执行该 Skill 时，必须遵守：

- 不只讲 API，必须下钻到源码实现
- 不只讲类名，必须解释关键方法和调用关系
- 不只讲模块图，必须给线程、状态机、时序
- 不只给结论，必须给证据链
- 不只指出问题，必须给验证方案
- 对历史实现与当前实现要显式区分
- 遇到厂商实现边界要明确说明“超出 AOSP 主干”

------


<!-- source: 68-212.md -->

# 21.2 音频无声

```
请分析 AOSP AudioTrack/AudioFlinger 无声问题，要求输出：

- AudioTrack 创建与 start 调用链
- AudioPolicy 路由决策
- AudioFlinger thread/track 模型
- HAL 输出路径
- 常见无声模式分类与证据
```
