# Power 源码分析

## 一、问题定义与范围
- 范围：PowerManagerService 与 DisplayPowerController 完整，可分析亮灭屏、唤醒与电源策略主链。
- 现状：本次输出基于目录源码静态分析，不包含运行时日志。

## 二、主调用链
- Wake/sleep request -> PowerManagerService -> DisplayPowerController -> display state
- 关键边界：重点关注 system_server、Binder、BufferQueue、SurfaceFlinger 等跨线程/跨进程收敛点。

## 三、设计思想与架构权衡
- 电源管理通过集中策略协调 wakelock、显示、电池与热策略，代价是决策面广。

## 四、架构图（Mermaid）
```mermaid
flowchart LR
    Caller --> PMSvc
    PMSvc --> DisplayPower
    DisplayPower --> Display
```

## 五、时序图（Mermaid）
```mermaid
sequenceDiagram
    participant Caller
    participant PMSvc
    participant DisplayPower
    participant Display
    Caller->>PMSvc: 进入主链
    PMSvc->>DisplayPower: 状态推进
    DisplayPower->>Display: 结果提交
```

## 六、关键代码详细分析
- PowerManagerService 收敛 wakelock、user activity、睡眠唤醒状态机。
- DisplayPowerController 把电源决策映射为屏幕亮度与电源状态改变。
- 耗电和无法休眠问题通常不是单服务问题，而是跨 wakelock、显示、传感器协作结果。

## 七、证据链（源码 + 运行时）
- 源码证据：`base/services/core/java/com/android/server/power/PowerManagerService.java`
- 源码证据：`base/services/core/java/com/android/server/display/DisplayPowerController.java`
- 运行时证据：当前目录仅含源码，无 logcat、dumpsys、Perfetto、Winscope，运行时证据未闭环。

## 八、根因结论与置信度
- 结论：当前仓库对 `aosp-power` 的覆盖状态为 `FULL`。
- 置信度：`Highly Likely`。

## 九、修复建议
- 若用于问题定位，优先围绕上述入口继续下钻；若为仓库缺口，则先补齐缺失源码再做闭环判断。

## 十、验证计划
- 补采与本模块对应的 `logcat`、`dumpsys`、Perfetto 或 Winscope，并回到文中主链逐段比对。

## 十一、证据缺口与后续采集
- 缺口：缺少运行时证据。
- 后续：按本模块主链采集线程栈、事务、buffer、焦点或权限状态。
