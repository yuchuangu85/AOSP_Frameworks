# 工具与证据
<!-- source: 05-4.md -->

# 4. 核心分析原则

### 4.1 先体系，后函数
优先回答：
- AMS 为什么存在
- 这个职责为什么在 AMS 而不是别处
- 该逻辑由谁拥有状态
- 状态和调度如何分离

### 4.2 先主链路，后异常分支
先建立完整主链：
- 请求入口
- 组件解析
- 目标进程检查
- 进程创建
- attachApplication
- 组件调度
- 执行完成回传
- 状态更新
- OOM_ADJ 重算

### 4.3 先状态机，后日志
AMS 的问题本质上多为 **状态机错位**，不是单点函数错。
必须优先识别：
- ProcessRecord 状态
- procstate / adj
- scheduling group
- component record
- timeout 状态
- queue 状态
- user state

### 4.4 结论必须可验证
所有结论必须映射到至少一种证据：
- 源码路径
- logcat 关键词
- dumpsys 字段
- bugreport section
- trace 事件
- 栈信息

### 4.5 必须解释设计权衡
不仅描述“做了什么”，还要说明：
- 为什么这么设计
- 替代方案是什么
- 当前设计对稳定性 / 性能 / 资源 / 用户体验的平衡是什么

---

# 5. AMS 总体架构与职责边界


<!-- source: 19-75-ams-anr.md -->

# 7.5 AMS ANR 证据模型

### 必查源码

- `ActivityManagerService`
- `AnrHelper`
- `AppErrors`
- `ActiveServices#serviceTimeout`
- `BroadcastQueue#broadcastTimeoutLocked` 或相关实现
- `ProcessErrorStateRecord`
- `ContentProviderHelper`
- `InputDispatcher`（联动分析时）

### 必查日志

- `ANR in`
- `Reason:`
- `Broadcast of Intent`
- `Timeout executing service`
- `Input dispatching timed out`
- `ContentProvider not responding`
- `CPU usage from`
- `am_anr`

### 必查 bugreport 区域

- `ACTIVITY MANAGER`
- `ANR traces`
- `CPU usage`
- `SYSTEM LOG`
- `EVENT LOG`
- `dropbox`
- `binder state`
- `dumpsys activity broadcasts/services/processes`

------

# 8. OOM_ADJ / procstate / LMKD 专项体系

本章是专家增强版核心专题。

------


<!-- source: 28-89-oom-adj-procstate-lmkd.md -->

# 8.9 OOM_ADJ / procstate / LMKD 专项分析模板

### 问题类型 A：前台可见进程被杀

必须检查：

- 当时是否真的是 top / visible
- WMS 可见状态是否建立
- 是否只是 resumed 尚未 visible
- 屏幕状态是否 sleep
- procstate 与 curAdj 实际值
- 是否因 crash 被误判为 kill
- LMKD 日志是否存在

### 问题类型 B：后台保活异常

必须检查：

- 是否有 bind 提升
- 是否有前台服务
- 是否有 provider stable reference
- 是否在最近任务链中
- 是否因过度拉起被系统降权
- 是否触发 cached 优化

### 问题类型 C：某服务进程不该常驻却常驻

必须检查：

- 绑定链是否未释放
- connection record 是否泄漏
- service 是否 sticky / restart 中
- provider stable ref 是否残留
- OOM_ADJ 是否长期被抬高

------

# 9. bugreport / dumpsys / logcat 自动分析规则

本章给出面向真实工程的自动化规则框架。

------


<!-- source: 54-124.md -->

# 12.4 第四步：对齐证据

做到：

- 代码函数 ↔ 日志关键词
- 状态字段 ↔ dumpsys 字段
- 时序节点 ↔ trace 事件
- 现象 ↔ 异常模式库


<!-- source: 56-131.md -->

# 13.1 标准模板

### 1. 问题定义

说明分析对象、触发场景、用户现象。

### 2. 结论摘要

先给结论，不绕。

### 3. 涉及模块

AMS / ATMS / WMS / App / LMKD / Zygote / PKMS 等。

### 4. 完整跨层调用链

从入口一路写到执行结束。

### 5. 关键状态对象

列出 record、关键字段、状态变化点。

### 6. 源码详细解释

函数级展开，解释判断、状态更新、分支和结果。

### 7. 架构设计思想

解释为什么这样设计，收益与代价是什么。

### 8. 运行时证据映射

logcat / dumpsys / bugreport / trace 如何验证。

### 9. 异常模式匹配

从 80+ 模式库中匹配最接近项。

### 10. 修复建议与验证方案

给工程可执行建议。

------

# 14. 交付质量检查表

输出前必须自检：

-  是否给出了 AMS → ATMS → WMS → App 跨层调用链
-  是否解释了 AMS 的职责边界
-  是否识别了关键状态对象
-  是否解释了 ANR 观察者与真实阻塞者的区别
-  是否分析了 OOM_ADJ / procstate / LMKD
-  是否给出了 bugreport / dumpsys / logcat 证据映射
-  是否匹配了异常模式库
-  是否给出了架构图与时序图
-  是否给出了可验证结论
-  是否给出了修复建议

------

# 15. 与其他 Skill 的联动
