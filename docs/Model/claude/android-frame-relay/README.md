# 一帧的接力 · Android 图形管线

入口：[index.html](index.html)。单文件，离线可用，直接用浏览器打开。

```sh
open docs/claude/android-frame-relay/index.html
```

用 25 个交接点讲一帧：从 `View.invalidate()` 到显示面板扫描，再到旧 buffer 复用。共 7 章，口诀是“标 · 等 · 录 · 渲 · 交 · 合 · 显”。

- 舞台：应用进程、SurfaceFlinger 进程、硬件 / HAL 三条色带。红点 F 表示当前执行者，红色流动线表示本步交接路径，黑色波纹表示 VSync。
- 指示灯：交接物、acquire fence、release fence、屏幕状态，同时显示 BufferQueue 槽位和 SF 图层快照。
- 讲解与源码：每一步有一句话要点、过程说明和易混淆点，右侧是本地源码摘录（保留原始行号，可点开文件）。
- 时间线：60Hz 示意毫秒，标出硬件 / APP / SF 三种 VSync，可点击色块跳转。
- 快捷键：`Space` 播放，`← / →` 步进，`1`–`7` 跳章，`?` 帮助。URL 带 `#步骤id` 可直接定位（如 `#commit`）。

## 边界

- 源码基于本仓库 `android16-qpr2-release` 工作树。GPU 执行和面板扫描两步是示意伪代码，相关实现不在本仓库。
- 时长、相位、3 块 buffer 和混合合成策略都是教学示例，不是真机测量。
- 不覆盖 SurfaceView / TextureView、软件绘制、多屏、VRR 和 latch-unsignaled 分支。

## 验证

`verify.cjs` 会逐字比对页面里引用的每一行源码和本地文件，并检查每一步的舞台、讲解、时间线、fence 状态、键盘操作、播放和响应式布局。依赖只装在 `/tmp`，使用系统 Chrome。

```sh
npm install --prefix /tmp/afr-test --no-save playwright@1.58.2
NODE_PATH=/tmp/afr-test/node_modules node docs/claude/android-frame-relay/verify.cjs
```
