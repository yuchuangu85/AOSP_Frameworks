# RemoteViews / AppWidget 到 Launcher

入口：[离线交互说明页](index.html)。直接打开即可，无开发服务器、CDN、字体或网络请求。

当前首页默认先解释 RemoteViews 本身：描述对象、Action、Parcel/Binder、宿主 inflate、viewId 定位和方法许可，随后再看 Widget 在 Launcher 中的完整接入。选择“Launcher · 完整接入与显示路径”可查看绑定、广播、HostView、窗口绘制和 SurfaceFlinger。详细文字版见 [LAUNCHER_FLOW.md](LAUNCHER_FLOW.md)。

源码范围：当前仓库 `base/` 与显示链路的 `native/`。构建记录 checkout commit、两棵源码 tree、与之相同的本地 `origin/main` 快照，以及所引文件 SHA-256。当前两棵源码树均与 `213ab0f8152c35a488fc526f7307c97bb33b18ae` 一致，该提交描述为 `update android17-release`；这不构成运行设备 Android 版本或 feature flag 的证明。仓库没有 Launcher3 实现，具体工作区挂接只标集成责任与测试宿主示例，不伪造 Launcher3 调用点。页面只链接本地文件，没有声称已经验证远端永久链接。

## 机制结论

1. **RemoteViews 不是远程 View 对象。** XML 主路径携带资源身份、布局 ID 与可序列化 Action。`setTextViewText` 只记录 `ReflectionAction`，直到宿主 apply 才真正调用 TextView。
2. **界面在宿主进程中创建和更新。** 宿主使用提供方资源、受限 Context 与类过滤器 inflate，再执行动作。`@RemoteView` 管类，`@RemotableViewMethod` 管反射方法；两者不等价，也不允许任意应用代码。
3. **服务更新方式与 View 创建方式独立。** 全量更新替换服务缓存描述，却仍可能在宿主 reapply 原树，保留未覆盖的属性。局部更新按 Action 键和策略合并，也可以在需要时重新 apply。
4. **异步分为执行器准备与主 Looper 完成。** inflate / initActionAsync 可在所选后台执行器中准备，最终 Action 与 OnViewAppliedListener 在 context 主 Looper 执行；自定义 Executor 的实际线程取决于它的实现。
5. **点击不是跨进程 Listener 回调。** 提供方传 RemoteResponse / PendingIntent，宿主安装监听器，点击后经 InteractionHandler / IntentSender 交给系统。
6. **通知与集合具有自己的分支。** SystemUI 当前使用 `NotificationRowContentBinderImpl.kt`，自行恢复 Builder 并选择 applyAsync / reapplyAsync；NMS 可以剥离自定义内容。集合有旧 RemoteViewsService 按项 RPC 和 RemoteCollectionItems 本地适配两条路，受 conversion flag 影响。

主线入口：

- [RemoteViews.java](../../../base/core/java/android/widget/RemoteViews.java)：描述、Action、Parcel、资源上下文、apply/reapply/async、点击与替代协议。
- [AppWidgetManager.java](../../../base/core/java/android/appwidget/AppWidgetManager.java)：提交、局部更新和 legacy-list 转换。
- [AppWidgetServiceImpl.java](../../../base/services/appwidget/java/com/android/server/appwidget/AppWidgetServiceImpl.java)：缓存、合并、预算、回调与 deferred 分支。
- [AppWidgetHost.java](../../../base/core/java/android/appwidget/AppWidgetHost.java) / [AppWidgetHostView.java](../../../base/core/java/android/appwidget/AppWidgetHostView.java)：Handler、复用、新建、取消、错误内容与接入宿主。
- [NotificationRowContentBinderImpl.kt](../../../base/packages/SystemUI/src/com/android/systemui/statusbar/notification/row/NotificationRowContentBinderImpl.kt)：当前通知内容执行器。

## 完成边界

`updateAppWidget()` 返回、服务受理、宿主 Binder 收到、Host Handler 执行、Action 应用完成、内容接入宿主、真实显示是不同观察点。普通同步 Binder 路径返回也不保证内容成功应用；legacy-list 转换路径甚至可能在 IPC 前返回。API 返回与 Host 回调可以并行交错，模型不人为排序返回时刻。

AppWidgetHost 回调入口会投递 Handler，但 `startListening()` 的 pending updates、`createView()/setListener()` 的缓存也存在直接调用线程分发入口。服务的 update sequence 不是 apply 成功 ACK，离线缓存也不保证逐次历史更新重放。

新树 `apply` 不自动 attach；AppWidgetHostView 的 `applyContent` 负责安装内容，而 Launcher 集成层另行负责把 HostView 放入工作区。异步 `onViewApplied → 宿主监听器 → applyContent` 是回调栈内的动作，模型分开显示这些观察点，不表示又排队了一次。新增场景标明 traversal / RenderThread / SurfaceFlinger 的通用交付，但不模拟内部调度或显示时间。

## 产物结构

- `data/launcher-stages.json`：7 个 Launcher 接入 / 绑定 / 广播 / 容器 / 绘制主题与源码证据，编辑源。
- `data/stages.json`：13 个 RemoteViews 机制主题与源码证据，保留通知 / 集合等附录。
- `src/model.js`：8 个场景与纯快照模型，共用一个 playhead；完整 Launcher 场景有 4 个进程角色，区分 Binder 与主线程；只模拟本例 setter 合并，不假装实现全部 Action。
- `src/app.js` / `src/style.css` / `src/shell.html`：视图、控件、响应式排版与页面结构。
- `build.py`：先核验摘录，再内嵌数据与源码模块，生成 `index.html`。
- `data/source-verification.json`：excerpt-only 机器结果，不能替代机制审查。
- `data/source-identity.json`：本地源码身份与文件哈希。
- `data/mechanism-review.md`：独立机制复核、错误树模型修正与证据边界。
- `tests/model.test.cjs`：因果顺序、树归属、复用、局部更新、异步观察点、失败、点击与生成一致性。
- `tests/browser.cjs` / `data/browser-verification.json`：隔离浏览器状态一致性、场景、源码主题、控件、锚点、文字边界和截图记录。

只编辑数据和生成输入，不手工修补生成页。没有修改 AOSP 实现、没有引入项目依赖。

## 重建与验证

在仓库根目录执行：

```bash
python3 -B docs/Model/remoteviews/build.py
node --test docs/Model/remoteviews/tests/model.test.cjs
node --check docs/Model/remoteviews/src/model.js
node --check docs/Model/remoteviews/src/app.js
git diff --check
```

构建使用已安装 skill 的标准库摘录校验器。其他安装位置可用 `SOURCE_PIPELINE_VERIFIER=/absolute/path/verify_sources.py` 指定；不隐式下载任何内容。

隔离浏览器验证复用宿主现有 Playwright 与 Chrome，不要求在项目安装 npm 依赖：

```bash
PLAYWRIGHT_MODULE=/absolute/path/to/playwright-core \
CHROME_PATH='/Applications/Google Chrome.app/Contents/MacOS/Google Chrome' \
node docs/Model/remoteviews/tests/browser.cjs
```

浏览器记录覆盖 1440、1920、390 和 320 像素宽，截图输出系统临时目录 `remoteviews-qa/`。浏览器报告只证明本地 HTML 的运行与排版，不能证明 Android 运行行为。没有执行 AOSP 构建、设备测试、性能测量、厂商验证或 feature flag 实测。
