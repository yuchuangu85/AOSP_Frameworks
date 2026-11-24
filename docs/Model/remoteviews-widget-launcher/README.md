# RemoteViews to Widget to Launcher

直接打开 `index.html`。所有样式、脚本、图标、模型数据和源码摘录都已内嵌，不依赖服务、网络、外部字体或 npm 安装。

## 内容

- 应用构造描述与 Actions、Binder 传输、AppWidget 服务缓存与转发、框架 Host 本地创建或复用 View、后续绘制和示意画面更新。
- 八种场景：首次添加、同布局复用、布局变化重建、异步加载、已有全量缓存上的局部更新、Host 离线补发、首次 apply 错误占位、reapply 失败后 fresh apply。
- 服务缓存、宿主本地结构、屏幕示意、时间轴和阶段解说由同一个纯模型与播放位置派生。
- 可调 Executor 准备与应用后示意绘制等待，支持播放、暂停、单步、拖动、变速和主题持久化。

## 边界

当前仓库只有框架 `base/` 与 `native/` 等源码，没有 Launcher3 私有实现。页面中的 Launcher 表示小组件 Host 角色，不声称验证 Workspace 网格摆放、具体 LauncherAppWidgetHost 子类或其 Executor 配置。

本例只覆盖普通 XML 路径、一个已获授权绑定的 Widget、正常有效的 Provider/Host 与一次更新。忽略集合、legacy list deferred-update、多更新取消竞态、完整多用户/遮罩路径、DrawInstructions 等扩展。局部更新明确假设已有全量缓存；当前可执行代码在旧缓存为空时的行为与旧公开注释有差异，不能直接泛化成注释所称的忽略。

图中的时间、跨边界标记及最终画面更新都是教学假设，不是实测。`onViewApplied` 之后仍可能没有实际显示；页面没有设备 present fence、FrameTimeline 或屏幕扫描证据。异步成功的最终动作、listener 与 Host applyContent 被放在同一个主 Looper 完成阶段，不制造额外跨帧回调。

原 `docs/Model/remoteviews` 的工作区删除状态未恢复或改动。本次仅新增当前目录，不修改 AOSP 实现。

## 维护与验证

编辑 `src/`、`data/appwidget-sources.json`、`data/remoteviews-sources.json`；`index.html`、`data/sources.json` 与身份文件由构建生成。

```sh
node docs/Model/remoteviews-widget-launcher/build.mjs
node --test docs/Model/remoteviews-widget-launcher/tests/model.test.cjs
python3 -B ~/.codex/skills/source-pipeline-explorer/scripts/verify_sources.py \
  docs/Model/remoteviews-widget-launcher/data/sources.json --source-root . \
  --report docs/Model/remoteviews-widget-launcher/data/source-verification.json
```

已有 Playwright 安装时可执行浏览器检查，不为页面新增依赖：

```sh
PLAYWRIGHT_MODULE=/path/to/playwright-core \
CHROME_PATH='/Applications/Google Chrome.app/Contents/MacOS/Google Chrome' \
node docs/Model/remoteviews-widget-launcher/tests/browser.cjs
```

摘录匹配、机制复核、模型回归、浏览器交互和真实设备显示是不同证据层。工具图标来自 Lucide，采用 ISC license；相关通知也嵌入最终单文件。
