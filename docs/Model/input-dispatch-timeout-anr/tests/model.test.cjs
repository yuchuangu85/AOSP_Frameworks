'use strict';

const assert = require('node:assert/strict');
const test = require('node:test');
const { scenarios, build, at } = require('../src/model.js');

const SCENARIOS = [
  'looper', 'handler', 'deferred', 'ack-delay',
  'normal', 'unhandled', 'multi', 'changed-timeout',
];
const SINGLE_TIMES = {
  looper: [7250, 7500, 7500, 7600],
  handler: [1100, 7500, 7500, 7600],
  deferred: [1100, 1300, 7500, 7600],
  'ack-delay': [1100, 1300, 1600, 7600],
  normal: [1100, 1600, 1600, 1700],
  unhandled: [1100, 1600, 1600, 1700],
};
const DEFAULT_INCIDENTS = {
  looper: [6000, 'E1', 'E1', 5000, 7600],
  handler: [6000, 'E1', 'E1', 5000, 7600],
  deferred: [6000, 'E1', 'E1', 5000, 7600],
  'ack-delay': [6000, 'E1', 'E1', 5000, 7600],
  multi: [6500, 'E2', 'E2', 5000, 8200],
  'changed-timeout': [4200, 'E2', 'E1', 3200, 8200],
};

function ids(entries) {
  return entries.map(entry => entry.id);
}

function entryOf(state, id = 'E1') {
  const entry = state.entries.find(item => item.id === id);
  assert.ok(entry, `missing ${id}`);
  return entry;
}

function scheduled(model, id) {
  const events = model.schedule.filter(event => event.id === id);
  assert.equal(events.length, 1, `expected exactly one ${id}`);
  return events[0];
}

function assertQueues(state, wait, tracker, outbound = []) {
  assert.deepEqual(ids(state.waitQueue), wait, `waitQueue at ${state.t}`);
  assert.deepEqual(ids(state.tracker), tracker, `tracker at ${state.t}`);
  assert.deepEqual(ids(state.outboundQueue), outbound, `outboundQueue at ${state.t}`);
}

function sampleTimes(model) {
  const boundaries = [0, model.duration, ...model.schedule.map(event => event.time),
    ...model.entries.flatMap(entry => [entry.eventTime, entry.deadline])];
  return [...new Set(boundaries.flatMap(time => [time - 0.5, time, time + 0.5]))]
    .filter(time => time >= 0 && time <= model.duration)
    .sort((a, b) => a - b);
}

test('the CommonJS API exposes all eight unique scenarios and defaults to looper', () => {
  assert.equal(typeof build, 'function');
  assert.equal(typeof at, 'function');
  assert.deepEqual(scenarios.map(scenario => scenario.id), SCENARIOS);
  assert.equal(new Set(scenarios.map(scenario => scenario.id)).size, 8);
  assert.equal(build().scenario.id, 'looper');
  for (const scenario of ['unknown', '', null, 123]) {
    assert.deepEqual(build({ scenario }), build());
  }
});

for (const scenario of SCENARIOS) {
  test(`${scenario}: default timeline, incident, recovery and final state`, () => {
    const model = build({ scenario });
    assert.equal(model.scenario.id, scenario);
    assert.equal(model.timeout, 5000);
    assert.equal(model.duration, 10000);
    assert.equal(new Set(model.entries.map(entry => entry.seq)).size, model.entries.length);
    assert.equal(new Set(model.schedule.map(event => event.id)).size, model.schedule.length);
    assert.equal(scheduled(model, 'origin').time, 0);

    if (SINGLE_TIMES[scenario]) {
      const entry = model.entries[0];
      assert.equal(model.entries.length, 1);
      assert.equal(entry.id, 'E1');
      assert.equal(entry.seq, 101);
      assert.equal(entry.action, 'DOWN');
      assert.equal(entry.handled, scenario !== 'unhandled');
      assert.equal(entry.eventTime, 400);
      assert.equal(entry.enqueue, 750);
      assert.equal(entry.delivery, 1000);
      assert.equal(entry.deadline, 6000);
      assert.deepEqual([entry.start, entry.workEnd, entry.finish, entry.ack],
        SINGLE_TIMES[scenario]);
      assert.equal(model.delay, entry.finish - entry.delivery);
      assert.equal(model.ackDelay, entry.ack - entry.finish);
    }

    for (const entry of model.entries) {
      assert.equal(entry.deadline, entry.delivery + entry.timeout);
      assert.ok(entry.eventTime < entry.enqueue);
      assert.ok(entry.enqueue < entry.delivery);
      assert.ok(entry.delivery < entry.start);
      assert.ok(entry.start <= entry.workEnd);
      assert.ok(entry.workEnd <= entry.finish);
      assert.ok(entry.finish < entry.ack);
      assert.equal(scheduled(model, `enqueue-${entry.id}`).time, entry.enqueue);
      assert.equal(scheduled(model, `publish-${entry.id}`).time, entry.delivery);
      assert.equal(scheduled(model, `consume-${entry.id}`).time, entry.start);
      assert.equal(scheduled(model, `finish-${entry.id}`).time, entry.finish);
      assert.equal(scheduled(model, `ack-${entry.id}`).time, entry.ack);
    }
    for (let index = 0; index < model.schedule.length; index++) {
      const event = model.schedule[index];
      assert.ok(Number.isFinite(event.time));
      assert.ok(event.time >= 0 && event.time <= model.duration);
      assert.ok(event.ref);
      if (index > 0) assert.ok(model.schedule[index - 1].time <= event.time);
    }

    const expected = DEFAULT_INCIDENTS[scenario];
    if (expected) {
      const [time, trigger, reasonEntry, reasonWait, recovery] = expected;
      assert.deepEqual(model.incidents, [{ time, trigger, reasonEntry, reasonWait,
        policyTime: time + 200 }]);
      assert.deepEqual(model.recoveries, [recovery]);
      assert.equal(scheduled(model, `timeout-${time}`).time, time);
      assert.equal(scheduled(model, `policy-${time}`).time, time + 200);
      assert.equal(scheduled(model, `recover-${recovery}`).time, recovery);
    } else {
      assert.deepEqual(model.incidents, []);
      assert.deepEqual(model.recoveries, []);
      assert.equal(model.schedule.some(event => /^(timeout|policy|recover)-/.test(event.id)), false);
    }
    const end = at(model, model.duration);
    assertQueues(end, [], []);
    assert.equal(end.completed, true);
    assert.equal(end.responsive, true);
    assert.equal(end.incidentCount, expected ? 1 : 0);
    assert.equal(end.recovered, Boolean(expected));
    assert.equal(end.policyReached, Boolean(expected));
    assert.equal(end.next, null);
    assert.equal(end.remaining, null);
    assert.equal(end.oldest, null);
  });

  test(`${scenario}: outbound is not tracked, publish enters waitQueue and arms its deadline`, () => {
    const model = build({ scenario });
    for (const entry of model.entries) {
      for (const time of [entry.eventTime, entry.enqueue - 0.5]) {
        const state = at(model, time);
        const current = entryOf(state, entry.id);
        assert.equal(current.phase, 'incoming');
        assert.equal(current.published, false);
        assert.equal(current.inWait, false);
        assert.equal(current.tracker, false);
        assert.equal(current.progress, 0);
      }
      for (const time of [entry.enqueue, entry.delivery - 0.5]) {
        const state = at(model, time);
        const current = entryOf(state, entry.id);
        assert.equal(current.phase, 'outbound');
        assert.equal(current.published, false);
        assert.equal(current.inWait, false);
        assert.equal(current.tracker, false);
        assert.equal(current.progress, 0);
        assert.ok(ids(state.outboundQueue).includes(entry.id));
        assert.equal(ids(state.waitQueue).includes(entry.id), false);
        assert.equal(ids(state.tracker).includes(entry.id), false);
      }
      const state = at(model, entry.delivery);
      const published = entryOf(state, entry.id);
      assert.equal(published.phase, 'channel');
      assert.equal(published.published, true);
      assert.equal(published.acked, false);
      assert.equal(published.inWait, true);
      assert.equal(published.tracker, true);
      assert.equal(published.deadline, entry.delivery + entry.timeout);
      assert.equal(published.progress, 0);
      assert.ok(ids(state.waitQueue).includes(entry.id));
      assert.ok(ids(state.tracker).includes(entry.id));
      assert.equal(ids(state.outboundQueue).includes(entry.id), false);
    }
  });
}

for (const scenario of Object.keys(SINGLE_TIMES)) {
  test(`${scenario}: consume and finish cannot clear the server wait; only ACK does`, () => {
    const model = build({ scenario });
    const entry = model.entries[0];
    for (const time of [entry.start, entry.workEnd, entry.finish, entry.ack - 0.5]) {
      const state = at(model, time);
      const current = entryOf(state);
      assertQueues(state, ['E1'], time < entry.deadline ? ['E1'] : []);
      assert.equal(current.published, true);
      assert.equal(current.acked, false);
      assert.equal(current.inWait, true);
      assert.equal(state.completed, false);
    }
    assert.equal(entryOf(at(model, entry.start)).phase, 'handling');
    assert.equal(entryOf(at(model, entry.finish)).phase, 'ack-flight');
    const ack = at(model, entry.ack);
    assertQueues(ack, [], []);
    assert.equal(entryOf(ack).phase, 'acked');
    assert.equal(entryOf(ack).acked, true);
    assert.equal(entryOf(ack).inWait, false);
    assert.equal(ack.completed, true);
  });
}

test('looper: time in the channel counts before the application consumes the event', () => {
  const model = build({ scenario: 'looper' });
  for (const time of [1000, 5999, 6000, 7249]) {
    const state = at(model, time);
    assert.equal(entryOf(state).phase, 'channel');
    assert.equal(entryOf(state).inWait, true);
  }
  assert.equal(at(model, 5999).remaining, 1);
  assert.equal(at(model, 6000).incidentCount, 1);
  assert.equal(entryOf(at(model, 7250)).phase, 'handling');
});

test('deferred: visible work ending does not imply finish or ACK', () => {
  const model = build({ scenario: 'deferred' });
  assert.equal(entryOf(at(model, 1299)).phase, 'handling');
  for (const time of [1300, 5999, 6000, 7499]) {
    const state = at(model, time);
    assert.equal(entryOf(state).phase, 'deferred');
    assert.equal(entryOf(state).inWait, true);
    assert.equal(state.completed, false);
  }
  assert.equal(entryOf(at(model, 7500)).phase, 'ack-flight');
  assert.equal(scheduled(model, 'work-E1').time, 1300);
});

test('ack-delay: finish has already happened when the outstanding ACK causes ANR', () => {
  const model = build({ scenario: 'ack-delay' });
  for (const time of [1600, 5999, 6000, 7599]) {
    const state = at(model, time);
    assert.equal(entryOf(state).phase, 'ack-flight');
    assert.equal(entryOf(state).inWait, true);
    assert.equal(entryOf(state).acked, false);
  }
  assert.equal(at(model, 6000).responsive, false);
  assert.equal(at(model, 7600).responsive, true);
});

test('handled=false still completes the dispatched Motion entry via FINISHED', () => {
  const normal = build({ scenario: 'normal' });
  const model = build({ scenario: 'unhandled' });
  assert.equal(model.entries[0].handled, false);
  assert.equal(normal.entries[0].handled, true);
  assert.deepEqual(model.entries, normal.entries.map(entry => ({ ...entry, handled: false })));
  assertQueues(at(model, 1699), ['E1'], ['E1']);
  const ack = at(model, 1700);
  assertQueues(ack, [], []);
  assert.equal(ack.completed, true);
  assert.equal(ack.responsive, true);
  assert.equal(ack.incident, null);
  assert.deepEqual(model.incidents, []);
});

test('deadline equality triggers ANR, while overdue uses the strict greater-than boundary', () => {
  const model = build({ scenario: 'handler' });
  const before = at(model, 5999.5);
  assertQueues(before, ['E1'], ['E1']);
  assert.equal(before.remaining, 0.5);
  assert.equal(before.responsive, true);
  assert.equal(before.incident, null);
  const due = at(model, 6000);
  assertQueues(due, ['E1'], []);
  assert.equal(due.responsive, false);
  assert.equal(due.incidentCount, 1);
  assert.equal(due.incident.time, 6000);
  assert.equal(entryOf(due).overdue, false);
  assert.equal(entryOf(due).progress, 1);
  assert.equal(due.next, null);
  assert.equal(due.remaining, null);
  assert.equal(entryOf(at(model, 6000.5)).overdue, true);
  assert.equal(at(model, 6199.5).policyReached, false);
  assert.equal(at(model, 6200).policyReached, true);
});

test('multi: E1 ACK removes only seq=101 and preserves E2 plus outbound E3', () => {
  const model = build({ scenario: 'multi' });
  assert.deepEqual(model.entries.map(entry => [entry.id, entry.seq, entry.delivery, entry.deadline]),
    [['E1', 101, 1000, 6000], ['E2', 102, 1500, 6500], ['E3', 103, 1900, 6900]]);
  assertQueues(at(model, 1699), ['E1', 'E2'], ['E1', 'E2'], ['E3']);
  const ack = at(model, 1700);
  assertQueues(ack, ['E2'], ['E2'], ['E3']);
  assert.equal(entryOf(ack, 'E1').acked, true);
  assert.equal(entryOf(ack, 'E2').acked, false);
  assert.equal(ack.next.seq, 102);
  assert.equal(ack.oldest.seq, 102);
  assert.equal(ack.remaining, 4800);
  assertQueues(at(model, 1900), ['E2', 'E3'], ['E2', 'E3']);
  const oldDeadline = at(model, 6000);
  assert.equal(oldDeadline.incident, null);
  assert.equal(oldDeadline.remaining, 500);
});

test('multi: E2 expiry erases the token tracker, not the outstanding waitQueue', () => {
  const model = build({ scenario: 'multi' });
  assertQueues(at(model, 6499), ['E2', 'E3'], ['E2', 'E3']);
  const timeout = at(model, 6500);
  assertQueues(timeout, ['E2', 'E3'], []);
  assert.equal(timeout.responsive, false);
  assert.deepEqual(timeout.incident, {
    time: 6500, trigger: 'E2', reasonEntry: 'E2', reasonWait: 5000, policyTime: 6700,
  });
  assert.equal(entryOf(timeout, 'E3').overdue, false);
  for (const time of [6500.5, 6900, 6900.5, 7699]) {
    const state = at(model, time);
    assertQueues(state, ['E2', 'E3'], []);
    assert.equal(state.incidentCount, 1);
    assert.equal(state.responsive, false);
    assert.equal(state.recovered, false);
    assert.equal(state.next, null);
    assert.equal(state.remaining, null);
  }
  assert.equal(entryOf(at(model, 6900.5), 'E3').overdue, true);
});

test('multi: E2 ACK cannot recover while E3 is overdue; the final ACK recovers', () => {
  const model = build({ scenario: 'multi' });
  const secondAck = at(model, 7700);
  assertQueues(secondAck, ['E3'], []);
  assert.equal(entryOf(secondAck, 'E2').acked, true);
  assert.equal(entryOf(secondAck, 'E3').overdue, true);
  assert.equal(secondAck.oldest.seq, 103);
  assert.equal(secondAck.responsive, false);
  assert.equal(secondAck.recovered, false);
  assert.equal(secondAck.completed, false);
  assert.equal(secondAck.incidentCount, 1);
  assert.equal(at(model, 8199).responsive, false);
  const finalAck = at(model, 8200);
  assertQueues(finalAck, [], []);
  assert.equal(finalAck.responsive, true);
  assert.equal(finalAck.recovered, true);
  assert.equal(finalAck.completed, true);
  assert.equal(finalAck.incident.trigger, 'E2');
  assert.deepEqual(model.recoveries, [8200]);
});

test('changed-timeout: E2 has the earliest deadline but the reason describes older E1', () => {
  const model = build({ scenario: 'changed-timeout' });
  assert.deepEqual(model.entries.map(entry => [entry.id, entry.delivery, entry.timeout, entry.deadline]),
    [['E1', 1000, 8000, 9000], ['E2', 2200, 2000, 4200]]);
  assert.equal(scheduled(model, 'config').time, 1800);
  assert.equal(entryOf(at(model, 1800)).deadline, 9000);
  const publish = at(model, 2200);
  assertQueues(publish, ['E1', 'E2'], ['E2', 'E1']);
  assert.equal(publish.next.id, 'E2');
  assert.equal(publish.oldest.id, 'E1');
  assert.equal(publish.remaining, 2000);
  assert.equal(at(model, 4199).remaining, 1);
  assert.equal(at(model, 4199).incident, null);
  const timeout = at(model, 4200);
  assertQueues(timeout, ['E1', 'E2'], []);
  assert.deepEqual(timeout.incident, {
    time: 4200, trigger: 'E2', reasonEntry: 'E1', reasonWait: 3200, policyTime: 4400,
  });
  assert.equal(entryOf(timeout, 'E1').overdue, false);
  assert.equal(entryOf(at(model, 4200.5), 'E2').overdue, true);
  assert.equal(at(model, 4399).policyReached, false);
  assert.equal(at(model, 4400).policyReached, true);
});

test('changed-timeout: E1 ACK leaves overdue E2 unresponsive until E2 ACK', () => {
  const model = build({ scenario: 'changed-timeout' });
  const firstAck = at(model, 7600);
  assertQueues(firstAck, ['E2'], []);
  assert.equal(firstAck.responsive, false);
  assert.equal(firstAck.recovered, false);
  assert.equal(entryOf(firstAck, 'E2').overdue, true);
  assert.equal(firstAck.incident.reasonEntry, 'E1');
  const secondAck = at(model, 8200);
  assertQueues(secondAck, [], []);
  assert.equal(secondAck.responsive, true);
  assert.equal(secondAck.recovered, true);
  assert.equal(secondAck.incident.reasonEntry, 'E1');
  assert.equal(at(model, 9000).incidentCount, 1);
  assert.deepEqual(model.recoveries, [8200]);
});

// This tie-break is a deterministic teaching-model rule, not a device scheduling guarantee.
for (const [delay, ackTime, incidents] of [[899, 1999, 0], [900, 2000, 0], [901, 2001, 1]]) {
  test(`ACK at ${ackTime}ms versus deadline 2000ms: ${incidents} incident(s)`, () => {
    const model = build({ scenario: 'handler', delay, ackDelay: 100, timeout: 1000 });
    assert.equal(model.entries[0].ack, ackTime);
    assert.equal(model.entries[0].deadline, 2000);
    assert.equal(model.incidents.length, incidents);
    const due = at(model, 2000);
    assert.equal(due.incidentCount, incidents);
    assert.equal(due.responsive, incidents === 0);
    assertQueues(due, incidents ? ['E1'] : [], []);
    if (incidents) {
      assert.deepEqual(model.recoveries, [2001]);
      assert.equal(at(model, 1999.5).incident, null);
    } else {
      assert.deepEqual(model.recoveries, []);
      assert.equal(due.completed, true);
      assert.equal(due.recovered, false);
      assert.equal(model.schedule.some(event => event.id.startsWith('timeout-')), false);
    }
  });
}

for (const [scenario, expected] of Object.entries(DEFAULT_INCIDENTS)) {
  test(`${scenario}: late ACK and recovery retain the historical incident and policy stage`, () => {
    const model = build({ scenario });
    const [time, , , , recovery] = expected;
    const incident = structuredClone(at(model, time).incident);
    assert.equal(at(model, time - 0.5).incident, null);
    for (const seek of [recovery, recovery + 0.5, model.duration]) {
      const state = at(model, seek);
      assert.equal(state.responsive, true);
      assert.equal(state.recovered, true);
      assert.equal(state.completed, true);
      assert.equal(state.incidentCount, 1);
      assert.equal(state.policyReached, true);
      assert.deepEqual(state.incident, incident);
    }
    assert.deepEqual(model.incidents, [incident]);
    assert.equal(scheduled(model, `policy-${time}`).time, incident.policyTime);
  });
}

test('FIFO: unresponsive policy precedes the ACK cleanup and recovery command', () => {
  const model = build({ scenario: 'handler', delay: 901, ackDelay: 100, timeout: 1000 });
  const incident = model.incidents[0];
  assert.ok(incident.policyTime >= incident.time);
  assert.ok(incident.policyTime < model.entries[0].ack);
  const recovered = at(model, 2001);
  assert.equal(recovered.responsive, true);
  assert.equal(recovered.recovered, true);
  assert.equal(recovered.incidentCount, 1);
  assert.equal(recovered.policyReached, true);
  const policy = at(model, incident.policyTime);
  assert.equal(policy.policyReached, true);
  assert.equal(policy.recovered, false);
  assert.deepEqual(policy.incident, recovered.incident);
  assert.equal(scheduled(model, 'policy-2000').time, incident.policyTime);
  const reviewed = build({ scenario: 'handler', delay: 4900, ackDelay: 200, timeout: 5000 });
  assert.ok(reviewed.incidents[0].policyTime < reviewed.recoveries[0]);
  for (const scenario of SCENARIOS) {
    for (const delay of [400, 4900, 5001, 8000]) {
      const current = build({ scenario, delay, ackDelay: 100, timeout: 5000 });
      for (const issue of current.incidents) {
        const nextAck = Math.min(...current.entries.filter(entry => entry.ack > issue.time).map(entry => entry.ack));
        assert.ok(issue.policyTime < nextAck);
      }
    }
  }
});

test('custom parameters replace preset outcome prose with the actual timing', () => {
  const preset = build({ scenario: 'normal' });
  assert.equal(preset.description, preset.scenario.cause);
  assert.equal(preset.customTiming, false);
  const custom = build({ scenario: 'normal', delay: 6500 });
  assert.equal(custom.incidents.length, 1);
  assert.equal(custom.customTiming, true);
  assert.match(custom.description, /\u81ea\u5b9a\u4e49\u53c2\u6570/);
  assert.doesNotMatch(custom.description, /\u5747\u5728\u622a\u6b62\u524d/);
});

test('delay changes processing and ACK times without moving publish or the deadline', () => {
  const fast = build({ scenario: 'handler', delay: 600 });
  const slow = build({ scenario: 'handler', delay: 5500 });
  assert.deepEqual([fast.entries[0].finish, fast.entries[0].ack], [1600, 1700]);
  assert.deepEqual([slow.entries[0].finish, slow.entries[0].ack], [6500, 6600]);
  assert.equal(fast.entries[0].delivery, slow.entries[0].delivery);
  assert.equal(fast.entries[0].deadline, slow.entries[0].deadline);
  assert.equal(fast.entries[0].start, slow.entries[0].start);
  assert.deepEqual(fast.incidents, []);
  assert.equal(slow.incidents[0].time, 6000);
  assert.deepEqual(slow.recoveries, [6600]);
  const looper = build({ scenario: 'looper', delay: 5500 });
  assert.equal(looper.entries[0].start, 6250);
  assert.equal(entryOf(at(looper, 6000)).phase, 'channel');
});

test('ackDelay alone can turn timely finish into ANR', () => {
  const fast = build({ scenario: 'handler', delay: 600, ackDelay: 100 });
  const slow = build({ scenario: 'handler', delay: 600, ackDelay: 5000 });
  assert.equal(fast.entries[0].finish, 1600);
  assert.equal(slow.entries[0].finish, 1600);
  assert.equal(slow.entries[0].ack, 6600);
  assert.equal(fast.entries[0].deadline, slow.entries[0].deadline);
  assert.deepEqual(fast.incidents, []);
  assert.equal(slow.incidents[0].time, 6000);
  assert.equal(entryOf(at(slow, 6000)).phase, 'ack-flight');
  assert.deepEqual(slow.recoveries, [6600]);
});

test('timeout changes deadline and incident without changing the application timeline', () => {
  const short = build({ scenario: 'handler', delay: 1600, ackDelay: 100, timeout: 1000 });
  const long = build({ scenario: 'handler', delay: 1600, ackDelay: 100, timeout: 2000 });
  for (const key of ['eventTime', 'enqueue', 'delivery', 'start', 'workEnd', 'finish', 'ack']) {
    assert.equal(short.entries[0][key], long.entries[0][key], key);
  }
  assert.equal(short.entries[0].deadline, 2000);
  assert.equal(long.entries[0].deadline, 3000);
  assert.equal(short.incidents[0].time, 2000);
  assert.deepEqual(short.recoveries, [2700]);
  assert.deepEqual(long.incidents, []);
  assert.equal(at(long, 2000).remaining, 1000);
});

test('all single-event scenarios propagate custom delay, ackDelay and timeout', () => {
  for (const scenario of Object.keys(SINGLE_TIMES)) {
    const model = build({ scenario, delay: 2400.5, ackDelay: 300.25, timeout: 2000.75 });
    const entry = model.entries[0];
    assert.equal(model.delay, 2400.5, scenario);
    assert.equal(model.ackDelay, 300.25, scenario);
    assert.equal(model.timeout, 2000.75, scenario);
    assert.equal(entry.finish, 3400.5, scenario);
    assert.equal(entry.ack, 3700.75, scenario);
    assert.equal(entry.timeout, 2000.75, scenario);
    assert.equal(entry.deadline, 3000.75, scenario);
    assert.equal(entry.start, scenario === 'looper' ? 3150.5 : 1100, scenario);
    assert.equal(entry.workEnd, ['deferred', 'ack-delay'].includes(scenario) ? 1300 : 3400.5,
      scenario);
    assert.equal(model.incidents[0].time, 3000.75, scenario);
    assert.deepEqual(model.recoveries, [3700.75], scenario);
  }
});

for (const scenario of ['multi', 'changed-timeout']) {
  test(`${scenario}: fixed demonstration entries keep their per-event timing configuration`, () => {
    const baseline = build({ scenario });
    const adjusted = build({ scenario, delay: 400, ackDelay: 6500, timeout: 1000 });
    assert.deepEqual(adjusted.entries, baseline.entries);
    assert.deepEqual(adjusted.incidents, baseline.incidents);
    assert.deepEqual(adjusted.recoveries, baseline.recoveries);
  });
}

for (const [key, min, max] of [['delay', 400, 8000], ['ackDelay', 100, 6500], ['timeout', 1000, 8000]]) {
  test(`${key}: finite numbers clamp at both ends and preserve interior fractions`, () => {
    const cases = [[-Number.MAX_VALUE, min], [0, min], [min - 0.5, min], [min, min],
      [min + 0.5, min + 0.5], [max - 0.5, max - 0.5], [max, max],
      [max + 0.5, max], [Number.MAX_VALUE, max]];
    for (const [value, expected] of cases) {
      const model = build({ scenario: 'normal', [key]: value });
      assert.equal(model[key], expected, `${key}=${value}`);
      const entry = model.entries[0];
      if (key === 'delay') assert.equal(entry.finish - entry.delivery, expected);
      if (key === 'ackDelay') assert.equal(entry.ack - entry.finish, expected);
      if (key === 'timeout') assert.equal(entry.deadline - entry.delivery, expected);
    }
  });
}

const INVALID_NUMBERS = [
  ['undefined', undefined], ['NaN', NaN], ['Infinity', Infinity], ['-Infinity', -Infinity],
  ['numeric string', '1500'], ['null', null], ['boolean', true], ['object', {}],
  ['array', []], ['bigint', 1500n], ['symbol', Symbol('invalid')],
];

for (const scenario of SCENARIOS) {
  test(`${scenario}: non-finite and non-number parameters fall back without coercion`, () => {
    const baseline = build({ scenario });
    for (const key of ['delay', 'ackDelay', 'timeout']) {
      for (const [name, value] of INVALID_NUMBERS) {
        const model = build({ scenario, [key]: value });
        assert.equal(model[key], baseline[key], `${scenario}.${key}: ${name}`);
        assert.deepEqual(model.entries, baseline.entries, `${scenario}.${key}: ${name}`);
        assert.deepEqual(model.incidents, baseline.incidents, `${scenario}.${key}: ${name}`);
        assert.deepEqual(model.recoveries, baseline.recoveries, `${scenario}.${key}: ${name}`);
      }
    }
  });
}

test('duration extends to the last ACK plus recovery margin for the slowest valid parameters', () => {
  const model = build({ scenario: 'handler', delay: 8000, ackDelay: 6500, timeout: 8000 });
  assert.equal(model.entries[0].finish, 9000);
  assert.equal(model.entries[0].ack, 15500);
  assert.equal(model.entries[0].deadline, 9000);
  assert.equal(model.duration, 16000);
  assert.equal(at(model, 15499).completed, false);
  assert.equal(at(model, 15500).completed, true);
  assert.equal(at(model, 15500).recovered, true);
  assert.equal(at(model, 16000).incidentCount, 1);
});

for (const scenario of SCENARIOS) {
  test(`${scenario}: at clamps finite seek values and resets invalid values to zero`, () => {
    const model = build({ scenario });
    for (const time of [-Number.MAX_VALUE, -1, -0.5, 0]) {
      assert.deepEqual(at(model, time), at(model, 0));
    }
    for (const time of [model.duration, model.duration + 0.5, Number.MAX_VALUE]) {
      assert.deepEqual(at(model, time), at(model, model.duration));
    }
    assert.equal(at(model, 1000.5).t, 1000.5);
    for (const [name, value] of INVALID_NUMBERS) {
      at(model, model.duration);
      assert.deepEqual(at(model, value), at(model, 0), `${scenario}: ${name}`);
    }
  });

  test(`${scenario}: forward, reverse and repeated seeks are deterministic and do not mutate the model`, () => {
    const options = Object.freeze({ scenario });
    const model = build(options);
    const original = structuredClone(model);
    const catalog = structuredClone(scenarios);
    const times = sampleTimes(model);
    const snapshots = new Map(times.map(time => [time, structuredClone(at(model, time))]));
    for (const time of [...times].reverse().concat(times, [0, model.duration, 1000, 0])) {
      assert.deepEqual(at(model, time), snapshots.get(time), `${scenario} at ${time}`);
    }
    assert.deepEqual(model, original);
    assert.deepEqual(scenarios, catalog);
    assert.deepEqual(options, { scenario });
    assert.deepEqual(build(options), original);
  });

  test(`${scenario}: boundary snapshots keep queues, progress and active schedule coherent`, () => {
    const model = build({ scenario });
    for (const time of sampleTimes(model)) {
      const state = at(model, time);
      assert.equal(state.t, time);
      assert.ok(state.active.time <= time);
      assert.deepEqual(state.active, model.schedule[state.activeIndex]);
      const nextStage = model.schedule[state.activeIndex + 1];
      if (nextStage) assert.ok(nextStage.time > time);
      assert.equal(state.completed, model.entries.every(entry => entry.ack <= time));
      assert.equal(state.incidentCount, model.incidents.filter(incident => incident.time <= time).length);
      assert.equal(state.next, state.tracker[0] || null);
      assert.equal(state.oldest, state.waitQueue[0] || null);
      assert.equal(state.remaining, state.next ? Math.max(0, state.next.deadline - time) : null);
      for (const entry of state.entries) {
        assert.equal(entry.published, time >= entry.delivery);
        assert.equal(entry.acked, time >= entry.ack);
        assert.equal(entry.inWait, time >= entry.delivery && time < entry.ack);
        assert.ok(Number.isFinite(entry.progress));
        assert.ok(entry.progress >= 0 && entry.progress <= 1);
        assert.equal(entry.overdue, entry.inWait && time > entry.deadline);
        if (entry.tracker) {
          assert.equal(entry.inWait, true);
          assert.equal(state.responsive, true);
        }
        if (entry.phase === 'outbound') {
          assert.equal(entry.inWait, false);
          assert.equal(entry.tracker, false);
        }
      }
      for (let index = 1; index < state.tracker.length; index++) {
        assert.ok(state.tracker[index - 1].deadline <= state.tracker[index].deadline);
      }
    }
  });
}
