(function () {
  'use strict';
  const { scenarios, build, at } = NoFocusModel;
  const $ = id => document.getElementById(id);
  const data = JSON.parse($('sourceData').textContent);
  const options = { scenario: 'timeout', eventTime: 1000, ready: 7400, preAnr: false };
  let model = build(options);
  let time = 2200;
  let playing = false;
  let frame = 0;
  let lastTimestamp = null;
  let lastStep = -1;
  const seconds = value => `${(value / 1000).toFixed(2)} s`;
  const setText = (id, value) => { $(id).textContent = value; };
  const refMap = {
    application: 'focused-application-route', focusability: 'focus-eligibility',
    routing: 'focus-dependent-event-routing', arm: 'arm-focus-wait',
    pending: 'arm-focus-wait', 'pre-anr': 'pre-anr-warning',
    timeout: 'deadline-and-native-recheck', reset: 'reset-entry-points',
    report: 'policy-jni-to-wms', policy: 'wms-attribution-and-recovery',
  };
  const alias = { application: 'application', focusability: 'focusability', routing: 'routing', arm: 'arm',
    pending: 'arm', 'pre-anr': 'pre-anr', timeout: 'timeout', reset: 'reset', report: 'report', policy: 'policy' };
  const sourceLabels = {
    application: 'InputMonitor / 焦点应用', focusability: 'FocusResolver / 资格',
    routing: 'Key & Motion / 分流', arm: 'findFocusedWindowTargetLocked',
    pending: 'mPendingEvent / PENDING', 'pre-anr': 'processPreAnrsLocked',
    timeout: 'processNoFocusedWindowAnrLocked', reset: 'resetNoFocusedWindowTimeoutLocked',
    report: 'onAnrLocked / JNI', policy: 'AnrController / 归因',
  };

  scenarios.forEach(item => {
    const option = document.createElement('option');
    option.value = item.id;
    option.textContent = item.label;
    $('scenario').append(option);
  });

  function segment(track, start, end, label, className) {
    if (end <= start) return;
    const node = document.createElement('span');
    node.className = `time-segment ${className}`;
    node.style.left = `${100 * start / model.duration}%`;
    node.style.width = `${100 * (end - start) / model.duration}%`;
    node.textContent = label;
    node.title = `${label} / ${seconds(start)} → ${seconds(end)}`;
    track.append(node);
  }

  function buildTimeline() {
    $('ruler').replaceChildren();
    for (let t = 0; t <= model.duration - 500; t += model.duration > 10000 ? 2000 : 1000) {
      const tick = document.createElement('span');
      tick.className = 'tick';
      tick.style.left = `${100 * t / model.duration}%`;
      tick.textContent = `${t / 1000}s`;
      $('ruler').append(tick);
    }
    $('windowTrack').replaceChildren();
    $('inputTrack').replaceChildren();
    const focusAt = model.switchTime === null ? model.ready : model.switchTime;
    segment($('windowTrack'), 0, focusAt === null ? model.duration : focusAt, model.fixedWindow ? '可见 · 无焦点' : '无焦点窗口', 'no-focus');
    if (focusAt !== null) segment($('windowTrack'), focusAt, model.duration, model.switchTime === null ? 'A 获得焦点' : 'B 获得焦点', 'focus');
    if (model.startsTimer) segment($('inputTrack'), model.eventTime, model.stopTime, 'PENDING · 等待目标', 'wait');
    else if (model.hasEvent) segment($('inputTrack'), model.eventTime, model.duration,
      model.pointer ? '按坐标分发' : !model.hasApp ? '查找失败' : '目标已就绪', model.hasApp ? 'focus' : 'no-focus');
    $('eventMark').hidden = !model.hasEvent;
    $('eventMark').style.left = `${100 * model.eventTime / model.duration}%`;
    $('deadlineMark').hidden = !model.startsTimer;
    $('deadlineMark').style.left = `${100 * (model.deadline || 0) / model.duration}%`;
  }

  function rebuildEvents() {
    $('eventList').replaceChildren();
    model.events.forEach((event, index) => {
      const li = document.createElement('li');
      const button = document.createElement('button');
      button.type = 'button';
      button.dataset.step = index;
      const code = document.createElement('code');
      code.textContent = seconds(event.time);
      const label = document.createElement('span');
      label.textContent = event.title;
      button.append(code, label);
      button.addEventListener('click', () => { pause(); seek(event.time); });
      li.append(button);
      $('eventList').append(li);
    });
    lastStep = -1;
  }

  function rebuild() {
    model = build(options);
    time = Math.min(time, model.duration);
    setText('scenarioDesc', model.scenario.description);
    setText('scenarioOutcome', model.reportsAnr ? 'ANR 上报' : model.scenario.outcome === 'ANR 上报' ? '恢复分发' : model.scenario.outcome);
    $('scrub').max = model.duration;
    $('readyControl').hidden = model.scenario.ready === null;
    $('eventControl').hidden = !model.hasEvent;
    $('eventRange').value = model.eventTime;
    $('readyRange').value = model.ready === null ? 0 : model.ready;
    setText('readyValue', model.ready === null ? '无' : seconds(model.ready));
    setText('eventValue', seconds(model.eventTime));
    $('preAnr').checked = Boolean(options.preAnr);
    buildTimeline();
    rebuildEvents();
    render();
  }

  function render() {
    const state = at(model, time);
    const resolved = !state.waiting && state.timerWasArmed && !state.timedOut;
    $('scrub').value = state.t;
    setText('timeNow', seconds(state.t));
    setText('focusedApp', state.application);
    setText('focusedWindow', state.focusedWindow);
    $('focusedWindow').className = state.focusedWindow === 'null' ? 'danger-text' : 'ok-text';
    setText('focusReason', state.focusReason);
    setText('reasonDesc', state.focusReason === 'OK' ? '已获得输入焦点' : state.focusReason === 'NOT_FOCUSABLE' ? '窗口可见，但不可聚焦' : '尚无可聚焦窗口');
    setText('eventType', !model.hasEvent ? '无焦点事件' : model.pointer ? 'MotionEvent' : 'KeyEvent');
    setText('eventId', !model.hasEvent ? '没有 Key / non-pointer' : model.pointer ? 'POINTER · ACTION_DOWN' : 'ENTER · event 0x01');
    setText('eventStatus', state.afterEvent ? model.pointer ? '进入触摸查找' : '进入焦点查找' : state.event);
    setText('eventLocation', model.pointer ? '按坐标命中目标' : model.hasEvent ? '需要焦点窗口' : '无须查找目标');
    setText('eventNote', state.waiting ? '尚未交付应用 A' : state.timedOut ? '这不是应用 ACK 超时' : model.pointer ? '普通触摸采用命中测试' : state.windowReady && state.afterEvent ? '后续处理 / ACK 不在模型内' : '事件不必随应用启动到达');
    const dispatcher = document.querySelector('.dispatcher-actor');
    dispatcher.className = `actor dispatcher-actor ${state.timedOut ? 'reported' : state.waiting ? 'waiting' : resolved ? 'resolved' : ''}`;
    setText('dispatchState', state.timedOut ? '焦点等待超时' : state.waiting ? '等待焦点窗口' : resolved ? '旧等待已清除' : state.afterEvent && state.focusedWindow !== 'null' ? '已有焦点目标' : '没有焦点等待计时');
    setText('pendingValue', state.waiting ? 'KeyEvent · PENDING' : state.timedOut ? '等待超时 · 已通知' : state.afterEvent ? state.event : '尚无待分发焦点事件');
    setText('pendingField', state.waiting ? 'mPendingEvent' : state.timedOut ? '本次焦点等待（历史）' : state.switched && state.afterEvent ? '重新查找新目标' : '焦点目标查找');
    $('pendingSlot').className = `pending-slot${state.waiting ? ' occupied' : ''}`;
    setText('stateField', state.waiting ? 'mNoFocusedWindowAnrState' : state.timerWasArmed ? '等待状态已 reset · 结果为历史记录' : 'mNoFocusedWindowAnrState = nullopt');
    setText('remaining', state.timedOut ? '0.00' : state.waiting ? (state.remaining / 1000).toFixed(2) : resolved ? 'RESET' : 'OFF');
    setText('timerUnit', state.waiting ? '秒剩余' : state.timedOut ? '等待期限已到' : resolved ? '不再等待原目标' : '计时尚未启动');
    $('ringProgress').style.strokeDashoffset = String(100 * (1 - state.progress));
    setText('timerState', state.timer);
    $('preStatus').hidden = !state.preNotified;
    setText('appName', state.switched ? 'B' : 'A');
    setText('windowTitle', state.switched ? 'Activity B' : 'Activity A');
    $('windowMock').className = `window-mock${state.visible ? ' visible' : ''}${state.focusedWindow !== 'null' ? ' focused' : ''}`;
    setText('windowState', state.focusedWindow !== 'null' ? '窗口已获得焦点' : state.visible ? '可见 · 可触摸' : '窗口尚未就绪');
    setText('windowFlags', state.focusReason);
    setText('windowVerdict', state.focusedWindow !== 'null' ? '有输入焦点' : '没有输入焦点');
    $('windowVerdict').style.color = state.focusedWindow !== 'null' ? 'var(--ok)' : 'var(--danger)';
    setText('windowNote', state.focusedWindow !== 'null' ? 'FocusResolver 已解析出有效 token' : state.visible ? 'NOT_FOCUSABLE 不妨碍普通触摸命中' : '启动流程 / 窗口提交尚未满足条件');
    const canDeliver = state.afterEvent && model.hasApp && (model.pointer || state.focusedWindow !== 'null') && !state.timedOut;
    const incomingProgress = (state.t - model.eventTime + 350) / 350;
    const deliveryTime = model.pointer ? model.eventTime : Math.max(model.eventTime, state.switched ? model.switchTime : model.ready || 0);
    const outgoingProgress = (state.t - deliveryTime) / 450;
    $('incoming').className = `connector${state.afterEvent ? ' active' : ''}${model.hasEvent && incomingProgress >= 0 && incomingProgress < 1 ? ' transit' : ''}`;
    $('incoming').style.setProperty('--transit', Math.max(0, Math.min(1, incomingProgress)));
    $('outgoing').className = `connector output-connector${canDeliver ? ' pass active' : ''}${canDeliver && outgoingProgress >= 0 && outgoingProgress < 1 ? ' transit' : ''}`;
    $('outgoing').style.setProperty('--transit', Math.max(0, Math.min(1, outgoingProgress)));
    setText('outgoingLabel', canDeliver ? model.pointer ? '命中分发' : '分发' : state.waiting ? '无目标' : '未分发');
    let title = '没有焦点事件进入查找，就不启动这一路等待';
    let detail = '只有焦点应用、没有焦点窗口，并不自动触发 no-focused-window ANR。';
    let tone = 'info';
    let marker = '○';
    if (state.waiting) { title = '目标缺席，事件留在 InputDispatcher'; detail = 'PENDING：尚未向 A 分发，不是在等待 A 的输入处理 ACK。'; tone = ''; marker = '…'; }
    else if (state.timedOut) { title = state.policyReached ? '已上报 policy，Java 侧继续检查与归因' : '截止后复查仍无焦点窗口，排入 ANR 通知'; detail = state.policyReached ? '通知进入 WMS / AMS 处理；是否弹窗、归因给谁，不由这个计时器单独决定。' : '复查应用 token 仍相同、对应 display 仍没有焦点窗口。'; tone = 'danger'; marker = '!'; }
    else if (state.switched) { title = model.startsTimer ? '不再等待 A，原按键向 B 的焦点窗口分发' : '事件到达前 B 已就绪，无需等待 A'; detail = '本例不含触摸裁剪：pending 事件重新入队并查找新窗口；取消等待计时不等于丢弃事件。'; tone = 'ok'; marker = '✓'; }
    else if (canDeliver) { title = model.pointer ? '普通触摸按坐标命中窗口，不启动焦点等待' : '窗口已获得焦点，事件可以开始分发'; detail = model.pointer ? '可触摸 ≠ 可聚焦；这里只选择普通 pointer 事件。' : '清除 no-focused-window 等待；之后仍可能有独立的应用处理超时。'; tone = 'ok'; marker = '✓'; }
    else if (state.afterEvent && !model.hasApp) { title = '没有焦点应用可等待，查找直接失败'; detail = '没有焦点窗口，同时也没有焦点应用：FAILED，不创建 no-focused-window 等待状态。'; marker = '×'; }
    $('decisionStrip').className = `decision-strip ${tone}`;
    setText('decisionMarker', marker);
    setText('decisionTitle', title);
    setText('decisionDetail', detail);
    setText('deadlineValue', model.startsTimer ? `deadline = ${(model.eventTime / 1000).toFixed(2)} + 5.00 s` : 'no focused-window timer');
    $('anrChecks').className = `anr-checks${state.timedOut ? ' triggered' : ''}`;
    setText('sameAppCheck', state.timedOut ? '复查时 token 仍为 A' : resolved ? '旧等待已取消' : !model.startsTimer ? '该场景未启动计时' : '待到期复查');
    setText('noWindowCheck', state.timedOut ? '复查时仍为 null' : resolved ? '等待条件已解除' : !model.startsTimer ? '无须到期复查' : '待到期复查');
    setText('reportAction', state.timedOut ? '条件同时成立，排入通知' : '尚未触发通知');
    $('nativeHop').className = state.timedOut ? 'reached' : '';
    ['jniHop', 'wmsHop', 'amsHop'].forEach(id => { $(id).className = state.policyReached ? 'reached' : ''; });
    $('playhead').style.left = `${100 * state.t / model.duration}%`;
    document.querySelectorAll('.time-segment').forEach(node => {
      const start = parseFloat(node.style.left) * model.duration / 100;
      node.classList.toggle('future', start > state.t);
    });
    if (lastStep !== state.activeIndex) {
      lastStep = state.activeIndex;
      setText('phaseCount', `STEP ${String(state.activeIndex + 1).padStart(2, '0')} / ${String(model.events.length).padStart(2, '0')}`);
      setText('phaseTime', seconds(state.active.time));
      setText('phaseTitle', state.active.title);
      setText('phaseDesc', state.active.description);
      $('phaseRefs').replaceChildren();
      state.active.refs.forEach(ref => {
        const entry = data.stages.find(item => item.id === refMap[ref]);
        if (!entry) return;
        const link = document.createElement('a');
        link.href = `#source-${alias[ref]}`;
        link.textContent = `${sourceLabels[ref]} ↗`;
        link.addEventListener('click', () => { const target = document.getElementById(`source-${alias[ref]}`); if (target) target.open = true; });
        $('phaseRefs').append(link);
      });
      $('eventList').querySelectorAll('button').forEach((button, index) => {
        button.className = index === state.activeIndex ? 'current' : index < state.activeIndex ? 'past' : '';
        if (index === state.activeIndex) button.setAttribute('aria-current', 'step');
        else button.removeAttribute('aria-current');
      });
    }
    document.documentElement.dataset.qa = JSON.stringify({ scenario: model.scenario.id, t: state.t,
      waiting: state.waiting, timer: state.timer, application: state.application, focusedWindow: state.focusedWindow,
      event: state.event, progress: state.progress, timedOut: state.timedOut, policyReached: state.policyReached,
      preNotified: state.preNotified, active: state.active.id, duration: model.duration, events: model.events.length,
      ready: model.ready, eventTime: model.eventTime, deadline: model.deadline, playing });
  }

  function seek(value) { time = Math.max(0, Math.min(model.duration, value)); render(); }
  function setPlayIcon() {
    $('playBtn').innerHTML = playing ? data.icons.pause : data.icons.play;
    $('playBtn').title = playing ? '暂停' : '播放';
    $('playBtn').setAttribute('aria-label', playing ? '暂停' : '播放');
    document.body.classList.toggle('playing', playing);
  }
  function pause() { playing = false; lastTimestamp = null; cancelAnimationFrame(frame); setPlayIcon(); }
  function tick(timestamp) {
    if (!playing) return;
    if (lastTimestamp !== null) time += Math.min(100, timestamp - lastTimestamp) * Number($('speed').value);
    lastTimestamp = timestamp;
    if (time >= model.duration) { time = model.duration; pause(); }
    render();
    if (playing) frame = requestAnimationFrame(tick);
  }
  function play() { if (time >= model.duration) time = 0; playing = true; lastTimestamp = null; setPlayIcon(); frame = requestAnimationFrame(tick); render(); }
  function step(direction) {
    pause();
    const candidates = model.events.filter(event => direction > 0 ? event.time > time + .01 : event.time < time - .01);
    const event = direction > 0 ? candidates[0] : candidates[candidates.length - 1];
    seek(event ? event.time : direction > 0 ? model.duration : 0);
  }
  $('playBtn').addEventListener('click', () => playing ? (pause(), render()) : play());
  $('restartBtn').addEventListener('click', () => { pause(); seek(0); });
  $('nextStep').addEventListener('click', () => step(1));
  $('prevStep').addEventListener('click', () => step(-1));
  $('scrub').addEventListener('input', event => { pause(); seek(Number(event.target.value)); });
  $('scenario').addEventListener('change', event => {
    pause();
    const scenario = scenarios.find(item => item.id === event.target.value);
    options.scenario = scenario.id;
    options.ready = scenario.ready;
    time = scenario.id === 'no-event' ? 2200 : 0;
    rebuild();
  });
  $('readyRange').addEventListener('input', event => { pause(); options.ready = Number(event.target.value); rebuild(); });
  $('eventRange').addEventListener('input', event => { pause(); options.eventTime = Number(event.target.value); rebuild(); });
  $('preAnr').addEventListener('change', event => { pause(); options.preAnr = event.target.checked; rebuild(); });
  document.addEventListener('visibilitychange', () => { if (document.hidden) { pause(); render(); } });
  document.addEventListener('keydown', event => {
    if (/INPUT|SELECT|TEXTAREA|BUTTON/.test(event.target.tagName) || event.metaKey || event.ctrlKey || event.altKey) return;
    if (event.code === 'Space') { event.preventDefault(); playing ? (pause(), render()) : play(); }
    if (event.code === 'ArrowRight') { event.preventDefault(); step(1); }
    if (event.code === 'ArrowLeft') { event.preventDefault(); step(-1); }
  });
  const preferences = ['auto', 'light', 'dark'];
  function applyTheme(pref) {
    document.documentElement.dataset.themePref = pref;
    document.documentElement.dataset.theme = pref === 'light' || pref === 'auto' && matchMedia('(prefers-color-scheme: light)').matches ? 'light' : 'dark';
    $('themeBtn').title = `配色：${{ auto: '跟随系统', light: '浅色', dark: '深色' }[pref]}`;
    $('themeBtn').setAttribute('aria-label', `${$('themeBtn').title}，切换配色`);
  }
  $('themeBtn').addEventListener('click', () => {
    const current = document.documentElement.dataset.themePref;
    const pref = preferences[(preferences.indexOf(current) + 1) % preferences.length];
    try { localStorage.setItem('fp-theme', pref); } catch (_) {}
    applyTheme(pref);
  });
  matchMedia('(prefers-color-scheme: light)').addEventListener('change', () => {
    if (document.documentElement.dataset.themePref === 'auto') applyTheme('auto');
  });
  document.querySelectorAll('a[href^="#source-"]').forEach(link => link.addEventListener('click', () => {
    const target = document.getElementById(link.hash.slice(1));
    if (target && target.tagName === 'DETAILS') target.open = true;
  }));
  setText('citationCount', `${data.stages.length} 组判定 / ${data.stages.reduce((n, stage) => n + stage.snippets.length, 0)} 段摘录`);
  applyTheme(document.documentElement.dataset.themePref);
  rebuild();
})();
