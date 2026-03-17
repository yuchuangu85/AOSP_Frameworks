# 问题模式与根因
<!-- source: 34-topic-34.md -->

# 第一步：明确问题类型

先判断属于哪一类：

- 亮灭屏异常
- 无法休眠
- 异常唤醒
- 待机耗电高
- WakeLock 泄漏
- Doze 不生效
- Battery Saver 不生效
- Thermal 导致性能/亮度异常
- 充电状态切换导致 power policy 异常

------


<!-- source: 37-topic-37.md -->

# 第四步：回到源码找逻辑根因

按现象回溯到：

- 状态字段
- 状态迁移函数
- 触发消息
- binder 入口
- policy 决策
- HAL 落地

------


<!-- source: 44-trace.md -->

# Trace 分析关注点

### 1. 息屏后是否仍有持续 CPU 活动

若有，继续定位：

- 哪个线程
- 哪个进程
- 是 binder storm 还是轮询
- 是 alarm 唤醒还是 sensor 回调

### 2. 息屏后 SurfaceFlinger / app 是否仍持续产帧

若有，说明：

- 动画未停
- AOD / doze 特殊模式
- 后台渲染异常
- 某层误持续刷新

### 3. 是否出现周期性唤醒

常见来源：

- AlarmManager
- modem
- wlan
- sensor hub
- rtc
- watchdog

------

# 典型问题分析模型

------


<!-- source: 45-1.md -->

# 模型 1：设备无法灭屏

### 现象

按电源键或超时后屏幕应熄灭，但设备仍保持亮屏或短暂亮灭后恢复。

### 分析路径

1. 看 `dumpsys power` 的 wakefulness 与 display power request
2. 看 `userActivity` 是否持续刷新
3. 看是否有 `SCREEN_BRIGHT_WAKE_LOCK` / `DRAW_WAKE_LOCK` / proximity 相关锁
4. 看 Window / Dream / AOD / Keyguard 是否接管显示
5. 看 DisplayPowerController 是否真正下发 screen off
6. 看 HAL / driver 是否真正落地背光关闭

### 常见根因

- 持续 userActivity
- 特殊 wakelock 未释放
- proximity 逻辑异常
- dream/doze/aod 状态切换干扰
- Display HAL 或 panel driver 未落地

------


<!-- source: 46-2-suspend.md -->

# 模型 2：息屏后无法 suspend

### 现象

屏幕已灭，但待机电流高，CPU 不休眠。

### 分析路径

1. 看 `dumpsys power` 是否仍有 partial wakelock / suspend blocker
2. 看 `batterystats` 的 partial wakelock、kernel wakelock、alarms
3. 看 `wakeup_sources` 的 `prevent_suspend_time`
4. 看 `suspend_stats`
5. 看 trace 中是否存在持续 binder / timer / irq 活动

### 常见根因

- App partial wakelock 泄漏
- Alarm 周期唤醒过频
- wlan / modem / sensor 唤醒源持续活跃
- framework 服务自旋
- 内核驱动 suspend callback 失败

------


<!-- source: 49-5.md -->

# 模型 5：待机耗电高

### 分析维度

必须拆为：

- Screen 贡献
- CPU 贡献
- Radio 贡献
- Wi-Fi 贡献
- Sensor 贡献
- GPS 贡献
- Thermal / charging 异常贡献
- app 前台/后台活动贡献

### 定位路径

1. batterystats 先看 UID 和 subsystem 排名
2. 结合 wakeup_sources 看谁阻止 suspend
3. 结合 trace 看息屏后 CPU 活动
4. 回到源码确认谁触发、谁未释放、谁未限流

------


<!-- source: 59-5.md -->

# 5. 根因分析

- 直接根因：
- 深层根因：
- 为什么会这样：


<!-- source: 64-trace.md -->

# 用户提供 trace

要主动分析：

- 息屏后 CPU 活跃线程
- 周期唤醒
- display/sf 活动
- irq / timer / alarm 事件
- suspend / resume 区间


<!-- source: 65-topic-65.md -->

# 用户提供源码片段

要主动输出：

- 所属模块职责
- 在系统中的调用位置
- 上下游关系
- 状态机作用
- 修改风险点

------

# 重点关注的高频异常模式库

以下为 AOSP 电源管理高频问题模式，分析时应主动匹配。


<!-- source: 69-topic-69.md -->

# 唤醒源

1. wlan 唤醒频繁
2. modem 唤醒频繁
3. rtc/alarm 唤醒过密
4. sensor hub 唤醒异常
5. touch irq 抖动
6. charger/usb 插拔抖动
7. fingerprint/face 唤醒误触发


<!-- source: 72-thermal.md -->

# Thermal

1. thermal 等级映射错误
2. thermal 过度降频
3. thermal 导致 brightness 异常受限
4. thermal 与 charging policy 冲突
5. thermal 事件抖动导致状态频繁切换
