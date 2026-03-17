# 问题模式与根因
<!-- source: 04-use-this-skill-when.md -->

# Use This Skill When

在以下场景下使用该 Skill：

- 用户明确要求分析 Input / 输入系统问题。
- 问题现象与以下内容强相关：
  - 触摸无响应
  - 点击没反应
  - 手势失效
  - 滑动不生效
  - 输入延迟大
  - 事件丢失
  - 事件打到错误窗口
  - Input ANR
  - focused window / input target 错误
  - 锁屏、Launcher、SystemUI、IME、应用之间的输入切换异常
  - 触摸事件被错误拦截、丢弃、取消
- 用户提供了：
  - `dumpsys input`
  - `dumpsys window`
  - `dumpsys activity`
  - InputDispatcher / InputReader 相关 logcat
  - Perfetto / trace 中 input / wm / sched / binder / app 主线程信息
  - ANR traces（若已经进入 Input ANR）
- 用户需要：
  - 架构设计思想
  - 架构图
  - 时序图
  - 关键代码详细讲解
  - 根因分析和修复建议

---


<!-- source: 12-sequence-diagram-requirements.md -->

# Sequence Diagram Requirements

分析结果必须包含至少一张 Mermaid 时序图。

### 必须覆盖内容
- 正常时序
- 异常时序
- 首发异常点
- 目标解析点
- 分发点
- App 消费点
- 超时/取消/ANR 点（如相关）

### 建议覆盖对象
- User
- Device / InputReader
- InputDispatcher
- WMS / Focus
- App Main / ViewRootImpl
- IME / SystemUI / Launcher（如相关）
- Binder / ANR 依赖对象（如相关）

### 必须标注
- 事件产生
- 事件入队
- 目标解析
- 事件发送
- ACK / finish / consume
- 等待点
- 超时点
- cancel 点

---


<!-- source: 17-root-cause-decision-rules.md -->

# Root Cause Decision Rules

### Rule 1：点击没反应不等于事件没进系统
用户看到“没反应”，可能是：

- 事件没上报
- 事件没找到目标
- 事件找到目标但未送达
- 事件送达后 App 未处理
- 事件处理了但 UI 无反馈
- 事件因 ANR / cancel / target 切换被取消

必须先确定首发层。

---

### Rule 2：FocusedWindow 错误不等于 InputDispatcher 根因
Dispatcher 只是使用目标解析结果。
若焦点/窗口状态本身错了，首发根因更可能在 WMS / transition / window state，而不是分发器本身。

---

### Rule 3：Input ANR 不等于输入系统本身有 bug
`Input dispatching timed out` 只是输入链的超时表现。
真正根因可能在：

- App 主线程阻塞
- Binder / 锁 / Future 等待
- system_server 焦点状态未推进
- WMS/IME/transition 状态异常
- 目标窗口不可消费却仍被认为是目标

---

### Rule 4：事件丢失必须区分“未分发”“被取消”“被覆盖”“未消费”
不能笼统说“事件丢了”。
必须明确是：

- 没进队列
- 没找到目标
- 找到目标但没送达
- 送达后 channel / target 变化导致 cancel
- App 接收了但逻辑吞掉了

---

### Rule 5：优先找第一个错误状态
不要把最终用户看到的失败点当首发点。
必须回答：

- 第一个错误状态是什么
- 是哪个对象第一次偏离正常值
- 这个错误如何传播到最终现象

---

### Rule 6：输入延迟要拆成多段
输入延迟至少拆成：

- 采集延迟
- 分发排队延迟
- 目标解析延迟
- App 主线程消费延迟
- UI 反馈延迟

不能只说“输入慢”。

---


<!-- source: 22-cross-skill-routing.md -->

# Cross-Skill Routing

本 Skill 是 Input / 输入系统深挖与源码理解的主 Skill。

如发现问题更适合其它方向，应做如下协同：

- 若首发异常在焦点、窗口可见性、IME target、transition：
  - 辅助使用 `aosp-wms`
- 若问题已经进入 ANR，且关键根因在主线程/Binder/锁链：
  - 辅助使用 `aosp-anr`
- 若用户真正问题是流畅度与渲染节奏，而输入链本身正常：
  - 转 `aosp-jank`
- 若根因位于 Surface / Buffer / 合成结果，输入只是用户感知入口：
  - 辅助使用 `aosp-graphics`
- 若问题首发于启动链、桌面切换链，输入只是触发器：
  - 辅助使用 `aosp-startup`

规则：

- 输入系统根因由本 Skill 负责。
- 辅助 Skill 只提供交叉证据，不替代输入系统根因结论。

------


<!-- source: 23-suggested-shared-resources.md -->

# Suggested Shared Resources

建议与以下共享资源配合使用：

- `../shared/templates/analysis_report.md`
- `../shared/templates/root_cause_report.md`
- `../shared/templates/evidence_table.md`
- `../shared/templates/architecture_analysis.md`
- `../shared/templates/architecture_diagram.md`
- `../shared/templates/sequence_diagram.md`
- `../shared/templates/code_walkthrough.md`
- `../shared/checklists/common_checklist.md`
- `../shared/checklists/log_checklist.md`
- `../shared/checklists/trace_checklist.md`
- `../shared/checklists/bugreport_checklist.md`
- `../shared/checklists/input_checklist.md`
- `../shared/refs/aosp_module_index.md`
- `../shared/refs/common_paths.md`
- `../shared/refs/android_version_notes.md`
- `../shared/refs/glossary.md`
- `../shared/refs/input_object_map.md`
- `../shared/refs/input_anr_guide.md`
- `../shared/refs/focus_target_guide.md`
- `../shared/examples/input_case_01.md`
- `../shared/examples/input_case_02.md`

------


<!-- source: 24-standard-operating-principles.md -->

# Standard Operating Principles

执行本 Skill 时，始终遵守以下原则：

1. 先定义输入问题类型，再看细节状态。
2. 先建立架构定位，再深入 dumpsys / trace / 日志。
3. 先画结构，再讲调用。
4. 先讲正常时序，再讲异常偏离。
5. 先找第一个错误状态，再解释最终用户现象。
6. 先区分目标解析问题、分发问题、消费问题，再谈 ANR。
7. 先看输入状态和焦点证据，再做经验推断。
8. 既要回答“为什么没响应/慢/打错”，也要回答“这套输入代码为什么这样设计”。
9. 证据不足时保持保守，不可伪确定。
10. 不要把输入系统的下游结果误当首发根因。

------
