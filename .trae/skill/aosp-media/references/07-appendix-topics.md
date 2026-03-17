# 补充专题
<!-- source: 41-11.md -->

# 11. 关键源码阅读顺序


<!-- source: 57-152.md -->

# 15.2 音频延迟分析

重点看：

- write 调用节奏
- PlaybackThread 周期
- mixer 负载
- HAL write 间隔
- underrun 标记
- CPU 调度抢占情况


<!-- source: 58-153-av-sync.md -->

# 15.3 AV sync 分析

重点看：

- audio timestamp
- video PTS
- renderer 投递时间
- SF present 时间
- late frame/drop frame 行为

------
