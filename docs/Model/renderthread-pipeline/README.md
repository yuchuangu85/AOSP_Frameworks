# RenderThread 解剖 · 一帧的 GPU 绘制流水线

单文件、离线可打开的交互式 HTML：只讲 UI 线程交棒之后，**RenderThread 内部**如何把一帧变成 GPU 命令、
BufferQueue buffer 与 BLAST Transaction。

## 打开

\`\`\`bash
cd docs/Model/renderthread-pipeline
open index.html
# 或同一份独立副本
open RenderThread_Visualization.html
\`\`\`

## 动画模型

时间轴以 **APP VSYNC → SF VSYNC 的 8.3ms APP 总预算**为参照，5 条 lane：

\`\`\`
UI Thread          traversal/draw 录制（2.6ms）→ postAndWait → 阻塞 → 解阻
RenderThread       threadLoop → syncFrameState → prepareTree → draw → Skia/GPU → eglSwap → BLAST
GPU                GPU 命令执行 / completion fence
BufferQueue/BLAST  dequeue → queueBuffer → Transaction apply → release
Callback/Fence     acquire fence / txn applied / frame commit / release
\`\`\`

- 可播放、暂停、拖拽、0.5×/1×/2× 变速；点击阶段编号跳到对应章节。
- 正常帧：UI 约 2.6ms + 交棒 0.4ms，RT 从 3.0ms 开始，8.0ms queueBuffer，8.3ms 是 SF VSYNC / APP deadline。
- “慢帧演示”：拉长 syncFrameState / prepareTree / GPU，让 queueBuffer 越过 8.3ms deadline，观察 RT 与 UI 的阻塞传播。
- 底部 slot 状态展示 dequeue → QUEUED → LATCHED 的 buffer 生命周期。
- 相位/耗时为示意值：RT 不是从 0 开始，只拿到 8.3ms 中除去 UI 绘制/交棒后的约 5.3ms；实际受视图规模、GPU、EGL 驱动、BufferQueue 深度与刷新率影响。

## 九个阶段

1. RenderThread 的创建与线程模型（RenderThread、WorkQueue、ChoreographerSource）
2. UI 线程交棒：syncAndDrawFrame → DrawFrameTask 同步协议
3. syncFrameState：树同步、纹理上传与 unblockUiThread
4. prepareTree：遍历 RenderNode、属性/动画、damage、reserveNext
5. CanvasContext::draw：脏区、取 Frame、RenderPipeline::draw
6. Skia 回放 DisplayList 与 GPU 命令提交
7. swapBuffers：EGL、dequeue、queueBuffer、fences
8. BLAST：buffer 作为 Transaction 提交给 SurfaceFlinger
9. 回调、帧时间线与 jank 判定

每阶段包含调用链、逐行核验的源码片段、关键字段、坑点与 trace 观测点。

## 源码与核验

- 仓库：当前工作区，\`android17-release\`，构建时 HEAD 见页面 footer
- 仅使用 \`base/\` 与 \`native/\` 下的实现
- 构建时核验：36/36 段代码片段与源码逐行匹配，覆盖 14 个文件（见 \`data/verify-report.json\`）
- 时间轴为示意，不是实测数据

## 构建 / 修改

\`\`\`bash
# 在本目录下
python3 build.py
# 或从仓库根目录
python3 docs/Model/renderthread-pipeline/build.py
\`\`\`

- 修改动画：\`src/scenarios.js\`（lane、阶段、块、slot、deadline）
- 修改播放/交互：\`src/rt-studio.js\`
- 修改样式/骨架：\`src/style.css\`、\`src/fp-studio.css\`、\`src/shell.html\`
- 修改内容：\`data/stage-01.json\` ... \`stage-09.json\`（schema 见 \`DATA_SCHEMA.md\`）

## 目录

\`\`\`
renderthread-pipeline/
├── index.html                     # 最终单文件页面
├── RenderThread_Visualization.html# 同一份独立副本
├── DATA_SCHEMA.md
├── build.py                       # 组装 + 源码核验
├── src/
│   ├── shell.html
│   ├── style.css
│   ├── fp-studio.css
│   ├── theme-dark.css / theme-light.css
│   ├── scenarios.js               # 正常帧 / 慢帧的时间轴数据
│   ├── rt-studio.js               # 动画引擎
│   └── app.js                     # 代码高亮、复制、scrollspy、theme
└── data/
    ├── stage-01.json ... stage-09.json
    └── verify-report.json
\`\`\`
