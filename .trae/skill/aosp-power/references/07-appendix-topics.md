# 补充专题
<!-- source: 04-2.md -->

# 2. 先状态机，后函数细节

分析电源问题时，必须先回答：

- 当前设备处于什么 power state
- 谁请求了状态变化
- 谁阻止了状态变化
- 哪个条件未满足
- 哪个模块仍在活跃
- 哪个唤醒源持续存在

再进入函数级源码分析。


<!-- source: 10-d.md -->

# D. 源码锚点

必须给出关键类、方法、字段、消息码、状态值。


<!-- source: 16-3-wakelock.md -->

# 3. WakeLock 类型模型

常见类型：

- `PARTIAL_WAKE_LOCK`
- `SCREEN_DIM_WAKE_LOCK`（旧）
- `SCREEN_BRIGHT_WAKE_LOCK`（旧）
- `FULL_WAKE_LOCK`（旧）
- `PROXIMITY_SCREEN_OFF_WAKE_LOCK`
- `DOZE_WAKE_LOCK`
- `DRAW_WAKE_LOCK`

重点分析：

- 哪个进程持有
- 获取时间
- 是否 timeout
- 是否存在 refCount 问题
- 是否 binder death 后仍残留
- 是否导致 CPU/Display 无法休眠

---


<!-- source: 21-4-battery-saver.md -->

# 4. Battery Saver

重点类：

- `BatterySaverStateMachine`
- `BatterySaverController`
- `BatterySaverPolicy`

重点分析：

- 进入条件
- 自动触发阈值
- policy 下发对象
- 对 brightness / animation / vibration / jobs / network 的影响

---


<!-- source: 28-4-doze.md -->

# 4. Doze 进入链路

```
Screen off
  → DeviceIdleController.updateInteractivityLocked()
    → inactive timeout 到达
      → stepIdleStateLocked()
        → Light Idle / Deep Idle 状态迁移
          → 限制作业 / 闹钟 / 网络 / 同步
            → 进入更深待机
```

分析点：

- screen off 后多久进入 inactive
- motion / charging 是否阻断
- whitelist 是否放行过多
- alarm/job/network 是否仍持续活跃

------


<!-- source: 31-2-android.md -->

# 2. Android 电源管理是多状态叠加系统

不是单一“开/关”模型，而是多维叠加：

- screen state
- interactive
- wakefulness
- doze state
- charging state
- thermal severity
- battery saver state
- kernel suspend state

因此问题分析必须避免“单变量思维”。

------


<!-- source: 32-3-wakelock.md -->

# 3. WakeLock 是“阻止某些休眠能力”的声明式约束

WakeLock 本质上不是“耗电功能”，而是阻止系统降低到更深功耗状态的约束。
 重点不是它“存在”，而是：

- 它阻止了什么
- 它持续多久
- 是否符合业务预期
- 是否已超出生命周期

------


<!-- source: 42-4-kernel-wakeup-sources.md -->

# 4. kernel wakeup_sources

重点字段通常包括：

- active_count
- event_count
- wakeup_count
- expire_count
- active_since
- total_time
- max_time
- prevent_suspend_time

关注问题：

- 哪个 source 的 `prevent_suspend_time` 最长
- 哪个 source 频繁 wakeup
- 是否存在 event_count 持续增长
- source 名称能否映射到驱动/外设/模块

------


<!-- source: 56-2.md -->

# 2. 当前状态判断

- wakefulness：
- display state：
- device idle state：
- battery saver：
- thermal state：
- suspend 状态：


<!-- source: 60-6.md -->

# 6. 修复建议

- 修改点：
- 风险：
- 替代方案：


<!-- source: 70-deviceidle-doze.md -->

# DeviceIdle / Doze

1. charging 状态导致永不 idle
2. motion 一直 active
3. whitelist 过多
4. overlay 参数不合理
5. 厂商定制绕过 deep idle
6. maintenance 窗口过于频繁
7. network policy 未真正限流
