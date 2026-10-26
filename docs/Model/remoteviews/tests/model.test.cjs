const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const model = require('../src/model.js');
const stages = fs.readdirSync(path.join(__dirname, '../data')).filter(file => file.endsWith('stages.json')).sort()
  .flatMap(file => JSON.parse(fs.readFileSync(path.join(__dirname, '../data', file), 'utf8')).stages);
const final = id => model.snapshot(id, model.scenario(id).events.length - 1);

test('every event maps to real authored evidence and a valid actor', () => {
  const stageIds = new Set(stages.map(stage => stage.id));
  const actors = new Set(model.actors.map(actor => actor.id));
  for (const scenario of model.scenarios) {
    assert.equal(new Set(scenario.events.map(event => event.id)).size, scenario.events.length);
    for (const event of scenario.events) {
      assert.ok(stageIds.has(event.stage), event.stage);
      assert.ok(actors.has(event.actor), event.actor);
    }
  }
});

test('all positions are finite and all actual trees are host-local', () => {
  for (const scenario of model.scenarios) {
    for (let i = -2; i < scenario.events.length + 2; i++) {
      const state = model.snapshot(scenario.id, i);
      assert.ok(Number.isFinite(state.index));
      if (state.tree) assert.match(state.tree.id, /^host-tree-/);
      assert.equal(state.screen, 'unobserved');
    }
  }
  assert.equal(model.snapshot('new', NaN).index, 0);
  assert.equal(model.snapshot('new', Infinity).index, model.scenario('new').events.length - 1);
  assert.throws(() => model.snapshot('missing', 0));
});

test('record, service acceptance and host receipt do not imply applied actions', () => {
  for (const id of ['new', 'async', 'error', 'click']) {
    const scenario = model.scenario(id);
    const state = model.snapshot(id, scenario.events.findIndex(event => event.id === 'received'));
    assert.equal(state.accepted, true);
    assert.equal(state.received, true);
    assert.equal(state.applied, false);
    assert.equal(state.tree, null);
    assert.equal(state.attached, false);
  }
});

test('XML inflate creates a separate unmodified, unattached host tree', () => {
  const state = model.snapshot('new', 5);
  assert.equal(state.prepared, true);
  assert.equal(state.tree.title, 'XML 默认文字');
  assert.equal(state.tree.listener, false);
  assert.equal(state.applied, false);
  assert.equal(state.attached, false);
});

test('apply completes actions before the caller installs new content', () => {
  const state = model.snapshot('new', 6);
  assert.equal(state.tree.title, '来自另一个进程');
  assert.equal(state.tree.listener, true);
  assert.equal(state.applied, true);
  assert.equal(state.attached, false);
  assert.equal(final('new').attached, true);
});

test('full description replacement can still reapply to the same tree and retain color', () => {
  const before = model.snapshot('reuse', 0);
  const after = final('reuse');
  assert.equal(before.tree.id, after.tree.id);
  assert.equal(after.tree.title, '来自另一个进程');
  assert.equal(after.tree.color, 'green');
  assert.equal(after.cache.some(action => action.method === 'setTextColor'), false);
});

test('partial replacement keeps independent setters and does not mutate old inputs', () => {
  const before = model.snapshot('partial', 0);
  const beforeCopy = JSON.stringify(before.cache);
  const merged = model.mergeSetters(before.cache, before.payload);
  assert.equal(JSON.stringify(before.cache), beforeCopy);
  assert.equal(merged.length, 3);
  assert.equal(merged.filter(action => action.method === 'setText').length, 1);
  assert.equal(merged.find(action => action.method === 'setTextColor').value, 'green');
  assert.equal(final('partial').tree.color, 'green');
  assert.equal(final('partial').tree.id, before.tree.id);
});

test('async preparation and onViewInflated precede final actions and completion callback', () => {
  const scenario = model.scenario('async');
  const index = id => scenario.events.findIndex(event => event.id === id);
  assert.equal(model.snapshot('async', index('queued')).tree, null);
  const inflated = model.snapshot('async', index('inflated'));
  assert.equal(inflated.prepared, true);
  assert.equal(inflated.applied, false);
  assert.equal(inflated.callback, false);
  const applied = model.snapshot('async', index('apply'));
  assert.equal(applied.applied, true);
  assert.equal(applied.callback, false);
  const callback = model.snapshot('async', index('callback'));
  assert.equal(callback.callback, true);
  assert.equal(callback.attached, false);
  assert.equal(final('async').attached, true);
});

test('failure never becomes successful content or an applied callback', () => {
  const state = final('error');
  assert.equal(state.payload[0].method, 'setId');
  assert.equal(state.error, true);
  assert.equal(state.fallback, true);
  assert.equal(state.applied, false);
  assert.equal(state.callback, false);
  assert.equal(state.tree.kind, 'error');
  assert.equal(state.attached, true);
});

test('regression: error fallback installs a real host error tree without making the update successful', () => {
  const state = final('error');
  assert.ok(state.tree, 'getErrorView creates a real View');
  assert.equal(state.tree.kind, 'error');
  assert.equal(state.attached, true, 'applyContent adds error content to the host');
  assert.equal(state.applied, false);
  assert.equal(state.screen, 'unobserved');
});

test('click capability is bound before interaction and component visibility remains unknown', () => {
  const scenario = model.scenario('click');
  const index = scenario.events.findIndex(event => event.id === 'clicked');
  assert.equal(model.snapshot('click', index - 1).clicked, false);
  const state = model.snapshot('click', index);
  assert.equal(state.tree.listener, true);
  assert.equal(state.attached, true);
  assert.equal(state.clicked, true);
  assert.equal(state.intent, false);
  assert.equal(final('click').intent, true);
  assert.equal(final('click').screen, 'unobserved');
});

test('seeking backward is pure, without old errors, callbacks or tree mutations leaking', () => {
  for (const scenario of model.scenarios) {
    const before = model.snapshot(scenario.id, 0);
    final(scenario.id);
    assert.deepEqual(model.snapshot(scenario.id, 0), before);
  }
});

test('RemoteViews separates description, serialization, host inflation and Action execution', () => {
  const scenario = model.scenario('remoteviews');
  const at = id => model.snapshot('remoteviews', scenario.events.findIndex(event => event.id === id));
  assert.equal(at('rv-construct').constructed, true);
  assert.equal(at('rv-construct').tree, null);
  assert.equal(at('rv-setter').actionRecorded, true);
  assert.equal(at('rv-setter').payload[0].method, 'setText');
  assert.equal(at('rv-setter').actionExecuted, false);
  assert.equal(at('rv-parcel').serialized, true);
  assert.equal(at('rv-parcel').transferred, false);
  assert.equal(at('rv-unparcel').deserialized, true);
  assert.equal(at('rv-unparcel').tree, null);
  assert.equal(at('rv-inflate').inflated, true);
  assert.equal(at('rv-inflate').tree.title, 'XML 默认文字');
  assert.equal(at('rv-resolve').resolved, true);
  assert.equal(at('rv-authorize').methodAllowed, true);
  assert.equal(at('rv-apply').actionExecuted, true);
  assert.equal(at('rv-apply').tree.title, '来自另一个进程');
  assert.equal(at('rv-bind-click').listenerInstalled, true);
  assert.equal(final('remoteviews').attached, true);
  assert.equal(final('remoteviews').screen, 'unobserved');
});

test('Launcher binding and container placement are distinct from content creation', () => {
  const scenario = model.scenario('launcher');
  const at = id => model.snapshot('launcher', scenario.events.findIndex(event => event.id === id));
  assert.equal(at('listen').listening, true);
  assert.equal(at('listen').widgetId, null);
  assert.equal(at('allocate').widgetId, 42);
  assert.equal(at('allocate').bound, false);
  assert.equal(at('bind').bound, true);
  assert.equal(at('bind').tree, null);
  const created = at('host-view');
  assert.equal(created.hostViewCreated, true);
  assert.equal(created.workspaceAttached, false);
  assert.equal(created.initialContent, true);
  assert.equal(created.tree, null, 'only the incoming update tree is tracked, not initialLayout');
  const placed = at('workspace');
  assert.equal(placed.workspaceAttached, true);
  assert.equal(placed.applied, false);
  assert.equal(placed.accepted, false);
  assert.equal(placed.attached, false, 'container placement does not install the new content');
  assert.equal(at('inflate').initialContent, true);
  assert.equal(at('attached').initialContent, false);
  assert.equal(at('attached').attached, true);
});

test('processes, Binder ingress and main-thread handling are explicit in Launcher steps', () => {
  const steps = model.scenario('launcher').events;
  const ids = steps.map(step => step.id);
  assert.ok(ids.indexOf('bind') < ids.indexOf('provider-callback'));
  assert.ok(ids.indexOf('broadcast') < ids.indexOf('provider-callback'));
  assert.ok(ids.indexOf('binder-callback') < ids.indexOf('received'));
  assert.match(steps.find(step => step.id === 'binder-callback').thread, /Binder/);
  assert.match(steps.find(step => step.id === 'received').thread, /主线程/);
  assert.match(steps.find(step => step.id === 'dispatch').thread, /ServiceThread/);
  assert.match(steps.find(step => step.id === 'provider-callback').thread, /Receiver.*主线程/);
  for (const step of steps) assert.ok(step.thread && step.handoff);
  assert.equal(model.actorsFor('launcher').length, 4);
  assert.equal(model.actorsFor('new').length, 3);
});

test('Launcher rendering handoffs still do not claim physical screen visibility', () => {
  const state = final('launcher');
  assert.equal(state.workspaceAttached, true);
  assert.equal(state.attached, true);
  assert.equal(state.traversal, true);
  assert.equal(state.renderSubmitted, true);
  assert.equal(state.composition, true);
  assert.equal(state.screen, 'unobserved');
  assert.equal(state.current.actor, 'compositor');
});

test('regression: Launcher fresh inflation declares why the initial content cannot be recycled', () => {
  const scenario = model.scenario('launcher');
  assert.ok(scenario.initialLayoutId && scenario.contentLayoutId,
    'initial content can be reapplied unless the selected fresh-tree policy declares incompatibility');
  assert.notEqual(scenario.initialLayoutId, scenario.contentLayoutId,
    'this teaching scenario selects layout mismatch, not a universal initial-view recreation rule');
});

test('built HTML embeds exact authored modules and fresh verified evidence', () => {
  const root = path.join(__dirname, '..');
  const html = fs.readFileSync(path.join(root, 'index.html'), 'utf8');
  for (const file of ['model.js', 'app.js', 'style.css']) {
    assert.ok(html.includes(fs.readFileSync(path.join(root, 'src', file), 'utf8')), file);
  }
  assert.equal(html.includes('/*__'), false);
  const embedded = JSON.parse(html.match(/<script id="rv-data" type="application\/json">([\s\S]*?)<\/script>/)[1]);
  assert.deepEqual(embedded.stages, stages);
  assert.equal(embedded.verification.failed, 0);
  assert.ok(embedded.verification.total > 0);
  assert.equal(embedded.modelContract.visibility, 'screen output always unobserved');
});
