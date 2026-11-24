(function (root, factory) {
  const api = factory();
  if (typeof module === 'object' && module.exports) module.exports = api;
  else root.NoFocusModel = api;
})(typeof globalThis === 'object' ? globalThis : this, function () {
  'use strict';

  const TIMEOUT = 5000;
  const scenarios = [
    { id: 'timeout', label: '窗口迟到', outcome: 'ANR 上报', ready: 7400,
      description: '应用 A 已成为焦点应用，但窗口直到等待期限之后才具备输入焦点。' },
    { id: 'recovery', label: '窗口及时就绪', outcome: '恢复分发', ready: 3400,
      description: '同一事件先等待，随后窗口获得焦点；等待状态被清除，事件才开始向窗口分发。' },
    { id: 'not-focusable', label: '可见但不可聚焦', outcome: 'ANR 上报', ready: null,
      description: '窗口已经可见，也可以接收触摸，但 NOT_FOCUSABLE 使它不能承接需要焦点的按键。' },
    { id: 'no-event', label: '没有焦点事件', outcome: '不启动等待', ready: null,
      description: '只有焦点应用、没有焦点窗口，不足以启动这一路计时；还需要进入焦点目标查找的事件。' },
    { id: 'pointer', label: '只有普通触摸', outcome: '按坐标分发', ready: null,
      description: '普通 pointer 触摸按坐标命中可触摸窗口，不以 focusedWindow 作为目标选择前提。' },
    { id: 'no-app', label: '应用与窗口都为空', outcome: '事件失败', ready: null,
      description: '既没有焦点窗口，也没有焦点应用时，焦点目标查找直接失败，不创建这一路等待状态。' },
    { id: 'switch', label: '切换到另一应用', outcome: '重定向到 B', ready: null,
      description: '焦点应用由 A 换为 B，清除旧等待；本例不走触摸裁剪分支，原按键重新查找已就绪的 B 窗口。' },
  ];

  function build(options = {}) {
    const scenario = scenarios.find(item => item.id === options.scenario) || scenarios[0];
    const eventTime = Number.isFinite(options.eventTime) ? Math.max(500, Math.min(4000, options.eventTime)) : 1000;
    const ready = scenario.ready === null ? null : Number.isFinite(options.ready) ? Math.max(0, Math.min(8000, options.ready)) : scenario.ready;
    const hasApp = scenario.id !== 'no-app';
    const hasEvent = scenario.id !== 'no-event';
    const pointer = scenario.id === 'pointer';
    const fixedWindow = ['not-focusable', 'pointer'].includes(scenario.id);
    const needsFocus = hasEvent && !pointer;
    const switchTime = scenario.id === 'switch' ? 3500 : null;
    const switchedBeforeEvent = switchTime !== null && switchTime <= eventTime;
    const startsTimer = needsFocus && hasApp && !switchedBeforeEvent && (ready === null || ready > eventTime);
    const deadline = startsTimer ? eventTime + TIMEOUT : null;
    const resolution = [ready, switchTime].filter(value => value !== null && value >= eventTime);
    const resolvedAt = resolution.length ? Math.min(...resolution) : null;
    const reportsAnr = startsTimer && (resolvedAt === null || resolvedAt > deadline);
    const stopTime = startsTimer ? Math.min(deadline, resolvedAt === null ? Infinity : resolvedAt) : null;
    const preTime = options.preAnr && startsTimer ? deadline - Math.max(TIMEOUT / 2, 2000) : null;
    const duration = Math.max(8500, eventTime + TIMEOUT + 1400, ready === null ? 0 : ready + 800);
    const events = [];
    const add = (id, time, title, description, refs, kind = 'info') => events.push({ id, time, title, description, refs, kind });

    add('application', 0, hasApp ? '焦点应用已是 A，焦点窗口仍为空' : '当前没有焦点应用，也没有焦点窗口',
      hasApp ? 'WMS 的焦点应用身份与输入焦点窗口通过不同路径更新。知道“应该等谁”，不代表已有能接收事件的窗口。' : '此时 InputDispatcher 没有可以归属的焦点应用。没有窗口本身不会自动启动 ANR 计时。', ['application']);
    if (fixedWindow) add('ineligible', 200, '窗口可见，但 FocusResolver 拒绝授予焦点',
      '窗口带有 NOT_FOCUSABLE。可见、可触摸和可接收焦点是不同条件；屏幕有内容也可能没有输入焦点窗口。', ['focusability'], 'warn');
    if (ready !== null && ready <= eventTime) add('focus', ready, '窗口在事件前获得输入焦点',
      '示意中窗口已进入输入窗口列表，焦点请求解析成功。事件到来后无需等待窗口。', ['focusability', 'reset'], 'ok');
    if (hasEvent) {
      if (pointer) add('touch', eventTime, '普通触摸按坐标找到窗口',
        '本例是普通 pointer 触摸。目标查找走触摸命中路径，即使 focusedWindow 为空，也不启动 no-focused-window 计时。', ['routing'], 'ok');
      else if (!hasApp) add('drop', eventTime, '查找失败：没有可等待的焦点应用',
        'focusedApplication 和 focusedWindow 都为空，findFocusedWindowTargetLocked 返回 FAILED，不创建等待状态。', ['arm'], 'warn');
      else if (!startsTimer) add('dispatch', eventTime, switchedBeforeEvent ? '按键到达时，B 已有焦点窗口' : '按键直接向已聚焦窗口分发',
        '焦点目标已经存在。本页只追踪“等待焦点窗口”这一分支，后续应用处理与 ACK 属于另一种输入 ANR。', ['routing', 'arm'], 'ok');
      else add('arm', eventTime, '按键找不到焦点窗口，开始等待',
        '查找同时满足“焦点应用存在、焦点窗口为空”。保存应用身份、displayId 与事件信息，deadline = 当前查找时刻 + 应用 dispatchingTimeout；返回 PENDING，事件尚未发给应用。', ['arm'], 'warn');
    }
    if (startsTimer && stopTime > eventTime + 700) add('waiting', Math.min(eventTime + 1200, (eventTime + stopTime) / 2), '事件停在 InputDispatcher，等待窗口',
      'mPendingEvent 保留待分发事件，Looper 等待下一次唤醒。计时由首次焦点目标查找启动，不从应用启动、首帧开始或 eventTime 字段直接起算。', ['pending', 'arm'], 'warn');
    if (preTime !== null && preTime < stopTime) add('pre-anr', preTime, '可选 pre-ANR 预警，不等于正式 ANR',
      '仅在对应 feature flag 启用时：进入截止前 max(timeout / 2, 默认预警窗口) 的区间后通知 policy。本例 5 s 等待中，2.5 s 时预警；它不自行取消等待，也不代表弹窗。', ['pre-anr'], 'warn');
    if (switchTime !== null) add('switch', switchTime, startsTimer ? '焦点应用 A → B，旧等待被取消' : '焦点应用 A → B，B 的窗口已就绪',
      startsTimer ? 'setFocusedApplicationLocked 更新身份并清除等待。B 获得焦点时，enqueueFocusEventLocked 将原 pending 事件放回队头，使它有机会向新窗口分发。本例排除触摸触发的 BLOCKED 裁剪；清除计时不等于丢弃事件。' : '焦点身份变为 B，B 同时具备可接收输入的焦点窗口。示意事件尚未到达，还没有针对 A 的等待；之后输入按新的焦点状态查找目标。', ['reset'], 'ok');
    if (ready !== null && ready > eventTime) add('focus', ready, reportsAnr ? '窗口终于获得焦点，但上报已经发生' : '窗口及时获得焦点，解除等待',
      reportsAnr ? '焦点恢复不能倒退已经发出的超时通知。后续是否继续归因、展示对话框或终止进程，由 Java policy / AMS 的当时状态与策略决定。' : 'FocusResolver 解析出新焦点，onFocusChangedLocked 清除等待状态。示意选择未暂停、未丢弃输入的窗口，待分发事件随后可以找到目标。', ['focusability', 'reset'], 'ok');
    if (reportsAnr) {
      add('timeout', deadline, '到期后复查：仍是 A，仍无焦点窗口',
        'processAnrsLocked 在到期后的调度循环中检查。processNoFocusedWindowAnrLocked 再核对应用 token 与该 display 的窗口；两个条件仍成立，才调用 onAnrLocked。这里把检查画在 deadline 上，实际执行受调度影响。', ['timeout'], 'danger');
      add('policy', deadline + 450, '向 Java policy 上报，进入 ANR 处理链',
        'onAnrLocked 排入 command，执行时释放 dispatcher 锁，经 NativeInputManager / JNI 到 InputManagerService，再到 WMS AnrController。WMS 还会排除已停止或未知 Activity，可能改归因给超时的焦点请求窗口。不是无条件立即弹窗。', ['report', 'policy'], 'danger');
    }
    if (!hasEvent) add('idle', 7000, '已过 7 秒，仍没有这一路 ANR 计时',
      '没有进入焦点目标查找的输入事件，就没有 no-focused-window 等待状态。应用也可能因其他 ANR 类型或系统问题出错，这里不作扩展。', ['arm'], 'info');
    events.sort((a, b) => a.time - b.time);
    return { scenario, eventTime, ready, hasApp, hasEvent, pointer, fixedWindow, needsFocus, startsTimer, deadline, stopTime, preTime, reportsAnr, switchTime, duration, events, timeout: TIMEOUT };
  }

  function at(model, time) {
    const t = Math.max(0, Math.min(model.duration, Number.isFinite(time) ? time : 0));
    const afterEvent = model.hasEvent && t >= model.eventTime;
    const switched = model.switchTime !== null && t >= model.switchTime;
    const windowReady = model.ready !== null && t >= model.ready;
    const timedOut = model.reportsAnr && t >= model.deadline;
    const waiting = model.startsTimer && t >= model.eventTime && t < model.stopTime;
    const timerWasArmed = model.startsTimer && t >= model.eventTime;
    const elapsed = timerWasArmed ? Math.max(0, Math.min(t, model.stopTime) - model.eventTime) : 0;
    const preNotified = model.preTime !== null && model.preTime < model.stopTime && t >= model.preTime;
    let event = '尚未到达';
    let location = '输入源';
    if (!model.hasEvent) { event = '无焦点事件'; location = '无事件'; }
    else if (afterEvent) {
      if (switched) { event = '开始分发'; location = 'B 的焦点窗口'; }
      else if (!model.hasApp) { event = 'FAILED'; location = '无焦点目标'; }
      else if (model.pointer) { event = '按坐标分发'; location = '可触摸窗口'; }
      else if (timedOut) { event = '等待已超时'; location = '未向 A 分发该等待事件'; }
      else if (windowReady) { event = '开始分发'; location = '焦点窗口'; }
      else { event = 'PENDING'; location = 'mPendingEvent'; }
    }
    const timer = timedOut ? '已到期并上报' : waiting ? '等待中' : timerWasArmed ? '已清除' : '未启动';
    const activeIndex = Math.max(0, model.events.findLastIndex(item => item.time <= t));
    return { t, afterEvent, switched, windowReady, timedOut, waiting, elapsed, preNotified,
      application: switched ? 'B' : model.hasApp ? 'A' : 'null',
      focusedWindow: switched ? 'B / Window' : windowReady ? 'A / Window' : 'null',
      visible: switched || windowReady || model.fixedWindow && t >= 200,
      focusReason: switched || windowReady ? 'OK' : model.fixedWindow && t >= 200 ? 'NOT_FOCUSABLE' : 'NO_WINDOW',
      event, location, timer, progress: elapsed / model.timeout,
      remaining: waiting ? Math.max(0, model.timeout - elapsed) : null,
      activeIndex, active: model.events[activeIndex], timerWasArmed,
      policyReached: timedOut && t >= model.deadline + 450,
    };
  }

  return { scenarios, build, at };
});
