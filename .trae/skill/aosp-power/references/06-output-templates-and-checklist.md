# 输出模板与检查清单
<!-- source: 54-3-suspend.md -->

# 3. Suspend 阻塞图

```
Screen Off
  → Framework 条件检查通过
    → 存在 PartialWakeLock / Kernel WakeupSource
      → suspend blocker 持续存在
        → autosuspend 无法进入
          → 待机耗电升高
```

------

# 输出模板

每次正式分析建议按以下模板输出：
