---
name: aosp-layertree
description: 面向 Android / AOSP 图形系统的 LayerTree 专项源码分析 Skill。用于分析 SurfaceFlinger 中 Layer 树的构建、组织、遍历、合成决策、事务更新、显示输出映射及其与 Window / Surface / Buffer / BLAST / HWC 的关系，定位黑屏、闪屏、层级错乱、遮挡异常、可见性异常、Z 序异常、旋转裁剪异常、事务未生效、截图与实显不一致等问题，并输出可验证的源码证据、调用链、状态树、时序图和根因结论。
---

# aosp-LayerTree

## 统一输出要求

- 所有输出文档必须保存到仓库根目录的 `docs/` 目录下。
- 文档文件必须使用 Markdown 格式，文件扩展名为 `.md`。
- 如果用户未指定文件名，使用与任务主题相关的语义化文件名。
- 每次执行本子 Skill，必须单独输出 1 个独立的 Markdown 分析文件，禁止只把内容并入总控回答而不落独立文件。
- 独立文件名必须包含当前子 Skill 名称或其对应问题域，便于总控汇总时直接引用。
- 独立输出文件名必须遵循统一模式：`docs/<skill-slug>-<topic>.md`。
- 其中 `<skill-slug>` 必须使用当前 Skill 的规范英文标识，例如 `aosp-ams`、`aosp-wms`、`aosp-surfaceflinger`。
- 其中 `<topic>` 必须是当前分析主题的语义化短名，使用小写英文和连字符，禁止空格。
- 如果用户未提供主题名，则默认使用 `analysis` 作为 `<topic>`。


## 工作目标
- 聚焦当前问题对应的主调用链与关键状态变化。
- 同时输出设计思想、代码证据、运行时证据和可验证结论。
- 给出可执行修复建议与回归验证方案。

## 精简执行流
1. 定义问题边界：现象、影响、触发条件、版本范围。
2. 锁定入口：从最接近现象的类/方法开始，标注关键分支。
3. 构建调用链：标注线程、进程、IPC/JNI 边界与等待点。
4. 解释设计权衡：说明当前机制为什么这样设计及代价。
5. 建立证据链：每个结论绑定源码证据 + 运行时证据。
6. 输出结论：根因分层、置信度、修复方案、验证计划。

## 输出规范
- 必须包含：问题定义、主调用链、证据链、结论、修复与验证。
- 证据不足时标注缺口，禁止输出强结论。
- 图表优先 Mermaid，保持与当前问题直接相关。

## 专题参考（按需加载）
- [01-overview.md](references/01-overview.md)
- [02-architecture-and-mechanisms.md](references/02-architecture-and-mechanisms.md)
- [03-callchains-and-timing.md](references/03-callchains-and-timing.md)
- [04-problem-patterns-and-root-cause.md](references/04-problem-patterns-and-root-cause.md)
- [05-tools-and-evidence.md](references/05-tools-and-evidence.md)
- [06-output-templates-and-checklist.md](references/06-output-templates-and-checklist.md)
- [07-appendix-topics.md](references/07-appendix-topics.md)


## 使用约束
- 不做与当前问题无关的泛化科普。
- 不堆砌模块名代替调用链分析。
- 不给无证据结论。

## 统一输出模板（必须）
每次分析必须按以下章节输出，章节名保持一致：

```markdown
## 一、问题定义与范围
- 现象、影响、触发条件、版本范围、设备与构建信息。

## 二、主调用链
- 给出主调用链（可附关键分支）。
- 标注线程/进程、IPC 或 JNI 边界、关键等待点（锁/fence/binder/message queue）。

## 三、设计思想与架构权衡
- 解释关键机制为什么这样设计。
- 说明收益与代价：稳定性、性能、复杂度、可维护性。

## 四、架构图（Mermaid）
- 图中必须体现问题相关模块边界与依赖方向。

## 五、时序图（Mermaid）
- 图中必须体现关键事件顺序与阻塞传播路径。
- 标注关键时间点或阶段（如输入、调度、合成、显示）。

## 六、关键代码详细分析
- 时序图完成后，必须紧接着对时序图中最关键的类、方法、分支和状态变化做详细源码分析。
- 至少解释 3 处关键代码：入口、关键分支或状态机、收敛点或返回路径。
- 说明每段代码在时序图中的位置、线程或进程上下文、输入输出与设计意图。

## 七、证据链（源码 + 运行时）
- 源码证据：文件路径 + 方法 + 关键条件。
- 运行时证据：logcat/dumpsys/Perfetto/Winscope/traces 等。
- 每个关键结论至少绑定 1 条源码证据和 1 条运行时证据。

## 八、根因结论与置信度
- 根因分层：App / Framework / Native / HAL / Kernel。
- 置信度：Confirmed / Highly Likely / Possible / Speculative。

## 九、修复建议
- 建议必须直连根因，说明影响面与潜在副作用。

## 十、验证计划
- 功能回归、性能回归、稳定性回归、边界场景验证。

## 十一、证据缺口与后续采集
- 列出当前缺口与补采方案（采集工具、场景、目标信号）。
```

## 图示规范（统一）
- 架构图与时序图必须使用 Mermaid。
- 图只保留与当前问题直接相关的节点与链路，避免“大而全”。
- 时序图至少包含：发起方、system_server 关键角色、图形/服务关键节点（按问题域选择）。
- 时序图之后必须紧接“关键代码详细分析”章节，逐段解释图中关键代码，禁止只给图不给代码解读。
- 图中命名与正文术语保持一致。

## 置信度分级（统一）
- `Confirmed`：源码与运行时证据闭环，结论可复现。
- `Highly Likely`：证据链完整度高，但存在单点缺口。
- `Possible`：存在方向性证据，但缺关键验证。
- `Speculative`：仅假设，必须显式标注不可直接下结论。
