# Wiki 变更日志

## 2026-11-24 · 初始构建（android17-release）

**范围**：仓库架构总览 + 6 条核心链路，落盘 7 页 + 索引 + 校验脚本。

| 页面 | 产出方式 | 自报行数 | 实测行数 |
| --- | --- | --- | --- |
| architecture-overview | 主控手写（事实取自 SystemServer/ZygoteInit 源码） | — | 152 |
| binder-ipc | 子代理 A（binder 域） | 258 | 258 |
| ams-activity-broadcast | 子代理 B（am/wm 域） | 239 | 239 |
| wms-transition | 子代理 C（wm/Shell 域） | 234 | 234 |
| input-dispatch | 子代理 D（inputflinger 域） | 233 | 233 |
| render-pipeline | 子代理 E（hwui/SF 域） | 250 | 250 |
| vsync-pipeline | 子代理 F（Scheduler 域） | 230 | 230 |

**校验**（`python3 omx_wiki/tools/verify_refs.py`）：

- 代码引用 730 处：文件全部存在，行号全部在文件长度内（`problems=0`）。
- 规范化：196 处裸文件名引用（如 `base/services/core/java/com/android/server/wm/ActivityStarter.java:1540`）已补全为仓库全路径；`ActivityStarter` / `Transition` / `InputMonitor` / `main.cpp` 这 4 个重名文件按上下文消歧到 wm / servicemanager 版本。
- 双链（wiki-link）12 处全部可达；`../` 相对链接全部存在；九节骨架 7/7 页面齐全。
- 抽查（逐个对照源码行）：`base/core/java/android/os/Binder.java:915`(`onTransact`)、`base/core/java/android/os/BinderProxy.java:568`(`transact`)、`base/core/java/android/os/Parcel.java:1400`(`writeStrongBinder`)、`native/libs/binder/IPCThreadState.cpp:948`(`TF_ONE_WAY` 分支)、`native/libs/binder/IServiceManager.cpp:333`(`defaultServiceManager`)、`native/services/surfaceflinger/Scheduler/FrameTimeline.h:193` 均命中。

**已知未闭合点**（来自各页 §9，均为"本仓库外"或"需真机验证"）：

1. binder：`dumpsys binder` 是否为注册服务名、RPC Binder 会话模型、进程冻结/oneway 限流阈值。
2. AMS：`expediteActivityLaunchOnColdStart` 的 aconfig 默认值、USAP 路径、有序广播阻塞队列唤醒链。
3. WMS：`useClientSurface()` 的设备侧取值（默认按开启叙述）、`InputDispatcher` 侧消费路径（`native/libs/input` 无 `InputDispatcher.cpp`）。
4. Input：无障碍 `InputFilter` 回注路径、EventHub 设备打开细节、窗口自定义 `dispatchingTimeout` 运行时来源。
5. 渲染：`HWComposer::setClientTarget` 调用者、Vulkan 路径、`Output::present` 的线程归属。
6. VSync：HWC 回调线程优先级、`JankClassificationThresholds` 默认数值、WorkDuration 三套配置的设备取值。

**环境说明**：本机未安装 `omx`，页面按 wiki 技能规范手写于 `omx_wiki/`，未调用 `omx wiki` 子命令。

## 2026-11-24 · 新增 HTML 深挖页：Shell Transition

- 产物：`docs/wiki_site/dist/shell-transition.html`（273 KB，离线单文件）
- 数据：`docs/wiki_site/topics/shell-transition/topic.json`（26 步 / 24 条时序消息 / 11 个类 / 10 条关系 / 7 条降级）
- 逐行注释：13 个 P0 函数、933 行代码、732 条注释，覆盖率 94.4%–100%
- 管线：`build.py` 抽取源码 + quote 校验 + 生成四类 SVG；`verify.py` 输出 `problems: 0`
- 与 Markdown wiki 的分工：`wms-transition.md` 仍是索引与结论；HTML 页承担「逐行 + 图联动」

## 2026-11-24 · Shell Transition 页：补流程完整性与全量方法清单（A+B）

- 步骤 26 → **42**：新增 player 注册、状态机、SyncSet 超时、addToSync、可见性提交、leash 构造、merge、sync/sleep 闸门、handler 分派链、动画落地、finish 顺序、cleanup、abort、旋转/睡眠、PiP/legacy、诊断
- 新增「方法清单」页：从 12 个相关文件抽出 **496 个方法 / 6,395 行**，标注逐行/部分/未覆盖，支持过滤与跳转
- 新增 10 张函数级卡片（finishTransition 323 行、dispatchReady 114 行、getChangeFlags 110 行等），降级条目 7 → 12
- 口径澄清：逐行注释仍为 13 个函数 / 933 行（方法体 14.6%），不做全量逐行
- 校验：`build.py` problems: 0；`verify.py` problems: 0（含 steps≥40、methods≥400、cards≥8 的完整性断言）

## 2026-11-24 · Shell Transition 页：新增「精读」层（42 篇）+ 事实校正

- 精读：42 步每步一篇 deep/<step>.json（结论 / 为什么需要 / 术语 / 逐调用 walk / 状态表 / 线程锁 / 新手常踩 / 验证命令 / 下一步），页面新增「精读」页、术语表（18 条）、真实转场时间线、4 条学习路径、5 张分支决策表
- 校正（由精读写作过程发现，已同步 brief/facts/pitfalls）：
  1. `requestTransitionIfNeeded` 返回时已是 COLLECTING，不是 PENDING；`start()` 才推进到 STARTED
  2. `createAndStartCollecting` 不申请 syncId；申请在 `Transition.startCollecting`
  3. `collect` 不做起始状态快照；快照在构建 TransitionInfo 时抄写
  4. SyncSet 完成由 `onSurfacePlacement` 驱动，不是 SF 的 commit 回调
  5. SyncSet 超时走 `onTimeout → finishNow → onTransactionReady`，不是「controller 强制 finish 后直接 apply」
  6. `finishTransition` 不释放 syncId（在 onTransactionReady 返回时摘除）
  7. **SYNC_METHOD 默认 METHOD_NONE**，BLAST 才需 `persist.wm.debug.shell_transit_blast=true`
  8. **FallbackPlayer 受 `Flags.fallbackTransitionPlayer()` 门控**，flag 关闭时不建转场
  9. `DefaultTransitionHandler` 用自己的 `addBackgroundColor`，不是 `TransitionAnimationHelper.addBackgroundToTransition`
  10. Perfetto tracer 记 create/send/finish/abort，request→ready 只在 ProtoLog
- 校验：`verify.py` 新增 deep 层断言（42/42、walk ≥3 且 ref 真实、state/pitfalls/debug 非空），problems: 0

## 2026-11-24 · Shell Transition 页：新增亮/暗色切换

- 右上角主题按钮 + 快捷键 `t`；默认跟随系统，选择存 localStorage；`?theme=light|dark` 可临时覆盖
- SVG 图表适配：架构/时序/类图/流程图的模块名、泳道名、边线在亮色下改为加深语义色（避免 #ffb454 在白底对比不足），代码 token、注释列、清单表、精读文章同步出亮色配色
- 构建期给图元加了 `grp-*` / `lane-*` / `side-*` / `edge-*` / `msg-*` 类名，主题规则只作用于文字与描边，不再染到容器矩形
- 校验：build problems 0；verify problems 0（deep 42/42、refs 全绿）

## 2026-11-24 · Shell Transition 页：新增「文件结构」视图

- 从 steps / deep walk / p0 / scope 聚合出 24 个文件、54,755 行的目录树；单链目录合并（如 `base/services/core/java/com/android/server/wm`），目录在前
- 每个目录显示「涉及 M / 共 N 个文件」，文件显示行数、逐行函数数、被引用次数；`仅引用` 标记不在深挖范围内的文件
- 文件详情：直接引用的步骤、精读 walk 里提到的步骤、逐行函数、主要方法（按行数），均可点击跳转
- 支持过滤、只看被引用、全部展开/折叠；深链 `#files:<path>` 可直接选中文件
- 主题：树与详情页在亮/暗色下都适配
- 校验：verify 增加文件树断言，problems: 0

## 2026-11-25 · 新增主题：RenderThread → SurfaceFlinger 全链路

- 产物：`docs/wiki_site/dist/render-thread-to-sf.html`（570 KB）；索引页 `dist/index.html`
- 规模：36 步 / 10 个 P0 函数（728 行，561 条逐行注释，覆盖率 100%）/ 36 篇精读 / 203 方法清单 / 36 文件树 / 12 类 / 29 条时序消息
- 管线改造：`build.py`、`verify.py` 支持 `--topic <slug>`；模板新增 `{{KICKER}}`；`verify` 阈值按 topic 配置
- 由精读写作校正的 topic 事实（已同步 brief/refs/pitfalls/branches）：
  1. s03：FrameInfo 由 JNI 拷进 task，RenderProxy 只转发
  2. s07：syncFrameState 返回 prepareTextures，跳帧看 skippedFrameReason
  3. s09：syncDisplayList 时 UI 仍阻塞在 mSignal
  4. s11：跳帧有两道闸门，且 NothingToDraw 不上报 SF
  5. s14：renderLayersImpl 在 renderFrame 里、早于 renderFrameImpl
  6. s16：GL/Vulkan 的 requireSwap 判定不同
  7. s19：queueBuffer 不等 slot，阻塞在 lastQueuedFence（Throttling EGL Production）
  8. s22/s32/s34/s36：行号偏移修正（667/726、1302、1390、trace 点）
  9. s25：latchBufferImpl 轮询 fence、不等不超时，调用方忽略返回值
  10. s28：CE 入口是 Display::chooseCompositionStrategy；validate 失败静默退化为全 CLIENT
  11. s29：RenderEngine 同步执行，串行靠 mRenderingMutex
  12. s31：release 走 releasePendingBuffer → TransactionCallbackInvoker
  13. s34：bufferReplaced 在标准 BLAST 路径不可达（mQueueBufferCanDrop=false）
- 校验：两个主题 verify 均 problems: 0

## 2026-11-25 · 三张图升级为「详细版」（RT→SF）

- 架构图：7 个时间阶段 × 4 个线程的网格，16 个节点 + 16 条带数据形态的边（FrameInfo / display list / GraphicBuffer+fence / Transaction），行内按 slot 堆叠避免遮挡
- 时序图：6 条泳道（UI/RT/GPU/BLAST/SF/HWC）、33 条消息、6 个阶段带、数据标签、红色阻塞标记（UI 阻塞在 mSignal）、左侧步骤号
- 流程图：3 个分区（判定 7 节点 / 入队 6 / 合成 9），侧边通道画重试回路（commit 重试、跳帧回路）
- 模板新增读图提示（archGrid/seqDetail/flowDetail 存在时自动切换文案）；渲染器对旧结构自动回退
- 校验：两个主题 verify 均 problems: 0

## 后续变更模板

```
## YYYY-MM-DD · <变更摘要>
- 页面：<slug>（新增/更新）
- 触发：<仓库升级 / 代码阅读发现错误 / 读者反馈>
- 校验：python3 omx_wiki/tools/verify_refs.py → problems=0
- 漂移：<哪些行号需要重核>
```
