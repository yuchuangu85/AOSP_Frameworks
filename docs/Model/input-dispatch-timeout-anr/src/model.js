(function (root, factory) {
  const api = factory();
  if (typeof module === 'object' && module.exports) module.exports = api;
  else root.InputTimeoutModel = api;
})(typeof globalThis === 'object' ? globalThis : this, function () {
  'use strict';

  const scenarios = [
    { id: 'looper', label: 'Looper 被其他任务占用', delay: 6500, ackDelay: 100, cause: '已发入通道，但接收线程尚未消费', result: '超时后恢复' },
    { id: 'handler', label: '输入处理链耗时', delay: 6500, ackDelay: 100, cause: '事件已进入应用处理，处理链迟迟不结束', result: '超时后恢复' },
    { id: 'deferred', label: '延迟调用 finish', delay: 6500, ackDelay: 100, cause: '可见处理已结束，但接收器或异步阶段尚未 finish', result: '超时后恢复' },
    { id: 'ack-delay', label: 'finish 后 ACK 尚未清理', delay: 600, ackDelay: 6000, cause: 'finish 已调用，Dispatcher 尚未完成 FINISHED 清理', result: '超时后恢复' },
    { id: 'normal', label: '正常完成', delay: 600, ackDelay: 100, cause: '处理、finish、服务端清理均在截止前完成', result: '不触发 ANR' },
    { id: 'unhandled', label: 'handled = false 仍正常完成', delay: 600, ackDelay: 100, cause: 'Motion 未被业务消费，但 FINISHED 仍确认本次完成', result: '不触发 ANR' },
    { id: 'multi', label: '多个在途事件，各有截止时间', delay: null, ackDelay: null, cause: 'E1 的 ACK 只删除 E1；E2、E3 仍独立等待', result: 'E2 触发超时' },
    { id: 'changed-timeout', label: '新事件使用更短 timeout', delay: null, ackDelay: null, cause: 'E2 更晚发送，却有更早 deadline；reason 仍描述最早发送的 E1', result: 'E2 到期 / E1 入日志' },
  ];
  const clamp = (value, min, max, fallback) => Number.isFinite(value) ? Math.max(min, Math.min(max, value)) : fallback;

  function build(options = {}) {
    const scenario = scenarios.find(item => item.id === options.scenario) || scenarios[0];
    const delay = clamp(options.delay, 400, 8000, scenario.delay || 6500);
    const ackDelay = clamp(options.ackDelay, 100, 6500, scenario.ackDelay || 100);
    const timeout = clamp(options.timeout, 1000, 8000, 5000);
    let entries;
    const create = (id, seq, delivery, start, workEnd, finish, ack, limit, action, handled = true) => ({
      id, seq, delivery, start, workEnd, finish, ack, timeout: limit, deadline: delivery + limit,
      enqueue: delivery - 250, eventTime: Math.max(0, delivery - 600), action, handled,
    });
    if (scenario.id === 'multi') {
      entries = [create('E1', 101, 1000, 1150, 1600, 1600, 1700, 5000, 'DOWN'),
        create('E2', 102, 1500, 1800, 7600, 7600, 7700, 5000, 'MOVE'),
        create('E3', 103, 1900, 7800, 8100, 8100, 8200, 5000, 'UP')];
    } else if (scenario.id === 'changed-timeout') {
      entries = [create('E1', 101, 1000, 1150, 7500, 7500, 7600, 8000, 'DOWN'),
        create('E2', 102, 2200, 7700, 8100, 8100, 8200, 2000, 'UP')];
    } else {
      const delivery = 1000;
      const finish = delivery + delay;
      const start = scenario.id === 'looper' ? finish - 250 : delivery + 100;
      const workEnd = scenario.id === 'deferred' || scenario.id === 'ack-delay' ? delivery + 300 : finish;
      entries = [create('E1', 101, delivery, start, workEnd, finish, finish + ackDelay, timeout, 'DOWN', scenario.id !== 'unhandled')];
    }
    const lastAck = Math.max(...entries.map(entry => entry.ack));
    const customTiming = scenario.delay !== null && (delay !== scenario.delay || ackDelay !== scenario.ackDelay || timeout !== 5000);
    const description = customTiming ? `自定义参数：发布后 ${(delay / 1000).toFixed(2)} s 调用 finish，再经 ${(ackDelay / 1000).toFixed(2)} s 清理 ACK；窗口 timeout 为 ${(timeout / 1000).toFixed(2)} s。` : scenario.cause;
    const model = { scenario, entries, delay, ackDelay, timeout, customTiming, description, duration: Math.max(10000, lastAck + 500), schedule: [] };
    const push = (id, time, title, description, ref, entry, kind = 'info') => model.schedule.push({ id, time, title, description, ref, entry: entry && entry.id, kind });
    push('origin', 0, '目标窗口已存在，本页追踪已发送连接', '示意选择一个正常注册的窗口连接 A。输入通过 InputChannel 的 socket 传输，不是每个事件都走 Binder；不涉及 no-focused-window 等待。', 'publish');
    for (const entry of entries) {
      push(`enqueue-${entry.id}`, entry.enqueue, `${entry.id} 放入 outboundQueue`, '窗口目标已确定，事件正等待发布。还没发布成功的 outbound 条目不登记为本次 ACK 超时；这不代表整个系统不存在其他等待或超时。', 'publish', entry);
      push(`publish-${entry.id}`, entry.delivery, `${entry.id} 发布成功，开始等待 FINISHED`, `seq=${entry.seq}。deliveryTime=${entry.delivery} ms，deadline=deliveryTime+${entry.timeout} ms。Dispatcher 侧条目进入 waitQueue，AnrTracker 登记 (deadline, token A)。应用可能还没开始消费。`, 'publish', entry, 'wait');
      push(`consume-${entry.id}`, entry.start, `${entry.id} 进入应用的输入处理链`, '应用接收线程从 InputChannel 读取事件，经 NativeInputEventReceiver / WindowInputEventReceiver 进入 ViewRootImpl。服务端 waitQueue 条目此时仍保留，不因“已读到”而解除。', 'receive', entry, 'info');
      if (entry.workEnd < entry.finish) push(`work-${entry.id}`, entry.workEnd, `${entry.id} 可见处理结束，但尚未 finish`, '这一步只是示意的业务处理边界。接收器或异步输入阶段还未发出最终完成信号；普通 View 回调的返回不应被等同为所有输入阶段都完成。', 'finish', entry, 'wait');
      push(`finish-${entry.id}`, entry.finish, `${entry.id} 调用 finishInputEvent`, `接收器在绑定的 Looper 线程提交 seq=${entry.seq}、handled=${entry.handled}，native 发送 FINISHED。handled 表示是否消费，不是是否完成；finish 调用本身也不等于服务端记录已经移除。`, 'finish', entry, 'ok');
      push(`ack-${entry.id}`, entry.ack, `${entry.id} FINISHED 按 seq 清理`, `Dispatcher 匹配 seq=${entry.seq}，从 waitQueue 移除这个条目，并 erase 对应 (timeoutTime, token)。其他事件的等待仍保留；若连接曾超时，还需检查剩余队列是否仍有过期项。`, 'ack', entry, 'ok');
    }
    if (scenario.id === 'changed-timeout') push('config', 1800, '窗口 timeout 从 8 s 改为 2 s', '已发送 E1 仍保留原 deadline=9.0 s；新 E2 使用 2 s 得到 deadline=4.2 s。这些数值是演示配置，不是设备默认值。', 'configuration');

    const timeline = [];
    entries.forEach(entry => {
      timeline.push({ time: entry.delivery, order: 1, type: 'publish', entry });
      timeline.push({ time: entry.ack, order: 0, type: 'ack', entry });
      timeline.push({ time: entry.deadline, order: 2, type: 'check', entry });
    });
    timeline.sort((a, b) => a.time - b.time || a.order - b.order);
    let responsive = true;
    let wait = [];
    let tracked = [];
    const incidents = [];
    const recoveries = [];
    for (const event of timeline) {
      if (event.type === 'publish') {
        wait.push(event.entry);
        if (responsive) tracked.push(event.entry);
      } else if (event.type === 'ack') {
        wait = wait.filter(entry => entry.seq !== event.entry.seq);
        tracked = tracked.filter(entry => entry.seq !== event.entry.seq);
        if (!responsive && !wait.some(entry => entry.deadline < event.time)) {
          responsive = true;
          recoveries.push(event.time);
        }
      } else if (responsive && tracked.some(entry => entry.deadline <= event.time)) {
        const trigger = [...tracked].sort((a, b) => a.deadline - b.deadline || a.seq - b.seq)[0];
        const oldest = [...wait].sort((a, b) => a.delivery - b.delivery || a.seq - b.seq)[0];
        if (!oldest) continue;
        responsive = false;
        tracked = [];
        const nextAck = Math.min(...entries.filter(entry => entry.ack > event.time).map(entry => entry.ack));
        // Keep the visual command transition before the later FIFO ACK cleanup command.
        const policyTime = event.time + Math.min(200, (nextAck - event.time) / 2);
        incidents.push({ time: event.time, trigger: trigger.id, reasonEntry: oldest.id, reasonWait: event.time - oldest.delivery, policyTime });
      }
    }
    for (const incident of incidents) {
      push(`timeout-${incident.time}`, incident.time, `${incident.trigger} 的截止时间到达，连接 A 标记不响应`, `processAnrsLocked 选中最早到期项。responsive=false；eraseToken(A) 清除该连接所有 tracker 登记，停止重复唤醒，但不清空原 waitQueue。reason 使用最早发送且仍未完成的 ${incident.reasonEntry}，已等待 ${incident.reasonWait} ms。`, 'timeout', null, 'danger');
      push(`policy-${incident.time}`, incident.policyTime, '执行 unresponsive 通知，进入 Java policy', 'onAnrLocked 先前已排队通知并生成取消事件。现在按 command FIFO 执行，释放 Dispatcher 锁后经 JNI 到 IMS / WMS；后续 ACK 清理与恢复命令不会越过已排队的通知。Java 继续核对归因，并非立即无条件弹窗或杀进程。', 'policy', null, 'danger');
    }
    recoveries.forEach(time => push(`recover-${time}`, time, '剩余 waitQueue 无过期项，连接恢复响应', 'ACK 清理后 isConnectionResponsive 检查整个剩余队列。恢复通知不等于撤销之前的 ANR 历史，也不等于所有业务工作、渲染或画面已经完成。', 'recovery', null, 'ok'));
    model.incidents = incidents;
    model.recoveries = recoveries;
    model.schedule.sort((a, b) => a.time - b.time);
    return model;
  }

  function at(model, value) {
    const t = clamp(value, 0, model.duration, 0);
    const incidents = model.incidents.filter(item => item.time <= t);
    const lastIncident = incidents[incidents.length - 1] || null;
    const recovered = lastIncident && model.recoveries.some(time => time >= lastIncident.time && time <= t);
    const responsive = !lastIncident || Boolean(recovered);
    const entries = model.entries.map(entry => {
      const published = t >= entry.delivery;
      const acked = t >= entry.ack;
      const inWait = published && !acked;
      const phase = acked ? 'acked' : t >= entry.finish ? 'ack-flight' : t >= entry.workEnd ? 'deferred' : t >= entry.start ? 'handling' : published ? 'channel' : t >= entry.enqueue ? 'outbound' : 'incoming';
      const tracker = inWait && responsive && !model.incidents.some(incident => incident.time <= t && incident.time >= entry.delivery);
      return { ...entry, published, acked, inWait, phase, tracker, overdue: inWait && t > entry.deadline,
        progress: published ? Math.max(0, Math.min(1, (Math.min(t, entry.ack) - entry.delivery) / entry.timeout)) : 0 };
    });
    const waitQueue = entries.filter(entry => entry.inWait);
    const outboundQueue = entries.filter(entry => entry.phase === 'outbound');
    const tracker = entries.filter(entry => entry.tracker).sort((a, b) => a.deadline - b.deadline || a.seq - b.seq);
    const next = tracker[0] || null;
    const activeIndex = Math.max(0, model.schedule.findLastIndex(event => event.time <= t));
    const completed = entries.every(entry => entry.acked);
    const live = waitQueue.find(entry => entry.phase === 'handling' || entry.phase === 'deferred') || waitQueue[0] || entries[entries.length - 1];
    let appState = completed ? '已完成本例输入' : live.phase === 'ack-flight' ? 'finish 已提交，等待服务端清理' : live.phase === 'handling' ? '正在输入处理链中' : live.phase === 'deferred' ? '处理已结束，finish 尚未调用' : live.phase === 'channel' ? '通道已发布，Looper 尚未消费' : '等待事件发布';
    if (model.scenario.id === 'looper' && t >= 800 && t < model.entries[0].start) appState = 'Looper 被其他任务占用';
    return { t, entries, waitQueue, outboundQueue, tracker, next, responsive, incident: lastIncident,
      incidentCount: incidents.length, recovered: Boolean(recovered), policyReached: Boolean(lastIncident && t >= lastIncident.policyTime),
      completed, live, appState, activeIndex, active: model.schedule[activeIndex],
      remaining: next ? Math.max(0, next.deadline - t) : null,
      oldest: waitQueue[0] || null,
    };
  }
  return { scenarios, build, at };
});
