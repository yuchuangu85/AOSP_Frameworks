const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const test = require('node:test');
const vm = require('node:vm');

const context = vm.createContext({ window: {} });
for (const name of ['fp-steps.js', 'fp-presets.js', 'fp-model.js', 'fp-steptimes.js']) {
  vm.runInContext(fs.readFileSync(path.join(__dirname, '../src', name), 'utf8'), context);
}
const FP = context.window.FP;
const EPS = 1e-6;
function preset(key, overrides = {}) {
  return FP.simulate(Object.assign(FP.paramsFromPreset(key), overrides));
}
function near(actual, expected) {
  assert.ok(Math.abs(actual - expected) < EPS, `${actual} != ${expected}`);
}

test('PhaseOffsets conversion preserves the source defaults', () => {
  const sixty = preset('aosp60');
  near(sixty.aD, sixty.P);
  near(sixty.sD, sixty.P - 1);
  const fast = preset('aosp120');
  near(fast.aD, 2 * fast.P - 1);
  near(fast.sD, fast.P - 1);
});

test('normal relay preserves frame order and reaches its illustrative targets', () => {
  const sim = preset('relay');
  for (const frame of sim.shown) {
    assert.equal(frame.verdict, 'ontime');
    near(frame.present, frame.expected);
  }
});

test('BLAST layer backpressure retains a late frame instead of replacing it', () => {
  const sim = preset('uicont', { backpressure: true });
  const [first, late, following] = sim.frames;
  assert.ok(!first.dropped && !late.dropped && !following.dropped);
  assert.ok(following.latch >= late.latch + sim.P - EPS);
});

test('latest-buffer replacement is limited to the no-backpressure scenario', () => {
  const sim = preset('uicont', { backpressure: false, latchUnsignaled: false });
  assert.equal(sim.frames[1].verdict, 'dropped');
  near(sim.frames[1].latch, sim.frames[2].latch);
});

test('eligible unsignaled latch does not turn every late acquire fence into a missed refresh', () => {
  const allowed = preset('gpu', { latchUnsignaled: true }).frames[1];
  assert.ok(allowed.fence > allowed.sfTarget);
  assert.ok(allowed.fence < allowed.expected);
  near(allowed.latch, allowed.sfTarget);
  assert.equal(allowed.verdict, 'ontime');
  assert.ok(!FP.predecessors(FP.STEP.latch, allowed).includes('gpu'));
  near(FP.stepSeq(allowed).find(step => step.id === 'latch').at, allowed.latch);
  assert.ok(FP.predecessors(FP.STEP.present, allowed).includes('gpu'));
  const disabled = preset('gpu', { latchUnsignaled: false }).frames[1];
  assert.equal(disabled.verdict, 'late');
  assert.ok(FP.predecessors(FP.STEP.latch, disabled).includes('gpu'));
});

test('an acquire fence that misses presentation still delays an unsignaled-latched frame', () => {
  const frame = preset('gpu', { latchUnsignaled: true, load: { gpu: 6 } }).frames[1];
  assert.ok(frame.present >= frame.fence);
  assert.equal(frame.verdict, 'late');
});

test('multi-period SF budgets do not wait for the immediate previous present fence', () => {
  const sim = FP.simulate({ fps: 60, dur: 'custom', aD: 1000 / 120, sD: 1000 / 30,
    frames: 3, cont: true, backpressure: true, latchUnsignaled: false });
  const [previous, next] = sim.frames;
  assert.ok(previous.present > next.sfTarget);
  near(next.latch, next.sfTarget);
  assert.equal(next.verdict, 'ontime');
});

test('release delivery and release-fence signal are separate events', () => {
  const sim = preset('relay');
  const first = sim.frames[0];
  assert.ok(first.release < first.releaseFence);
  near(first.releaseFence, sim.frames[1].present);
  near(FP.stepTimes(first).release.t0, first.release);
  assert.ok(sim.slots[0].some(segment => segment.state === 'ACQUIRED'));
  assert.ok(sim.slots.every(segments => segments.every(segment =>
    !['LATCHED', 'DISPLAY'].includes(segment.state))));
});

test('a reused slot may be dequeued before its release fence permits writes', () => {
  const sim = preset('aosp120');
  const previous = sim.frames[0], reuse = sim.frames[3];
  assert.ok(reuse.dequeue0 >= previous.release - EPS);
  assert.ok(reuse.rt0 >= previous.releaseFence - EPS);
  assert.ok(reuse.dequeue0 < reuse.rt0);
  assert.ok(FP.waitBlocks(reuse).some(block => block.why.includes('release fence')));
});

test('repeated late GPU work retains a pending historically due fence as SF backpressure', () => {
  const sim = preset('gpu', { cont: true, frames: 3, load: { gpu: 6 }, latchUnsignaled: true });
  const late = sim.frames[1], following = sim.frames[2];
  assert.ok(late.present > following.sfTarget);
  assert.ok(following.latch >= late.present - EPS);
});

test('retained frames cannot silently share one display interval under a multi-period budget', () => {
  const sim = FP.simulate({ fps: 120, dur: 'custom', aD: 0.5, sD: 12.5, load: { gpu: 8 },
    loadFrame: 1, frames: 3, cont: true, backpressure: true, latchUnsignaled: true });
  const frames = sim.frames.filter(frame => !frame.dropped);
  for (let i = 1; i < frames.length; i++) assert.ok(frames[i].present >= frames[i - 1].present + sim.P - EPS);
  for (const frame of sim.shown) assert.ok(sim.display.some(interval => interval.k === frame.k));
});

test('all presets and policy branches have finite, ordered frame work and disjoint slots', () => {
  for (const scenario of FP.PRESETS) {
    for (const backpressure of [false, true]) {
      for (const latchUnsignaled of [false, true]) {
        const sim = preset(scenario.key, { backpressure, latchUnsignaled });
        const presented = sim.frames.filter(frame => !frame.dropped);
        for (let i = 1; i < presented.length; i++) assert.ok(presented[i].present >= presented[i - 1].present + sim.P - EPS);
        for (const frame of sim.frames) {
          for (const key of ['wake', 'doframe0', 'sync0', 'sync1', 'dequeue0', 'rt0', 'queue1', 'fence', 'txn', 'latch']) {
            assert.ok(Number.isFinite(frame[key]), `${scenario.key}: ${key}`);
          }
          assert.ok(frame.dequeue0 <= frame.rt0 + EPS);
          assert.ok(frame.queue1 >= frame.rt0);
          assert.ok(Number.isFinite(frame.release) === Number.isFinite(frame.releaseFence));
          if (!frame.dropped) assert.ok(frame.present >= Math.max(frame.hwc1, frame.fence) - EPS);
          const steps = FP.stepSeq(frame);
          for (let i = 1; i < steps.length; i++) assert.ok(steps[i].at >= steps[i - 1].at);
        }
        for (const segments of sim.slots) {
          for (let i = 1; i < segments.length; i++) {
            assert.ok(segments[i].t0 >= segments[i - 1].t1 - EPS, scenario.key + ': overlapping ownership');
          }
        }
      }
    }
  }
});
