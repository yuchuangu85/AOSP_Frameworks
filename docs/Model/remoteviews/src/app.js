(function () {
  'use strict';
  const data = JSON.parse(document.getElementById('rv-data').textContent);
  const model = window.RVModel;
  const byId = id => document.getElementById(id);
  const create = (tag, className, text) => {
    const element = document.createElement(tag);
    if (className) element.className = className;
    if (text !== undefined) element.textContent = text;
    return element;
  };
  let scenarioId = 'remoteviews';
  let position = 0;
  let timer = null;

  function actionsList(actions) {
    const list = create('ul', 'actions-list');
    for (const action of actions) {
      const row = create('li');
      row.append(create('span', '', `#${action.viewId} · ${action.method}`));
      row.append(create('span', 'action-value', `${action.type} = ${action.value}`));
      list.append(row);
    }
    return list;
  }

  function widget(state) {
    if (state.fallback) {
      const failure = create('div', 'widget error');
      failure.append(create('span', 'widget-header', '宿主错误内容'), create('strong', 'widget-title', '无法加载此小组件'), create('span', 'small-note', 'ActionException → getErrorView()'));
      return failure;
    }
    if (!state.tree) return create('div', 'empty-tree', state.hostViewCreated
      ? 'Launcher HostView 已创建；保留 initialLayout 等初始内容'
      : '本次 RemoteViews 内容树尚未创建');
    const preview = create('div', 'widget');
    const header = create('div', 'widget-header');
    header.append(create('span', 'widget-glyph', '▤'), create('span', '', '示例小组件 · 宿主 View'));
    const bottom = create('div', 'widget-bottom');
    bottom.append(create('span', '', state.tree.listener ? 'PendingIntent 已绑定' : '点击响应尚未绑定'));
    const target = create('button', 'widget-action', '↗');
    target.type = 'button';
    target.title = '宿主点击目标';
    target.setAttribute('aria-label', '宿主点击目标');
    target.disabled = scenarioId !== 'click' || !state.tree.listener || !state.attached || state.intent;
    target.addEventListener('click', () => {
      pause();
      position = model.scenario('click').events.findIndex(step => step.id === 'clicked');
      render();
    });
    bottom.append(target);
    preview.append(header, create('div', `widget-title ${state.tree.color === 'green' ? 'green' : ''}`, state.tree.title), bottom);
    return preview;
  }

  function renderActors(state) {
    const container = byId('actors');
    container.replaceChildren();
    const actors = model.actorsFor(scenarioId);
    container.style.setProperty('--actor-count', actors.length);
    for (const actor of actors) {
      const panel = create('section', `actor ${state.current.actor === actor.id ? 'active' : ''}`);
      panel.dataset.actor = actor.id;
      panel.append(create('span', 'actor-id', actor.id.toUpperCase()), create('h3', '', actor.name), create('p', 'actor-kicker', actor.detail));
      if (actor.id === 'provider') {
        const identity = create('div', 'payload-meta', 'com.example.widget');
        identity.append(create('span', '', `layout: ${state.scenario.contentLayoutId || 'widget_status'} · 实际 View：无`));
        if (scenarioId === 'remoteviews') {
          panel.append(identity, create('p', 'wire-label', '对象：RemoteViews'), create('p', 'small-note', 'View 引用：∅ · 只有 layoutId + Action'), actionsList(state.payload), create('p', 'actor-status', state.serialized ? '已写入 Parcel；提供方仍不持有宿主 View' : state.actionRecorded ? 'setter 只追加 Action，没有调用 TextView' : '等待构造描述对象'));
        } else {
          panel.append(identity, actionsList(state.payload), create('p', 'actor-status', state.submitted ? '描述已提交；没有显示完成回执' : state.payload.length ? 'Action 仅记录在应用内' : state.providerCallback ? '已进入回调，等待应用生成内容' : '等待应用提交本次内容'));
        }
      } else if (actor.id === 'system') {
        if (scenarioId === 'remoteviews') {
          panel.append(create('p', 'wire-label', 'Parcel / Binder'), create('p', 'payload-meta', '字段：applicationInfo · layoutId · actionTag · action data'), create('p', 'payload-meta', state.transferred ? '跨进程：Provider → system_server → Host' : state.serialized ? '等待 Binder 传递' : '尚未得到 Parcel'), create('p', 'actor-status', state.deserialized ? 'Action 已恢复；system_server 不创建 View' : state.transferred ? '收到描述快照；等待接收端重建 Action' : '不持有 View 树'));
          if (state.cache.length) panel.append(actionsList(state.cache));
        } else {
          if (scenarioId === 'launcher') panel.append(create('p', 'relationship', `${state.widgetId === null ? 'ID 未分配' : `appWidgetId: ${state.widgetId}`} · ${state.bound ? '已绑定 Provider' : '未绑定'} · ${state.listening ? 'Host 已监听' : 'Host 未监听'}`));
          panel.append(create('div', 'payload-meta', state.accepted ? '缓存：本次有效描述' : state.cache.length ? '缓存：上一次完整描述' : '缓存：尚无描述'));
          if (state.cache.length) panel.append(actionsList(state.cache));
          else panel.append(create('p', 'small-note', '服务端保存描述，不持有这棵宿主 View 树。'));
          panel.append(create('p', 'actor-status', state.intent ? '已提交 IntentSender，目标启动结果未知' : state.received ? '宿主已开始处理；无 apply ACK' : state.accepted ? '已受理，继续排队通知宿主' : '等待本次更新'));
        }
      } else if (actor.id === 'host') {
        if (scenarioId === 'remoteviews') {
          panel.append(create('p', 'wire-label', '宿主真正拥有 View'), create('p', 'small-note', state.inflated ? 'XML 已在宿主进程 inflate' : 'View 树：∅'), create('p', 'mechanism-status', `${state.resolved ? '✓ findViewById(title)' : '· 等待 findViewById'}  ${state.methodAllowed ? '✓ @RemotableViewMethod' : '· 等待方法许可'}  ${state.actionExecuted ? '✓ MethodHandle.invoke' : '· 等待 Action.apply'}`));
        } else if (scenarioId === 'launcher') {
          panel.append(create('p', 'relationship', state.workspaceAttached ? 'HostView 已放入桌面工作区' : state.hostViewCreated ? 'HostView 尚未放入桌面工作区' : '尚未创建 HostView 容器'));
          panel.append(create('p', 'small-note', `初始: ${state.scenario.initialLayoutId} / 业务: ${state.scenario.contentLayoutId}（不同 ID）`));
        }
        panel.append(widget(state));
        panel.append(create('p', 'tree-id', state.fallback ? `${state.tree.id} · 错误内容已接入宿主` : state.tree ? `${state.tree.id} · ${state.attached ? '内容已接入宿主' : '尚未接入宿主'}` : 'treeId: ∅'));
        panel.append(create('p', 'actor-status', state.error ? '更新失败；不承诺动作回滚' : state.callback ? 'onViewApplied 已回调；上屏未观测' : state.applied ? '新动作已完成；上屏未观测' : state.prepared ? '树已准备；新动作尚未完成' : '本次新动作尚未执行'));
        if (state.initialContent && state.tree && !state.attached) panel.append(create('p', 'small-note', '上方是待接入的新树；HostView 的初始内容仍可存在。'));
      } else {
        panel.append(create('p', 'payload-meta', state.composition ? '流程已到窗口合成阶段' : state.renderSubmitted ? 'Launcher 窗口输出已提交' : '等待 Launcher 窗口输出'));
        const chain = create('ol', 'composition-chain');
        for (const label of ['Launcher 窗口 Buffer', '图层 latch / 合成', 'HWC / 显示系统']) chain.append(create('li', '', label));
        panel.append(chain, create('p', 'actor-status', '不执行 Widget 的 Java View 代码；物理显示始终未观测'));
      }
      container.append(panel);
    }
  }

  function renderCompletion(state) {
    const list = scenarioId === 'remoteviews' ? [
      { text: '描述已构造', done: state.constructed },
      { text: 'Action 已序列化', done: state.serialized },
      { text: '宿主已 inflate', done: state.inflated },
      { text: state.error ? 'Action 失败' : 'Action 已执行', done: state.actionExecuted, failed: state.error },
      { text: '真实上屏：未观测', done: false }
    ] : [
      { text: '服务已受理', done: state.accepted },
      { text: '宿主已处理消息', done: state.received },
      { text: state.error ? '本次应用失败' : '本次动作已完成', done: state.applied, failed: state.error },
      { text: '真实上屏：未观测', done: false }
    ];
    byId('completion').replaceChildren(...list.map(item => {
      const row = create('div', `completion-item ${item.failed ? 'failed' : item.done ? 'done' : ''}`);
      row.append(create('span', 'status-mark', item.failed ? '×' : item.done ? '✓' : '·'), create('b', '', item.text));
      return row;
    }));
  }

  function showTopic(id) {
    if (!data.stages.some(stage => stage.id === id)) return;
    for (const element of document.querySelectorAll('[data-topic]')) element.hidden = element.dataset.topic !== id;
    for (const anchor of byId('stage-nav').querySelectorAll('a')) {
      const active = anchor.dataset.stage === id;
      anchor.classList.toggle('active', active);
      if (active) anchor.setAttribute('aria-current', 'location');
      else anchor.removeAttribute('aria-current');
    }
  }

  function buildSources() {
    for (const [index, stage] of data.stages.entries()) {
      const anchor = create('a', '', `${String(index + 1).padStart(2, '0')}  ${stage.title}`);
      anchor.href = `#topic-${stage.id}`;
      anchor.dataset.stage = stage.id;
      anchor.addEventListener('click', () => showTopic(stage.id));
      byId('stage-nav').append(anchor);
      const article = create('section');
      article.id = `topic-${stage.id}`;
      article.dataset.topic = stage.id;
      article.append(create('h3', '', stage.title), create('p', 'stage-summary', stage.summary));
      const chain = create('div', 'callchain');
      stage.chain.forEach((call, i) => {
        if (i) chain.append(create('span', 'arrow', '→'));
        chain.append(create('span', '', call));
      });
      const facts = create('ol', 'facts');
      stage.facts.forEach(fact => facts.append(create('li', '', fact)));
      const boundary = create('p', 'boundary');
      boundary.append(create('strong', '', '范围与限制'), document.createTextNode(stage.boundary));
      article.append(chain, facts, boundary);
      for (const snippet of stage.snippets) {
        const figure = create('figure', 'snippet');
        const caption = create('figcaption');
        const source = create('a', '', snippet.file.split('/').pop());
        source.href = `../../../${snippet.file}`;
        source.target = '_blank';
        source.rel = 'noopener';
        source.title = snippet.file;
        caption.append(source, create('span', '', `L${snippet.lines} · 当前本地源码`));
        const pre = create('pre');
        pre.setAttribute('tabindex', '0');
        pre.setAttribute('aria-label', `${snippet.file} 源码摘录`);
        pre.append(create('code', '', snippet.code));
        figure.append(caption, pre, create('p', 'snippet-note', snippet.note));
        article.append(figure);
      }
      byId('stage-content').append(article);
    }
  }

  function pause() {
    if (timer !== null) window.clearInterval(timer);
    timer = null;
    byId('play').textContent = '▶';
    byId('play').title = '播放';
    byId('play').setAttribute('aria-label', '播放');
  }

  function render() {
    const state = model.snapshot(scenarioId, position);
    position = state.index;
    byId('policy').textContent = state.scenario.policy;
    byId('step-count').textContent = `${position + 1} / ${state.scenario.events.length}`;
    byId('seek').max = state.scenario.events.length - 1;
    byId('seek').value = position;
    byId('prev').disabled = position === 0;
    byId('next').disabled = position === state.scenario.events.length - 1;
    byId('event-actor').textContent = `${model.actors.find(actor => actor.id === state.current.actor).name} · ${state.current.id}`;
    byId('event-title').textContent = state.current.title;
    byId('event-description').textContent = state.current.detail;
    byId('execution-process').textContent = model.actors.find(actor => actor.id === state.current.actor).name;
    byId('execution-thread').textContent = state.current.thread;
    byId('execution-handoff').textContent = state.current.handoff;
    byId('event-source').href = `#topic-${state.current.stage}`;
    byId('events').replaceChildren(...state.scenario.events.map((step, index) => {
      const li = create('li');
      const button = create('button', index === position ? 'active' : '');
      button.type = 'button';
      button.dataset.step = index;
      if (index === position) button.setAttribute('aria-current', 'step');
      button.append(create('span', 'event-number', String(index + 1).padStart(2, '0')), create('span', 'event-name', step.title), create('span', 'event-owner', step.actor === 'provider' ? 'Provider' : step.actor === 'system' ? '系统' : step.actor === 'host' ? 'Launcher' : 'SF'));
      button.addEventListener('click', () => { pause(); position = index; render(); });
      li.append(button);
      return li;
    }));
    renderActors(state);
    renderCompletion(state);
    showTopic(state.current.stage);
    document.documentElement.dataset.qa = JSON.stringify({ scenario: scenarioId, index: position,
      event: state.current.id, stage: state.current.stage, applied: state.applied,
      callback: state.callback, error: state.error, fallback: state.fallback,
      tree: state.tree, attached: state.attached, screen: state.screen,
      widgetId: state.widgetId, bound: state.bound, hostViewCreated: state.hostViewCreated,
      workspaceAttached: state.workspaceAttached, initialContent: state.initialContent,
      listening: state.listening, renderSubmitted: state.renderSubmitted, composition: state.composition,
      constructed: state.constructed, actionRecorded: state.actionRecorded, serialized: state.serialized,
      transferred: state.transferred, deserialized: state.deserialized, inflated: state.inflated,
      resolved: state.resolved, methodAllowed: state.methodAllowed, actionExecuted: state.actionExecuted,
      listenerInstalled: state.listenerInstalled });
  }
  byId('scenario').value = scenarioId;

  for (const item of model.scenarios) {
    const option = create('option', '', item.name);
    option.value = item.id;
    byId('scenario').append(option);
  }
  byId('scenario').addEventListener('change', event => { pause(); scenarioId = event.target.value; position = 0; render(); });
  byId('seek').addEventListener('input', event => { pause(); position = Number(event.target.value); render(); });
  byId('reset').addEventListener('click', () => { pause(); position = 0; render(); });
  byId('prev').addEventListener('click', () => { pause(); position--; render(); });
  byId('next').addEventListener('click', () => { pause(); position++; render(); });
  byId('play').addEventListener('click', () => {
    if (timer !== null) return pause();
    if (position === model.scenario(scenarioId).events.length - 1) position = 0;
    byId('play').textContent = 'Ⅱ';
    byId('play').title = '暂停';
    byId('play').setAttribute('aria-label', '暂停');
    render();
    timer = window.setInterval(() => {
      position++;
      render();
      if (position === model.scenario(scenarioId).events.length - 1) pause();
    }, 1000);
  });
  byId('event-source').addEventListener('click', () => showTopic(model.snapshot(scenarioId, position).current.stage));
  byId('theme').addEventListener('change', event => { document.documentElement.dataset.theme = event.target.checked ? 'dark' : 'light'; });
  function route() {
    const id = window.location.hash.replace(/^#topic-/, '');
    if (window.location.hash.startsWith('#topic-')) showTopic(id);
  }
  window.addEventListener('hashchange', route);
  document.addEventListener('visibilitychange', () => { if (document.hidden) pause(); });
  window.addEventListener('pagehide', pause);
  byId('revision').textContent = `源码快照 ${data.identity.sourceCommit.slice(0, 9)}`;
  byId('verification').textContent = `${data.verification.passed}/${data.verification.total} 段摘录匹配 · excerpt-only`;
  byId('identity').textContent = `本地 checkout ${data.identity.checkoutCommit.slice(0, 9)}；base tree ${data.identity.baseTree.slice(0, 12)}，native tree ${data.identity.nativeTree.slice(0, 12)}。${data.identity.clean ? '所引源码无本地修改。' : '所引源码包含本地修改，见证据报告。'}两棵源码树均与 ${data.identity.sourceCommit.slice(0, 9)} 相同；未提供未经远端校验的永久链接。`;
  buildSources();
  render();
  route();
})();
