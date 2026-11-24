(function () {
  'use strict';
  const { scenarios, build, at } = InputTimeoutModel;
  const $ = id => document.getElementById(id);
  const data = JSON.parse($('sourceData').textContent);
  const options = { scenario: 'looper' };
  const seconds = time => `${(time / 1000).toFixed(2)} s`;
  const setText = (id, text) => { $(id).textContent = text; };
  let model = build(options);
  let time = 2800;
  let playing = false;
  let animation = 0;
  let lastTick = null;
  let lastStep = -1;

  for (const scenario of scenarios) {
    const option = document.createElement('option');
    option.value = scenario.id;
    option.textContent = scenario.label;
    $('scenario').append(option);
  }

  function queue(container, entries, type, state) {
    container.replaceChildren();
    if (!entries.length) {
      const empty = document.createElement('span');
      empty.className = 'queue-empty';
      empty.textContent = type === 'outbound' ? '∅ / 无待发布条目' : state.completed ? '∅ / 本例原始事件已清理' : '∅ / 无已发未完成原始事件';
      container.append(empty);
    }
    for (const entry of entries) {
      const chip = document.createElement('div');
      chip.className = `event-chip${entry.overdue ? ' overdue' : ''}${type === 'outbound' ? ' outbound' : ''}`;
      const title = document.createElement('strong');
      title.textContent = `${entry.id} · seq ${entry.seq}`;
      const first = document.createElement('small');
      const second = document.createElement('small');
      first.textContent = type === 'outbound' ? '尚未发布成功' : `sent ${seconds(entry.delivery)}`;
      second.textContent = type === 'outbound' ? '未登记本条 tracker' : `due ${seconds(entry.deadline)}`;
      chip.append(title, first, second);
      container.append(chip);
    }
  }

  function timeline() {
    $('ruler').replaceChildren();
    $('timeRows').replaceChildren();
    $('timelineLabels').replaceChildren();
    for (let tickTime = 0; tickTime <= model.duration - 500; tickTime += 2000) {
      const tick = document.createElement('span');
      tick.className = 'tick';
      tick.style.left = `${100 * tickTime / model.duration}%`;
      tick.textContent = `${tickTime / 1000}s`;
      $('ruler').append(tick);
    }
    for (const entry of model.entries) {
      const label = document.createElement('div');
      const title = document.createElement('b');
      title.textContent = entry.id;
      const seq = document.createElement('small');
      seq.textContent = `seq ${entry.seq}`;
      label.append(title, seq);
      $('timelineLabels').append(label);
      const row = document.createElement('div');
      row.className = 'time-row';
      const segment = (start, end, label, kind) => {
        if (end <= start) return;
        const span = document.createElement('span');
        span.className = `time-segment ${kind}`;
        span.style.left = `${100 * start / model.duration}%`;
        span.style.width = `${100 * (end - start) / model.duration}%`;
        span.textContent = label;
        span.title = `${entry.id} ${label}: ${seconds(start)} → ${seconds(end)}`;
        row.append(span);
      };
      segment(entry.delivery, Math.min(entry.deadline, entry.ack), '已发 · 等待 FINISHED', 'waiting');
      segment(entry.deadline, entry.ack, '已过期 · 尚未完成', 'late');
      segment(entry.start, entry.finish, '', 'work');
      segment(entry.ack, model.duration, 'ACK ✓', 'ack');
      const deadline = document.createElement('span');
      deadline.className = 'deadline-mark';
      deadline.style.left = `${100 * entry.deadline / model.duration}%`;
      const deadlineLabel = document.createElement('b');
      deadlineLabel.textContent = `${(entry.deadline / 1000).toFixed(1)}s`;
      deadline.append(deadlineLabel);
      row.append(deadline);
      $('timeRows').append(row);
    }
  }

  function rebuild() {
    model = build(options);
    time = Math.min(time, model.duration);
    setText('scenarioDesc', model.description);
    const custom = model.scenario.delay !== null;
    setText('scenarioResult', `${model.customTiming ? '自定义 / ' : ''}${model.incidents.length ? model.scenario.id === 'changed-timeout' ? 'E2 触发 / E1 入日志' : '超时后恢复' : '不触发 ANR'}`);
    $('parameters').hidden = !custom;
    $('delayRange').value = model.delay;
    $('ackRange').value = model.ackDelay;
    $('timeout').value = model.timeout;
    setText('delayValue', seconds(model.delay));
    setText('ackDelayValue', seconds(model.ackDelay));
    $('scrub').max = model.duration;
    $('eventList').replaceChildren();
    model.schedule.forEach((event, index) => {
      const li = document.createElement('li');
      const button = document.createElement('button');
      button.type = 'button';
      button.dataset.step = index;
      const stamp = document.createElement('code');
      stamp.textContent = seconds(event.time);
      const text = document.createElement('span');
      text.textContent = event.title;
      button.append(stamp, text);
      button.addEventListener('click', () => { pause(); seek(event.time); });
      li.append(button);
      $('eventList').append(li);
    });
    lastStep = -1;
    timeline();
    render();
  }

  function packet(id, ratio, visible, reverse) {
    const node = $(id);
    const width = Math.max(0, node.parentElement.clientWidth - 8);
    node.style.transform = `translateX(${(reverse ? 1 - ratio : ratio) * width}px)`;
    node.style.opacity = visible ? '1' : '0';
  }

  function render() {
    const state = at(model, time);
    $('scrub').value = state.t;
    setText('timeNow', seconds(state.t));
    setText('outboundCount', state.outboundQueue.length);
    setText('waitCount', state.waitQueue.length);
    queue($('outboundQueue'), state.outboundQueue, 'outbound', state);
    queue($('waitQueue'), state.waitQueue, 'wait', state);
    $('connectionState').className = `connection-state${state.responsive ? '' : ' bad'}`;
    $('connectionState').querySelector('code').textContent = `responsive = ${state.responsive}`;
    const live = state.live;
    const sent = [...state.entries].reverse().find(entry => entry.published) || state.entries[0];
    const receiving = state.entries.find(entry => state.t >= entry.delivery - 250 && state.t < entry.delivery);
    const finishing = state.entries.find(entry => state.t >= entry.finish && state.t < entry.ack);
    packet('sendPacket', receiving ? (state.t - receiving.delivery + 250) / 250 : 0, Boolean(receiving), false);
    packet('ackPacket', finishing ? (state.t - finishing.finish) / (finishing.ack - finishing.finish) : 0, Boolean(finishing), true);
    $('sendWire').classList.toggle('active', sent.published);
    $('ackWire').classList.toggle('active', state.entries.some(entry => state.t >= entry.finish));
    setText('sendLabel', `${receiving ? receiving.id : sent.id} · Motion ${receiving ? receiving.action : sent.action} · seq ${receiving ? receiving.seq : sent.seq}`);
    const ackEntry = finishing || [...state.entries].reverse().find(entry => state.t >= entry.finish) || state.entries[0];
    setText('ackLabel', `seq ${ackEntry.seq} · handled = ${ackEntry.handled}`);
    setText('channelCallout', finishing ? 'finish 已调用，服务端清理尚未发生' : state.completed ? '原始事件的 FINISHED 均已按 seq 清理' : '发布成功，不保证应用已消费');
    setText('appStatus', state.appState);
    const blocked = model.scenario.id === 'looper' && state.t >= 800 && state.t < model.entries[0].start;
    const handling = live.phase === 'handling';
    const deferred = live.phase === 'deferred';
    $('looperStage').className = `looper-stage${blocked ? ' blocked' : handling || deferred ? ' handling' : state.completed ? ' done' : ''}`;
    setText('appWork', blocked ? '其他任务 / 等待锁 / 同步调用' : handling ? `${live.id} · ViewRootImpl 输入阶段` : deferred ? `${live.id} · 可见处理已结束，等待 finish` : live.phase === 'ack-flight' ? `${live.id} · FINISHED 已提交 native` : state.completed ? '本例输入处理已完成' : '等待 InputChannel 的读回调');
    setText('appEvent', `${live.id} · seq ${live.seq} · ${live.action}${blocked ? ' / 尚未消费' : ''}`);
    const progress = blocked ? (state.t - 800) / (model.entries[0].start - 800) : (state.t - live.start) / (live.workEnd - live.start);
    $('workProgress').style.width = `${100 * Math.max(0, Math.min(1, progress))}%`;
    const consumeDone = state.t >= live.start;
    const finishDone = state.t >= live.finish;
    const ackDone = state.t >= live.ack;
    [['consumeState', consumeDone], ['finishState', finishDone], ['ackState', ackDone]].forEach(([id, done]) => $(id).classList.toggle('done', done));
    setText('consumeValue', consumeDone ? `${live.id} · ${seconds(live.start)}` : '尚未执行');
    setText('finishValue', finishDone ? `${live.id} · ${seconds(live.finish)}` : '尚未调用');
    setText('ackValue', ackDone ? `${live.id} · ${seconds(live.ack)}` : '尚未清理');

    $('trackerEntries').replaceChildren();
    if (!state.tracker.length) {
      const empty = document.createElement('span');
      empty.className = 'queue-empty';
      empty.textContent = state.incident && !state.responsive ? '∅ / 已 eraseToken(A)，不再重复唤醒' : '∅ / 无当前登记项';
      $('trackerEntries').append(empty);
    }
    state.tracker.forEach((entry, index) => {
      const tag = document.createElement('span');
      tag.className = `tracker-entry${index === 0 ? ' first' : ''}`;
      tag.textContent = `${entry.id} (${(entry.deadline / 1000).toFixed(2)}s, A)`;
      $('trackerEntries').append(tag);
    });
    $('remaining').replaceChildren();
    if (state.next) {
      $('remaining').append(document.createTextNode((state.remaining / 1000).toFixed(2) + ' '));
      const unit = document.createElement('small'); unit.textContent = 's'; $('remaining').append(unit);
    } else setText('remaining', state.recovered ? '恢复' : state.incident ? '到期' : 'OFF');
    setText('clockLabel', state.next ? '最早截止还剩' : state.incident ? '本轮连接超时历史' : '无当前 ACK 计时项');
    setText('nextDeadline', state.next ? `${state.next.id} · deadline ${seconds(state.next.deadline)}` : state.incident ? `${state.incident.trigger} · at ${seconds(state.incident.time)}` : '等发布成功后登记');
    $('remaining').style.color = state.incident && !state.responsive ? 'var(--danger)' : state.recovered || state.completed ? 'var(--ok)' : 'var(--warn)';
    setText('ruleState', !state.responsive ? '已标记不响应，等待队列恢复' : state.completed ? '本例原始等待记录已清理' : '匹配的 FINISHED 先清理，可解除该项');

    let title = '尚未发布，不登记这个事件的 ACK 超时';
    let desc = '目标连接已确定；只有成功发布的条目才进入 waitQueue 与 AnrTracker。';
    let tone = 'idle';
    if (!state.responsive) {
      title = `${state.incident.trigger} 截止到期，连接 A 已标记不响应`;
      desc = state.policyReached ? '已通知 policy；waitQueue 原始记录仍在。其他连接可继续工作，后续弹窗与进程处理另有策略。' : 'eraseToken(A) 停止重复追踪，不是清空 waitQueue；onAnrLocked 随后排队通知 policy。';
      tone = 'danger';
    } else if (state.recovered) {
      title = '剩余队列已无过期项，连接恢复响应';
      desc = 'notifyWindowResponsive 可进入恢复处理；已经发生的 ANR 历史不会被迟到 ACK 抹除。';
      tone = 'ok';
    } else if (state.completed) {
      title = 'FINISHED 已按 seq 清理，本例未发生 ANR';
      desc = model.scenario.id === 'unhandled' ? 'handled=false 说明未消费，但完成信号仍有效。Motion 场景不涉及 key fallback。' : 'waitQueue 与 tracker 中对应条目已移除，不再等待这个事件。';
      tone = 'ok';
    } else if (state.waitQueue.length) {
      title = finishing ? 'finish 已调用，服务端仍未清理对应记录' : '事件已发送，服务端还在等完成应答';
      desc = model.entries.length > 1 ? '每条 DispatchEntry 各有 deadline；一个 ACK 不会把其他等待统一清除或重新计时。' : '应用消费事件、开始处理、调用 finish，与 Dispatcher 完成 FINISHED 清理是不同边界。';
      tone = '';
    }
    $('verdict').className = `verdict ${tone}`;
    setText('verdictTitle', title);
    setText('verdictDesc', desc);
    setText('reasonLabel', state.incident ? `触发 ${state.incident.trigger} / reason ${state.incident.reasonEntry} / Waited ${state.incident.reasonWait} ms` : '本例尚无 ANR 历史');
    ['detectHop', 'notifyHop'].forEach(id => $(id).classList.toggle('reached', Boolean(state.incident)));
    ['wmsHop', 'amsHop'].forEach(id => $(id).classList.toggle('reached', state.policyReached));
    $('playhead').style.left = `${100 * state.t / model.duration}%`;
    if (lastStep !== state.activeIndex) {
      lastStep = state.activeIndex;
      setText('phaseCount', `STEP ${String(state.activeIndex + 1).padStart(2, '0')} / ${model.schedule.length}`);
      setText('phaseTime', seconds(state.active.time));
      setText('phaseTitle', state.active.title);
      setText('phaseDesc', state.active.description);
      const stage = data.stages.find(stage => stage.id === state.active.ref);
      $('phaseRef').href = `#source-${state.active.ref}`;
      $('phaseRef').textContent = stage ? `${stage.title} ↗` : '';
      $('eventList').querySelectorAll('button').forEach((button, index) => {
        button.className = index === state.activeIndex ? 'current' : index < state.activeIndex ? 'past' : '';
        if (index === state.activeIndex) button.setAttribute('aria-current', 'step');
        else button.removeAttribute('aria-current');
      });
    }
    document.documentElement.dataset.qa = JSON.stringify({ scenario: model.scenario.id, t: state.t, duration: model.duration,
      wait: state.waitQueue.map(entry => entry.id), outbound: state.outboundQueue.map(entry => entry.id),
      tracker: state.tracker.map(entry => entry.id), next: state.next && state.next.id, responsive: state.responsive,
      incidents: state.incidentCount, incident: state.incident, recovered: state.recovered, completed: state.completed,
      policyReached: state.policyReached, active: state.active.id, playing, delay: model.delay, ackDelay: model.ackDelay, timeout: model.timeout,
      entries: state.entries.map(entry => ({ id: entry.id, phase: entry.phase, deadline: entry.deadline, handled: entry.handled, ack: entry.ack })) });
  }

  function playIcon() {
    $('playBtn').innerHTML = playing ? data.icons.pause : data.icons.play;
    const label = playing ? '暂停' : '播放';
    $('playBtn').title = label;
    $('playBtn').setAttribute('aria-label', label);
  }
  function pause() { playing = false; lastTick = null; cancelAnimationFrame(animation); playIcon(); }
  function seek(value) { time = Math.max(0, Math.min(model.duration, value)); render(); }
  function tick(timestamp) {
    if (!playing) return;
    if (lastTick !== null) time += Math.min(100, timestamp - lastTick) * Number($('speed').value);
    lastTick = timestamp;
    if (time >= model.duration) { time = model.duration; pause(); }
    render();
    if (playing) animation = requestAnimationFrame(tick);
  }
  function play() { if (time >= model.duration) time = 0; playing = true; lastTick = null; playIcon(); animation = requestAnimationFrame(tick); render(); }
  function step(direction) {
    pause();
    const events = model.schedule.filter(event => direction > 0 ? event.time > time + .01 : event.time < time - .01);
    const event = direction > 0 ? events[0] : events[events.length - 1];
    seek(event ? event.time : direction > 0 ? model.duration : 0);
  }
  $('playBtn').addEventListener('click', () => playing ? (pause(), render()) : play());
  $('restartBtn').addEventListener('click', () => { pause(); seek(0); });
  $('prevStep').addEventListener('click', () => step(-1));
  $('nextStep').addEventListener('click', () => step(1));
  $('scrub').addEventListener('input', event => { pause(); seek(Number(event.target.value)); });
  $('scenario').addEventListener('change', event => { pause(); options.scenario = event.target.value; delete options.delay; delete options.ackDelay; delete options.timeout; time = 0; rebuild(); });
  [['delayRange', 'delay'], ['ackRange', 'ackDelay'], ['timeout', 'timeout']].forEach(([id, key]) => {
    $(id).addEventListener(id === 'timeout' ? 'change' : 'input', event => { pause(); options[key] = Number(event.target.value); rebuild(); });
  });
  $('phaseRef').addEventListener('click', () => { const target = document.querySelector($('phaseRef').getAttribute('href')); if (target) target.open = true; });
  document.querySelectorAll('a[href^="#source-"]').forEach(link => link.addEventListener('click', () => { const target = document.querySelector(link.getAttribute('href')); if (target) target.open = true; }));
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
  setText('citationCount', `${data.stages.length} 组机制 / ${data.stages.reduce((n, stage) => n + stage.snippets.length, 0)} 段源码`);
  theme(document.documentElement.dataset.themePref);
  rebuild();
})();
