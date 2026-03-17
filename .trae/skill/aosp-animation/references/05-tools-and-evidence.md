# 工具与证据
<!-- source: 49-18-perfetto-trace.md -->

# 18. Perfetto / Trace 分析规则

当用户提供 Perfetto / System Trace / Winscope / bugreport 时，按以下规则分析。


<!-- source: 53-19-dumpsys-bugreport.md -->

# 19. dumpsys / bugreport 分析规则


<!-- source: 64-6.md -->

# 6. 运行时证据
- trace：
- dumpsys：
- log：
- layer/state：


<!-- source: 82-26.md -->

# 26. 执行指令模板

以下内容可作为调用本 Skill 时的标准 Prompt：

```
请按 AOSP 动画系统专家模式分析以下问题，并严格输出：

1. 动画类型判定
2. 触发源
3. App → Framework → WMS/Shell → Surface → SF 跨层调用链
4. 关键源码类、关键方法、关键状态位
5. 动画时序（触发、首帧逻辑开始、首帧上屏、结束、最终收敛）
6. trace / dumpsys / log / layer 证据
7. 根因归类（逻辑未启动 / 可见性问题 / 帧驱动问题 / transaction问题 / SF问题 / 状态收敛问题）
8. 修复与优化建议

若有 Perfetto / bugreport / dumpsys / winscope，请结合运行时证据交叉验证；
若证据不足，请明确指出缺失证据与下一步建议，不要凭空猜测。
```

------
