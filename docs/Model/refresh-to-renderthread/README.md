# 一帧之前 · 从 requestLayout 到 RenderThread 接手

单文件、离线可打开的交互式 HTML：讲一次界面刷新请求如何从 View 的脏标记，
经过 scheduleTraversals、同步屏障、Choreographer 的 VSYNC 预约、doFrame、
WMS relayout、measure/layout、DisplayList 录制，最后交棒给 RenderThread。

## 打开

```bash
cd docs/Model/refresh-to-renderthread
open index.html
# 或同一份独立副本
open Refresh_Visualization.html
```

## 动画模型

5 条 lane：

```
APP · UI Thread       requestLayout / invalidate / doFrame / measure / layout / draw
Choreographer/VSYNC   scheduleTraversals → postVsyncCallback → VSYNC → doFrame 回调
WindowManagerService  relayoutWindow / frame / insets
RenderNode/DisplayList RecordingCanvas / DisplayList
RenderThread          DrawFrameTask / postAndWait 交棒
```

- 触发后先等 APP VSYNC；traversal 被 `mTraversalScheduled` 合并，不会重复调度
- 顺序按源码：`measureHierarchy` 预测量 → `relayoutWindow` → `performMeasure` → `performLayout` → `draw`
- 正常帧：APP VSYNC 后 UI 工作约 8ms，24.8ms 交棒，33.3ms 前完成
- 慢帧：measure/layout/draw 变长，交棒 35.0ms，越过 33.3ms 截止
- 可播放、暂停、拖拽、0.5×/1×/2× 变速；点击阶段编号跳到章节
- 时间轴为示意，真实耗时随 View 层级、窗口变化与设备性能变化

## 八个阶段

1. 触发：requestLayout / invalidate
2. 调度：scheduleTraversals 与同步屏障
3. 请求 VSYNC：Choreographer 到 SurfaceFlinger
4. VSYNC 送达与 doFrame
5. relayoutWindow：向 WMS 要窗口 frame / insets
6. measure 与 layout
7. draw：只负责录制 DisplayList
8. 交棒 RenderThread：syncAndDrawFrame / postAndWait

每阶段包含调用链、逐行核验的源码片段、关键字段、坑点与 trace 观测点。
页面在 `syncAndDrawFrame` / `postAndWait` 的 UI 阻塞边界结束，RenderThread 内部流程见
[RenderThread 专题](../renderthread-pipeline/index.html)。

## 源码与核验

- 仓库：当前工作区，`android17-release`，构建时 HEAD 见页面 footer
- 仅使用 `base/` 与 `native/` 下的实现
- 构建时核验：32/32 段代码片段与源码逐行匹配，覆盖 11 个文件（见 `data/verify-report.json`）
- `android17` 已无 `mTraversalRunnable`，traversal 入口是 `ViewRootImpl.TraversalCallback`
- `scheduleTraversals` 会 `postTraversalBarrier()` 插同步屏障；`Choreographer` 的帧消息 `setAsynchronous(true)` 不受屏障阻塞
- 时间轴为示意，不是实测数据

## 构建 / 修改

```bash
cd docs/Model/refresh-to-renderthread
python3 build.py
```

- 修改动画：`src/scenarios.js`
- 修改播放/交互：`src/rf-studio.js`
- 修改样式/骨架：`src/style.css`、`src/fp-studio.css`、`src/shell.html`
- 修改内容：`data/stage-01.json` ... `stage-08.json`（schema 见 `DATA_SCHEMA.md`）

## 目录

```
refresh-to-renderthread/
├── index.html                    # 最终单文件页面
├── Refresh_Visualization.html    # 同一份独立副本
├── DATA_SCHEMA.md
├── build.py                      # 组装 + 源码核验
├── src/
│   ├── shell.html
│   ├── style.css
│   ├── fp-studio.css
│   ├── theme-dark.css / theme-light.css
│   ├── scenarios.js              # 正常帧 / 慢帧
│   ├── rf-studio.js              # 动画引擎
│   └── app.js                    # 代码高亮、复制、scrollspy
└── data/
    ├── stage-01.json ... stage-08.json
    └── verify-report.json
```
