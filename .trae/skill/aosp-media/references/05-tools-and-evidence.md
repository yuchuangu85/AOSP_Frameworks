# 工具与证据
<!-- source: 04-3.md -->

# 3. 分析原则

### 3.1 源码分析必须回答的 8 个问题
1. **模块职责是什么**
2. **模块在系统中的位置是什么**
3. **输入输出是什么**
4. **线程模型是什么**
5. **状态机是什么**
6. **关键对象生命周期如何管理**
7. **跨进程调用链如何形成**
8. **性能瓶颈与失败模式在哪里**

### 3.2 证据优先级
分析时必须遵循以下证据优先级：

1. **源码真实实现**
2. **trace / systrace / perfetto / atrace**
3. **logcat / event log / media log**
4. **dumpsys**
5. **tombstone / crash**
6. **经验推断**

禁止：
- 脱离源码臆测模块行为
- 把历史实现当作当前版本事实
- 把 Java API 行为误认为 native 真正执行路径
- 忽略线程切换、binder 边界、buffer 生命周期

---


<!-- source: 50-14.md -->

# 14. 证据采集清单


<!-- source: 54-144-crash.md -->

# 14.4 crash 关键证据

- tombstone backtrace
- abort message
- signal type
- crashing thread
- mutex/futex wait
- binder dead object
- shared memory failure

------


<!-- source: 55-15-perfetto-trace.md -->

# 15. Perfetto / Trace 联合分析规则


<!-- source: 61-162.md -->

# 16.2 专家版模板

```
# 1. 问题背景
# 2. 用户现象与复现路径
# 3. Media 架构分层定位
# 4. 跨进程/跨线程调用链
# 5. 关键对象生命周期
# 6. 状态机分析
# 7. Buffer 流与时间戳流
# 8. Audio/Video clock 模型
# 9. 关键源码逐段解释
# 10. trace/log/dumpsys 证据对照
# 11. 根因归纳
# 12. 修复方案
# 13. 风险评估
# 14. 回归验证清单
```

------


<!-- source: 65-20-skill-prompt.md -->

# 20. 调用本 Skill 时的标准 Prompt 模板

```
请基于 AOSP Media 源码分析以下问题，必须输出：

1. 问题所在模块与职责边界
2. 完整调用链（Java → JNI → native → service → HAL/Surface）
3. 关键类、关键方法、关键线程
4. 状态机与时序图
5. buffer 生命周期与时间戳流转
6. 根因判断与证据链
7. 验证方法与修复建议

分析对象：
[填写类/模块/日志/trace/现象]

现象：
[填写现象]

版本：
[Android 版本 / 分支]

期望重点：
[播放 / 解码 / 音频 / 同步 / 录制 / DRM / 性能 / 崩溃]
```

------


<!-- source: 69-213.md -->

# 21.3 首帧慢

```
请分析 AOSP 媒体播放首帧慢问题，要求分解：

- setDataSource → prepare → decoder start → first frame queue → sf present
- 每阶段耗时与潜在瓶颈
- trace/log/dumpsys 对照分析
- 优化建议
```


<!-- source: 72-23.md -->

# 23. 最终要求

该 Skill 的本质不是“介绍 Android Media”，而是：

- **建立媒体系统跨层认知模型**
- **还原源码真实执行路径**
- **把日志/trace/现象映射到代码**
- **输出可验证、可修复、可复用的分析结论**

执行时应始终以“系统级源码分析专家”的标准产出结果。
