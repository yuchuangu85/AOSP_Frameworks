# SurfaceFlinger 解剖 · 从 Transaction 到扫描输出

单文件、离线可打开的交互式 HTML：讲 RenderThread 交帧之后，SurfaceFlinger 如何应用 BLAST Transaction、
latch buffer、做合成决策、调用 HWC/RenderEngine present，并在 HW VSYNC 扫描输出后回收 buffer。

## 打开

\`\`\`bash
cd docs/Model/surfaceflinger-pipeline
open index.html
# 或同一份独立副本
open SurfaceFlinger_Visualization.html
\`\`\`

## 动画模型

以 60Hz 的**自定义 WorkDuration（appWorkDuration = sfWorkDuration = 8.3ms）**为参照，5 条 lane：

\`\`\`
APP · BLAST          Transaction + BufferData 到达
SF · Scheduler       MessageQueue::vsyncCallback → onFrameSignal → commit / composite 两段式
SF · Layer           apply transactions / updateLayerSnapshots / latch (acquire fence)
Composition / HWC    chooseCompositionStrategy → validate → RenderEngine client comp → presentDisplay
Display / Timeline   HW VSYNC 扫描输出 → retire / release / FrameTimeline
\`\`\`

- 时间轴从 **APP VSYNC = 0.0ms** 开始；APP 在 0–8.3ms 完成绘制/提交，**SF VSYNC = +8.3ms**，目标 **HW VSYNC = +16.7ms**，扫描输出到 33.3ms。
- 正常帧：present 约 12.9ms 完成，距 16.7ms HW VSYNC 约 3.7ms 余量；16.7ms 开始扫描，33.3ms retire/release。
- 慢帧 / GPU 合成：RenderEngine client composition 吃掉 8.3ms SF 预算，present 约 18.2ms，越过 16.7ms HW VSYNC；本帧顺延到 33.3ms 才扫描上屏。
- 可播放、暂停、拖拽、0.5×/1×/2× 变速；点击阶段编号跳到对应章节。
- 底部 buffer 状态展示 QUEUED → LATCHED → DISPLAY → FREE。
- 相位/耗时为示意值；真实 WorkDuration 受设备配置、图层数、HWC 能力、GPU、面板与刷新率影响。

## 九个阶段

1. 输入：BLAST Transaction 与 buffer 到达 SF
2. Scheduler：commit 与 composite 两段式
3. commit：事务应用、LayerSnapshot 与 latch
4. latch：buffer 与 acquire fence
5. 合成决策：CompositionEngine + HWC validate
6. GPU client composition：RenderEngine
7. present：HWC presentDisplay 与 fences
8. display 与 retire：HWC / DRM / panel
9. 回调、buffer 回收与 jank 闭环

每阶段包含调用链、逐行核验的源码片段、关键字段、坑点与 trace 观测点。

## 源码与核验

- 仓库：当前工作区，\`android17-release\`，构建时 HEAD 见页面 footer
- 仅使用 \`base/\` 与 \`native/\` 下的实现
- 构建时核验：36/36 段代码片段与源码逐行匹配，覆盖 17 个文件（见 \`data/verify-report.json\`）
- HWC HAL 以下的 DRM/KMS、面板扫描、vendor EGL 驱动行为不在本仓库，页面已标注边界
- 时间轴为示意，不是实测数据

## 构建 / 修改

\`\`\`bash
# 在本目录下
python3 build.py
# 或从仓库根目录
python3 docs/Model/surfaceflinger-pipeline/build.py
\`\`\`

- 修改动画：\`src/scenarios.js\`（lane、阶段、块、slot、deadline）
- 修改播放/交互：\`src/sf-studio.js\`
- 修改样式/骨架：\`src/style.css\`、\`src/fp-studio.css\`、\`src/shell.html\`
- 修改内容：\`data/stage-01.json\` ... \`stage-09.json\`（schema 见 \`DATA_SCHEMA.md\`）

## 目录

\`\`\`
surfaceflinger-pipeline/
├── index.html                       # 最终单文件页面
├── SurfaceFlinger_Visualization.html# 同一份独立副本
├── DATA_SCHEMA.md
├── build.py                         # 组装 + 源码核验
├── src/
│   ├── shell.html
│   ├── style.css
│   ├── fp-studio.css
│   ├── theme-dark.css / theme-light.css
│   ├── scenarios.js                 # 正常帧 / GPU 合成慢帧
│   ├── sf-studio.js                 # 动画引擎
│   └── app.js                       # 代码高亮、复制、scrollspy
└── data/
    ├── stage-01.json ... stage-09.json
    └── verify-report.json
\`\`\`
