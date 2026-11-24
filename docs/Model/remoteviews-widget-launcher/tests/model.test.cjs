'use strict';

const assert = require('node:assert/strict');
const test = require('node:test');
const api = require('../src/model.js');
const { scenarios, build, at } = api;

const SCENARIOS = ['first', 'reapply', 'reinflate', 'async', 'partial', 'offline', 'error', 'fallback'];
const EXISTING = new Set(['reapply', 'reinflate', 'partial', 'offline', 'fallback']);
const REUSE = new Set(['reapply', 'partial', 'offline']);
const DEFAULT_END = {
  first: [5700, 6300, 7700, 10000],
  reapply: [null, 5900, 7300, 10000],
  reinflate: [5700, 6300, 7700, 10000],
  async: [7300, 7550, 8950, 10000],
  partial: [null, 5900, 7300, 10000],
  offline: [null, 7900, 9300, 10100],
  error: [5700, 6300, 7700, 10000],
  fallback: [6200, 6800, 8200, 10000],
};
const TIMING_OPTIONS = [
  {},
  { background: 600, renderDelay: 400 },
  { background: 4000, renderDelay: 2600 },
  { background: 1234.5, renderDelay: 999.5 },
];

function eventOf(model, id) {
  const matches = model.events.filter(event => event.id === id);
  assert.equal(matches.length, 1, `expected exactly one ${id}`);
  return matches[0];
}

function sampleTimes(model) {
  const boundaries = [0, model.duration, ...Object.values(model.times),
    ...model.events.map(event => event.time)].filter(Number.isFinite);
  return [...new Set(boundaries.flatMap(time => [time - 0.5, time, time + 0.5]))]
    .filter(time => time >= 0 && time <= model.duration)
    .sort((a, b) => a - b);
}

function assertContent(actual, expected, message) {
  assert.deepEqual(actual, expected, message);
}

test('CommonJS exposes the RemoteWidgetModel API and all eight unique scenarios', () => {
  assert.deepEqual(Object.keys(api).sort(), ['at', 'build', 'scenarios']);
  assert.equal(typeof build, 'function');
  assert.equal(typeof at, 'function');
  assert.deepEqual(scenarios.map(scenario => scenario.id), SCENARIOS);
  assert.equal(new Set(scenarios.map(scenario => scenario.id)).size, 8);
  for (const scenario of scenarios) {
    assert.equal(scenario.existing, EXISTING.has(scenario.id));
    assert.ok(scenario.label.length > 0);
    assert.ok(scenario.description.length > 0);
  }
});

test('build defaults to first and unknown scenario ids fall back without coercion', () => {
  const expected = build();
  assert.equal(expected.scenario.id, 'first');
  assert.equal(expected.background, 2000);
  assert.equal(expected.renderDelay, 1400);
  for (const scenario of [undefined, null, '', 'unknown', 'FIRST', 0, {}, ['async']]) {
    assert.deepEqual(build({ scenario }), expected);
  }
});

for (const scenario of SCENARIOS) {
  test(`${scenario}: default timeline and branch-specific events`, () => {
    const model = build({ scenario });
    const [tree, commit, screen, duration] = DEFAULT_END[scenario];
    assert.equal(model.scenario.id, scenario);
    assert.equal(model.reuse, REUSE.has(scenario));
    for (const flag of ['async', 'partial', 'offline', 'error', 'fallback']) {
      assert.equal(model[flag], flag === scenario);
    }
    assert.equal(model.times.create, 1200);
    assert.equal(model.times.actions, 1700);
    assert.equal(model.times.submit, 2300);
    assert.equal(model.times.cache, 2900);
    assert.equal(model.times.host, scenario === 'offline' ? 6500 : 4500);
    assert.equal(model.times.select, scenario === 'offline' ? 6900 : 4900);
    assert.equal(model.times.prepare, scenario === 'offline' ? 7300 : 5300);
    assert.equal(model.times.tree, tree);
    assert.equal(model.times.apply, commit);
    assert.equal(model.times.commit, commit);
    assert.equal(model.times.screen, screen);
    assert.equal(model.duration, duration);
    const expectedIds = ['context', 'trigger', 'create', 'actions', 'submit', 'cache',
      ...(scenario === 'offline' ? ['deferred', 'reconnect', 'replay'] : ['notify', 'callback']),
      'host', 'select', 'prepare', ...(scenario === 'fallback' ? ['fallback'] : []),
      ...(REUSE.has(scenario) ? [] : ['tree']), 'commit', 'traversal', 'draw', 'screen'];
    assert.deepEqual(model.events.map(event => event.id), expectedIds);
    assert.equal(eventOf(model, 'context').time, 0);
    assert.equal(eventOf(model, 'trigger').time, 700);
    for (let index = 0; index < model.events.length; index++) {
      const event = model.events[index];
      assert.ok(Number.isFinite(event.time));
      assert.ok(event.time >= 0 && event.time <= model.duration);
      for (const field of ['actor', 'title', 'description', 'ref', 'kind']) {
        assert.equal(typeof event[field], 'string');
        assert.ok(event[field].length > 0);
      }
      if (index) assert.ok(model.events[index - 1].time <= event.time);
      if (Object.hasOwn(model.times, event.id)) {
        assert.equal(event.time, model.times[event.id]);
      }
    }
  });

  test(`${scenario}: constructing the descriptor and recording Actions create no target View`, () => {
    const model = build({ scenario });
    for (const time of [0, model.times.create - 0.5, model.times.create,
      model.times.actions - 0.5, model.times.actions, model.times.submit - 0.5]) {
      const state = at(model, time);
      assert.equal(state.created, time >= 1200);
      assert.equal(state.actionsReady, time >= 1700);
      assert.equal(state.providerActionCount, time < 1700 ? 0 : scenario === 'partial' ? 1 : 3);
      assert.equal(state.submitted, false);
      assert.equal(state.cached, false);
      assert.equal(state.prepared, false);
      assert.equal(state.newTreeCreated, false);
      assert.equal(state.applied, false);
      assert.equal(state.tree, EXISTING.has(scenario) ? 'T0' : null);
      assertContent(state.cache, model.old);
      assertContent(state.localContent, model.old);
      assertContent(state.visible, model.old);
      assert.equal(state.serviceHasView, false);
    }
  });

  test(`${scenario}: submit and service cache acceptance are not host receipt or application`, () => {
    const model = build({ scenario });
    const submitted = at(model, model.times.submit);
    assert.equal(submitted.submitted, true);
    assert.equal(submitted.cached, false);
    assertContent(submitted.cache, model.old);
    const beforeCache = at(model, model.times.cache - 0.5);
    assertContent(beforeCache.cache, model.old);
    for (const time of [model.times.cache, model.times.host - 0.5]) {
      const state = at(model, time);
      assert.equal(state.cached, true);
      assert.equal(state.cache.revision, 1);
      assert.equal(state.cache.count, 3);
      assertContent(state.cache, model.target);
      assert.equal(state.cacheActionCount, 3);
      assert.equal(state.hostReceived, false);
      assert.equal(state.prepared, false);
      assert.equal(state.applied, false);
      assert.equal(state.screenUpdated, false);
      assertContent(state.localContent, model.old);
      assertContent(state.visible, model.old);
    }
    const host = at(model, model.times.host);
    assert.equal(host.hostReceived, true);
    assert.equal(host.committed, false);
    assert.equal(host.applied, false);
  });

  test(`${scenario}: state transitions are inclusive at their exact boundary`, () => {
    const model = build({ scenario });
    const transitions = [
      ['created', model.times.create], ['actionsReady', model.times.actions],
      ['submitted', model.times.submit], ['cached', model.times.cache],
      ['hostReceived', model.times.host], ['committed', model.times.commit],
      ['traversed', model.times.traversal], ['drawn', model.times.draw],
      ['screenUpdated', model.times.screen],
    ];
    if (scenario === 'offline') transitions.push(['hostListening', model.times.reconnect]);
    else transitions.push(['callbackQueued', model.times.notify], ['callbackReceived', model.times.callback]);
    if (scenario !== 'error') transitions.push(['applied', model.times.commit]);
    if (!REUSE.has(scenario) && scenario !== 'error') {
      transitions.push(['prepared', model.times.tree], ['newTreeCreated', model.times.tree]);
    }
    if (scenario === 'async') transitions.push(['listenerApplied', model.times.commit]);
    for (const [flag, time] of transitions) {
      assert.equal(at(model, time - 0.5)[flag], false, `${flag} before ${time}`);
      assert.equal(at(model, time)[flag], true, `${flag} at ${time}`);
      assert.equal(at(model, time + 0.5)[flag], true, `${flag} after ${time}`);
    }
    for (const event of model.events) {
      assert.equal(at(model, event.time).active.id, event.id);
      if (event.time > 0) assert.notEqual(at(model, event.time - 0.5).active.id, event.id);
    }
  });

  test(`${scenario}: apply and attachment share a stage, not the screen-update stage`, () => {
    const model = build({ scenario });
    assert.equal(model.times.apply, model.times.commit);
    assert.ok(model.times.commit < model.times.traversal);
    assert.ok(model.times.traversal < model.times.draw);
    assert.ok(model.times.draw < model.times.screen);
    const before = at(model, model.times.commit - 0.5);
    assert.equal(before.committed, false);
    assert.equal(before.applied, false);
    assertContent(before.localContent, model.old);
    for (const time of [model.times.apply, model.times.commit, model.times.traversal,
      model.times.draw, model.times.screen - 0.5]) {
      const state = at(model, time);
      assert.equal(state.committed, true);
      assert.equal(state.applied, scenario !== 'error');
      assert.equal(state.listenerApplied, scenario === 'async');
      assert.equal(state.localContent.tree, scenario === 'error' ? 'ERROR' : model.target.tree);
      assert.equal(state.screenUpdated, false);
      assertContent(state.visible, model.old);
    }
    const screen = at(model, model.times.screen);
    assert.equal(screen.screenUpdated, true);
    assert.equal(screen.traversed, true);
    assert.equal(screen.drawn, true);
    assertContent(screen.visible, screen.localContent);
    assert.equal(screen.visible.tree, scenario === 'error' ? 'ERROR' : model.target.tree);
  });

  test(`${scenario}: service owns no View and ownership and state invariants hold across timings`, () => {
    for (const options of TIMING_OPTIONS) {
      const model = build({ ...options, scenario });
      for (const time of sampleTimes(model)) {
        const state = at(model, time);
        const context = `${scenario} at ${time}`;
        assert.equal(state.serviceHasView, false, context);
        assert.equal(state.treeOwner, state.tree === null ? null : 'host', context);
        assert.equal(state.newTreeCreated, !REUSE.has(scenario) && scenario !== 'error'
          && time >= model.times.tree, context);
        assert.equal(state.listenerApplied, scenario === 'async' && state.applied, context);
        assertContent(state.cache, state.cached ? model.target : model.old, context);
        assert.equal(state.cacheActionCount, state.cache === null ? 0 : 3, context);
        assertContent(state.visible, state.screenUpdated ? state.localContent : model.old, context);
        if (!state.committed) assertContent(state.localContent, model.old, context);
        if (state.applied) {
          assert.equal(state.hostReceived, true, context);
          assert.equal(state.committed, true, context);
          assertContent(state.localContent, model.target, context);
        }
        if (state.hostReceived) assert.equal(state.cached, true, context);
        if (state.cached) assert.equal(state.submitted, true, context);
        if (state.submitted) assert.equal(state.actionsReady, true, context);
        if (state.actionsReady) assert.equal(state.created, true, context);
        if (state.drawn) assert.equal(state.traversed, true, context);
        if (state.traversed) assert.equal(state.committed, true, context);
        if (state.screenUpdated) assert.equal(state.drawn, true, context);
        assert.ok(state.status.length > 0, context);
        const eligible = model.events.filter(event => event.time <= time);
        assert.equal(state.activeIndex, eligible.length - 1, context);
        assertContent(state.active, eligible.at(-1), context);
      }
    }
  });

  test(`${scenario}: seek is deterministic in either direction and does not mutate the model`, () => {
    for (const options of TIMING_OPTIONS) {
      const model = build({ ...options, scenario });
      const original = structuredClone(model);
      const times = sampleTimes(model);
      const expected = times.map(time => structuredClone(at(model, time)));
      const order = [...times.keys()].reverse();
      order.push(...times.keys());
      order.push(...times.map((_, index) => index).filter(index => index % 2));
      for (const index of order) {
        assert.deepEqual(at(model, times[index]), expected[index], `seek to ${times[index]}`);
      }
      assert.deepEqual(model, original);
      assert.deepEqual(build({ ...options, scenario }), original);
    }
  });

  test(`${scenario}: background duration moves only the async dependent chain`, () => {
    const base = build({ scenario, background: 2000 });
    const changed = build({ scenario, background: 3400 });
    assert.equal(changed.background, 3400);
    if (scenario !== 'async') {
      assert.deepEqual({ ...changed, background: 2000 }, base);
      return;
    }
    const dependent = new Set(['tree', 'apply', 'commit', 'traversal', 'draw', 'screen']);
    for (const [name, time] of Object.entries(base.times)) {
      assert.equal(changed.times[name], dependent.has(name) ? time + 1400 : time, name);
    }
    for (const event of base.events) {
      assert.deepEqual(eventOf(changed, event.id), {
        ...event, time: event.time + (dependent.has(event.id) ? 1400 : 0),
      });
    }
    assert.deepEqual(changed.old, base.old);
    assert.deepEqual(changed.target, base.target);
    assert.equal(changed.duration, 11150);
    assert.equal(at(changed, base.times.tree).prepared, false);
    assert.equal(at(changed, changed.times.tree).prepared, true);
    assert.equal(at(changed, base.times.commit).applied, false);
    assert.equal(at(changed, changed.times.commit).applied, true);
    for (const time of [0, base.times.create, base.times.prepare]) {
      assert.deepEqual(at(changed, time), at(base, time));
    }
  });

  test(`${scenario}: render delay moves only traversal, draw, screen and the duration tail`, () => {
    const base = build({ scenario, renderDelay: 1400 });
    const changed = build({ scenario, renderDelay: 2600 });
    const shifts = { traversal: 300, draw: 720, screen: 1200 };
    for (const [name, time] of Object.entries(base.times)) {
      assert.equal(changed.times[name], Object.hasOwn(shifts, name) ? time + shifts[name] : time, name);
    }
    for (const event of base.events) {
      assert.deepEqual(eventOf(changed, event.id), { ...event, time: event.time + (shifts[event.id] || 0) });
    }
    assert.deepEqual(changed.old, base.old);
    assert.deepEqual(changed.target, base.target);
    assert.equal(changed.duration, Math.max(10000, changed.times.screen + 800));
    for (const time of [0, base.times.cache, base.times.host, base.times.commit]) {
      assert.deepEqual(at(changed, time), at(base, time));
    }
    assert.equal(at(changed, base.times.screen).screenUpdated, false);
    assertContent(at(changed, base.times.screen).visible, base.old);
    assert.equal(at(changed, changed.times.screen).screenUpdated, true);
  });

  test(`${scenario}: playhead clamps finite inputs and rejects non-finite or coerced values`, () => {
    const model = build({ scenario });
    const start = at(model, 0);
    const end = at(model, model.duration);
    for (const value of [-Number.MAX_VALUE, -1, -0.5]) {
      assert.deepEqual(at(model, value), start);
    }
    for (const value of [model.duration + 0.5, Number.MAX_VALUE]) {
      assert.deepEqual(at(model, value), end);
    }
    for (const value of [undefined, null, NaN, Infinity, -Infinity, '4500', '', true,
      false, {}, [], [4500], new Number(4500), 4500n, Symbol('time')]) {
      assert.deepEqual(at(model, value), start);
    }
    assert.deepEqual(at(model), start);
    for (const time of [0, 1200.25, model.times.commit - 0.25, model.duration]) {
      assert.equal(at(model, time).t, time);
    }
  });
}

for (const scenario of SCENARIOS.filter(id => id !== 'offline')) {
  test(`${scenario}: queued notification and Binder callback do not imply UI handling`, () => {
    const model = build({ scenario });
    assert.equal(model.times.notify, 3500);
    assert.equal(model.times.callback, 4100);
    assert.equal(model.times.reconnect, null);
    assert.equal(model.times.replay, null);
    const queued = at(model, model.times.notify);
    assert.equal(queued.callbackQueued, true);
    assert.equal(queued.callbackReceived, false);
    assert.equal(queued.hostReceived, false);
    for (const time of [model.times.callback, model.times.host - 0.5]) {
      const state = at(model, time);
      assert.equal(state.callbackQueued, true);
      assert.equal(state.callbackReceived, true);
      assert.equal(state.hostReceived, false);
      assert.equal(state.prepared, false);
      assert.equal(state.applied, false);
      assertContent(state.localContent, model.old);
      assertContent(state.visible, model.old);
    }
  });
}

test('first: a new host-owned T1 is prepared before it replaces the empty content', () => {
  const model = build({ scenario: 'first' });
  assert.equal(model.old, null);
  assert.equal(model.target.tree, 'T1');
  assert.equal(model.target.layout, 'widget_tasks');
  assert.equal(at(model, model.times.tree - 0.5).tree, null);
  const prepared = at(model, model.times.tree);
  assert.equal(prepared.tree, 'T1');
  assert.equal(prepared.treeOwner, 'host');
  assert.equal(prepared.newTreeCreated, true);
  assert.equal(prepared.prepared, true);
  assert.equal(prepared.localContent, null);
  assert.equal(prepared.applied, false);
  assert.equal(prepared.visible, null);
  assertContent(at(model, model.times.commit).localContent, model.target);
  assertContent(at(model, model.times.screen).visible, model.target);
});

for (const scenario of REUSE) {
  test(`${scenario}: the existing T0 is reused, including partial and reconnect updates`, () => {
    const model = build({ scenario });
    assert.equal(model.old.tree, 'T0');
    assert.equal(model.target.tree, 'T0');
    assert.equal(model.times.tree, null);
    assert.equal(model.events.some(event => event.id === 'tree'), false);
    for (const time of sampleTimes(model)) {
      const state = at(model, time);
      assert.equal(state.tree, 'T0');
      assert.equal(state.prepared, false);
      assert.equal(state.newTreeCreated, false);
      assert.equal(state.treeOwner, 'host');
      assert.equal(state.localContent.revision, time < model.times.commit ? 0 : 1);
      assert.equal(state.visible.revision, time < model.times.screen ? 0 : 1);
    }
  });
}

test('full versus partial: service cache replacement or merge is independent of host reuse', () => {
  const full = build({ scenario: 'reapply' });
  const partial = build({ scenario: 'partial' });
  const newLayout = build({ scenario: 'reinflate' });
  assert.equal(full.partial, false);
  assert.equal(partial.partial, true);
  assert.equal(newLayout.partial, false);
  for (const model of [full, partial, newLayout]) {
    assert.equal(at(model, model.times.actions).providerActionCount, model === partial ? 1 : 3);
    const before = at(model, model.times.cache - 0.5);
    const cached = at(model, model.times.cache);
    assert.equal(before.cache.revision, 0);
    assert.equal(before.cache.count, 1);
    assert.equal(cached.cache.revision, 1);
    assert.equal(cached.cache.count, 3);
    assert.equal(cached.cacheActionCount, 3);
    assert.equal(cached.localContent.tree, 'T0');
    assert.equal(cached.localContent.count, 1);
    assert.equal(cached.hostReceived, false);
  }
  assert.equal(partial.target.title, partial.old.title);
  assert.equal(partial.target.layout, partial.old.layout);
  assert.deepEqual(full.target, partial.target);
  assert.equal(full.reuse, true);
  assert.equal(partial.reuse, true);
  assert.equal(newLayout.reuse, false);
  assert.equal(at(full, full.times.commit).newTreeCreated, false);
  assert.equal(at(partial, partial.times.commit).newTreeCreated, false);
  assert.equal(at(newLayout, newLayout.times.commit).newTreeCreated, true);
});

test('reinflate: a new layout creates T1 without prematurely replacing the old T0 content', () => {
  const model = build({ scenario: 'reinflate' });
  assert.equal(model.old.layout, 'widget_tasks');
  assert.equal(model.target.layout, 'widget_tasks_wide');
  assert.notEqual(model.old.title, model.target.title);
  assert.equal(model.old.tree, 'T0');
  assert.equal(model.target.tree, 'T1');
  assert.equal(at(model, model.times.tree - 0.5).tree, 'T0');
  for (const time of [model.times.tree, model.times.commit - 0.5]) {
    const state = at(model, time);
    assert.equal(state.tree, 'T1');
    assert.equal(state.newTreeCreated, true);
    assert.equal(state.applied, false);
    assertContent(state.localContent, model.old);
    assertContent(state.visible, model.old);
  }
  assertContent(at(model, model.times.commit).localContent, model.target);
  assertContent(at(model, model.times.commit).visible, model.old);
  assertContent(at(model, model.times.screen).visible, model.target);
});

test('async: background preparation is not applied; callback and attachment complete in one UI stage', () => {
  for (const options of TIMING_OPTIONS) {
    const model = build({ ...options, scenario: 'async' });
    assert.equal(eventOf(model, 'prepare').actor, 'executor');
    assert.equal(eventOf(model, 'tree').actor, 'executor');
    assert.equal(eventOf(model, 'commit').actor, 'host');
    assert.equal(at(model, model.times.prepare).prepared, false);
    for (const time of [model.times.tree, model.times.commit - 0.5]) {
      const state = at(model, time);
      assert.equal(state.prepared, true);
      assert.equal(state.tree, 'T1');
      assert.equal(state.applied, false);
      assert.equal(state.listenerApplied, false);
      assert.equal(state.committed, false);
      assert.equal(state.localContent, null);
      assert.equal(state.visible, null);
    }
    const commit = at(model, model.times.commit);
    assert.equal(commit.applied, true);
    assert.equal(commit.listenerApplied, true);
    assert.equal(commit.committed, true);
    assertContent(commit.localContent, model.target);
    assert.equal(commit.visible, null);
    assert.deepEqual(model.events.filter(event => event.time === model.times.commit)
      .map(event => event.id), ['commit']);
    for (const time of sampleTimes(model).filter(time => time >= model.times.commit && time < model.times.screen)) {
      const state = at(model, time);
      assert.equal(state.listenerApplied, true);
      assert.equal(state.visible, null);
      assert.equal(state.screenUpdated, false);
    }
  }
});

test('offline: no callback is queued or received, even after reconnect and snapshot replay', () => {
  const model = build({ scenario: 'offline' });
  assert.equal(model.times.notify, null);
  assert.equal(model.times.callback, null);
  assert.equal(model.times.reconnect, 6000);
  assert.equal(model.times.replay, 6200);
  assert.equal(model.events.some(event => ['notify', 'callback'].includes(event.id)), false);
  assert.equal(eventOf(model, 'deferred').time, 3500);
  assert.equal(eventOf(model, 'reconnect').actor, 'host');
  assert.equal(eventOf(model, 'replay').actor, 'service');
  assert.ok(model.times.cache < model.times.reconnect);
  assert.ok(model.times.reconnect < model.times.replay);
  assert.ok(model.times.replay < model.times.host);
  for (const time of sampleTimes(model)) {
    const state = at(model, time);
    assert.equal(state.callbackQueued, false);
    assert.equal(state.callbackReceived, false);
    assert.equal(state.hostListening, time >= model.times.reconnect);
    if (time >= model.times.cache && time < model.times.host) {
      assertContent(state.cache, model.target);
      assertContent(state.localContent, model.old);
      assertContent(state.visible, model.old);
      assert.equal(state.hostReceived, false);
    }
  }
  assert.equal(at(model, model.times.replay).hostReceived, false);
  assert.equal(at(model, model.times.host).hostReceived, true);
  assert.equal(at(model, model.times.commit).tree, 'T0');
  assertContent(at(model, model.duration).visible, model.target);
});

test('error: transport succeeds but target application never succeeds; the screen gets an error placeholder', () => {
  const model = build({ scenario: 'error' });
  assert.equal(eventOf(model, 'tree').kind, 'danger');
  assert.equal(eventOf(model, 'commit').kind, 'danger');
  assert.equal(eventOf(model, 'screen').kind, 'danger');
  for (const time of sampleTimes(model)) {
    const state = at(model, time);
    assert.equal(state.applied, false);
    assert.equal(state.listenerApplied, false);
    assert.equal(state.prepared, false);
    assert.equal(state.newTreeCreated, false);
    if (time >= model.times.cache) assertContent(state.cache, model.target);
    if (time >= model.times.host) assert.equal(state.hostReceived, true);
    if (time < model.times.commit) {
      assert.equal(state.tree, null);
      assert.equal(state.localContent, null);
    } else {
      assert.equal(state.tree, 'ERROR');
      assert.equal(state.localContent.tree, 'ERROR');
      assert.equal(state.localContent.revision, 'error');
      assert.equal(state.localContent.count, null);
      assert.ok(state.localContent.title.length > 0);
      assert.notDeepEqual(state.localContent, model.target);
    }
    if (time < model.times.screen) assert.equal(state.visible, null);
    else assert.equal(state.visible.tree, 'ERROR');
  }
});

test('fallback: failed reapply precedes fresh apply, which creates T1 and eventually succeeds', () => {
  const model = build({ scenario: 'fallback' });
  const failed = eventOf(model, 'fallback');
  assert.equal(model.fallback, true);
  assert.equal(model.reuse, false);
  assert.equal(failed.time, 5500);
  assert.equal(failed.kind, 'wait');
  assert.equal(failed.ref, 'host-apply');
  assert.ok(model.times.prepare < failed.time);
  assert.ok(failed.time < model.times.tree);
  assert.ok(model.times.tree < model.times.commit);
  for (const time of [model.times.prepare, failed.time, model.times.tree - 0.5]) {
    const state = at(model, time);
    assert.equal(state.tree, 'T0');
    assert.equal(state.newTreeCreated, false);
    assert.equal(state.applied, false);
    assertContent(state.localContent, model.old);
    assertContent(state.visible, model.old);
  }
  const fresh = at(model, model.times.tree);
  assert.equal(fresh.tree, 'T1');
  assert.equal(fresh.newTreeCreated, true);
  assert.equal(fresh.applied, false);
  assertContent(fresh.localContent, model.old);
  const committed = at(model, model.times.commit);
  assert.equal(committed.applied, true);
  assert.equal(committed.tree, 'T1');
  assertContent(committed.localContent, model.target);
  assertContent(committed.visible, model.old);
  assertContent(at(model, model.times.screen).visible, model.target);
});

for (const [option, min, max, fallback] of [
  ['background', 600, 4000, 2000], ['renderDelay', 400, 2600, 1400],
]) {
  test(`${option}: finite values clamp at both ends and retain fractional precision`, () => {
    for (const [value, expected] of [
      [-Number.MAX_VALUE, min], [-1, min], [0, min], [min - 0.5, min],
      [min, min], [min + 0.25, min + 0.25], [max, max], [max + 0.5, max], [Number.MAX_VALUE, max],
    ]) {
      const actual = build({ scenario: 'async', [option]: value });
      assert.equal(actual[option], expected);
      assert.deepEqual(actual, build({ scenario: 'async', [option]: expected }));
    }
  });

  test(`${option}: invalid types, NaN and infinities use defaults without coercion`, () => {
    const expected = build({ scenario: 'async', [option]: fallback });
    for (const value of [undefined, null, NaN, Infinity, -Infinity, '1000', '', true,
      false, {}, [], [1000], new Number(1000), 1000n, Symbol(option)]) {
      assert.deepEqual(build({ scenario: 'async', [option]: value }), expected);
    }
  });
}

test('combined timing parameters preserve the dependency chain and extend the playback tail', () => {
  const model = build({ scenario: 'async', background: 4000, renderDelay: 2600 });
  assert.equal(model.times.prepare, 5300);
  assert.equal(model.times.tree, 9300);
  assert.equal(model.times.apply, 9550);
  assert.equal(model.times.commit, 9550);
  assert.equal(model.times.traversal, 10200);
  assert.equal(model.times.draw, 11110);
  assert.equal(model.times.screen, 12150);
  assert.equal(model.duration, 12950);
  assert.equal(at(model, 10000).screenUpdated, false);
  assertContent(at(model, model.duration).visible, model.target);
});
