# 调用链与时序
<!-- source: 24-7-kernel-driver.md -->

# 7. Kernel / Driver

常见节点与信息源：

- `/sys/kernel/debug/wakeup_sources`
- `/sys/kernel/debug/suspend_stats`
- `/sys/power/state`
- `/sys/power/wakeup_count`
- `/d/wakeup_sources`
- `/proc/wakelocks`（老内核）
- `/sys/class/thermal/*`
- `/sys/devices/system/cpu/cpu*/cpufreq/*`
- `/sys/class/power_supply/*`

---

# 完整跨层调用链

---


<!-- source: 58-4.md -->

# 4. 源码调用链

- 从入口到落点完整路径
- 关键类/方法/状态字段


<!-- source: 67-display.md -->

# Display / 亮灭屏

1. 灭屏请求发出但背光未关闭
2. proximity 导致屏幕状态异常
3. AOD / dream 干扰正常灭屏
4. Keyguard / policy 接管流程异常
5. 自动亮度与 power policy 冲突
6. thermal brightness throttling 误触发
7. Display HAL 落地失败
