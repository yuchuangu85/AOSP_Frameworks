# 一帧的旅程 · AOSP 渲染管线可视化

单文件、离线可打开的交互式 HTML：从 `View.requestLayout()` 到 VSYNC、Choreographer、measure/layout/draw、
RenderThread、BLAST、SurfaceFlinger、HWC 的源码讲解与流程示意。动画不是精确时序预测器，也不输出真机掉帧归因。

## 打开

```bash
cd docs/Model/vsync-pipeline
open index.html
# 深链：?preset=ui|uicont|drop|gpu|gpuwait|sf|aosp60|aosp120&mode=step|proc&focus=1&t=40&pause=1&theme=light|dark&zoom=fit|2|4|6&follow=0
```

## 相位模型

页面显式区分三个相位，而不是把 APP / SF 画成相邻两拍：

```
DISPLAY VSYNC V0
      │
      ├─ APP VSYNC（Choreographer 唤醒）── UI + RenderThread 绘制 ── queueBuffer
      │                                                    │
      │                                       SF VSYNC（同一刷新周期内更晚的相位）
      │                                                    │
      │                                       commit / latch / compose / present
      ▼
DISPLAY VSYNC V2 ── present fence signal（固定刷新率示例）
```

- 两个唤醒点从预期上屏时刻倒推：`SF = present − sfWorkDuration`，`APP = SF − appWorkDuration`
  （`VSyncDispatchTimerQueue.cpp`：wakeup = vsync − workDuration − readyDuration）。
- APP 的生产预算 = 相位差 = `appWorkDuration`。动画取 appWorkDuration ≈ sfWorkDuration ≈ 8.3ms，
  APP 与 SF 同周期接力、V2 上屏。这是一组合法配置，**不是 AOSP 默认值**。
- 代码默认是 `PhaseOffsets`（60Hz：app = sf = 1ms，APP→上屏约 2P；高刷：app = 2ms、sf = 1ms，约 3P）；
  `debug.sf.use_phase_offsets_as_durations` 或 `DisplayModeSpecs.workDuration` 会切到 `WorkDuration`。
  真机用 `dumpsys SurfaceFlinger --vsync` 查看。
- 本帧赶不上 SF 唤醒时顺延一个周期，DISPLAY V2 重复上一帧，V3 才上屏。
- BLAST 默认 layer backpressure 对同层后续自动时间戳事务留队；只有显式关闭后才演示同轮覆盖旧 buffer。
- 默认 `AutoSingleLayer` 的未完成 latch 有首个事务、单层简单更新、非 early 配置等条件。动画的开关表示“假定条件满足”，不是只读一个 sysprop 就保证准入。`gpu` / `gpuwait` 对照先 latch 和等待完成后再 latch。
- SF 背压检查此时本应完成的历史 present fence，而非一律等待紧邻上一帧；本例省略宽限期和多显示器策略。
- release 回传、slot 归还、release fence signal 与安全写入是不同事件。本例用后续 HWC 提交后归还、显示节拍 signal 表示这种区别；不承诺真实回收时刻。
- 所有动画均假设固定刷新率、单 layer、DEVICE 合成、三个轮转 slot。真实 slot 容量按配置计算；BLAST acquire 后的队列状态是 `ACQUIRED`，SF latch / 显示另行表示。GL dequeue 时机、client composition、VRR 与 vendor EGL/HWC 均未还原。

## 内容

- **动画工作室（参数化、多帧）**：同一份模拟，两种视图共用一个播放头。
  - 时间轴：7 条 lane（UI / RenderThread / GPU / BufferQueue 3 个 slot / SF Scheduler / SurfaceFlinger / Display），
    同时画 F0~F2（及一帧后续帧），跨 lane 交接用箭头标出传递的数据，虚线为跨进程。
  - 步骤：焦点帧的 18 次交接等距排列，数据包沿因果边移动，可单步（← →）。
  - 时间轴默认 4×（56 px/ms），可切换 适应宽度 / 2× / 4× / 6×；时间轴和步骤视图都可横向滚动，
    播放时自动跟随播放头（手动横向滚动后关闭跟随），泳道名固定在左侧。
  - 进程舞台：沿用 `claude/android-frame-relay` 的三层布局（APP / SURFACEFLINGER / HARDWARE），
    每个执行者一个方框，红点是焦点帧所在位置，红色流动线是本步交接；同时标出其他帧的并行与阻塞，
    以及 BufferQueue 槽位、SF 本轮输入和 acquire / release fence、屏幕四个指示灯。
  - 状态面板：调用链逐条高亮、跨线程/进程传递的数据、Choreographer 回调队列、View 流程、buffer 所有权、slot 卡片。
  - 场景预设：同周期接力、默认 PhaseOffsets 参数 60Hz / 120Hz、UI 延后（单次 / 连续动画）、关闭 layer 背压、GPU 延迟与等完成再 latch、SF 延后；
    也可调刷新率、appWorkDuration / sfWorkDuration、两项策略条件与 F1 的各段负载。结果仅是模拟的按时 / 延后 / 跳过，不等同 FrameTimeline jank 分类。
- **8 个源码阶段**：每阶段包含调用链、逐行核验的代码片段、关键字段、坑点、trace 观测点。
  代码框标题栏带「GitHub 源码 ↗ · 本地文件」：GitHub 链接固定到构建时的 commit 并带行号，本地链接是相对仓库根的路径。
- **延迟模型 / 掉帧归因 / 真机观测命令 / 源码索引**。
- **配色**：深色沿用 `AOSP_Frame_Rendering_Flow.html`（`src/theme-dark.css`），浅色沿用 `claude/android-frame-relay`
  （`src/theme-light.css`）；顶栏按钮在跟随系统 / 浅色 / 深色之间切换，或用 `?theme=`。
- **六个常见误解**（页面底部，来自 `claude/android-frame-relay`）：每条可跳回动画中对应的步骤。

## 源码版本与核验

- 仓库：当前工作区，`android17-release`；页面中的 HEAD 由 `build.py` 构建时从 git 读取
- 仅使用 `base/` 与 `native/` 下的实现；**未参考 `docs/` 目录**
- 构建时自动核验：每段代码片段的非 `// ...` 行必须按顺序与引用范围内的源码行**精确一致**
  （只忽略空白差异），且引用范围不能明显宽于片段（核验明细见 `data/verify-report.json`）
- 时间轴耗时为示意量级，用于表达阶段重叠、相位与依赖关系，不是实测值。片段通过只证明引用一致，不证明正文或动画完整还原了所有源码分支。

## 构建 / 修改

```bash
# 在本目录下
python3 build.py
# 或从仓库根目录
python3 docs/Model/vsync-pipeline/build.py
# 无第三方依赖的模型回归
node --test docs/Model/vsync-pipeline/tests/fp-model.test.cjs
# 可选：使用已安装的 playwright-core 和 Chrome 做隔离无头页面测试
# PLAYWRIGHT_MODULE 可指定现有模块路径，CHROME_PATH 可指定 Chrome 可执行文件
node docs/Model/vsync-pipeline/tests/browser-smoke.cjs
```

- 修改样式 / 交互：`src/style.css`、`src/app.js`、`src/shell.html`
- 修改动画：`src/fp-steps.js`（步骤目录）、`src/fp-presets.js`（耗时与预设）、`src/fp-model.js`（时序规则）、
  `src/fp-board.js` / `fp-arrows.js` / `fp-stepview.js` / `fp-panel.js` / `fp-studio.js`（视图与控制）、`src/fp-studio.css`
- 修改内容数据：`data/stage-01.json` ... `stage-08.json`（schema 见 `DATA_SCHEMA.md`）

构建脚本会检查每个 snippet 的文件、行号与代码；核验失败的片段会打印警告并计入
`data/verify-report.json`，存在失败时退出码为 1。`index.html` 由 `build.py` 生成，不要手工修改。

## 目录

```
vsync-pipeline/
├── index.html                        # 最终单文件页面（自包含 CSS/JS/数据）
├── DATA_SCHEMA.md                    # 阶段数据的交付与核验规范
├── build.py                          # 组装 + 源码核验
├── src/
│   ├── shell.html                    # 页面骨架与静态章节
│   ├── style.css                     # 设计系统与响应式布局
│   ├── app.js                        # 代码高亮、复制、scrollspy
│   ├── fp-*.js / fp-studio.css       # 动画工作室：步骤、模型、两种视图、面板、控制器
└── data/
    ├── stage-01.json ... stage-08.json
    └── verify-report.json            # 引用核验结果
```
