# 工具与证据
<!-- source: 07-inputs-required.md -->

# Inputs Required

至少需要以下之一：

- `dumpsys input`
- InputDispatcher / InputReader 相关日志
- `dumpsys window`
- 与问题时刻对应的 Perfetto / trace
- ANR traces（若已进入 Input ANR）
- 关键源码路径 / patch / 行为描述

同时最好具备：

- Android 版本 / AOSP 分支
- 包名 / Activity / 窗口名
- 复现步骤
- 问题发生时间点
- 是否与解锁、桌面、IME、旋转、切应用、多窗口、手势导航相关

---


<!-- source: 16-evidence-priority.md -->

# Evidence Priority

证据优先级从高到低：

1. `dumpsys input` / 关键时刻输入状态 / wait queue / target 状态
2. 与问题时刻对齐的 Perfetto / trace / sched / app main / binder / wm 信息
3. `dumpsys window` / `dumpsys activity` / 焦点与窗口状态
4. InputDispatcher / InputReader / WMS / ANR 相关 logcat
5. ANR traces / 线程栈（若已进入超时）
6. 源码静态调用链
7. 经验推断

规则：

- 输入状态树和 wait queue 证据高于单条日志猜测。
- 焦点/窗口状态证据高于经验判断。
- 若输入目标正确但 App 未及时 finish，首发更可能在 App 或其上游依赖，而非 Dispatcher 本身。
- 若多个证据冲突，必须保留冲突事实并解释可能原因。

---


<!-- source: 21-missing-information-policy.md -->

# Missing Information Policy

当输入不足时，按以下策略处理。

### 可以输出

- 当前最可能问题类型
- 初步架构定位
- 候选架构图 / 时序图
- 关键对象初步状态判断
- 候选链路
- 最小补充清单

### 不可以输出

- 确定性根因
- 未被证据支撑的输入目标/超时定性
- 未打通状态链的最终归因
- 只凭类名/字段名做详细代码定性

### 信息不足时必须明确写出

- 当前已知事实
- 当前未知事实
- 当前图和时序中哪些是推断
- 哪条链路卡在什么断点
- 最小补充清单，例如：
  - 对应时刻 `dumpsys input`
  - `dumpsys window`
  - `dumpsys activity`
  - InputDispatcher / WMS 相关 logcat
  - 对应时刻 Perfetto
  - 若超时则补充完整 traces.txt

------
