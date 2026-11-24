(function () {
  'use strict';
  const { scenarios, build, at } = RemoteWidgetModel;
  const $ = id => document.getElementById(id);
  const data = JSON.parse($('sourceData').textContent);
  const options = { scenario: 'first' };
  const seconds = value => `${(value / 1000).toFixed(2)} s`;
  const setText = (id, value) => { $(id).textContent = value; };
  const actors = { provider: '提供方应用', service: 'AppWidget 服务', host: '宿主 UI', executor: '宿主 Executor', render: '宿主绘制 / 显示示意' };
  let model = build(options);
  let time = 4100;
  let playing = false;
  let frame = 0;
  let lastTick = null;
  let lastStep = -1;

  for (const scenario of scenarios) {
    const option = document.createElement('option');
    option.value = scenario.id;
    option.textContent = scenario.label;
    $('scenario').append(option);
  }

  function actions(state) {
    $('actionList').replaceChildren();
    const lines = !state.created ? [['尚未创建描述', '等待触发更新']] : !state.actionsReady ? [['Actions = ∅', '构造器尚未记录更新操作']] : model.partial ? [['ReflectionAction', 'count.setText("3 项待办")']] : [
      ['ReflectionAction', `title.setText("${model.target.title}")`],
      ['ReflectionAction', 'count.setText("3 项待办")'],
      ['SetOnClickResponse', 'PendingIntent → 目标组件'],
    ];
    lines.forEach(([name, value]) => {
      const row = document.createElement('div');
      const title = document.createElement('b'); title.textContent = name;
      const detail = document.createElement('small'); detail.textContent = value;
      row.append(title, detail);
      $('actionList').append(row);
    });
  }

  function timeline() {
    $('ruler').replaceChildren();
    $('timeRows').replaceChildren();
    $('timelineLabels').replaceChildren();
    for (let stamp = 0; stamp <= model.duration - 500; stamp += 2000) {
      const tick = document.createElement('span');
      tick.className = 'tick';
      tick.style.left = `${100 * stamp / model.duration}%`;
      tick.textContent = `${stamp / 1000}s`;
      $('ruler').append(tick);
    }
    const rows = [
      ['应用描述', model.times.create, model.times.submit, '构造 / 记录 Actions', 'provider'],
      ['服务缓存', model.times.cache, model.offline ? model.times.reconnect : model.times.callback, model.offline ? '已缓存 · 等待监听' : '缓存 / 通知', model.offline ? 'offline' : 'service'],
      ['Host UI', model.times.host, model.times.commit, model.reuse ? 'reapply' : '接收 / 构建', 'host'],
      ...(model.async ? [['Executor', model.times.prepare, model.times.tree, 'inflate / 准备', 'executor']] : []),
      ['绘制示意', model.times.commit, model.times.screen, 'View 已挂入 ≠ 已显示', 'render'],
    ];
    rows.forEach(([label, start, end, text, kind]) => {
      const heading = document.createElement('div'); heading.textContent = label;
      $('timelineLabels').append(heading);
      const row = document.createElement('div'); row.className = 'time-row';
      const segment = document.createElement('span'); segment.className = `time-segment ${kind}`;
      segment.style.left = `${100 * start / model.duration}%`;
      segment.style.width = `${100 * (end - start) / model.duration}%`;
      segment.textContent = text;
      segment.title = `${label}: ${seconds(start)} → ${seconds(end)}`;
      row.append(segment);
      $('timeRows').append(row);
    });
  }

  function rebuild() {
    model = build(options);
    time = Math.min(time, model.duration);
    setText('scenarioDesc', model.scenario.description);
    setText('scenarioResult', model.error ? '目标失败 / 错误占位' : model.reuse ? '保留原树 T0' : model.fallback ? '失败后重建 T1' : '创建新树 T1');
    $('backgroundControl').hidden = !model.async;
    $('backgroundRange').value = model.background;
    $('renderRange').value = model.renderDelay;
    setText('backgroundValue', seconds(model.background));
    setText('renderValue', seconds(model.renderDelay));
    $('scrub').max = model.duration;
    $('eventList').replaceChildren();
    model.events.forEach((event, index) => {
      const li = document.createElement('li');
      const button = document.createElement('button'); button.type = 'button'; button.dataset.step = index;
      const stamp = document.createElement('code'); stamp.textContent = seconds(event.time);
      const title = document.createElement('span'); title.textContent = event.title;
      button.append(stamp, title);
      button.addEventListener('click', () => { pause(); seek(event.time); });
      li.append(button);
      $('eventList').append(li);
    });
    lastStep = -1;
    timeline();
    render();
  }

  function packet(id, connectorId, start, end, t, reverse = false) {
    const node = $(id);
    const connector = $(connectorId);
    const visible = start !== null && t >= start && t < end;
    const ratio = start === null ? 0 : Math.max(0, Math.min(1, (t - start) / (end - start)));
    const vertical = connectorId === 'serviceHostWire' && matchMedia('(max-width: 950px)').matches;
    const distance = vertical ? 40 : Math.max(0, connector.clientWidth - 7);
    const offset = (reverse ? 1 - ratio : ratio) * distance;
    node.style.transform = vertical ? `translateY(${offset}px)` : `translateX(${offset}px)`;
    node.style.opacity = visible ? '1' : '0';
    connector.classList.toggle('reverse', reverse);
  }

  function render() {
    const state = at(model, time);
    $('scrub').value = state.t;
    setText('timeNow', seconds(state.t));
    setText('providerStatus', !state.created ? '更新尚未构建' : state.submitted ? '已提交本次描述 R1' : state.actionsReady ? `已记录 ${state.providerActionCount} 条 Actions` : '只创建了布局描述');
    setText('providerRevision', state.created ? 'R1' : '∅');
    setText('providerLayout', state.created ? model.target.layout : '尚未创建');
    setText('providerApi', model.partial ? 'partiallyUpdateAppWidget(37, patch)' : 'updateAppWidget(37, views)');
    actions(state);
    setText('serviceStatus', !state.cached ? model.old ? '仍保留旧缓存 R0' : '等待提供方提交' : model.offline && !state.hostListening ? '新缓存 R1 / Host 未监听' : '本次描述已缓存 R1');
    setText('cacheRevision', state.cache ? `R${state.cache.revision}` : '∅');
    setText('cacheTitle', state.cache ? state.cache.title : '没有目标内容缓存');
    setText('cacheCount', state.cache ? `count = ${state.cache.count}` : 'count = ∅');
    setText('cacheActions', `${state.cacheActionCount} Actions`);
    setText('cacheMode', state.cached ? model.partial ? '1 patch → 3 Actions' : '全量替换描述' : state.cache ? '先前全量缓存' : '等待全量描述');
    $('callbackState').className = `callback-state${model.offline && !state.hostListening ? ' wait' : state.callbackQueued || state.hostReceived ? ' active' : ''}`;
    setText('callbackText', model.offline ? state.hostReceived ? 'startListening 补发已处理' : state.hostListening ? '重新监听，拉取待更新列表' : 'callbacks = null · 暂不通知' : state.hostReceived ? '宿主 UI 已处理消息' : state.callbackReceived ? 'Binder 回调已到 / UI 待处理' : state.callbackQueued ? 'CallbackHandler 已排队' : '尚未排队本次通知');
    $('appServiceWire').classList.toggle('active', state.submitted);
    packet('appPacket', 'appServiceWire', model.times.submit, model.times.cache, state.t);
    if (model.offline) {
      const requesting = state.t >= model.times.reconnect && state.t < model.times.replay;
      $('serviceHostWire').classList.toggle('active', state.hostListening);
      setText('hostWireLabel', requesting ? 'startListening ←' : '待更新快照 →');
      setText('hostWireNote', !state.hostListening ? '无实时 oneway 回调' : 'Binder 返回列表 / 调用线程');
      packet('hostPacket', 'serviceHostWire', state.hostListening ? requesting ? model.times.reconnect : model.times.replay : null,
        requesting ? model.times.replay : model.times.host, state.t, requesting);
    } else {
      $('serviceHostWire').classList.toggle('active', state.callbackQueued);
      setText('hostWireLabel', 'oneway Binder');
      setText('hostWireNote', '描述副本 + widgetId');
      packet('hostPacket', 'serviceHostWire', model.times.notify, model.times.callback, state.t);
    }

    let hostStatus = !state.hostReceived ? state.callbackReceived ? '回调已到，UI 尚未应用' : '尚未收到本次内容' : state.committed ? model.error ? '目标失败 / 已挂入错误占位' : '本地树与动作已应用' : state.t < model.times.prepare ? '选择布局与复用路径' : model.async && !state.prepared ? 'Executor 后台准备中' : state.prepared ? '新树已准备，待应用动作' : model.reuse ? '原树 reapply 中' : '本地构建进行中';
    setText('hostStatus', hostStatus);
    const candidate = state.prepared && !state.committed;
    setText('treeIdentity', state.committed ? `${state.localContent.tree} / ${model.error ? '错误占位' : model.reuse ? '原树复用' : '新内容树'}` : candidate ? `${model.target.tree} / 候选树未挂入` : model.old ? 'T0 / 原内容树' : '没有目标内容树');
    setText('treeLayout', state.committed && model.error ? 'getErrorView()' : candidate ? `${model.target.layout} · 准备完成` : state.localContent ? state.localContent.layout : '默认布局 / 容器');
    setText('treeTitle', candidate ? 'title = XML 默认值' : state.localContent ? `title = ${state.localContent.title}` : 'title = ∅');
    setText('treeCount', candidate ? 'Actions 尚未应用完成' : state.localContent ? `count = ${state.localContent.count === null ? '∅' : state.localContent.count}` : 'count = ∅');
    $('treeBox').className = `tree-child${candidate ? ' prepared' : ''}${state.applied ? ' applied' : ''}${state.committed && model.error ? ' error' : ''}`;
    const visible = state.visible;
    $('widgetPreview').className = `widget-preview${visible ? ' visible' : ''}${visible && visible.revision === 'error' ? ' error' : ''}`;
    setText('widgetKicker', !visible ? 'HOST / DEFAULT PLACEHOLDER' : visible.revision === 'error' ? 'HOST / ERROR PLACEHOLDER' : `TASKS / R${visible.revision}`);
    setText('widgetTitle', visible ? visible.title : '等待小组件内容');
    setText('widgetCount', visible ? visible.revision === 'error' ? '!' : visible.count : '—');
    setText('widgetUnit', visible ? visible.revision === 'error' ? '目标内容未成功显示' : '项待办' : '默认占位示意');
    $('widgetItems').hidden = !visible || visible.revision === 'error';
    const method = model.async ? 'Executor → context 主 Looper → applyContent' : model.reuse ? 'canRecycleView → reapply → applyContent' : model.fallback ? 'reapply 失败 → apply 新树 → applyContent' : model.error ? 'apply 失败 → getErrorView → applyContent' : 'inflate → performApply → addView';
    setText('hostMethod', state.hostReceived ? method : 'AppWidgetHost → AppWidgetHostView');
    const values = [
      ['receivedBoundary', 'receivedValue', state.hostReceived, false, state.hostReceived ? '已处理描述 R1' : '尚未处理'],
      ['appliedBoundary', 'appliedValue', state.applied, state.committed && model.error, state.applied ? `${model.target.tree} / 内容已应用` : state.committed && model.error ? '失败 / error view' : '尚未完成'],
      ['screenBoundary', 'screenValue', state.screenUpdated && !model.error, state.screenUpdated && model.error, state.screenUpdated ? model.error ? '错误占位（示意）' : 'R1 已显示（示意）' : visible ? '仍为旧画面 R0' : '仍为默认占位'],
    ];
    values.forEach(([id, output, done, error, text]) => { $(id).className = error ? 'error' : done ? 'done' : ''; setText(output, text); });
    let title = '应用构建描述，实际 View 由宿主创建';
    let desc = 'package、layoutId 和 Actions 可以跨进程传递；View 树不会从提供方搬到桌面。';
    let tone = '';
    if (state.screenUpdated) {
      title = model.error ? '示意画面是错误占位，不是 Provider 目标内容' : '示意画面已更新；本例的目标内容来自宿主本地 View';
      desc = '这只是教学显示时刻，不是 onViewApplied 的物理上屏保证，也不是真机测量。';
      tone = model.error ? 'error' : 'ok';
    } else if (state.committed) {
      title = model.error ? '目标应用失败；错误占位已挂入，尚未完成示意绘制' : 'View 已应用 / 已挂入，但示意画面还未更新';
      desc = model.async && state.applied ? 'onViewApplied 已执行；右侧画面仍保持旧内容或占位，直到后续示意绘制完成。' : '内容已进入宿主本地结构，仍需参与窗口后续 traversal 与 draw。';
      tone = model.error ? 'error' : 'wait';
    } else if (model.offline && state.cached && !state.hostListening) {
      title = '服务缓存是 R1，桌面仍是 R0：Host 没有实时监听';
      desc = '尚无宿主回调；旧树与旧画面保留。重新监听后可按待更新列表补发。';
      tone = 'wait';
    } else if (state.hostReceived) {
      title = state.prepared ? '宿主新树已准备，尚未完成最终动作应用' : model.reuse ? '服务端更新描述，宿主尝试复用同一棵树' : '宿主已接住描述，正在构建自己的内容树';
      desc = model.partial ? '一条 patch 在服务端合并成完整描述；Host 复用是独立判定。' : '提供方资源 Context 与受限加载器在宿主进程内工作。';
    } else if (state.cached) {
      title = state.callbackReceived ? 'Binder 回调已到，不代表 UI 已经应用' : '服务端已缓存描述，不代表宿主已创建内容';
      desc = '服务端描述、宿主本地树和屏幕画面可以处于不同进度。';
    }
    $('verdict').className = `verdict ${tone}`;
    setText('verdictTitle', title);
    setText('verdictDesc', desc);
    setText('ownershipValue', `App: ${state.created ? 'R1 描述' : '未构建'} / Service: ${state.cache ? `R${state.cache.revision} 描述` : '∅'} / Host: ${state.localContent ? state.localContent.tree : '容器'}`);
    [['localHop', state.committed], ['traversalHop', state.traversed], ['drawHop', state.drawn], ['screenHop', state.screenUpdated]].forEach(([id, reached]) => $(id).classList.toggle('reached', reached));
    $('playhead').style.left = `${100 * state.t / model.duration}%`;
    if (lastStep !== state.activeIndex) {
      lastStep = state.activeIndex;
      setText('phaseCount', `STEP ${String(state.activeIndex + 1).padStart(2, '0')} / ${model.events.length}`);
      setText('phaseTime', seconds(state.active.time));
      setText('phaseActor', actors[state.active.actor]);
      setText('phaseTitle', state.active.title);
      setText('phaseDesc', state.active.description);
      const source = data.stages.find(stage => stage.id === state.active.ref);
      $('phaseRef').href = `#source-${state.active.ref}`;
      $('phaseRef').textContent = source ? `${source.title} ↗` : '';
      $('eventList').querySelectorAll('button').forEach((button, index) => {
        button.className = index === state.activeIndex ? 'current' : index < state.activeIndex ? 'past' : '';
        if (index === state.activeIndex) button.setAttribute('aria-current', 'step');
        else button.removeAttribute('aria-current');
      });
    }
    document.documentElement.dataset.qa = JSON.stringify({ scenario: model.scenario.id, t: state.t, duration: model.duration, times: model.times,
      cached: state.cached, cacheRevision: state.cache && state.cache.revision, providerActionCount: state.providerActionCount, cacheActionCount: state.cacheActionCount,
      hostReceived: state.hostReceived, callbackReceived: state.callbackReceived, hostListening: state.hostListening, prepared: state.prepared,
      committed: state.committed, applied: state.applied, listenerApplied: state.listenerApplied, screenUpdated: state.screenUpdated,
      tree: state.tree, localTree: state.localContent && state.localContent.tree, visibleRevision: state.visible && state.visible.revision,
      active: state.active.id, playing, background: model.background, renderDelay: model.renderDelay, serviceHasView: state.serviceHasView });
  }

  function playIcon() {
    $('playBtn').innerHTML = playing ? data.icons.pause : data.icons.play;
    const label = playing ? '暂停' : '播放';
    $('playBtn').title = label; $('playBtn').setAttribute('aria-label', label);
  }
  function pause() { playing = false; lastTick = null; cancelAnimationFrame(frame); playIcon(); }
  function seek(value) { time = Math.max(0, Math.min(model.duration, value)); render(); }
  function tick(timestamp) {
    if (!playing) return;
    if (lastTick !== null) time += Math.min(100, timestamp - lastTick) * Number($('speed').value);
    lastTick = timestamp;
    if (time >= model.duration) { time = model.duration; pause(); }
    render();
    if (playing) frame = requestAnimationFrame(tick);
  }
  function play() { if (time >= model.duration) time = 0; playing = true; lastTick = null; playIcon(); frame = requestAnimationFrame(tick); render(); }
  function step(direction) {
    pause();
    const candidates = model.events.filter(event => direction > 0 ? event.time > time + .01 : event.time < time - .01);
    const event = direction > 0 ? candidates[0] : candidates[candidates.length - 1];
    seek(event ? event.time : direction > 0 ? model.duration : 0);
  }
  $('playBtn').addEventListener('click', () => playing ? (pause(), render()) : play());
  $('restartBtn').addEventListener('click', () => { pause(); seek(0); });
  $('prevStep').addEventListener('click', () => step(-1));
  $('nextStep').addEventListener('click', () => step(1));
  $('scrub').addEventListener('input', event => { pause(); seek(Number(event.target.value)); });
  $('scenario').addEventListener('change', event => { pause(); options.scenario = event.target.value; delete options.background; delete options.renderDelay; time = 0; rebuild(); });
  [['backgroundRange', 'background'], ['renderRange', 'renderDelay']].forEach(([id, key]) => $(id).addEventListener('input', event => { pause(); options[key] = Number(event.target.value); rebuild(); }));
  const openSource = link => { const target = document.querySelector(link.getAttribute('href')); if (target) target.open = true; };
  $('phaseRef').addEventListener('click', () => openSource($('phaseRef')));
  document.querySelectorAll('a[href^="#source-"]').forEach(link => link.addEventListener('click', () => openSource(link)));
  document.addEventListener('visibilitychange', () => { if (document.hidden) { pause(); render(); } });
  document.addEventListener('keydown', event => {
    if (/INPUT|SELECT|TEXTAREA|BUTTON/.test(event.target.tagName) || event.metaKey || event.ctrlKey || event.altKey) return;
    if (event.code === 'Space') { event.preventDefault(); playing ? (pause(), render()) : play(); }
    if (event.code === 'ArrowRight') { event.preventDefault(); step(1); }
    if (event.code === 'ArrowLeft') { event.preventDefault(); step(-1); }
  });
  const prefs = ['auto', 'light', 'dark'];
  function theme(pref) {
    document.documentElement.dataset.themePref = pref;
    document.documentElement.dataset.theme = pref === 'light' || pref === 'auto' && matchMedia('(prefers-color-scheme: light)').matches ? 'light' : 'dark';
    $('themeBtn').title = `配色：${{ auto: '跟随系统', light: '浅色', dark: '深色' }[pref]}`;
    $('themeBtn').setAttribute('aria-label', `${$('themeBtn').title}，切换配色`);
  }
  $('themeBtn').addEventListener('click', () => { const pref = prefs[(prefs.indexOf(document.documentElement.dataset.themePref) + 1) % prefs.length]; try { localStorage.setItem('fp-theme', pref); } catch (_) {} theme(pref); });
  matchMedia('(prefers-color-scheme: light)').addEventListener('change', () => { if (document.documentElement.dataset.themePref === 'auto') theme('auto'); });
  window.addEventListener('resize', render);
  setText('citationCount', `${data.stages.length} 组机制 / ${data.stages.reduce((count, stage) => count + stage.snippets.length, 0)} 段源码`);
  theme(document.documentElement.dataset.themePref);
  rebuild();
})();
