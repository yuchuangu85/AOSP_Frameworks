# 工具与证据
<!-- source: 03-1.md -->

# 1. 证据优先

所有结论必须优先建立在以下证据之上：

- AOSP 源码
- logcat
- dumpsys
- bugreport
- Perfetto / systrace
- kernel debugfs / sysfs 节点
- Battery Historian / batterystats
- HAL 实现代码
- 驱动源码或内核节点状态

禁止仅凭经验直接下定论。


<!-- source: 35-topic-35.md -->

# 第二步：先看状态，再看代码

优先查看：

- `dumpsys power`
- `dumpsys deviceidle`
- `dumpsys batterystats`
- `dumpsys battery`
- `dumpsys thermalservice`
- `dumpsys display`
- `dumpsys alarm`
- `dumpsys jobscheduler`

kernel 侧查看：

- `wakeup_sources`
- `suspend_stats`
- `dmesg`
- `logcat -b system -b events`

------


<!-- source: 38-topic-38.md -->

# 第五步：给出修复方案

修复必须明确到：

- 哪个模块
- 哪个时机
- 哪个锁或状态
- 是否有副作用
- 如何验证

------

# 常用 dumpsys / 节点分析模板

------


<!-- source: 39-1-dumpsys-power.md -->

# 1. dumpsys power 重点关注

重点字段：

- Wake Locks
- Suspend Blockers
- Display Power
- mWakefulness
- mUserActivitySummary
- mWakeLockSummary
- mDirty
- mIsPowered
- mBatteryLevel
- mStayOn
- mProximityPositive
- mBootCompleted

关注问题：

- 是否有长期存在的 partial wakelock
- suspend blockers 是否持续活跃
- display request 是否仍是 bright/dim
- wakefulness 是否处于 AWAKE

------


<!-- source: 40-2-dumpsys-deviceidle.md -->

# 2. dumpsys deviceidle

重点查看：

- 当前 state
- light / deep idle 是否进入
- whitelist
- motion active
- charging
- screen state
- next alarm / next transition

关注问题：

- 停在哪个 state
- 为什么没进入 IDLE
- 被什么条件打断

------


<!-- source: 43-5-suspend-stats.md -->

# 5. suspend_stats

关注：

- success 次数
- fail 次数
- last_failed_dev
- last_failed_errno
- last_resume_reason

用于判断：

- 是根本没进 suspend
- 还是 suspend 设备阶段失败
- 还是进入后快速 resume

------

# Perfetto / Trace 分析要求

分析电源问题时，如有 trace，重点查看：

- Power rails（若设备支持）
- suspend / resume slices
- irq activity
- CPU idle state
- kernel wakelock
- binder activity
- AlarmManager 唤醒
- JobScheduler
- Display state
- SurfaceFlinger 帧活动
- input activity
- thermal events
- frequency scaling

------


<!-- source: 57-3.md -->

# 3. 关键证据

- dumpsys power：
- dumpsys deviceidle：
- batterystats：
- wakeup_sources：
- trace/log：


<!-- source: 61-7.md -->

# 7. 验证方案

- 复现步骤：
- 预期日志变化：
- 预期功耗/状态变化：

------

# 自动分析时的执行要求

当用户提供以下材料时，应自动进入相应模式：


<!-- source: 62-logcat.md -->

# 用户提供 logcat

要主动提取：

- PowerManagerService
- DeviceIdleController
- BatterySaver
- Thermal
- DisplayPowerController
- DreamManager
- AlarmManager
- Suspend / kernel PM 关键日志
