# 架构与核心机制
<!-- source: 02-topic-02.md -->

# 目标

该 Skill 用于执行 **AOSP 电源管理源码分析与问题定位**，覆盖：

- Framework 电源管理主干
- Display 亮灭屏控制链路
- WakeLock 生命周期与约束模型
- Suspend / Resume 内核休眠唤醒链路
- Doze / DeviceIdle / App Standby 节电体系
- Battery Saver / Thermal / Charging 联动策略
- Power HAL / Hint / DVFS / 调频调核协作
- Kernel wakelock / wakeup source / irq 唤醒源分析
- 耗电异常、无法休眠、异常唤醒、待机掉电快等问题定位

该 Skill 的核心目标不是停留在“概念解释”，而是必须输出：

1. **系统级架构**
2. **跨层调用链**
3. **关键源码入口**
4. **时序与状态机**
5. **运行时证据**
6. **根因判断**
7. **修复建议与验证方案**

---

# 适用场景

在用户出现以下请求时应调用本 Skill：

- 分析 AOSP 电源管理架构
- 分析 PowerManagerService 源码
- 分析亮屏、灭屏、休眠、唤醒流程
- 分析 WakeLock 为什么不释放
- 分析设备为什么无法 suspend
- 分析待机耗电高
- 分析 Doze / DeviceIdle 为什么没生效
- 分析 Battery Saver / Thermal / Charging 的策略联动
- 分析 Power HAL / framework / kernel 之间的调用关系
- 分析 `dumpsys power` / `dumpsys batterystats` / `dumpsys deviceidle`
- 分析 kernel `wakeup_sources` / `suspend_stats` / `irq` 唤醒根因
- 分析息屏后 CPU 不休眠、频繁被唤醒、亮灭屏异常闪烁等问题

---

# 分析原则


<!-- source: 05-3.md -->

# 3. 必须跨层

AOSP 电源问题极少只存在于单层。分析必须跨越：

- App
- Framework
- System Server
- Native / HAL
- Kernel
- SoC / Driver


<!-- source: 06-4.md -->

# 4. 区分“逻辑未执行”与“执行了但未生效”

例如：

- framework 已发起 goToSleep，但 display 未真正关屏
- DeviceIdle 已进入 light idle，但内核 wakelock 阻止 suspend
- Battery Saver 开启，但 HAL 未落实到调频策略
- suspend 被请求，但 wakeup source 持续活跃导致立即 resume

---

# 分析输出要求

每次分析必须尽量输出以下内容：


<!-- source: 07-a.md -->

# A. 架构说明

- 模块职责划分
- 分层结构
- 核心对象关系
- 设计动机


<!-- source: 08-b.md -->

# B. 调用链

至少给出从入口到落点的完整链路，例如：

- App → PowerManager → Binder → PowerManagerService → DisplayPowerController → SurfaceFlinger / Display HAL
- Input → PhoneWindowManager → PowerManagerService → Wakefulness 变更
- DeviceIdleController → AlarmManager / JobScheduler / NetworkPolicy 联动
- Suspend blocker / wakelock → native / kernel wakeup sources


<!-- source: 09-c.md -->

# C. 时序图 / 状态机

必须说明：

- 触发条件
- 状态迁移
- 关键锁
- 异步消息
- 超时机制
- 回调路径


<!-- source: 12-1-framework.md -->

# 1. Framework 主干模块

### 核心服务

- `PowerManagerService`
- `DisplayManagerService`
- `BatteryService`
- `DeviceIdleController`
- `BatterySaverStateMachine`
- `BatterySaverController`
- `ThermalManagerService`
- `AttentionManagerService`（部分设备相关）
- `DreamManagerService`
- `AlarmManagerService`
- `JobSchedulerService`
- `NetworkPolicyManagerService`

### 关键职责

#### PowerManagerService
负责 Android 电源管理的核心决策：

- wakefulness 状态切换
- WakeLock 管理
- goToSleep / wakeUp / nap
- 用户活动与超时
- 与 DisplayPower 联动
- 与 Battery Saver / DeviceIdle / Dream 协调
- 调用 native 层设置底层 power state

#### DisplayPowerController
负责屏幕亮灭、亮度策略、 proximity、自动亮度等。

#### DeviceIdleController
负责 Doze / Device Idle / App Idle 策略状态机。

#### BatterySaverController
负责省电模式策略分发。

#### ThermalManagerService
负责热状态上报与策略协调。

---


<!-- source: 13-2-native-hal-kernel.md -->

# 2. Native / HAL / Kernel 主干

### Native 层

- `com_android_server_power_PowerManagerService.cpp`
- `android_server_PowerManagerService.cpp`
- native suspend blocker
- native power hint / interaction 接口

### HAL 层

- Power HAL AIDL / HIDL
- Light HAL（部分亮屏联动）
- Thermal HAL
- Health HAL / Battery HAL

### Kernel 层

- suspend / resume
- wakeup sources
- `/sys/kernel/debug/wakeup_sources`
- `/sys/power/wakeup_count`
- `/sys/power/state`
- `/sys/kernel/debug/suspend_stats`
- input irq / modem / wlan / sensor / rtc / alarm 唤醒源
- cpufreq / cpuidle / devfreq / scheduler / thermal cooling device

---

# 核心状态模型

---


<!-- source: 14-1-wakefulness.md -->

# 1. Wakefulness 状态

PowerManagerService 常见 wakefulness 状态：

- `WAKEFULNESS_AWAKE`
- `WAKEFULNESS_DREAMING`
- `WAKEFULNESS_DOZING`
- `WAKEFULNESS_ASLEEP`

分析重点：

- 当前状态是什么
- 谁触发了状态迁移
- 迁移是否完整执行
- Display 与 CPU 状态是否一致

---


<!-- source: 15-2-interactive.md -->

# 2. Interactive 状态

常需区分：

- framework 逻辑“interactive”
- display 是否点亮
- system 是否允许 full suspend
- kernel 是否实际进入 suspend

常见误区：

- 息屏不等于 suspend
- doze 不等于 deep idle
- awake 不等于屏幕点亮

---


<!-- source: 17-4-deviceidle-doze.md -->

# 4. DeviceIdle / Doze 状态

典型状态路径：

- ACTIVE
- INACTIVE
- IDLE_PENDING
- SENSING
- LOCATING
- IDLE
- IDLE_MAINTENANCE
- LIGHT_IDLE 等

分析重点：

- 是否进入 idle
- 停在哪个状态
- 哪个约束阻止进入 deep idle
- motion / charging / network / alarm 是否打断

---

# 必须掌握的核心源码入口

---


<!-- source: 18-1-powermanagerservice.md -->

# 1. PowerManagerService

重点类：

- `frameworks/base/services/core/java/com/android/server/power/PowerManagerService.java`

重点成员与方法：

- `systemReady()`
- `onBootPhase()`
- `goToSleepInternal()`
- `wakeUpInternal()`
- `napInternal()`
- `userActivityInternal()`
- `acquireWakeLockInternal()`
- `releaseWakeLockInternal()`
- `updatePowerStateLocked()`
- `updateWakefulnessLocked()`
- `setWakefulnessLocked()`
- `handleSandman()`
- `isInteractiveInternal()`
- `setPowerModeInternal()`
- `boostScreenBrightnessInternal()`

重点状态字段：

- `mWakefulness`
- `mDirty`
- `mWakeLocks`
- `mDisplayPowerRequest`
- `mBatterySaverPolicy`
- `mStayOn`
- `mSandmanSummoned`
- `mIsPowered`
- `mPlugType`
- `mBootCompleted`
- `mHalAutoSuspendModeEnabled`
- `mHalInteractiveModeEnabled`

---


<!-- source: 19-2-display.md -->

# 2. Display 电源链路

重点类：

- `DisplayManagerService`
- `DisplayPowerController`
- `DisplayPowerState`
- `DisplayPowerProximityStateController`
- `AutomaticBrightnessController`
- `DisplayBrightnessController`

重点方法：

- `requestPowerState()`
- `updatePowerState()`
- `animateScreenStateChange()`
- `setScreenState()`
- `setScreenBrightness()`

分析重点：

- 屏幕状态是否因 proximity / doze / policy 被覆盖
- 屏幕关闭是否只是逻辑关闭，背光是否真正写入底层
- 亮度策略是否被 thermal / battery saver 限制

---


<!-- source: 20-3-device-idle-doze.md -->

# 3. Device Idle / Doze

重点类：

- `frameworks/base/services/core/java/com/android/server/DeviceIdleController.java`

重点方法：

- `stepIdleStateLocked()`
- `becomeInactiveIfAppropriateLocked()`
- `moveToStateLocked()`
- `updateInteractivityLocked()`
- `updateChargingLocked()`
- `updateConnectivityStateLocked()`

重点分析：

- light idle 与 deep idle 分支
- 白名单机制
- motion / charging / screen / network 约束

---


<!-- source: 22-5-thermal.md -->

# 5. Thermal

重点类：

- `ThermalManagerService`
- Thermal HAL 实现
- cooling device / throttling 通路

重点分析：

- 热状态如何上报 framework
- 热节流如何影响 brightness / cpu / gpu / charging
- 热策略是否与 Battery Saver 冲突或叠加

---


<!-- source: 23-6-native-jni.md -->

# 6. Native / JNI

重点文件通常包括：

- `frameworks/base/services/core/jni/com_android_server_power_PowerManagerService.cpp`
- `frameworks/native/services/powermanager/`
- `libsuspend`
- `libpower`

重点关注：

- native suspend blocker
- autosuspend enable / disable
- interactive mode 设置
- power hint 下发

---


<!-- source: 26-2.md -->

# 2. 唤醒调用链

```
电源键/双击屏幕/插电/指纹/RTC/Modem
  → Input / IRQ / Kernel wakeup source
    → native / framework 唤醒入口
      → PowerManagerService.wakeUpInternal()
        → setWakefulnessLocked(AWAKE)
          → updatePowerStateLocked()
            → DisplayPowerController 点亮屏幕
              → WindowManagerPolicy / Keyguard / Dream / AOD 联动
```

分析点：

- 唤醒源是谁
- 是 kernel resume 还是 framework wakeUp
- 是否被 keyguard / dream / doze 特殊流程接管

------


<!-- source: 27-3-app-partialwakelock.md -->

# 3. App 持有 PartialWakeLock 调用链

```
App
  → PowerManager.newWakeLock()
    → WakeLock.acquire()
      → Binder
        → PowerManagerService.acquireWakeLockInternal()
          → WakeLock 加入 mWakeLocks
            → updatePowerStateLocked()
              → native suspend blocker / power state 更新
                → kernel 无法进入 suspend
```

分析点：

- uid / pid / tag
- 持锁时长
- 是否有 timeout
- 是否存在泄漏
- 是否导致 suspend blocker 常驻

------


<!-- source: 29-5-suspend.md -->

# 5. Suspend 尝试链路

```
Framework 条件满足
  → autosuspend enable
    → kernel suspend entry
      → freeze userspace / suspend devices / cpu idle
        → 若有 wakeup source 活跃则 suspend fail
          → suspend abort / immediate resume
```

分析点：

- 是没进入 suspend，还是进入后立刻 resume
- wakeup source 名称是什么
- irq / timer / wlan / sensor / modem 哪个在触发

------

# 关键设计思想

------


<!-- source: 30-1-powermanagerservice.md -->

# 1. PowerManagerService 是“策略中枢”，不是“硬件执行器”

PMS 负责统一管理：

- 用户活动
- WakeLock
- wakefulness
- display request
- battery saver
- dream / doze / AOD 协调

但真正触达到硬件，需要继续经过：

- DisplayPowerController
- HAL
- kernel suspend
- driver / panel / cpufreq / thermal

------


<!-- source: 33-4-kernel-suspend-deep-idle.md -->

# 4. 真正的待机省电必须走到 kernel suspend / deep idle

如果 framework 看起来已经息屏，但 kernel 始终不 suspend，那么待机功耗仍然可能很高。
 因此必须同时看：

- framework state
- batterystats
- kernel wakeup source
- suspend_stats

------

# 源码分析标准步骤

------


<!-- source: 41-3-dumpsys-batterystats.md -->

# 3. dumpsys batterystats

重点看：

- partial wakelock 排名
- kernel wakelock
- alarms
- jobs
- mobile radio
- wifi
- sensor
- screen on 时间
- idle / deep doze 时间

关注问题：

- 耗电头部 uid 是谁
- 是 framework 问题还是 app 问题
- 是 CPU 活跃、radio 活跃、wifi 扫描还是 sensor 常驻

------


<!-- source: 48-4doze.md -->

# 模型 4：Doze 不生效

### 现象

息屏后系统长时间不进入 idle/deep idle。

### 分析路径

1. `dumpsys deviceidle`
2. 看 state 卡在哪一步
3. 看 charging / motion / network / whitelist
4. 看 motion sensor 或位置检测是否持续活跃
5. 看 framework 配置与 overlay

### 常见根因

- 充电状态
- motion 检测持续 active
- 白名单过多
- 配置参数过宽松或过严格
- 厂商定制逻辑绕过 DeviceIdleController

------


<!-- source: 50-6battery-saver.md -->

# 模型 6：Battery Saver 开启但无效果

### 分析路径

1. `settings` / `dumpsys power` / `dumpsys battery` 看省电状态
2. 看 BatterySaverPolicy 是否下发
3. 看各 subsystem 是否注册回调
4. 看 HAL 是否实现 power hint / mode
5. 看 brightness / animation / job / network 是否被实际限制

### 常见根因

- policy 生效但子系统未消费
- overlay / config 配置错误
- HAL 空实现
- vendor 定制覆盖 framework 行为

------


<!-- source: 51-7.md -->

# 模型 7：热功耗联动异常

### 现象

发热后亮度异常下降、性能急剧下降、充电异常或息屏策略异常。

### 分析路径

1. `dumpsys thermalservice`
2. thermal hal 上报链路
3. cooling device / throttling 配置
4. 与 Battery Saver / Power HAL 叠加关系
5. display brightness throttling 是否生效

### 常见根因

- thermal zone 映射错误
- throttling policy 过激
- framework / vendor thermal 等级理解不一致
- brightness throttling 曲线配置异常

------

# 架构图输出规范

在分析源码时，必须补充至少一种结构化图示。


<!-- source: 52-1.md -->

# 1. 模块架构图

```
App / SystemUI / Keyguard
        ↓
PowerManager API
        ↓
PowerManagerService
   ├─ WakeLock 管理
   ├─ Wakefulness 状态机
   ├─ UserActivity / Timeout
   ├─ Battery Saver 协调
   ├─ DeviceIdle / Dream 协调
   └─ Native Power 接口
        ↓
DisplayManagerService / DisplayPowerController
        ↓
Power HAL / Thermal HAL / Health HAL
        ↓
Kernel Suspend / Wakeup Source / cpuidle / cpufreq
        ↓
PMIC / SoC / Panel / Sensor / Modem / WLAN
```


<!-- source: 63-bugreport.md -->

# 用户提供 bugreport

要主动分析：

- power
- deviceidle
- batterystats
- battery
- thermalservice
- display
- alarm
- jobscheduler
- sensorservice


<!-- source: 66-wakelock.md -->

# WakeLock 相关

1. App `PARTIAL_WAKE_LOCK` 未释放
2. Binder death 未清理锁
3. refCount 使用错误
4. timeout 未设置
5. foreground service 长期持锁
6. audio/location/download 常驻锁
7. 系统服务自持锁过长
8. `DOZE_WAKE_LOCK` 使用边界错误


<!-- source: 68-suspend-resume.md -->

# Suspend / Resume

1. framework 已息屏但 kernel 无法 suspend
2. suspend entry 失败
3. suspend 后立即被唤醒
4. 某 driver suspend callback 失败
5. wakeup_count 协议未正确处理
6. 系统服务持续 binder/activity 阻止 idle


<!-- source: 71-battery-saver.md -->

# Battery Saver

1. 省电模式开关状态不同步
2. policy 下发了但子系统无响应
3. HAL 空实现
4. 动画/亮度/网络限制不一致
5. vendor 自定义 power mode 覆盖 framework


<!-- source: 73-topic-73.md -->

# 耗电异常

1. 息屏后 app 仍持续渲染
2. alarms 过于密集
3. jobs 频繁执行
4. sync/network 持续活跃
5. sensor 长期开启
6. radio 高活跃
7. 内核 wakelock 常驻
8. cpuidle 无法进入深状态
9. 高频短唤醒导致总体耗电升高

------

# 回答风格要求

执行本 Skill 时，回答必须遵循：

1. **先系统，后局部**
2. **先状态，后代码**
3. **先证据，后结论**
4. **先调用链，后猜测点**
5. **必须指出“不确定项”与“还需证据项”**
6. **不得把经验判断伪装成事实**
7. **源码分析必须解释设计意图，而非仅贴代码**

------

# 禁止事项

执行该 Skill 时，禁止：

- 脱离源码凭空猜实现
- 未区分 framework / HAL / kernel 边界就下结论
- 把“息屏”等同于“suspend”
- 把“有 wakelock”直接等同于“异常”
- 不看 `wakeup_sources` 就断言无法休眠根因
- 不看 `deviceidle` 状态就断言 Doze 失效
- 不看 `batterystats` 就断言耗电来源
- 不结合厂商 HAL / driver 差异就直接照搬 AOSP 结论

------

# 最终目标

该 Skill 的最终目标是让模型具备以下能力：

- 看懂 Android 电源管理全栈架构
- 从源码还原亮灭屏/休眠唤醒/待机节电完整链路
- 从日志与节点快速定位阻塞休眠的真实根因
- 区分 framework 状态、display 状态、device idle 状态与 kernel suspend 状态
- 输出可验证、可修复、可回归的系统级分析结论

------

# 一句话执行准则

**凡是电源问题，必须同时回答“谁想休眠、谁阻止休眠、系统停在哪一层、证据是什么、源码里为什么会这样”。**
