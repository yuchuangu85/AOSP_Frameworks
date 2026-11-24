(function (root, factory) {
  const api = factory();
  if (typeof module === 'object' && module.exports) module.exports = api;
  else root.RemoteWidgetModel = api;
})(typeof globalThis === 'object' ? globalThis : this, function () {
  'use strict';
  const scenarios = [
    { id: 'first', label: '首次添加小组件', existing: false, description: '已授权绑定 id=37；Provider 收到更新请求，构建描述，宿主首次创建内容 View。' },
    { id: 'reapply', label: '同布局更新 / 复用 View', existing: true, description: '已有小组件。选择颜色映射未变、canRecycleView 成立的分支，复用宿主的原 View 树。' },
    { id: 'reinflate', label: '布局变化 / 创建新 View', existing: true, description: '全量更新换了 layoutId，旧树不符合复用条件；宿主创建新树并替换内容。' },
    { id: 'async', label: '设置 Executor / 异步加载', existing: false, description: '明确选择宿主设置 Executor 的分支；后台 inflate / 准备，context 主 Looper 完成 Actions 与回调。' },
    { id: 'partial', label: '局部更新 / 合并 Actions', existing: true, description: '已有全量缓存。只提交 count 的更新；服务端按 Action 合并规则得到完整描述，宿主再复用 View。' },
    { id: 'offline', label: '宿主未监听 / 重连补发', existing: true, description: 'Host 不在线监听时仍可更新服务端缓存；重连通过 startListening 返回待更新快照，而不是实时回调。' },
    { id: 'error', label: '应用失败 / 错误占位', existing: false, description: 'Binder 与缓存均成功，但宿主 apply 失败。本例选择首次 apply 失败，挂入 error view，不把它当目标内容成功。' },
    { id: 'fallback', label: 'reapply 失败 / 重新 apply', existing: true, description: '复用资格不保证执行一定成功；选择 reapply 抛异常、随后创建新树并 apply 成功的分支。' },
  ];
  const clamp = (value, min, max, fallback) => Number.isFinite(value) ? Math.max(min, Math.min(max, value)) : fallback;

  function build(options = {}) {
    const scenario = scenarios.find(item => item.id === options.scenario) || scenarios[0];
    const background = clamp(options.background, 600, 4000, 2000);
    const renderDelay = clamp(options.renderDelay, 400, 2600, 1400);
    const async = scenario.id === 'async';
    const partial = scenario.id === 'partial';
    const offline = scenario.id === 'offline';
    const error = scenario.id === 'error';
    const fallback = scenario.id === 'fallback';
    const reuse = ['reapply', 'partial', 'offline'].includes(scenario.id);
    const old = scenario.existing ? { revision: 0, title: '今天的计划', count: 1, layout: 'widget_tasks', tree: 'T0' } : null;
    const target = { revision: 1, title: scenario.id === 'reinflate' ? '一周计划' : '今天的计划', count: 3,
      layout: scenario.id === 'reinflate' ? 'widget_tasks_wide' : 'widget_tasks', tree: reuse ? 'T0' : 'T1' };
    const times = { create: 1200, actions: 1700, submit: 2300, cache: 2900,
      notify: offline ? null : 3500, callback: offline ? null : 4100,
      reconnect: offline ? 6000 : null, replay: offline ? 6200 : null,
      host: offline ? 6500 : 4500 };
    times.select = times.host + 400;
    times.prepare = times.select + 400;
    times.tree = reuse ? null : times.prepare + (async ? background : fallback ? 900 : 400);
    times.apply = reuse ? times.prepare + 600 : times.tree + (async ? 250 : 600);
    times.commit = times.apply;
    times.traversal = times.commit + renderDelay * .25;
    times.draw = times.commit + renderDelay * .6;
    times.screen = times.commit + renderDelay;
    const events = [];
    const add = (id, time, actor, title, description, ref, kind = 'info') => events.push({ id, time, actor, title, description, ref, kind });
    add('context', 0, 'host', scenario.existing ? '前置：已绑定的小组件和旧内容存在' : '前置：绑定已获授权，宿主容器已准备',
      '示意 appWidgetId=37 对应 Provider 与 Host。授权绑定、创建容器和监听是前置设置，不把 RemoteViews 当成“桌面摆放命令”；本仓库无 Launcher3 私有 Workspace 源码。', 'binding');
    add('trigger', 700, 'provider', scenario.id === 'first' ? '更新广播到 Provider.onUpdate' : '应用主动发起一次小组件更新',
      scenario.id === 'first' ? '选择正常绑定后请求 Provider 更新的路径。AppWidgetProvider.onReceive 分派 ACTION_APPWIDGET_UPDATE 到 onUpdate；业务代码负责构建并提交 RemoteViews。' : 'AppWidgetManager.updateAppWidget 不只可在 onUpdate 中调用；本例是应用数据变化后主动提交。局部更新场景使用 partiallyUpdateAppWidget。', 'trigger');
    add('create', times.create, 'provider', '构造 RemoteViews，不创建跨进程 View',
      'RemoteViews(packageName, layoutId) 保存应用资源身份与布局描述。这里没有创建将搬进 Launcher 的 TextView 实例；最终本地 View 属于宿主进程。', 'descriptor');
    add('actions', times.actions, 'provider', partial ? '只记录 count 的一条更新 Action' : '把文本与点击操作记录为 Actions',
      partial ? '本例已有一次全量缓存，只给 count 写入 setText 操作。合并如何处理冲突取决于 Action 的 mergeBehavior，不把所有局部更新都简单当字典覆盖。' : 'setTextViewText 等调用追加可序列化 Action；setOnClickPendingIntent 写入点击响应描述，不把应用的任意 View 或 Java lambda 直接传给宿主。', 'descriptor');
    add('submit', times.submit, 'provider', '以 appWidgetId 提交，经 Binder 传描述',
      'AppWidgetManager 经 IAppWidgetService 进入 system_server。Parcel 传资源元数据、Actions 及支持的数据/句柄，不是把应用进程中已 inflate 的 View 树序列化过去。', 'parcel');
    add('cache', times.cache, 'service', partial ? '服务端将一条 patch 合并进已有缓存' : '校验归属与资源访问，替换全量缓存',
      partial ? '选择 widget.views 已存在的分支，mergeRemoteViews 合并 Actions；本例保留标题和点击响应，替换 count 的 setText。缓存结果与宿主复用 View 是两个不同决定。' : 'AppWidgetServiceImpl 查找调用者可更新的 Widget，更新 widget.views 并做内存限制检查。system_server 缓存的是描述，不在这里 inflate Launcher 的 View。', 'service');
    if (offline) {
      add('deferred', 3500, 'service', 'Host 无 callbacks：缓存保留，本轮不实时回调', '选择 callbacks=null 的宿主未监听分支。更新序号仍被记录；画面与旧树不因服务端缓存改变而自动更新。不是承诺系统重启后保存缓存，也不保证回放每个中间版本。', 'service', 'wait');
      add('reconnect', times.reconnect, 'host', 'Host.startListening 重新注册监听', '本例保留了 id=37 的 listener。Host 调用服务端 startListening，获取待更新列表；不把离线期间画成仍在接收 oneway 回调。', 'reconnect');
      add('replay', times.replay, 'service', '通过返回列表补发最新待更新快照', 'AppWidgetHost 遍历 PendingHostUpdate 后调用 updateAppWidgetView。本例选择在宿主 UI 线程调用 startListening；这个补发入口不是 Callbacks.updateAppWidget 的 Binder 线程消息。', 'reconnect');
    } else {
      add('notify', times.notify, 'service', 'CallbackHandler 排队宿主通知', '服务端为普通 XML 更新准备描述副本和更新序号，交给 CallbackHandler；本例排除 legacy list 的 deferred-update 分支。API 接受描述不代表宿主已构造内容。', 'callback');
      add('callback', times.callback, 'host', 'oneway IAppWidgetHost 进入宿主回调', '宿主 Binder 回调把 (id=37, RemoteViews) 转成 Handler 消息。跨进程递送成功也不等于宿主主线程已处理、更不等于画面更新。', 'callback');
    }
    add('host', times.host, 'host', '按 id 找到 AppWidgetHostView，交给 UI 路径',
      'AppWidgetHost.updateAppWidgetView 将描述交给对应 listener。本例是 AppWidgetHostView，再从 updateAppWidget 进入 applyRemoteViews。普通回调路径选择绑定宿主主 Looper 的 Handler。', 'host');
    add('select', times.select, 'host', reuse || fallback ? '检查是否可复用现有本地树' : '选择本次布局，准备创建本地内容',
      reuse || fallback ? '选择颜色映射未改变、布局 tag 和 override id 匹配的普通 XML 分支；canRecycleView 成立时尝试 reapply。它不是 RemoteViews 对象相等检查，也不由服务端 full/partial 决定。' : '宿主按尺寸/配置选择 RemoteViews。本例只有一个普通 XML 布局；首次没有旧内容树，换 layoutId 时旧树也不符合复用条件。', 'host-apply');
    add('prepare', times.prepare, async ? 'executor' : 'host', async ? 'Executor 后台 inflate 与 Action 准备' : reuse || fallback ? '尝试在原 View 树上 reapply' : '宿主 inflate 提供方资源中的 XML',
      async ? '这是显式设置宿主 Executor 后的分支，不是所有 Launcher 的默认保证。AsyncApplyTask 在后台创建/准备树和 initActionAsync；最终 Action 与 listener 会回到 context 的主 Looper。' : reuse || fallback ? 'reapply 对宿主拥有的现有对象回放动作，不重新创建整个 widget。复用资格只是进入条件，执行仍可能抛异常。' : '在宿主进程使用提供方资源 Context 和受限 LayoutInflater 创建本地树；支持的 View 类与可调用方法受限制，不是任意自定义 View 都能跨进程运行。', async ? 'async' : reuse || fallback ? 'host-apply' : 'inflate');
    if (fallback) add('fallback', times.prepare + 200, 'host', 'reapply 抛异常，退回 fresh apply', '本例明确选择旧树执行 reapply 失败、重新 inflate 后成功的分支。AppWidgetHostView 捕获异常后尝试 apply；不是所有异常都能被重新加载修好。', 'host-apply', 'wait');
    if (!reuse) add('tree', times.tree, async ? 'executor' : 'host', error ? '布局/动作应用失败，目标树未完成' : '本地 View 树已准备，但目标内容尚未应用完成',
      error ? '本例选择非法布局或不被允许的组件导致 apply 失败；服务端之前接收并缓存描述并不能证明宿主能成功构建它。' : '宿主本地树已创建。资源来自提供方，View 对象由宿主持有；异步路径此时尚未在主 Looper 回放最终动作并执行成功回调。', error ? 'host-apply' : async ? 'async' : 'inflate', error ? 'danger' : 'info');
    add('commit', times.commit, 'host', error ? '目标 apply 失败，挂入 error view' : async ? '主 Looper：Actions → onViewApplied → applyContent' : reuse ? '动作已应用，保留原树作为内容' : 'Actions 回放完成，挂入新内容树',
      error ? 'AppWidgetHostView.applyContent 收不到目标 content 时取 getErrorView 并挂入。它是错误占位，不是 Provider 的目标内容；后面只演示该占位进入绘制。' : async ? '同一 onPostExecute 阶段内，先 onViewInflated，再依序 a.apply，成功后 onViewApplied；Host listener 调用 applyContent。此处不插入虚构的跨帧回调间隔。回调成功仍不是物理显示完成。' : reuse ? 'reapply 成功后 content 仍指向原 View；applyContent(recycled=true) 不 addView 新树。服务端全量缓存更新与宿主复用可以同时发生。' : 'RemoteViews.apply 完成 inflate 和 performApply，Host 再 prepareView / addView，必要时移除旧内容。动作执行和 View 都发生在宿主侧，不在提供方 app 的 View 树里。', async ? 'async' : 'host-apply', error ? 'danger' : 'ok');
    add('traversal', times.traversal, 'render', '宿主窗口参与常规 traversal', '内容变化接入已有窗口的 ViewRootImpl；按需测量、布局和 draw。不是每一次 reapply 都必然重新创建 Surface 或完整重做 measure/layout。此处时刻是教学假设。', 'render');
    add('draw', times.draw, 'render', 'Launcher 窗口绘制与显示链继续推进', '示意窗口走硬件加速绘制及后续合成/显示链。RemoteViews 没有独立的“widget 显示进程”，也没有 onViewApplied 到物理屏幕固定耗时的保证。', 'render');
    add('screen', times.screen, 'render', error ? '示意画面显示错误占位，不是目标内容' : '示意屏幕更新为新的 Widget 内容', '本页在这个假设时刻更新右侧画面，用来区分宿主应用完成和显示结果；未采集真机 present fence、FrameTimeline 或物理扫描证据。', 'render', error ? 'danger' : 'ok');
    events.sort((a, b) => a.time - b.time);
    return { scenario, background, renderDelay, async, partial, offline, error, fallback, reuse, old, target, times, events,
      duration: Math.max(10000, times.screen + 800) };
  }

  function at(model, value) {
    const t = clamp(value, 0, model.duration, 0);
    const times = model.times;
    const created = t >= times.create;
    const actionsReady = t >= times.actions;
    const submitted = t >= times.submit;
    const cached = t >= times.cache;
    const callbackReceived = times.callback !== null && t >= times.callback;
    const hostReceived = t >= times.host;
    const prepared = times.tree !== null && t >= times.tree && !model.error;
    const committed = t >= times.commit;
    const applied = committed && !model.error;
    const screenUpdated = t >= times.screen;
    const cache = cached ? model.target : model.old;
    const localContent = committed ? model.error ? { tree: 'ERROR', title: '小组件暂时无法加载', count: null, revision: 'error' } : model.target : model.old;
    const visible = screenUpdated ? localContent : model.old;
    let tree = model.old && model.old.tree;
    if (prepared) tree = model.target.tree;
    if (committed) tree = localContent.tree;
    const activeIndex = Math.max(0, model.events.findLastIndex(event => event.time <= t));
    const callbackQueued = times.notify !== null && t >= times.notify;
    const hostListening = !model.offline || t >= times.reconnect;
    return { t, created, actionsReady, submitted, cached, callbackQueued, callbackReceived, hostReceived, prepared, committed, applied,
      screenUpdated, hostListening, cache, localContent, visible, tree,
      providerActionCount: actionsReady ? model.partial ? 1 : 3 : 0,
      cacheActionCount: cache ? 3 : 0,
      newTreeCreated: prepared && !model.reuse,
      serviceHasView: false,
      treeOwner: tree ? 'host' : null,
      listenerApplied: model.async && applied,
      traversed: t >= times.traversal,
      drawn: t >= times.draw,
      status: screenUpdated ? model.error ? '错误占位已显示（示意）' : '目标内容已显示（示意）' : committed ? model.error ? '错误占位已挂入' : 'View 已应用，等待示意绘制' : hostReceived ? '宿主处理中' : cached ? hostListening ? '描述已缓存，等待宿主' : '缓存已更新，宿主未监听' : '构建并提交描述',
      activeIndex, active: model.events[activeIndex],
    };
  }
  return { scenarios, build, at };
});
