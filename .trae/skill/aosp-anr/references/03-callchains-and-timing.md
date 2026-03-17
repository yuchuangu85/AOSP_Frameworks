# 调用链与时序
<!-- source: 07-6.md -->

# 6. 标准输出要求

输出结果必须包含以下模块：

1. **ANR 类型判定**
2. **现象总结**
3. **关键证据**
4. **源码调用链**
5. **时序分析**
6. **线程阻塞分析**
7. **根因判断**
8. **排除项**
9. **修复建议**
10. **验证方案**

---


<!-- source: 34-14-anr.md -->

# 14. ANR 分析标准时序图


<!-- source: 36-142-broadcast-anr.md -->

# 14.2 Broadcast ANR 时序图

```
AMS
  |
  +--> select receiver
  |
  +--> start target process if needed
  |
  +--> schedule receiver to app
  |
  +--> ActivityThread dispatch
  |
  +--> BroadcastReceiver.onReceive()
  |
  +--> finishReceiver()
  |
  +--> timeout if not finished in time
```


<!-- source: 37-143-service-anr.md -->

# 14.3 Service ANR 时序图

```
AMS / ActiveServices
  |
  +--> scheduleCreateService / scheduleServiceArgs
  |
  +--> app main thread
  |
  +--> Service lifecycle callback
  |
  +--> callback return / lifecycle complete
  |
  +--> timeout if callback path not completed
```

------


<!-- source: 60-202.md -->

# 20.2 分析切入点

- provider 进程是否现拉起
- `onCreate` 是否重
- 是否有 DB schema 升级
- 是否读取大文件/配置
- 是否锁等待
- caller 是否同步等待 provider ready

------


<!-- source: 82-6.md -->

# 6. 时序还原
- T0:
- T1:
- T2:
- T3:
- T_timeout:


<!-- source: 92-step-5.md -->

# Step 5：恢复调用链

从 AOSP 源码角度还原从“事件入口”到“超时判定”的完整路径。


<!-- source: 97-28.md -->

# 28. 禁止事项

执行该 Skill 时，禁止以下行为：

1. 只看 ANR 文本就下结论
2. 只看应用主线程，不看 system_server
3. 把“超时检测点”误当“根因点”
4. 不结合源码调用链
5. 不做时序对齐
6. 用经验拍脑袋归因
7. 忽略 Input / WMS / AMS 之间的关系
8. 忽略 Binder 和锁竞争
9. 忽略 Perfetto 中 runnable 但未运行的情况
10. 在证据不足时输出确定性结论

------


<!-- source: 98-29.md -->

# 29. 输出质量标准

最终输出必须满足：

- **准确**：结论和证据一致
- **完整**：有调用链、有线程栈、有时序
- **可验证**：给出验证方法
- **可修复**：给出工程建议
- **跨层**：不是单点、不是单线程视角
- **正式**：可直接进入缺陷报告或技术评审材料

------
