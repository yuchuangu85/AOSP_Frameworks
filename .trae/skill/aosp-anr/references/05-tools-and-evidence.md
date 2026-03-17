# 工具与证据
<!-- source: 02-1-skill.md -->

# 1. Skill 定位

该 Skill 专门用于 **AOSP ANR 深度分析**，不是泛泛而谈的应用层排查模板，而是一个面向源码、运行时与系统协同的系统级分析能力定义。

适用目标：

1. 分析 Android Framework / Native / Kernel 相关 ANR 根因。
2. 建立 **“场景 → 超时类型 → 触发路径 → 线程阻塞点 → 资源竞争点 → 根因”** 的完整证据链。
3. 结合 **AOSP 源码 + traces.txt / tombstone / bugreport / logcat / dumpsys / Perfetto** 进行联合判断。
4. 在结论中明确区分：
   - **直接超时点**
   - **真实阻塞点**
   - **上游诱因**
   - **系统放大器**
   - **最终根因**
5. 输出可用于研发、性能团队、系统团队、架构评审的正式分析结果。

---


<!-- source: 05-4.md -->

# 4. 强制分析原则

执行本 Skill 时，必须遵守以下原则：

### 4.1 证据优先
任何结论必须基于至少一种可验证证据：

- AOSP 源码调用链
- traces.txt / stack trace
- logcat
- event log
- bugreport
- dumpsys
- Perfetto trace
- binder transaction trace
- systrace / ftrace
- tombstone / native backtrace

### 4.2 区分“超时点”和“根因点”
ANR 报出的地方往往只是 **检测到超时的位置**，不是根因。  
必须继续追查：

- 谁在等待
- 等谁
- 为什么迟迟不返回
- 上游被谁堵住
- 是否是系统级放大

### 4.3 跨层分析
禁止只看 Java 主线程栈就下结论。  
必须跨层考虑：

- App 线程
- Binder IPC
- System Server
- Input / AMS / WMS / ATMS / PMS
- SurfaceFlinger / RenderThread / GPU（如与卡顿级联有关）
- Linux scheduler / futex / I/O wait

### 4.4 时序一致性
必须校准多个证据源的时间线：

- ANR 报警时间
- traces 抓取时间
- logcat 时间
- Perfetto 时间
- 事件开始时间
- 事件超时时间

### 4.5 明确不确定性
若证据不足，必须说明：

- 当前能确定什么
- 不能确定什么
- 缺少哪些关键证据
- 下一步如何验证

---


<!-- source: 101-32.md -->

# 32. 推荐调用提示词模板

### 32.1 通用调用

```
请使用 aosp-anr skill 分析以下 ANR 问题，要求：
1. 明确 ANR 类型
2. 给出 AOSP 源码调用链
3. 结合 traces/logcat/bugreport/Perfetto 做证据链分析
4. 区分超时点、阻塞点、根因点
5. 输出正式分析报告
```

### 32.2 Input ANR 调用

```
请使用 aosp-anr skill 深入分析这个 Input ANR。
要求重点关注：
- InputDispatcher 超时路径
- 焦点窗口/焦点应用状态
- app 主线程是否真正阻塞
- WMS/窗口切换/首帧是否级联影响
- system_server 是否存在锁竞争/Binder 堵塞
```

### 32.3 Broadcast / Service / Provider 调用

```
请使用 aosp-anr skill 对该 ANR 做系统级分析。
要求：
- 识别是 Broadcast / Service / Provider 哪一类
- 给出 AMS 侧检测逻辑
- 给出应用侧生命周期执行路径
- 说明真正阻塞点和上游原因
- 给出修复建议和验证方案
```

------


<!-- source: 62-22-bugreport-dumpsys.md -->

# 22. bugreport / dumpsys 联合分析规则


<!-- source: 79-3.md -->

# 3. 关键证据
- traces:
- logcat:
- bugreport:
- dumpsys:
- Perfetto:


<!-- source: 86-10.md -->

# 10. 验证方案
- 日志补点：
- trace 补抓：
- 源码实验：
- 压测/复现：
```

------
