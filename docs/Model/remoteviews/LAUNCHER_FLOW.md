# AppWidget 显示到 Launcher：完整过程与进程

入口：[交互说明页](index.html)，默认选择“Launcher · 完整接入与显示路径”。

本文基于当前 checkout，所引 `base/` 与 `native/` 源码树和本地提交 `213ab0f8152c35a488fc526f7307c97bb33b18ae` 一致。当前仓库没有 Launcher3 的实现，所以不编造其 Workspace 类、数据库或具体调用位置。框架事实、宿主测试示例和 Launcher 的集成责任分别说明。

## 一、先明确进程与对象归属

- **Launcher 进程：**持有 `AppWidgetHost`、`AppWidgetHostView`、桌面容器与实际的 `TextView` / `ImageView`。负责布局、交互、窗口绘制。Launcher 包名和 PID 由产品决定，不固定等于 `com.android.launcher3`。
- **system_server 进程：**运行 `AppWidgetServiceImpl`。负责 Provider / Host / Widget 关系、权限、缓存描述、广播触发与回调分发，不在此创建 Widget 的 Java View 树。
- **Provider Receiver 所在进程：**执行 `AppWidgetProvider.onReceive/onUpdate` 等应用代码并构造 `RemoteViews`。通常是提供方应用主进程，但可以由 Receiver 的清单 `android:process` 改变。
- **SurfaceFlinger 进程：**处理包括 Launcher 窗口在内的图层与 Buffer 合成，衔接 HWC / 显示系统，不执行 `onUpdate()` 或 `TextView.setText()`。HWC 的具体实现与进程布局取决于设备。

“Widget Provider 进程”是职责名称，不是系统为每个 Widget 创建的专属进程。`AppWidgetHost` 是 Launcher 中的客户端对象，也不是独立进程。

普通 XML 路径的逻辑关系为：

```text
Launcher 进程                 system_server                    Provider 进程
    │                             │                               │
    ├─ 注册 Host / 申请 ID ───────►│                               │
    ├─ 绑定 Provider ────────────►│                               │
    │                             ├─ 调度更新广播 ───────────────►│
    │                             │                         onReceive / onUpdate
    │                             │                               │
    │                             │◄────── updateAppWidget ───────┤
    │                             │        RemoteViews 描述        │
    │◄────── IAppWidgetHost ───────┤                               │
 Binder 线程 → 主线程 Handler       │                               │
 inflate / reapply / Action        │                               │
    │                                                             │
 桌面 View 树 → Launcher 窗口绘制                                  │
    │
    └─ 窗口 Buffer ──► SurfaceFlinger 进程 ──► HWC / 显示系统
```

箭头表示交付关系，不表示固定时长。Provider 广播处理和 Launcher 创建容器可以并发交错。

## 二、Provider 声明，并被 Launcher 选择

**执行进程：提供方的安装信息由系统读取；选择界面属于 Launcher。**

Provider 在 Manifest 中声明 Receiver，监听 `android.appwidget.action.APPWIDGET_UPDATE`，并以 `android.appwidget.provider` 元数据指向 Widget 配置 XML。XML 描述 `initialLayout`、尺寸、配置 Activity、更新周期等信息。

`AppWidgetProvider` 继承 `BroadcastReceiver`，并非 `ContentProvider`。系统发现 Provider 后提供 `AppWidgetProviderInfo`，Launcher 可以查询并展示可添加项。仅声明或安装应用不会自动产生一个桌面实例。

证据：[AppWidgetProvider.java:42](../../../base/core/java/android/appwidget/AppWidgetProvider.java)、[测试 Provider 清单:8](../../../base/tests/appwidgets/AppWidgetProviderTest/AndroidManifest.xml)。

## 三、Launcher 建立 Host 并注册回调

**调用方：Launcher 主线程。服务端执行：system_server Binder 线程。**

Launcher 持有一个带稳定 `hostId` 的 `AppWidgetHost`。默认构造器将其 `UpdateHandler` 绑定到 `context.getMainLooper()`。

`startListening()` 通过同步 Binder 把以下信息交给 `AppWidgetServiceImpl`：宿主包名、hostId、`IAppWidgetHost` 回调端点、已监听的实例 ID。服务端查找或创建 Host，保存 callback；Host 身份还包含 UID，不是只凭 hostId 整数识别。

若是恢复已有 Widget，服务可返回 pending updates，`startListening()` 在当前调用栈直接分发。这不同于后续 Binder 回调先投 Handler 的路径。

证据：[AppWidgetHost.java:221、260](../../../base/core/java/android/appwidget/AppWidgetHost.java)、[AppWidgetServiceImpl.java:1399](../../../base/services/appwidget/java/com/android/server/appwidget/AppWidgetServiceImpl.java)。

## 四、申请实例 ID，并绑定 Provider

**调用方：Launcher。执行方：system_server Binder 线程，关系修改在服务锁内。**

`AppWidgetHost.allocateAppWidgetId()` 请求系统分配一个 `appWidgetId`。服务创建 `Widget` 记录并关联 Host，此时还没有 Provider，也没有桌面内容 View。

随后 Launcher 调用 `AppWidgetManager.bindAppWidgetIdIfAllowed(id, provider, options)`。服务检查调用包与 UID、跨用户权限、绑定权限 / allowlist、ID 所属、Provider 是否存在和是否有效，再建立：

```text
Widget.appWidgetId
    ├─ host → Launcher 对应的 Host
    └─ provider → 所选 Provider 组件
```

这里不是 `bindService()`。普通 XML Widget 不依靠“长期绑定一个远端绘制 Service”显示。

返回 `false` 时，Launcher 可发起 `ACTION_APPWIDGET_BIND` 请求用户授权，或处理其他绑定失败原因；取消要清理 ID。返回 `true` 只证明服务端绑定步骤已完成，不证明 Provider 已运行、配置完成或内容显示。

证据：[AppWidgetHost.java:323](../../../base/core/java/android/appwidget/AppWidgetHost.java)、[AppWidgetManager.java:1485](../../../base/core/java/android/appwidget/AppWidgetManager.java)、[AppWidgetServiceImpl.java:1478、1765](../../../base/services/appwidget/java/com/android/server/appwidget/AppWidgetServiceImpl.java)。

## 五、系统广播触发 Provider 回调

**广播发送与调度：system_server。回调执行：Receiver 所在进程的主线程。**

绑定成功后，该 Provider 当前首个 Widget 实例触发 enable + update 逻辑，后续实例只触发更新。当前源码允许发送两个广播，也允许在条件满足时合并为 `ACTION_APPWIDGET_ENABLE_AND_UPDATE`。

合并广播需要 `mIsCombinedBroadcastEnabled`、有效 Provider 信息和 `isExtendedFromAppWidgetProvider` 标记同时满足；不能只依据默认配置认定实际设备使用合并路径。

接收链为：系统广播调度 → Provider 的 `IApplicationThread.scheduleReceiver()` → 投递 `H.RECEIVER` → `ActivityThread.handleReceiver()` → `onReceive()`。因此，不是在 Provider 的 Binder 接收线程直接执行 `onUpdate()`。

基类 `AppWidgetProvider.onReceive()` 解析非空实例 ID 数组，再调用应用覆写的 `onUpdate()`。合并广播内部依次分发 enabled/update，不是再次向系统发送两个广播。基类 `onUpdate()` 为空，收到广播本身不会自动生成内容。

Provider 进程普通退出后，系统在允许投递广播时可以冷启动其 Receiver 进程；这不等于强停状态也一定投递，也不表示广播结束立即杀进程或永久保活。

证据：[AppWidgetServiceImpl.java:1854、3700](../../../base/services/appwidget/java/com/android/server/appwidget/AppWidgetServiceImpl.java)、[ActivityThread.java:1253、5384](../../../base/core/java/android/app/ActivityThread.java)、[AppWidgetProvider.java:57](../../../base/core/java/android/appwidget/AppWidgetProvider.java)。

## 六、配置 Activity 是条件分支，不是固定同步关卡

**由 Launcher 发起，目标配置组件在其清单指定的应用进程运行；可能另有系统代理 Activity。**

如果 Provider 有必需配置 Activity，服务会记录 `isFirstConfigActivityPending`；可选配置与必需配置不同。Launcher 负责启动配置并处理成功或取消。内容由应用代码提交，可以发生在 onUpdate、配置 Activity 或自行安排的任务中，并不要求一定由配置 Activity 直接发送初始 RemoteViews。

但本 checkout 的绑定代码在设置 pending 后仍提交初始广播，所以不能概括为“有配置 Activity 就不会先收到 onUpdate”。广播实际处理与配置启动可以交错。

当前 `remoteAdapterConversion` 还会影响 `AppWidgetHost.startAppWidgetConfigureActivityForResult()` 是否经 `AppWidgetConfigActivityProxy` 启动配置；代理返回后对 pending 实例的完成通知可能补发更新。不能把这一 Host API 的行为外推成所有 Launcher 自行启动配置的行为。

默认交互场景排除此分支，以免将产品配置策略伪装成框架统一时序。

证据：[AppWidgetServiceImpl.java:1840、1715、2254](../../../base/services/appwidget/java/com/android/server/appwidget/AppWidgetServiceImpl.java)、[AppWidgetHost.java:386](../../../base/core/java/android/appwidget/AppWidgetHost.java)、[AppWidgetConfigActivityProxy.java:65](../../../base/core/java/android/appwidget/AppWidgetConfigActivityProxy.java)。

## 七、应用生成 RemoteViews，并提交更新

**生成与调用：Provider 进程，线程由应用决定；本例在 onUpdate 主线程。接收：system_server Binder 线程。**

应用创建 `RemoteViews(packageName, layoutId)`，再调用 `setTextViewText` 等方法。setter 只是追加可序列化 Action，不会在应用进程中找到 Launcher 的 TextView。

`AppWidgetManager.updateAppWidget(id, views)` 携带实例 ID 和描述，通过 `IAppWidgetService.updateAppWidgetIds` 提交。普通非集合路径是同步 Binder；开启 legacy-list 转换并满足相关条件时可能先做异步准备，不应把两条路径混同。

服务检查调用权限及资源访问权限，查找 Widget，完整更新替换 `widget.views`；局部更新合并 Action。还会检查图像内存预算，再安排通知实际 Host。服务端缓存的是描述，不是 Java View 树。

这次调用是“提交内容”，不是“请求 Provider 更新”的广播，也不会自动再次回调自己的 `onUpdate()`。二者方向相反。

证据：[RemoteViews.java:7011、8138](../../../base/core/java/android/widget/RemoteViews.java)、[AppWidgetManager.java:758](../../../base/core/java/android/appwidget/AppWidgetManager.java)、[AppWidgetServiceImpl.java:2855、3134](../../../base/services/appwidget/java/com/android/server/appwidget/AppWidgetServiceImpl.java)。

## 八、系统回调到 Launcher，Binder 线程再转 UI 线程

**system_server Binder 线程 → 专用 CallbackHandler / ServiceThread → Launcher Binder 线程 → Launcher 主线程。**

服务更新缓存后向 CallbackHandler 投消息。专用 ServiceThread 调用 `IAppWidgetHost.updateAppWidget(id, views)`，接口为 `oneway`；这次 Binder 返回不是“Launcher 已应用”的 ACK。

Launcher 的 `AppWidgetHost.Callbacks.updateAppWidget()` 在 Binder 入口只封装 `HANDLE_UPDATE` 消息并投递 Handler。默认主 Looper 的 UpdateHandler 随后进入 `updateAppWidgetView()`，按 ID 查 listener，再调用目标 `AppWidgetHostView.updateAppWidget()`。

服务保存的 sequence 前进不代表 Launcher Handler 已执行，接口也没有向 Provider 返回端到端的显示完成确认。

证据：[AppWidgetServiceImpl.java:433、3267、3322](../../../base/services/appwidget/java/com/android/server/appwidget/AppWidgetServiceImpl.java)、[IAppWidgetHost.aidl:25](../../../base/core/java/com/android/internal/appwidget/IAppWidgetHost.aidl)、[AppWidgetHost.java:121、186、655](../../../base/core/java/android/appwidget/AppWidgetHost.java)。

## 九、Launcher 创建 HostView，且有两层挂接

**执行进程：Launcher；UI 树操作应在其主线程。**

第一层由 Launcher 集成代码负责：`mHost.createView(...)` 返回 HostView，再把 HostView 放入桌面工作区。createView 不自动知道工作区父容器。当前仓库测试宿主在 `mAppWidgetContainer.addView(view, layoutParams)` 完成放置；这只是框架测试示例，不是 Launcher3 具体源码。

createView 内部还会 `setListener()`，同步拉取当前缓存并直接执行初始更新。第二层由框架完成：`AppWidgetHostView.applyContent()` 将 RemoteViews 内容加入 HostView。

```text
Launcher 窗口的 ViewRootImpl / DecorView
    └─ Launcher 工作区容器                 具体实现不在当前仓库
         └─ AppWidgetHostView               Launcher 集成层放置
              └─ RemoteViews 内容根         框架 applyContent 接入
                   ├─ TextView
                   ├─ ImageView
                   └─ Button
```

这两层父子关系不可混淆。Provider 先提交、容器后创建时，setListener 可以取缓存；容器先创建、Provider 后提交时，可先显示 initialLayout，再由后续 callback 更新业务内容。初始内容若兼容可以直接 reapply，不必再次 addView。交互模型额外选择初始 widget_initial 与业务 widget_status 的布局 ID 不同、不可复用的新树路径，不主张所有 Launcher 都按相同顺序或每次都创建新树。

证据：[AppWidgetHost.java:499、627](../../../base/core/java/android/appwidget/AppWidgetHost.java)、[AppWidgetHostView.java:644、789](../../../base/core/java/android/appwidget/AppWidgetHostView.java)、[测试宿主 AppWidgetHostActivity.java:134](../../../base/tests/appwidgets/AppWidgetHostTest/src/com/android/tests/appwidgethost/AppWidgetHostActivity.java)。

## 十、Launcher 内 inflate、执行动作或复用旧树

**同步路径：Launcher 当前 UI 调用线程。异步路径：所选 Executor 准备，再由 context 主 Looper 收尾。**

HostView 选择尺寸 / 方向变体，然后尝试复用。布局和 root override ID 兼容、颜色映射未变化时，可用 `reapply()` 在旧树上执行 Action；否则 `apply()` 创建新内容。全量缓存更新并不强制重新创建 View。

XML inflation 使用提供方资源 Context，却仍在 Launcher 进程中创建对象；类过滤要求 `@RemoteView`。反射 Action 通过 viewId 找到真实节点，检查 `@RemotableViewMethod` 后调用允许的方法。使用提供方资源，不等于让 View 在提供方进程执行。

`inflate(..., parent, false)` 不自动挂接。新内容完成后才由 `applyContent()` 安装，替换旧内容。失败可安装错误 View，不能理解为 Launcher 中不存在任何内容树。

若 Host 设置异步 Executor，inflate / initActionAsync 可在该执行器准备，最终 Action 和完成回调在 context 主 Looper 执行。该 Executor 不是 HWUI 的 RenderThread。

证据：[AppWidgetHostView.java:581、669、719](../../../base/core/java/android/appwidget/AppWidgetHostView.java)、[RemoteViews.java:8572、8593、8736、8899](../../../base/core/java/android/widget/RemoteViews.java)。

## 十一、随 Launcher 窗口绘制，再进入显示合成

**Launcher 主线程 → 所选硬件路径的 Launcher RenderThread → SurfaceFlinger 进程 → 设备显示系统。**

View 的变化按需触发 requestLayout / invalidate，窗口根 ViewRootImpl 调度 traversal；不是每次更新都无条件完整重做全部布局。Widget 子树作为 Launcher 窗口树的一部分参与布局与绘制。

硬件加速路径中，主线程更新窗口 DisplayList，`ThreadedRenderer` 通过 DrawFrameTask 将工作交给 Launcher 进程内 RenderThread / HWUI，再输出窗口 Buffer。软件绘制分支也存在，所以 RenderThread 不是所有情况下的绝对必经步骤。

窗口 BLASTBufferQueue 将 Buffer 和 fence 放入 SurfaceControl transaction，随后 SurfaceFlinger 处理图层并衔接 HWC。普通 XML Widget 的 appWidgetId 不会自动创建独立 Window / Surface / 渲染进程。这个结论不排除 Launcher 还有其他 Surface，也不覆盖特殊自定义嵌入方案。

内容 addView、Action 完成、Buffer 提交或 `present()` 调用，都不能单独证明像素已经上屏。页面始终把真实显示标为“未观测”。

证据：[ViewGroup.java:5239](../../../base/core/java/android/view/ViewGroup.java)、[ViewRootImpl.java:3330、6320、6353](../../../base/core/java/android/view/ViewRootImpl.java)、[ThreadedRenderer.java:827](../../../base/core/java/android/view/ThreadedRenderer.java)、[DrawFrameTask.cpp:82](../../../base/libs/hwui/renderthread/DrawFrameTask.cpp)、[BLASTBufferQueue.cpp:667、726](../../../native/libs/gui/BLASTBufferQueue.cpp)、[SurfaceFlinger.cpp:3448](../../../native/services/surfaceflinger/SurfaceFlinger.cpp)。

## 十二、进程退出、后续更新与重连

- Provider 普通进程退出不要求 Launcher 删除已有 View；它已经是 Launcher 自己的对象。但 force-stop、卸载、mask、用户锁定等状态另有规则，不能保证任何情况下都保持相同内容。
- 仅 Launcher 重启且 system_server 缓存仍在时，Launcher 恢复原 appWidgetIds 后可经 createView / startListening 取得当前有效描述。具体桌面数据库不在本仓库中；不能宣称缓存会跨系统重启原样保留所有 RemoteViews。
- 服务端保存的是当前描述与更新序列，不是保证逐次重放的内容日志；离线期间的中间更新可能不会逐个显示。
- `updatePeriodMillis` 控制系统周期更新广播，不是主动 updateAppWidget 的统一频率限制；当前源码的最小周期还受 `Build.IS_DEBUGGABLE` 分支影响。
- 普通点击在 Launcher 处理，PendingIntent 经系统启动目标组件；应用 Listener 闭包不会跨进程传过来。

## 验证边界

源码摘录匹配结果见 `data/source-verification.json`，纯模型与隔离浏览器测试独立运行。`data/source-identity.json` 记录所引文件哈希，`data/mechanism-review.md` 记录机制审查。

本次没有修改 AOSP 实现，也没有运行 Launcher3、构建 Android、采集设备 trace、验证产品 feature flag 或测量物理显示。交互场景是有源码依据的因果教学模型，不是系统调度器或真实显示模拟器。
