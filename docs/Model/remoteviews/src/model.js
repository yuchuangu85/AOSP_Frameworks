(function (root) {
  'use strict';
  const text = value => ({ viewId: 'title', method: 'setText', type: 'CharSequence', value });
  const tint = { viewId: 'title', method: 'setTextColor', type: 'int', value: 'green' };
  const click = { viewId: 'open', method: 'SetOnClickResponse', type: 'PendingIntent', value: 'token:open' };
  const oldActions = [text('上一条内容'), tint, click];
  const newActions = [text('来自另一个进程'), click];
  const actors = [
    { id: 'provider', name: 'Widget 提供方进程', detail: 'Receiver / 数据 / RemoteViews' },
    { id: 'system', name: 'system_server', detail: '缓存、检查、IPC 分发' },
    { id: 'host', name: '宿主进程（Launcher）', detail: 'HostView / View 树 / 绘制' },
    { id: 'compositor', name: 'SurfaceFlinger 进程', detail: 'Launcher 窗口的后续合成' }
  ];
  const event = (id, stage, actor, title, detail, effect, execution = {}) => ({
    id, stage, actor, title, detail, effect,
    thread: execution.thread || (actor === 'host' ? 'Launcher 主线程（本例）' : actor === 'system' ? 'system_server Binder 线程' : '提供方调用线程'),
    handoff: execution.handoff || '进程内调用；跨进程入口见源码调用链'
  });
  const prefix = [
    event('record', 'record', 'provider', '记录界面描述', '应用保存 layoutId 和 Action；尚未创建宿主的 View。', 'record'),
    event('parcel', 'parcel', 'provider', '提交可序列化数据', 'AppWidgetManager 发起更新。传递的是描述快照，不是 View 引用。', 'parcel'),
    event('accepted', 'service', 'system', '更新服务端缓存', '校验通过，服务端保存描述；这是受理，不是宿主应用完成。', 'accept'),
    event('dispatch', 'host', 'system', '发送宿主回调', '服务端 Binder 路径先排队，CallbackHandler 随后发起 oneway IAppWidgetHost 回调。', 'dispatch', { thread: 'CallbackHandler / 专用 ServiceThread', handoff: 'oneway Binder → Launcher Binder 线程' }),
    event('received', 'host', 'host', '宿主主线程取出消息', 'Callbacks 先投递 HANDLE_UPDATE，宿主 Handler 随后处理。', 'receive')
  ];
  const attach = event('attached', 'reuse', 'host', '宿主接入内容树', 'AppWidgetHostView.applyContent 安装内容。真实屏幕显示尚未观测。', 'attach');
  const scenarios = [
    { id: 'remoteviews', name: 'RemoteViews · 描述到宿主 View', policy: '只看 RemoteViews 核心协议：提供方构造描述，Parcel/Binder 携带 Action，宿主 inflate 后执行；不模拟 Launcher 绑定生命周期', initial: 'empty',
      actorIds: ['provider', 'system', 'host'], contentLayoutId: 'widget_status', events: [
        event('rv-construct', 'record', 'provider', '创建 RemoteViews 描述对象', '应用保存 applicationInfo 与 layoutId；此刻没有创建要显示在宿主中的 View。', 'rv-construct', { thread: 'Provider 应用代码所在进程 / 调用线程', handoff: '进程内构造；对象仍只在提供方进程' }),
        event('rv-setter', 'record', 'provider', 'setter 追加 ReflectionAction', 'setTextViewText(title, text) 被翻译成 viewId=title、method=setText、参数类型和值；不是直接调用 TextView。', 'rv-setter', { thread: 'Provider 应用代码所在进程 / 调用线程', handoff: 'Action 写入 RemoteViews 对象；没有跨进程 View 引用' }),
        event('rv-click', 'click', 'provider', '记录 PendingIntent 响应', '点击能力保存为 RemoteResponse / PendingIntent 令牌，不序列化应用的 OnClickListener 闭包。', 'rv-click', { thread: 'Provider 应用代码所在进程 / 调用线程', handoff: '可序列化能力令牌随 RemoteViews 传递' }),
        event('rv-parcel', 'parcel', 'provider', 'writeToParcel 写出描述', 'Parcel 写入 applicationInfo、layoutId、Action tag 和 Action 数据；传递的是协议字段，不是 View 树。', 'rv-parcel', { thread: 'Provider Binder 调用方线程', handoff: 'RemoteViews.writeToParcel → Binder 参数' }),
        event('rv-binder', 'parcel', 'system', 'Binder 跨进程携带 Parcel', 'system_server 收到 updateAppWidget 的描述快照。它保存和转发 RemoteViews，不 inflate 提供方布局。', 'rv-binder', { thread: 'system_server Binder 线程', handoff: 'Provider 进程 → system_server；跨进程边界' }),
        event('rv-unparcel', 'parcel', 'system', '按 Action tag 重建对象', '接收方通过 RemoteViews(Parcel) / getActionFromParcel 恢复 ReflectionAction、点击 Action 等框架对象。', 'rv-unparcel', { thread: '接收方处理 RemoteViews 的线程', handoff: 'Parcel 数据 → 框架 Action；仍没有真实 View' }),
        event('rv-host', 'host', 'host', '宿主取得 RemoteViews 描述', 'Launcher 或其他 Host 收到描述；RemoteViews 对象进入宿主进程，提供方进程不拥有这棵 View 树。', 'rv-host', { thread: 'Host Binder 线程 → Host Handler / UI 线程', handoff: 'IAppWidgetHost → Handler；具体 Host 入口决定线程' }),
        event('rv-inflate', 'inflate', 'host', '宿主用 layoutId inflate XML', 'LayoutInflater 在宿主进程使用提供方资源 Context 创建 XML View 树；@RemoteView 过滤器限制可加载的类。', 'rv-inflate', { thread: 'Host UI 线程或其异步 apply Executor 的准备阶段', handoff: 'layoutId → 宿主 View 对象；inflate(parent, false)' }),
        event('rv-resolve', 'actions', 'host', 'Action 通过 viewId 找到目标节点', '宿主执行 root.findViewById(title)，得到真实 TextView。提供方进程没有这个对象。', 'rv-resolve', { thread: 'Host UI 线程', handoff: 'RemoteViews Action → 宿主 View 树节点' }),
        event('rv-authorize', 'actions', 'host', '检查 @RemotableViewMethod', 'getMethod 验证 setText 是否带 RemoteViews 许可注解，并解析同步 / 异步实现；任意 public 方法不能直接调用。', 'rv-authorize', { thread: 'Host UI 线程或 Action 准备阶段', handoff: '方法许可检查 → MethodHandle' }),
        event('rv-apply', 'actions', 'host', 'MethodHandle 调用 TextView.setText', 'Action.apply 在宿主 View 上执行，文字才真正写入 TextView。到这里仍只是 View 树状态，不是物理上屏。', 'rv-apply', { thread: 'Host UI 线程；异步 apply 最终回到 context 主 Looper', handoff: 'MethodHandle.invoke → TextView.setText' }),
        event('rv-bind-click', 'click', 'host', '宿主安装点击监听器', 'SetOnClickResponse.apply 在宿主目标 View 上安装监听器；点击后经 InteractionHandler / IntentSender 交给系统。', 'rv-bind-click', { thread: 'Host UI 线程', handoff: 'PendingIntent 令牌 → 宿主 OnClickListener' }),
        event('rv-attach', 'reuse', 'host', '内容树加入 HostView', 'HostView.addView(content) 接入宿主容器；Launcher 工作区放置和后续窗口绘制属于更外层步骤。', 'rv-attach', { thread: 'Host UI 线程', handoff: '内容根 View → AppWidgetHostView；上屏仍未观测' })
      ] },
    { id: 'launcher', name: 'Launcher · 完整接入与显示路径', policy: '第三方 Widget / 已授权 / 无配置 / 无集合；初始与业务布局 ID 不同，选择新建树 + 硬件绘制及一种允许的交错', initial: 'empty',
      initialLayoutId: 'widget_initial', contentLayoutId: 'widget_status',
      actorIds: ['provider', 'system', 'host', 'compositor'], events: [
        event('select-provider', 'launcher-declare', 'host', '选择 Widget Provider', 'Launcher 从 AppWidgetProviderInfo 列表选择 Receiver 组件。仅有声明不会自动出现在桌面。', 'select', { handoff: '同步 Binder 查询可用 Provider 信息' }),
        event('listen', 'launcher-host', 'host', '注册 Launcher Host 回调', 'AppWidgetHost.startListening 经同步 Binder 将 IAppWidgetHost 注册到服务端。恢复已有 Widget 时同时取得 pending updates。', 'listen', { handoff: 'Launcher → IAppWidgetService → system_server Binder 线程' }),
        event('allocate', 'launcher-bind', 'system', '为 Launcher 分配 appWidgetId', 'Launcher 调用 allocateAppWidgetId；服务端创建 Widget 记录并关联 Host。本例 ID=42 只是教学值。', 'allocate', { handoff: '同步 Binder 返回 appWidgetId；此时尚未绑定 Provider' }),
        event('bind', 'launcher-bind', 'system', '校验授权并绑定 Provider', 'bindAppWidgetIdIfAllowed 提交 ID、组件、用户与 options。服务端关联 widget.host 和 widget.provider，而不是绑定一个持续渲染的 Service。', 'bind', { handoff: '同步 Binder；权限不满足时返回 false，另走授权界面' }),
        event('broadcast', 'launcher-provider', 'system', '调度 Provider 启用和更新广播', '该 Provider 的首个实例触发 enable + update 逻辑；合并广播与分开广播均有条件分支。', 'broadcast', { handoff: '系统广播调度 → 提供方 IApplicationThread → Receiver' }),
        event('host-view', 'launcher-view', 'host', '在 Launcher 创建 HostView', 'createView 创建 AppWidgetHostView，按 ID 注册 listener 并同步拉取缓存。本交错中 Provider 尚未提交，先显示 initialLayout 等初始内容。', 'host-view', { handoff: '本地创建容器；getAppWidgetViews 为同步 Binder' }),
        event('workspace', 'launcher-workspace', 'host', '把 HostView 放进桌面容器', 'Launcher 将 HostView 放入自己的工作区。当前仓库无 Launcher3；此步是宿主集成责任，框架测试样例展示了相同 addView 范式。', 'workspace'),
        event('provider-callback', 'launcher-provider', 'provider', 'Receiver 主线程进入 onUpdate', 'ActivityThread 分发清单 Receiver；AppWidgetProvider.onReceive 解析 IDs 并调用 onUpdate。此处不是 Launcher 线程，也不是 system_server。', 'provider-callback', { thread: 'Provider Receiver 所在进程的主线程', handoff: '广播先经该进程 Binder，再由 ActivityThread 主线程处理' }),
        event('record', 'record', 'provider', '构建 RemoteViews 与 Action', 'onUpdate 中的应用代码创建布局描述并记录文字、图片或点击动作，不创建桌面里的真实 View。', 'record', { thread: '本例 onUpdate 主线程；数据准备也可由应用自行异步安排' }),
        event('parcel', 'parcel', 'provider', '提交 updateAppWidget', '普通非集合路径向 IAppWidgetService 发起同步 Binder。Launcher 不是由 Provider 直接调用。', 'parcel', { thread: '本例 Provider 主线程', handoff: 'Provider → system_server；载荷为 appWidgetId + RemoteViews' }),
        prefix[2], prefix[3],
        event('binder-callback', 'host', 'host', 'Launcher Binder 接收回调', 'AppWidgetHost.Callbacks 不在入口操作 UI，而是向指定 Handler 投递 HANDLE_UPDATE。', 'binder-callback', { thread: 'Launcher Binder 线程（跨进程调用）', handoff: 'Binder Stub → Launcher 主 Looper 消息队列' }),
        prefix[4],
        event('inflate', 'inflate', 'host', 'Launcher 加载资源并创建内容树', '本例初始布局 widget_initial 与业务布局 widget_status 的 ID 不同，不能复用，因此使用提供方资源创建新内容；新树未接入前初始内容仍可存在。', 'inflate'),
        event('apply', 'actions', 'host', '在 Launcher 执行 Action', '找到真实 TextView 后调用获准的方法。Provider 的业务线程不会绘制这棵桌面内容树。', 'apply'),
        attach,
        event('traversal', 'launcher-draw', 'host', 'Launcher 执行常规 View 绘制', 'HostView 是桌面子 View，经 Launcher 窗口的 traversal / measure / layout / draw；安装内容本身不是已经显示。', 'traversal', { handoff: '主线程调度并执行 traversal；硬件路径交给 RenderThread' }),
        event('render-submit', 'launcher-draw', 'host', 'Launcher 窗口提交绘制结果', '本例选择硬件绘制：RenderThread / HWUI 处理 Launcher 窗口，普通 XML Widget 不另开一个独立窗口提交。', 'render-submit', { thread: 'Launcher RenderThread / HWUI（所选硬件路径）', handoff: 'Launcher 窗口 Buffer → 显示合成链路；非每个 Widget 单独提交' }),
        event('composition', 'launcher-draw', 'compositor', 'SurfaceFlinger 接续窗口合成', 'SurfaceFlinger 处理 Launcher 窗口图层并衔接显示系统。这里是源码路径说明，没有观测 present fence 或物理像素。', 'composition', { thread: 'SurfaceFlinger 调度与合成路径；不建模其内部线程切换', handoff: 'SurfaceFlinger → HWC / 显示系统；物理显示结果仍未知' })
      ] },
    { id: 'new', name: '小组件 · 首次创建', policy: '全量描述 / 同步 apply / 新树', initial: 'empty', events: [...prefix,
      event('inflate', 'inflate', 'host', '加载资源并 inflate', '使用提供方资源、受限类过滤器创建尚未 attach 的宿主树。', 'inflate'),
      event('apply', 'actions', 'host', '执行动作并返回', 'findViewById → 许可方法 → setText；点击响应也在此安装。', 'apply'), attach] },
    { id: 'reuse', name: '小组件 · 全量更新与复用', policy: '全量替换描述 / reapply / 原树保留', initial: 'old', events: [...prefix,
      event('compatible', 'reuse', 'host', '判断旧树可复用', '本例布局 ID、root override ID 一致且颜色映射未变。', 'compatible'),
      event('reapply', 'actions', 'host', '在原树执行新动作', 'treeId 不变。新全量描述未设置文字颜色，旧树绿色仍可能保留。', 'reapply'), attach] },
    { id: 'partial', name: '小组件 · 局部更新合并', policy: '已有完整缓存 / setText 替换 / 其他动作保留', initial: 'old', partial: true, events: [
      prefix[0], prefix[1],
      event('merge', 'partial', 'system', '合并描述中的 Action', '同一 setText 被替换；setTextColor 与点击响应保留在服务缓存中。', 'merge'),
      prefix[3], prefix[4],
      event('compatible', 'reuse', 'host', '选择原树复用', '局部更新与是否复用不是同一策略；本例恰好满足复用条件。', 'compatible'),
      event('reapply', 'actions', 'host', '执行合并后的描述', '新文本和缓存中的颜色、点击动作一起应用到原树。', 'reapply'), attach] },
    { id: 'async', name: '小组件 · 异步 apply', policy: '已设置宿主 Executor / 后台准备 / 主线程完成', initial: 'empty', events: [...prefix,
      event('queued', 'async', 'host', '向 Executor 提交任务', '返回 CancellationSignal；排队不等于后台已经完成。', 'queue'),
      event('prepare', 'async', 'host', '后台 inflate 与准备', 'doInBackground 创建新树并 initActionAsync；树仍未接入宿主。', 'prepare', { thread: 'Launcher 配置的后台 Executor（本例）', handoff: 'Executor 完成准备 → context 主 Looper' }),
      event('inflated', 'async', 'host', '主线程 onViewInflated', 'onPostExecute 开始；最终 Action 尚未全部执行。', 'inflated'),
      event('apply', 'async', 'host', '主线程执行最终 Action', '按顺序执行准备后的 Action。', 'apply'),
      event('callback', 'async', 'host', '发出 onViewApplied', '动作成功完成，调用宿主监听器；不是显示完成信号。', 'callback'), attach] },
    { id: 'error', name: '小组件 · 非法方法与错误内容', policy: '新树 / 非许可方法 / ActionException', initial: 'empty', invalid: true, events: [...prefix,
      event('inflate', 'inflate', 'host', 'XML 创建成功', '布局类合法，但并不代表每个待执行方法都获准。', 'inflate'),
      event('error', 'actions', 'host', '方法许可检查失败', '示例请求 View.setId(int)，该 public 方法没有 @RemotableViewMethod，抛 ActionException。', 'error'),
      event('fallback', 'reuse', 'host', '宿主装入错误 View', '新 apply 失败，applyContent(null, …) 使用 getErrorView；不标记成功内容。', 'fallback')] },
    { id: 'click', name: '小组件 · 点击回传', policy: '普通组件 / 默认 InteractionHandler / PendingIntent', initial: 'empty', events: [...prefix,
      event('inflate', 'inflate', 'host', '创建宿主 View 树', '点击目标按钮也属于宿主，不属于提供方 Activity。', 'inflate'),
      event('apply', 'click', 'host', '绑定宿主点击监听器', 'SetOnClickResponse 安装监听器，保存 PendingIntent 响应。', 'apply'), attach,
      event('clicked', 'click', 'host', '用户点击宿主按钮', 'RemoteResponse 交给 InteractionHandler；尚未在应用进程运行闭包。', 'clicked'),
      event('intent', 'click', 'system', '系统接收 IntentSender 请求', '默认处理器调用 startIntentSender；目标组件显示不在本模型证据中。', 'intent')] }
  ];

  function actionKey(action) { return [action.viewId, action.method, action.type].join(':'); }

  // Only the demonstrated setter REPLACE policy, not a generic RemoteViews merger.
  function mergeSetters(previous, incoming) {
    const keys = new Set(incoming.map(actionKey));
    return [...previous.filter(action => !keys.has(actionKey(action))), ...incoming].map(action => ({ ...action }));
  }

  function tree(id, title, color, listener, kind = 'content') { return { id, title, color, listener, kind }; }

  function applyActions(view, actions) {
    const result = { ...view };
    for (const action of actions) {
      if (action.method === 'setText') result.title = action.value;
      if (action.method === 'setTextColor') result.color = action.value;
      if (action.method === 'SetOnClickResponse') result.listener = true;
    }
    return result;
  }

  function scenario(id) {
    const value = scenarios.find(item => item.id === id);
    if (!value) throw new Error(`Unknown scenario: ${id}`);
    return value;
  }

  function actorsFor(id) {
    const ids = scenario(id).actorIds || ['provider', 'system', 'host'];
    return actors.filter(actor => ids.includes(actor.id));
  }

  function snapshot(id, position) {
    const selected = scenario(id);
    const index = Math.max(0, Math.min(selected.events.length - 1, Math.floor(Number(position) || 0)));
    const old = selected.initial === 'old';
    const state = {
      index, scenario: selected, current: selected.events[index],
      payload: [], cache: old ? oldActions.map(action => ({ ...action })) : [],
      tree: old ? tree('host-tree-1', '上一条内容', 'green', true) : null,
      attached: old, accepted: false, received: false, applied: false,
      prepared: false, callback: false, error: false, fallback: false,
      submitted: false, intent: false, clicked: false, queued: false,
      listening: false, widgetId: null, bound: false, broadcast: false,
      hostViewCreated: false, workspaceAttached: false, initialContent: false,
      providerCallback: false, traversal: false, renderSubmitted: false, composition: false,
      constructed: false, actionRecorded: false, serialized: false, transferred: false,
      deserialized: false, inflated: false, resolved: false, methodAllowed: false,
      actionExecuted: false, listenerInstalled: false,
      screen: 'unobserved'
    };
    for (const step of selected.events.slice(0, index + 1)) {
      switch (step.effect) {
        case 'listen': state.listening = true; break;
        case 'allocate': state.widgetId = 42; break;
        case 'bind': state.bound = true; break;
        case 'broadcast': state.broadcast = true; break;
        case 'host-view': state.hostViewCreated = true; state.initialContent = true; break;
        case 'workspace': state.workspaceAttached = true; break;
        case 'provider-callback': state.providerCallback = true; break;
        case 'rv-construct': state.constructed = true; break;
        case 'rv-setter': state.constructed = true; state.actionRecorded = true; state.payload = [text('来自另一个进程')]; break;
        case 'rv-click': state.actionRecorded = true; state.payload = [text('来自另一个进程'), click]; break;
        case 'rv-parcel': state.serialized = true; state.submitted = true; break;
        case 'rv-binder': state.transferred = true; break;
        case 'rv-unparcel': state.deserialized = true; state.cache = state.payload.map(action => ({ ...action })); break;
        case 'rv-host': state.received = true; break;
        case 'rv-inflate': state.inflated = true; state.prepared = true; state.tree = tree('host-tree-rv', 'XML 默认文字', 'default', false); break;
        case 'rv-resolve': state.resolved = true; break;
        case 'rv-authorize': state.methodAllowed = true; break;
        case 'rv-apply': state.actionExecuted = true; state.applied = true; state.tree = applyActions(state.tree, state.cache); break;
        case 'rv-bind-click': state.listenerInstalled = true; state.tree = applyActions(state.tree, [click]); break;
        case 'rv-attach': state.attached = true; break;
        case 'record': state.payload = selected.invalid
          ? [{ viewId: 'title', method: 'setId', type: 'int', value: 1 }]
          : selected.partial ? [text('来自另一个进程')] : newActions.map(action => ({ ...action })); break;
        case 'parcel': state.submitted = true; break;
        case 'accept': state.cache = state.payload.map(action => ({ ...action })); state.accepted = true; break;
        case 'merge': state.cache = mergeSetters(state.cache, state.payload); state.accepted = true; break;
        case 'receive': state.received = true; break;
        case 'queue': state.queued = true; break;
        case 'inflate': case 'prepare':
          state.tree = tree('host-tree-2', 'XML 默认文字', 'default', false);
          state.prepared = true; break;
        case 'apply': case 'reapply':
          state.tree = applyActions(state.tree, state.cache); state.applied = true; break;
        case 'callback': state.callback = true; break;
        case 'attach': state.attached = true; state.initialContent = false; break;
        case 'traversal': state.traversal = true; break;
        case 'render-submit': state.renderSubmitted = true; break;
        case 'composition': state.composition = true; break;
        case 'error': state.error = true; break;
        case 'fallback':
          state.tree = tree('host-tree-error', '无法加载此小组件', 'default', false, 'error');
          state.fallback = true; state.attached = true; break;
        case 'clicked': state.clicked = true; break;
        case 'intent': state.intent = true; break;
      }
    }
    return state;
  }

  const api = { actors, actorsFor, scenarios, scenario, snapshot, mergeSetters, actionKey };
  root.RVModel = api;
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
})(typeof globalThis === 'undefined' ? this : globalThis);
