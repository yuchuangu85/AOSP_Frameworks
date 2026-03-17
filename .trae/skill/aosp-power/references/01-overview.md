# 概览与范围
<!-- source: 00-overview.md -->

# AOSP Power Analysis Expert


<!-- source: 11-e.md -->

# E. 问题定位

必须输出：

- 现象
- 证据
- 根因
- 修复点
- 验证方法

---

# 电源管理全局架构

---


<!-- source: 25-1.md -->

# 1. 按电源键灭屏调用链

```text
InputReader
  → InputDispatcher
    → PhoneWindowManager.interceptPowerKeyDown / interceptKeyBeforeQueueing
      → PowerManager.goToSleep()
        → Binder
          → PowerManagerService.goToSleepInternal()
            → goToSleepNoUpdateLocked()
              → updatePowerStateLocked()
                → DisplayPowerController.requestPowerState()
                  → 屏幕状态/亮度更新
                    → Display HAL / SurfaceFlinger / 背光节点
                      → 最终屏幕熄灭
```

分析点：

- key 是否成功分发
- policy 是否拦截为其他行为
- PMS 是否真的切换 wakefulness
- display request 是否生效
- 底层 panel / backlight 是否真正关闭

------


<!-- source: 36-topic-36.md -->

# 第三步：定位阻塞点

必须回答：

- 谁还在持有 wakelock
- 谁在持续产生 alarm / job / network activity
- 是否有 sensor / modem / wlan / touch irq 唤醒
- display 是否真的 off
- thermal / charging 是否改变了 power policy
- Battery Saver / DeviceIdle 是否进入目标状态

------


<!-- source: 47-3.md -->

# 模型 3：设备频繁自动亮屏/唤醒

### 现象

设备在待机状态下周期性亮屏或频繁 resume。

### 分析路径

1. 查最近唤醒源
2. 查 input / sensor / rtc / alarm / modem / usb 插拔
3. 看 `last_resume_reason`
4. 看 framework `wakeUpInternal` 调用来源
5. 看是否为指纹、抬手亮屏、双击亮屏等 feature

### 常见根因

- 传感器误触发
- RTC / Alarm 频繁唤醒
- USB / charger 抖动
- modem/network wakeup
- policy feature 配置不当

------


<!-- source: 53-2.md -->

# 2. 亮灭屏时序图

```
PowerKey Press
  → InputDispatcher
    → PhoneWindowManager
      → PowerManagerService.goToSleepInternal
        → setWakefulnessLocked(ASLEEP/DOZING)
          → updatePowerStateLocked
            → DisplayPowerController.requestPowerState
              → Screen off animation / backlight off
                → kernel/display driver
```


<!-- source: 55-1.md -->

# 1. 问题定义

- 现象：
- 触发条件：
- 影响范围：
