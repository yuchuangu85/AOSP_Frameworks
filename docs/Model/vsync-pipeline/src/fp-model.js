/* ============================================================
   Frame Pipeline Studio · simulation
   Illustrative single-layer DEVICE-composition timeline, not a device
   predictor or a reimplementation of FrameTimeline jank classification.
   Source-derived constraints (see stage-02 / 06 / 07 / 08):
   - wakeup = vsync − workDuration − readyDuration (VSyncDispatchTimerQueue)
   - APP wake = present − aD − sD, SF wake = present − sD
   - eligible unsignaled latch is an explicit scenario assumption
   - layer backpressure retains later automatic-timestamp transactions
   - only a historically due present fence can backpressure SF
   - release delivery can precede signal; writes retain the dependency
   - exactly 3 rotating slots are a teaching assumption, not an AOSP default
   ============================================================ */
window.FP = window.FP || {};
(function (FP) {
  'use strict';
  var EPS = 1e-6;
  function gridAtOrAfter(t, base, P) { return base + Math.max(0, Math.ceil((t - base) / P - EPS)) * P; }

  FP.simulate = function (raw) {
    var p = FP.resolveParams(raw), P = p.P, aD = p.aD, sD = p.sD, C = FP.COST;
    var n0 = Math.ceil((0.8 * P + aD + sD) / P - EPS);
    var W0 = n0 * P - aD - sD, S0 = n0 * P - sD;
    var count = p.frames + (p.cont ? 1 : 0);          // one extra frame only to resolve releases / pipelining
    var F = [];
    function L(k) { return k === p.loadFrame ? p.load : { ui: 1, rt: 1, gpu: 1, sf: 1 }; }
    function relOf(j) {
      if (j < -1) return { delivered: 0, signaled: 0 };
      if (j >= 0 && F[j].dropped) return { delivered: F[j].latch + 0.08, signaled: F[j].fence };
      for (var m = j + 1; m < F.length; m++) {
        if (!F[m].dropped && F[m].present != null) {
          return { delivered: F[m].hwc1 + 0.08, signaled: F[m].present };
        }
      }
      return { delivered: Infinity, signaled: Infinity };
    }
    // FrameTargeter selects a fence that should already have signaled, not
    // necessarily the immediately preceding submission (N-VSYNC pipelines).
    function sfWakeAfter(t, replaced) {
      var wake = gridAtOrAfter(t, S0, P);
      for (var j = F.length - 1; j >= 0; j--) {
        var old = F[j];
        if (old === replaced || old.dropped || old.sfExpected > wake + EPS) continue;
        if (old.present > wake + EPS) wake = gridAtOrAfter(old.present, S0, P);
        break;
      }
      return wake;
    }
    var uiFree = 0, rtFree = 0, gpuFree = 0;
    for (var k = 0; k < count; k++) {
      var ld = L(k), f = { k: k, extra: k >= p.frames, blocks: [], ticks: [] };
      f.req = k === 0 ? W0 - 0.92 * P : F[k - 1].doframe0 + C.doframe * 0.6;
      f.vreq = (k === 0 ? f.req + C.req : f.req + 0.1);
      var wake = gridAtOrAfter(f.vreq + 0.05, W0, P);
      var start = Math.max(wake, uiFree);
      if (start - wake >= P - EPS) wake = gridAtOrAfter(start - P + EPS, W0, P); // Choreographer jitter: latest missed VSYNC
      f.wake = wake; f.doframe0 = start;
      f.expected = wake + aD + sD; f.sfTarget = wake + aD;
      var t = start;
      f.doframe1 = t += C.doframe * ld.ui;
      f.trav1 = t += C.trav * ld.ui;
      f.draw1 = t += C.draw * ld.ui;
      f.sync0 = Math.max(t, rtFree);
      f.sync1 = f.sync0 + C.sync;
      f.commit1 = f.sync1 + C.commit;
      uiFree = f.commit1;
      var need = relOf(k - 3);
      f.dequeue0 = Math.max(f.sync1, need.delivered);
      f.slotWait = f.dequeue0 > f.sync1 + EPS ? need.delivered : 0;
      f.rt0 = Math.max(f.dequeue0, need.signaled);
      f.fenceWait = f.rt0 > f.dequeue0 + EPS ? need.signaled : 0;
      f.rtdraw1 = f.rt0 + C.rtdraw * ld.rt;
      f.queue1 = f.rtdraw1 + C.queue * ld.rt;
      rtFree = f.queue1;
      f.gpu0 = Math.max(f.rtdraw1, gpuFree);
      f.fence = f.gpu0 + C.gpu * ld.gpu; gpuFree = f.fence;
      f.acquire = f.queue1 + 0.04;
      f.txn = f.queue1 + 0.08;
      f.ready = p.latchUnsignaled ? f.txn : Math.max(f.txn, f.fence);
      var prev = F[k - 1], before = F[k - 2];
      function cpuUntil(g) { return g && !g.dropped ? g.hwc1 : 0; }
      var cand = sfWakeAfter(Math.max(f.ready, f.sfTarget, cpuUntil(before)), prev);
      if (!p.backpressure && prev && !prev.dropped && prev.fence <= cand + EPS &&
          f.fence <= cand + EPS && Math.abs(cand - prev.latch) < EPS) {
        prev.dropped = true; prev.comp1 = prev.hwc1 = prev.present = null;   // superseded at the same SF wake
        f.latch = cand;
      } else {
        var earliest = Math.max(f.ready, f.sfTarget, cpuUntil(prev), cpuUntil(before));
        if (p.backpressure && prev && !prev.dropped) earliest = Math.max(earliest, prev.latch + P);
        f.latch = sfWakeAfter(earliest);
      }
      f.unsignaledLatch = !f.dropped && f.fence > f.latch + EPS;
      f.sfExpected = f.latch + sD;
      var ls = (k === p.loadFrame ? p.load.sf : 1);
      f.latch1 = f.latch + C.latch * ls;
      f.comp1 = f.latch1 + C.comp * ls;
      f.hwc1 = f.comp1 + C.hwc * ls;
      var nextDisplay = 0;
      for (var j = F.length - 1; j >= 0; j--) {
        if (!F[j].dropped) { nextDisplay = F[j].present + P; break; }
      }
      f.present = gridAtOrAfter(Math.max(f.sfExpected - EPS, f.hwc1, f.fence, nextDisplay), 0, P);
      F.push(f);
    }
    // Releases, verdicts.
    F.forEach(function (f) {
      var release = relOf(f.k);
      f.release = release.delivered;
      f.releaseFence = release.signaled;
      if (f.dropped) { f.verdict = 'dropped'; f.late = null; }
      else { f.late = Math.round((f.present - f.expected) / P); f.verdict = f.late > 0 ? 'late' : 'ontime'; }
      f.reason = f.dropped ? '示意：关闭 layer backpressure 后，被同轮 F' + (f.k + 1) + ' 替换；' + blame(f) : blame(f);
    });
    function blame(f) {
      if (f.verdict === 'ontime') return f.unsignaledLatch
        ? '示意按时：满足未完成 latch 条件，acquire fence 在目标上屏前 signal'
        : '示意按时：事务与 GPU 依赖满足本模型的目标';
      if (f.unsignaledLatch && f.fence > f.expected + EPS) return '示意延后：允许先 latch，但 GPU 依赖仍错过目标上屏';
      if (f.ready > f.sfTarget + EPS) {
        if (f.fence > f.sfTarget && f.txn <= f.sfTarget) return '示意延后：本例要求先完成 acquire fence，事务因此错过接纳目标';
        if (f.fenceWait) return '示意延后：已 dequeue，但安全写入仍受 release fence 约束';
        if (f.slotWait) return '示意延后：本例三个 slot 尚未归还';
        if (f.doframe0 > f.wake + 0.5) return '示意延后：UI doFrame 起点已晚于目标 APP 唤醒';
        return '示意延后：本例生产 / 提交晚于 SF 接纳目标；不是对真机 UI / RT 的自动归因';
      }
      if (f.latch > f.sfTarget + EPS) return '示意延后：layer 留队、SF 主线程占用或本应完成的历史 present fence 阻塞';
      return '示意延后：SF 工作与 GPU 依赖未在目标显示节拍前完成';
    }
    var shown = F.filter(function (f) { return !f.extra; });
    var endT = 0;
    shown.forEach(function (f) {
      endT = Math.max(endT, f.dropped ? f.latch : f.present, f.fence,
        isFinite(f.release) ? f.release : 0, isFinite(f.releaseFence) ? f.releaseFence : 0);
    });
    var total = Math.max(endT + P, 3 * P);
    total = Math.ceil(total / P - EPS) * P;

    // Display occupancy per HW VSYNC interval, plus misses.
    var display = [], misses = [];
    for (var v = 0; v * P < total - EPS; v++) {
      var t0 = v * P, onK = -1;
      F.forEach(function (f) { if (!f.dropped && f.present <= t0 + EPS) onK = f.k; });
      var last = display[display.length - 1];
      if (last && last.k === onK) last.t1 = t0 + P; else display.push({ k: onK, t0: t0, t1: t0 + P });
    }
    shown.forEach(function (f) {
      if (f.verdict !== 'ontime' && f.expected < total - EPS)
        misses.push({ t: f.expected, k: f.k, label: f.dropped ? 'F' + f.k + ' 被跳过' : 'F' + f.k + ' 未赶上，重复上一帧' });
    });

    // BLAST owns ACQUIRED slots independently of SF latch/display state.
    var slots = [[], [], []];
    slots[2].push({ k: -1, state: 'ACQUIRED', t0: 0, t1: relOf(-1).delivered });
    F.forEach(function (f) {
      var s = slots[f.k % 3], end = Math.min(f.release, 1e9);
      s.push({ k: f.k, state: 'DEQUEUED', t0: f.dequeue0, t1: f.queue1 });
      s.push({ k: f.k, state: 'QUEUED', t0: f.queue1, t1: f.acquire });
      s.push({ k: f.k, state: 'ACQUIRED', t0: f.acquire, t1: end });
    });

    return { p: p, P: P, aD: aD, sD: sD, W0: W0, S0: S0, total: total, frames: F, shown: shown,
             display: display, misses: misses, slots: slots };
  };
})(window.FP);
