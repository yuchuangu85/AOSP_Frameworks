'use strict';

const assert = require('node:assert/strict');
const test = require('node:test');
const { scenarios, build, at } = require('../src/model.js');

const LABEL = {
  idle: '\u672a\u542f\u52a8',
  waiting: '\u7b49\u5f85\u4e2d',
  cleared: '\u5df2\u6e05\u9664',
  reported: '\u5df2\u5230\u671f\u5e76\u4e0a\u62a5',
  dispatch: '\u5f00\u59cb\u5206\u53d1',
  expired: '\u7b49\u5f85\u5df2\u8d85\u65f6',
  touch: '\u6309\u5750\u6807\u5206\u53d1',
};

const EXPECTED = {
  timeout: { startsTimer: true, reportsAnr: true, stopTime: 6000 },
  recovery: { startsTimer: true, reportsAnr: false, stopTime: 3400 },
  'not-focusable': { startsTimer: true, reportsAnr: true, stopTime: 6000 },
  'no-event': { startsTimer: false, reportsAnr: false, stopTime: null },
  pointer: { startsTimer: false, reportsAnr: false, stopTime: null },
  'no-app': { startsTimer: false, reportsAnr: false, stopTime: null },
  switch: { startsTimer: true, reportsAnr: false, stopTime: 3500 },
};

function eventsOf(model, id) {
  return model.events.filter(event => event.id === id);
}

function eventOf(model, id) {
  const matches = eventsOf(model, id);
  assert.equal(matches.length, 1, `${model.scenario.id}: expected one ${id} event`);
  return matches[0];
}

function assertIdle(state) {
  assert.equal(state.timerWasArmed, false);
  assert.equal(state.waiting, false);
  assert.equal(state.timedOut, false);
  assert.equal(state.policyReached, false);
  assert.equal(state.preNotified, false);
  assert.equal(state.elapsed, 0);
  assert.equal(state.progress, 0);
  assert.equal(state.remaining, null);
  assert.equal(state.timer, LABEL.idle);
}

function sampleTimes(model) {
  const boundaries = [0, 200, model.eventTime, model.ready, model.preTime,
    model.switchTime, model.stopTime, model.deadline, model.duration,
    ...model.events.map(event => event.time)];
  return [...new Set(boundaries.filter(Number.isFinite)
    .flatMap(time => [time - 1, time, time + 1]))]
    .filter(time => time >= 0 && time <= model.duration)
    .sort((a, b) => a - b);
}

test('the CommonJS API exposes exactly the seven intended scenarios', () => {
  assert.equal(typeof build, 'function');
  assert.equal(typeof at, 'function');
  assert.equal(scenarios.length, 7);
  assert.deepEqual(scenarios.map(scenario => scenario.id).sort(),
    Object.keys(EXPECTED).sort());
  assert.equal(new Set(scenarios.map(scenario => scenario.id)).size, 7);
  assert.equal(build().scenario.id, 'timeout');
  assert.equal(build({ scenario: 'unknown' }).scenario.id, 'timeout');
});

for (const [scenario, expected] of Object.entries(EXPECTED)) {
  test(`${scenario}: default outcome and event schedule`, () => {
    const model = build({ scenario });
    assert.equal(model.scenario.id, scenario);
    assert.equal(model.eventTime, 1000);
    assert.equal(model.timeout, 5000);
    for (const [key, value] of Object.entries(expected)) {
      assert.equal(model[key], value, `${scenario}.${key}`);
    }
    assert.equal(model.deadline, expected.startsTimer ? 6000 : null);
    assert.equal(model.hasApp, scenario !== 'no-app');
    assert.equal(model.hasEvent, scenario !== 'no-event');
    assert.equal(model.pointer, scenario === 'pointer');
    assert.equal(model.needsFocus, !['no-event', 'pointer'].includes(scenario));
    assert.equal(model.fixedWindow, ['not-focusable', 'pointer'].includes(scenario));
    assert.equal(eventsOf(model, 'arm').length, Number(expected.startsTimer));
    assert.equal(eventsOf(model, 'timeout').length, Number(expected.reportsAnr));
    assert.equal(eventsOf(model, 'policy').length, Number(expected.reportsAnr));
    assert.equal(new Set(model.events.map(event => event.id)).size, model.events.length);
    for (let i = 0; i < model.events.length; i++) {
      const event = model.events[i];
      assert.ok(Number.isFinite(event.time));
      assert.ok(event.time >= 0 && event.time <= model.duration);
      assert.ok(event.refs.length > 0);
      if (i > 0) assert.ok(model.events[i - 1].time <= event.time);
    }
    const end = at(model, model.duration);
    assert.equal(end.waiting, false);
    assert.equal(end.timedOut, expected.reportsAnr);
    assert.equal(end.policyReached, expected.reportsAnr);
  });
}

test('an absent focused window alone does not arm the timer before target lookup', () => {
  const model = build({ scenario: 'timeout', eventTime: 4000, preAnr: true });
  for (const time of [0, 200, 1000, 2500, 3999]) {
    const state = at(model, time);
    assert.equal(state.application, 'A');
    assert.equal(state.focusedWindow, 'null');
    assert.equal(state.afterEvent, false);
    assertIdle(state);
  }
  assert.equal(eventOf(model, 'arm').time, 4000);
  const lookup = at(model, 4000);
  assert.equal(lookup.timerWasArmed, true);
  assert.equal(lookup.waiting, true);
  assert.equal(lookup.event, 'PENDING');
  assert.equal(lookup.location, 'mPendingEvent');
  assert.equal(lookup.elapsed, 0);
  assert.equal(lookup.remaining, 5000);
  assert.equal(model.deadline, 9000);
});

test('the five-second wait expires at its deadline, before policy delivery', () => {
  const model = build({ scenario: 'timeout' });
  const pending = at(model, 5999);
  assert.equal(pending.waiting, true);
  assert.equal(pending.elapsed, 4999);
  assert.equal(pending.remaining, 1);
  assert.equal(pending.progress, 4999 / 5000);
  assert.equal(pending.timedOut, false);
  assert.equal(pending.timer, LABEL.waiting);
  const expired = at(model, 6000);
  assert.equal(expired.waiting, false);
  assert.equal(expired.timedOut, true);
  assert.equal(expired.event, LABEL.expired);
  assert.equal(expired.timer, LABEL.reported);
  assert.equal(expired.elapsed, 5000);
  assert.equal(expired.remaining, null);
  assert.equal(expired.progress, 1);
  assert.equal(expired.policyReached, false);
  assert.equal(eventOf(model, 'timeout').time, 6000);
  assert.equal(eventOf(model, 'policy').time, 6450);
  assert.equal(at(model, 6449).policyReached, false);
  assert.equal(at(model, 6450).policyReached, true);
});

test('timely focus recovery clears the wait and dispatches the pending event', () => {
  const model = build({ scenario: 'recovery', preAnr: true });
  assert.equal(at(model, 3399).event, 'PENDING');
  assert.equal(at(model, 3399).focusedWindow, 'null');
  assert.equal(eventOf(model, 'focus').time, 3400);
  for (const time of [3400, 6000, model.duration]) {
    const state = at(model, time);
    assert.equal(state.windowReady, true);
    assert.equal(state.focusedWindow, 'A / Window');
    assert.equal(state.waiting, false);
    assert.equal(state.timerWasArmed, true);
    assert.equal(state.timer, LABEL.cleared);
    assert.equal(state.event, LABEL.dispatch);
    assert.equal(state.elapsed, 2400);
    assert.equal(state.progress, 2400 / 5000);
    assert.equal(state.remaining, null);
    assert.equal(state.timedOut, false);
    assert.equal(state.policyReached, false);
    assert.equal(state.preNotified, false);
  }
});

test('a visible NOT_FOCUSABLE window still times out for a focused key event', () => {
  const model = build({ scenario: 'not-focusable' });
  assert.equal(at(model, 199).visible, false);
  for (const time of [200, 999, 1000, 5999, 6000, model.duration]) {
    const state = at(model, time);
    assert.equal(state.visible, true);
    assert.equal(state.windowReady, false);
    assert.equal(state.focusedWindow, 'null');
    assert.equal(state.focusReason, 'NOT_FOCUSABLE');
  }
  assert.equal(at(model, 5999).event, 'PENDING');
  assert.equal(at(model, 6000).timedOut, true);
  assert.equal(at(model, model.duration).policyReached, true);
});

for (const scenario of ['pointer', 'no-event', 'no-app']) {
  test(`${scenario}: no no-focused-window timer or ANR is created`, () => {
    const model = build({ scenario, preAnr: true });
    assert.equal(model.startsTimer, false);
    assert.equal(model.deadline, null);
    assert.equal(model.stopTime, null);
    assert.equal(model.preTime, null);
    for (const id of ['arm', 'waiting', 'pre-anr', 'timeout', 'policy']) {
      assert.equal(eventsOf(model, id).length, 0, `${scenario}: unexpected ${id}`);
    }
    for (const time of sampleTimes(model)) assertIdle(at(model, time));
    const state = at(model, model.eventTime);
    if (scenario === 'pointer') {
      assert.equal(state.event, LABEL.touch);
      assert.equal(state.focusedWindow, 'null');
      assert.equal(state.visible, true);
      assert.equal(eventOf(model, 'touch').time, model.eventTime);
    } else if (scenario === 'no-app') {
      assert.equal(state.application, 'null');
      assert.equal(state.focusedWindow, 'null');
      assert.equal(state.event, 'FAILED');
      assert.equal(eventOf(model, 'drop').time, model.eventTime);
    } else {
      assert.equal(state.afterEvent, false);
      assert.equal(model.hasEvent, false);
      assert.equal(eventsOf(model, 'dispatch').length, 0);
      assert.equal(eventOf(model, 'idle').time, 7000);
    }
  });
}

test('switching from A to B cancels an existing wait without reporting A', () => {
  const model = build({ scenario: 'switch', preAnr: true });
  assert.equal(model.switchTime, 3500);
  assert.equal(model.stopTime, 3500);
  assert.equal(eventOf(model, 'switch').time, 3500);
  assert.equal(at(model, 3499).waiting, true);
  for (const time of [3500, 3501, 6000, model.duration]) {
    const state = at(model, time);
    assert.equal(state.switched, true);
    assert.equal(state.application, 'B');
    assert.equal(state.focusedWindow, 'B / Window');
    assert.equal(state.focusReason, 'OK');
    assert.equal(state.visible, true);
    assert.equal(state.event, LABEL.dispatch);
    assert.equal(state.location, 'B \u7684\u7126\u70b9\u7a97\u53e3');
    assert.equal(state.waiting, false);
    assert.equal(state.timer, LABEL.cleared);
    assert.equal(state.elapsed, 2500);
    assert.equal(state.preNotified, false);
    assert.equal(state.timedOut, false);
    assert.equal(state.policyReached, false);
  }
});

for (const eventTime of [3501, 4000]) {
  test(`switch: an event at ${eventTime}ms must not arm a wait after B has focus`, () => {
    const model = build({ scenario: 'switch', eventTime, preAnr: true });
    const switched = at(model, 3500);
    assert.equal(switched.application, 'B');
    assert.equal(switched.focusedWindow, 'B / Window');
    assert.equal(switched.afterEvent, false);
    assert.equal(model.startsTimer, false, 'B already has a focused window before lookup');
    assert.equal(model.deadline, null);
    assert.equal(model.stopTime, null);
    assert.equal(model.reportsAnr, false);
    for (const id of ['arm', 'waiting', 'pre-anr', 'timeout', 'policy']) {
      assert.equal(eventsOf(model, id).length, 0, `unexpected ${id} for cancelled A`);
    }
    for (const time of [eventTime, eventTime + 2500, eventTime + 5000, model.duration]) {
      const state = at(model, time);
      assert.equal(state.focusedWindow, 'B / Window');
      assertIdle(state);
    }
  });
}

test('pre-ANR is disabled by default and by an explicit false flag', () => {
  for (const scenario of Object.keys(EXPECTED)) {
    for (const options of [{ scenario }, { scenario, preAnr: false }]) {
      const model = build(options);
      assert.equal(model.preTime, null);
      assert.equal(eventsOf(model, 'pre-anr').length, 0);
      for (const time of sampleTimes(model)) assert.equal(at(model, time).preNotified, false);
    }
  }
});

test('enabled pre-ANR notifies halfway through the wait, not at application start', () => {
  for (const eventTime of [500, 1000, 4000]) {
    const model = build({ scenario: 'not-focusable', eventTime, preAnr: true });
    const preTime = eventTime + 2500;
    assert.equal(model.preTime, preTime);
    assert.equal(eventOf(model, 'pre-anr').time, preTime);
    assert.equal(at(model, preTime - 1).preNotified, false);
    const state = at(model, preTime);
    assert.equal(state.preNotified, true);
    assert.equal(state.waiting, true);
    assert.equal(state.timedOut, false);
    assert.equal(state.policyReached, false);
    assert.equal(state.event, 'PENDING');
    assert.equal(state.elapsed, 2500);
    assert.equal(state.remaining, 2500);
    assert.equal(state.progress, 0.5);
    const disabled = build({ scenario: 'not-focusable', eventTime, preAnr: false });
    assert.equal(model.deadline, disabled.deadline);
    assert.equal(model.stopTime, disabled.stopTime);
    assert.equal(model.reportsAnr, disabled.reportsAnr);
  }
});

for (const ready of [3499, 3500, 3501]) {
  test(`pre-ANR: recovery at ${ready}ms only notifies if still waiting at 3500ms`, () => {
    const model = build({ scenario: 'recovery', ready, preAnr: true });
    const shouldNotify = ready > 3500;
    assert.equal(model.preTime, 3500);
    assert.equal(eventsOf(model, 'pre-anr').length, Number(shouldNotify));
    assert.equal(at(model, 3499).preNotified, false);
    assert.equal(at(model, 3500).preNotified, shouldNotify);
    assert.equal(at(model, model.duration).preNotified, shouldNotify);
    assert.equal(at(model, ready).waiting, false);
    assert.equal(model.reportsAnr, false);
  });
}

test('switch cancels future pre-ANR but preserves an already-delivered pre-ANR', () => {
  for (const [eventTime, notified] of [[500, true], [1000, false], [2000, false]]) {
    const model = build({ scenario: 'switch', eventTime, preAnr: true });
    assert.equal(eventsOf(model, 'pre-anr').length, Number(notified));
    for (const time of [3500, 6000, model.duration]) {
      const state = at(model, time);
      assert.equal(state.preNotified, notified);
      assert.equal(state.waiting, false);
      assert.equal(state.timedOut, false);
      assert.equal(state.policyReached, false);
    }
  }
});

for (const ready of [6001, 6449, 6450, 7400]) {
  test(`late focus at ${ready}ms cannot erase the timeout or queued policy report`, () => {
    const model = build({ scenario: 'timeout', ready, preAnr: true });
    assert.equal(model.reportsAnr, true);
    assert.equal(eventOf(model, 'timeout').time, 6000);
    assert.equal(eventOf(model, 'policy').time, 6450);
    assert.equal(at(model, 6000).timedOut, true);
    for (const time of [ready, Math.max(ready, 6450), model.duration]) {
      const state = at(model, time);
      assert.equal(state.windowReady, true);
      assert.equal(state.focusedWindow, 'A / Window');
      assert.equal(state.event, LABEL.expired);
      assert.notEqual(state.event, LABEL.dispatch);
      assert.equal(state.waiting, false);
      assert.equal(state.timedOut, true);
      assert.equal(state.timer, LABEL.reported);
      assert.equal(state.elapsed, 5000);
      assert.equal(state.progress, 1);
      assert.equal(state.preNotified, true);
      assert.equal(state.policyReached, time >= 6450);
    }
  });
}

for (const ready of [0, 999, 1000, 1001, 5999, 6000, 6001, 8000]) {
  test(`ready=${ready}ms: event and deadline boundaries have deterministic precedence`, () => {
    const model = build({ scenario: 'recovery', eventTime: 1000, ready });
    const startsTimer = ready > 1000;
    const reportsAnr = ready > 6000;
    assert.equal(model.ready, ready);
    assert.equal(model.startsTimer, startsTimer);
    assert.equal(model.deadline, startsTimer ? 6000 : null);
    assert.equal(model.stopTime, startsTimer ? Math.min(ready, 6000) : null);
    assert.equal(model.reportsAnr, reportsAnr);
    assert.equal(eventOf(model, 'focus').time, ready);
    assert.equal(eventsOf(model, 'arm').length, Number(startsTimer));
    assert.equal(eventsOf(model, 'timeout').length, Number(reportsAnr));
    assert.equal(at(model, 999).timerWasArmed, false);
    const arrived = at(model, 1000);
    assert.equal(arrived.waiting, startsTimer);
    assert.equal(arrived.event, startsTimer ? 'PENDING' : LABEL.dispatch);
    const focused = at(model, Math.max(1000, ready));
    assert.equal(focused.focusedWindow, 'A / Window');
    assert.equal(focused.waiting, false);
    assert.equal(focused.event, reportsAnr ? LABEL.expired : LABEL.dispatch);
    assert.equal(at(model, model.duration).policyReached, reportsAnr);
    if (!startsTimer) assertIdle(focused);
  });
}

test('event times throughout 500-4000ms shift lookup, pre-ANR and deadline together', () => {
  for (let eventTime = 500; eventTime <= 4000; eventTime += 125) {
    const model = build({ scenario: 'not-focusable', eventTime, preAnr: true });
    assert.equal(model.eventTime, eventTime);
    assert.equal(model.timeout, 5000);
    assert.equal(model.deadline, eventTime + 5000);
    assert.equal(model.stopTime, eventTime + 5000);
    assert.equal(eventOf(model, 'arm').time, eventTime);
    assert.equal(eventOf(model, 'pre-anr').time, eventTime + 2500);
    assert.equal(eventOf(model, 'timeout').time, eventTime + 5000);
    assert.equal(eventOf(model, 'policy').time, eventTime + 5450);
    assertIdle(at(model, eventTime - 1));
    assert.equal(at(model, eventTime).elapsed, 0);
    assert.equal(at(model, eventTime).waiting, true);
    assert.equal(at(model, eventTime + 4999).remaining, 1);
    assert.equal(at(model, eventTime + 5000).timedOut, true);
    assert.ok(model.duration >= eventTime + 5450);
  }
});

test('eventTime clamps finite inputs to 500-4000ms and defaults non-finite inputs', () => {
  const cases = [[-100, 500], [0, 500], [499, 500], [500, 500],
    [500.5, 500.5], [1000, 1000], [3999.5, 3999.5], [4000, 4000],
    [4001, 4000], [100000, 4000], [undefined, 1000], [null, 1000],
    [NaN, 1000], [Infinity, 1000], [-Infinity, 1000], ['2000', 1000]];
  for (const [input, expected] of cases) {
    const model = build({ scenario: 'not-focusable', eventTime: input });
    assert.equal(model.eventTime, expected, `eventTime=${String(input)}`);
    assert.equal(model.deadline, expected + 5000);
    assert.equal(eventOf(model, 'arm').time, expected);
  }
});

for (const scenario of Object.keys(EXPECTED)) {
  test(`${scenario}: one playhead cannot be both focused/cancelled and waiting`, () => {
    for (const eventTime of [500, 1000, 3499, 3500, 3501, 4000]) {
      for (const preAnr of [false, true]) {
        const model = build({ scenario, eventTime, preAnr });
        for (const time of sampleTimes(model)) {
          const state = at(model, time);
          const context = `${scenario}, eventTime=${eventTime}, preAnr=${preAnr}, t=${time}`;
          assert.equal(state.active, model.events[state.activeIndex], context);
          assert.ok(state.active.time <= state.t, context);
          const next = model.events[state.activeIndex + 1];
          if (next) assert.ok(next.time > state.t, context);
          assert.ok(state.elapsed >= 0 && state.elapsed <= 5000, context);
          assert.equal(state.progress, state.elapsed / 5000, context);
          if (state.waiting) {
            assert.equal(state.afterEvent, true, context);
            assert.equal(state.timerWasArmed, true, context);
            assert.equal(state.application, 'A', context);
            assert.equal(state.focusedWindow, 'null', context);
            assert.equal(state.switched, false, context);
            assert.equal(state.timedOut, false, context);
            assert.equal(state.event, 'PENDING', context);
            assert.equal(state.location, 'mPendingEvent', context);
            assert.equal(state.timer, LABEL.waiting, context);
            assert.equal(state.remaining, 5000 - state.elapsed, context);
            assert.ok(state.remaining > 0, context);
          } else {
            assert.equal(state.remaining, null, context);
            assert.notEqual(state.event, 'PENDING', context);
          }
          if (state.focusedWindow !== 'null') {
            assert.equal(state.visible, true, context);
            assert.equal(state.focusReason, 'OK', context);
            assert.equal(state.waiting, false, context);
          }
          if (state.timedOut) {
            assert.equal(state.timerWasArmed, true, context);
            assert.equal(state.waiting, false, context);
            assert.equal(state.event, LABEL.expired, context);
            assert.equal(state.timer, LABEL.reported, context);
            assert.equal(state.elapsed, 5000, context);
          }
          if (state.policyReached) assert.equal(state.timedOut, true, context);
          if (state.preNotified) {
            assert.equal(preAnr, true, context);
            assert.equal(state.timerWasArmed, true, context);
            assert.ok(state.t >= model.preTime, context);
          }
          if (state.switched) {
            assert.equal(state.application, 'B', context);
            assert.equal(state.focusedWindow, 'B / Window', context);
            assert.equal(state.waiting, false, context);
            assert.equal(state.timedOut, false, context);
            assert.equal(state.policyReached, false, context);
          }
        }
      }
    }
  });
}

test('at is seekable and deterministic without mutating the model or scenario registry', () => {
  const registry = structuredClone(scenarios);
  for (const scenario of Object.keys(EXPECTED)) {
    const model = build({ scenario, preAnr: true });
    const original = structuredClone(model);
    const forward = sampleTimes(model).map(time => at(model, time));
    for (const state of forward.toReversed()) assert.deepEqual(at(model, state.t), state);
    for (const state of forward) assert.deepEqual(at(model, state.t), state);
    assert.deepEqual(model, original);
  }
  assert.deepEqual(scenarios, registry);
});

test('at clamps out-of-range playheads and defaults non-finite inputs to zero', () => {
  const model = build({ preAnr: true });
  for (const time of [-1000, -1, undefined, null, NaN, Infinity, -Infinity, '6000']) {
    assert.deepEqual(at(model, time), at(model, 0), `time=${String(time)}`);
  }
  assert.deepEqual(at(model, model.duration + 1000), at(model, model.duration));
});
