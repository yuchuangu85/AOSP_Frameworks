---
name: aosp-analysis-orchestrator
description: 作为 AOSP 分析总控技能，对复杂系统问题进行分类、路由与编排；执行时必须读取并串联当前会话中全部可用的 AOSP 子技能，在统一证据链下输出跨层结论。
---

# AOSP Analysis Orchestrator

## 统一输出要求

- 所有输出文档必须保存到仓库根目录的 `docs/` 目录下。
- 文档文件必须使用 Markdown 格式，文件扩展名为 `.md`。
- 如果用户未指定文件名，使用与任务主题相关的语义化文件名。
- 执行每个 AOSP 子 Skill 时，必须各自产出 1 个独立的 Markdown 分析文件，禁止只在总控回答中内联展示子 Skill 结果。
- 在全部子 Skill 执行结束后，总控还必须再额外产出 1 个汇总 Markdown 文件，用于串联所有子 Skill 的结论、引用路径与覆盖缺口。
- 汇总文件必须与子 Skill 独立文件分离，不能用任一子 Skill 文件兼作总报告。
- 子 Skill 独立文件命名必须统一为：`docs/<skill-slug>-<topic>.md`。
- 总控汇总文件命名必须统一为：`docs/aosp-summary-<topic>.md`。
- `<topic>` 必须在本次 orchestrator 执行周期内保持一致，供全部子 Skill 文件和总控汇总文件共享。
- 如果用户未提供主题名，则默认使用 `analysis` 作为 `<topic>`。


## 角色定义
本技能是 AOSP 源码分析总控，不替代专项技能，负责：
- 问题归类与优先级判断。
- 路由到正确的专项技能组合。
- 统一证据链与输出结构。
- 协调跨模块分析顺序，避免单点误判。

## 强制委派协议
- 执行 `aosp-analysis-orchestrator` 时，必须读取并执行当前会话中全部可用的 AOSP 子技能，不允许只选择其中一部分。
- 执行过程中，必须让每个已执行子 Skill 各自落 1 个独立的 `.md` 文件，并严格遵循统一命名规则。
- 在所有子 Skill 独立文件生成后，必须再落 1 个总控汇总 `.md` 文件，并严格遵循统一命名规则。
- 路由仍然需要进行，但路由的作用只用于确定：
  - 主故障域与主叙事顺序。
  - 各子技能在最终报告中的权重和先后顺序。
  - 哪些结论属于主链，哪些结论属于辅助链。
- 路由不能作为跳过子技能的理由。
- 只有在以下情况之一成立时，某个子技能才允许不执行：
  - 该子技能在当前会话不可用。
  - 对应 `SKILL.md` 无法读取。
  - 当前仓库不存在该子技能所需的最小源码入口，且必须在结论中显式标记为仓库覆盖缺口。
- 最终输出必须明确写出：
  - 当前会话全部可用的 AOSP 子技能列表。
  - 已实际执行的子技能列表。
  - 未执行的子技能列表及原因。
  - 每个子技能对应的源码入口或覆盖缺口。
  - 每个子技能独立输出文件路径。
  - 总控汇总文件路径。

## 子技能可用性校验
- 在开始分析前，先枚举当前会话全部可用的 AOSP 子技能。
- 对每个子技能执行以下检查：
  - 技能目录是否存在。
  - `SKILL.md` 是否可读取。
  - 当前仓库中是否存在最小源码入口。
- 通过校验的子技能必须全部执行。
- 未通过校验的子技能必须写入最终报告，并标记为：
  - `Unavailable`：技能不存在或 `SKILL.md` 无法读取。
  - `Repo Gap`：技能存在，但当前仓库缺少最小源码入口。

## 总览分析模式
- `aosp-analysis-orchestrator` 不再允许通过“总览分析模式”跳过子技能执行。
- 即使用户只要求“分析 AOSP 目录”或“做源码总览”，也必须遍历全部可用 AOSP 子技能。
- 在总览类请求下，允许对子技能输出做深度压缩，但不允许省略执行记录。

## 适用场景
在下列情况下优先使用本技能：
- 用户不确定问题属于哪个模块。
- 问题跨多个域（例如 Input + WMS + SurfaceFlinger）。
- 需要“现象 -> 调用链 -> 证据 -> 根因 -> 修复”的一体化报告。
- 需要把 bugreport/logcat/Perfetto/dumpsys 联合到源码分析中。

## 编排输入最小集
信息不足时先补齐：
- Android 版本与分支/tag。
- 设备与构建类型（user/userdebug/eng）。
- 现象、触发步骤、频率、首次出现版本。
- 可用证据（logcat、dumpsys、Perfetto、winscope、traces、tombstone）。

## 全量可路由专项技能
- `aosp-anr`
- `aosp-ams`
- `aosp-pms`
- `aosp-wms`
- `aosp-input`
- `aosp-graphics`
- `aosp-animation`
- `aosp-surface`
- `aosp-surfacecontrol`
- `aosp-surfaceflinger`
- `aosp-bufferqueue`
- `aosp-fence`
- `aosp-vsync`
- `aosp-layertree`
- `aosp-transaction`
- `aosp-shell`
- `aosp-binder`
- `aosp-handler`
- `aosp-media`
- `aosp-camera`
- `aosp-storage`
- `aosp-power`
- `aosp-security`

## 路由原则
- 先判定主故障域，再决定主技能。
- 路由时可定义 1 个主技能和若干辅技能，但这只影响组织顺序，不影响子技能执行范围。
- 按“最接近用户可见症状”的域作为主技能。
- 若证据冲突，优先运行时证据，再回溯源码机制。
- 无证据闭环时，结论最高只能到 `Possible`。
- 路由完成后，必须继续执行“读取全部可用子技能 -> 分别提取证据 -> 统一汇总”的闭环，不能停在路由表。

## 执行流程（必须）
1. 枚举当前会话全部可用的 AOSP 子技能。
2. 对全部子技能执行可用性校验与源码入口校验。
3. 根据问题现象确定主技能与辅技能顺序，但不得删减执行列表。
4. 逐个打开全部可用子技能的 `SKILL.md`，按各自流程提取源码证据、运行时证据要求和覆盖缺口。
5. 先确定本次分析共享的 `<topic>` 名称。
6. 让每个子技能各自按 `docs/<skill-slug>-<topic>.md` 生成独立的 Markdown 分析文件。
7. 再按 `docs/aosp-summary-<topic>.md` 生成总控汇总报告，报告中必须同时包含：
   - 主技能叙事主链。
   - 其他全部子技能的执行记录。
   - 每个子技能对应的源码入口、运行时证据要求、仓库覆盖状态。
   - 每个子技能独立文件的引用路径。
8. 若某个子技能因不可用或仓库缺口无法执行，必须单独列项说明，禁止静默跳过。

## 一级路由矩阵

| 问题特征 | 主技能 | 常用辅技能 |
|---|---|---|
| Input dispatch timeout / 无响应触控 | `aosp-input` | `aosp-anr`, `aosp-wms`, `aosp-binder` |
| Broadcast/Service/Provider 超时 | `aosp-anr` | `aosp-ams`, `aosp-binder` |
| Activity/进程生命周期异常 | `aosp-ams` | `aosp-wms`, `aosp-binder`, `aosp-handler` |
| 安装/升级/权限授予异常 | `aosp-pms` | `aosp-security`, `aosp-storage`, `aosp-ams` |
| 窗口不可见/焦点错乱/层级异常 | `aosp-wms` | `aosp-surfacecontrol`, `aosp-layertree`, `aosp-surfaceflinger` |
| 黑屏/闪屏/花屏/残影 | `aosp-graphics` | `aosp-surface`, `aosp-surfaceflinger`, `aosp-bufferqueue`, `aosp-fence` |
| 掉帧/Jank/输入到显示延迟 | `aosp-vsync` | `aosp-graphics`, `aosp-animation`, `aosp-bufferqueue`, `aosp-fence` |
| Transaction 不生效/状态错乱 | `aosp-transaction` | `aosp-surfacecontrol`, `aosp-layertree`, `aosp-shell` |
| Shell Transition / 分屏 / PiP 异常 | `aosp-shell` | `aosp-transition`, `aosp-wms`, `aosp-surfacecontrol` |
| Binder 堵塞/线程池耗尽/IPC 抖动 | `aosp-binder` | `aosp-anr`, `aosp-ams`, `aosp-handler` |
| Looper/MessageQueue 阻塞 | `aosp-handler` | `aosp-ams`, `aosp-input`, `aosp-binder` |
| 播放/编解码/AV sync 异常 | `aosp-media` | `aosp-graphics`, `aosp-bufferqueue`, `aosp-binder` |
| 相机首帧慢/流配置失败/黑屏 | `aosp-camera` | `aosp-media`, `aosp-surface`, `aosp-fence` |
| 挂载失败/容量异常/多用户存储问题 | `aosp-storage` | `aosp-pms`, `aosp-security` |
| 无法休眠/异常唤醒/耗电突增 | `aosp-power` | `aosp-ams`, `aosp-security`, `aosp-binder` |
| SELinux/权限边界/越权风险 | `aosp-security` | `aosp-pms`, `aosp-binder`, `aosp-storage` |

## 典型组合编排

### ANR 组合
- 主技能：`aosp-anr`
- 辅技能：`aosp-input`（输入超时时）/ `aosp-ams` / `aosp-binder`
- 关键输出：超时类型、阻塞链、首发阻塞点、修复建议。

### 图形显示组合
- 主技能：`aosp-graphics`
- 辅技能：`aosp-surface`、`aosp-surfaceflinger`、`aosp-bufferqueue`、`aosp-fence`、`aosp-vsync`
- 关键输出：帧管线时序、buffer/fence 状态、掉帧根因层。

### 窗口与转场组合
- 主技能：`aosp-wms` 或 `aosp-shell`
- 辅技能：`aosp-transition`、`aosp-surfacecontrol`、`aosp-layertree`
- 关键输出：窗口状态机、事务生效链路、可见性判定。

### 安装与安全组合
- 主技能：`aosp-pms` 或 `aosp-security`
- 辅技能：`aosp-storage`、`aosp-ams`
- 关键输出：安装/权限决策链、配置状态、兼容性影响。

## 冲突裁决规则
- 同一现象有多解释时，优先“可复现实验 + 运行时证据”解释。
- 输入迟滞与掉帧并存时，先判定首发点：
  - 输入队列堆积先发：优先 `aosp-input`。
  - 渲染/合成拥塞先发：优先 `aosp-vsync` 或 `aosp-graphics`。
- 现象在应用层出现但系统无异常证据时，不强行归因系统层。
- 归因到内核/HAL前，必须完成 Framework/Native 证据排除。

## 标准输出协议
最终输出必须包含：
- 问题定义与范围。
- 路由决策（为什么选择这些技能）。
- 主调用链（线程/进程/IPC/JNI/等待点）。
- 源码证据与运行时证据。
- 根因与置信度（`Confirmed`/`Highly Likely`/`Possible`/`Speculative`）。
- 修复建议、影响面、回归验证计划。
- 未闭环证据缺口与补采方案。

## 图示要求
复杂问题至少输出 1 个 Mermaid 图：
- 时序图：用于跨线程/跨进程链路。
- 调用图：用于跨模块调用关系。
- 状态图：用于窗口、事务、生命周期等状态机。

## 与 `aosp-source-analysis-expert` 的关系
- 当问题边界不清、跨域较多时，先由本技能枚举全部子技能并完成全量执行。
- 若未来存在 `aosp-source-analysis-expert`，也只能作为附加细化技能，不能替代其他子技能的执行。
- 本技能负责整合全部子技能结果，输出统一结论。

## 质量红线
- 禁止只列模块名不做调用链。
- 禁止无证据强结论。
- 禁止把现象描述当根因。
- 禁止忽略版本差异和构建差异。
- 禁止只执行部分子技能。
- 禁止只完成路由、不实际读取全部可用子技能。
- 禁止只输出汇总文件而不输出子 Skill 独立文件。
- 禁止只输出子 Skill 独立文件而不输出总控汇总文件。
- 禁止把“应该调用某技能”表述成“已经完成该技能分析”。
- 禁止引用当前会话中不存在的技能而不标明缺失状态。
- 禁止因“总览请求”而跳过子技能执行。

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

## 六、证据链（源码 + 运行时）
- 源码证据：文件路径 + 方法 + 关键条件。
- 运行时证据：logcat/dumpsys/Perfetto/Winscope/traces 等。
- 每个关键结论至少绑定 1 条源码证据和 1 条运行时证据。

## 七、根因结论与置信度
- 根因分层：App / Framework / Native / HAL / Kernel。
- 置信度：Confirmed / Highly Likely / Possible / Speculative。

## 八、修复建议
- 建议必须直连根因，说明影响面与潜在副作用。

## 九、验证计划
- 功能回归、性能回归、稳定性回归、边界场景验证。

## 十、证据缺口与后续采集
- 列出当前缺口与补采方案（采集工具、场景、目标信号）。
```

## 图示规范（统一）
- 架构图与时序图必须使用 Mermaid。
- 图只保留与当前问题直接相关的节点与链路，避免“大而全”。
- 时序图至少包含：发起方、system_server 关键角色、图形/服务关键节点（按问题域选择）。
- 图中命名与正文术语保持一致。

## 置信度分级（统一）
- `Confirmed`：源码与运行时证据闭环，结论可复现。
- `Highly Likely`：证据链完整度高，但存在单点缺口。
- `Possible`：存在方向性证据，但缺关键验证。
- `Speculative`：仅假设，必须显式标注不可直接下结论。
