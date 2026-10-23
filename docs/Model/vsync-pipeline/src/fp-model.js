/* ============================================================
   Frame Pipeline Studio · simulation
   Computes a multi-frame timeline from VSYNC parameters. Rules mirror
   the source semantics (see stage-02 / 06 / 07 / 08):
   - wakeup = vsync − workDuration − readyDuration (VSyncDispatchTimerQueue)
   - APP wake = present − aD − sD, SF wake = present − sD
   - SF latches only at/after the frame's own SF wake (frameIsEarly) and
     only when the transaction is queued AND the acquire fence signaled
   - two buffers ready at the same SF wake → only the newest is latched
   - SF skips commit while the previous present fence is pending
     (FrameTargeter::wouldBackpressureHwc → SurfaceFlinger::commit)
   - buffer N is released when N+1 is presented (dropped: at latch)
   - 3 slots: frame k dequeues the slot released by frame k−3
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
    function relOf(j) {                                 // release time of buffer j as far as known now
      if (j < -1) return 0;
      if (j >= 0 && F[j].dropped) return F[j].latch;
      for (var m = j + 1; m < F.length; m++) if (!F[m].dropped && F[m].present != null) return F[m].present;
      return Infinity;
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
      f.slotWait = need > f.sync1 + EPS ? need : 0;
      f.rt0 = Math.max(f.sync1, need === Infinity ? f.sync1 : need);
      f.rtdraw1 = f.rt0 + C.rtdraw * ld.rt;
      f.queue1 = f.rtdraw1 + C.queue * ld.rt;
      rtFree = f.queue1;
      f.gpu0 = Math.max(f.rtdraw1, gpuFree);
      f.fence = f.gpu0 + C.gpu * ld.gpu; gpuFree = f.fence;
      f.txn = f.queue1 + 0.08;
      f.ready = Math.max(f.txn, f.fence);
      // SF latch: first SF wake ≥ max(ready, own SF target) while SF is not busy.
      var prev = F[k - 1], before = F[k - 2];
      function busyUntil(g) { return g && !g.dropped && g.hwc1 ? Math.max(g.hwc1, g.present) : 0; }
      var sfFreeA = busyUntil(before);
      var cand = gridAtOrAfter(Math.max(f.ready, f.sfTarget, sfFreeA), S0, P);
      if (prev && !prev.dropped && Math.abs(cand - prev.latch) < EPS) {
        prev.dropped = true; prev.comp1 = prev.hwc1 = prev.present = null;   // superseded at the same SF wake
        f.latch = cand;
      } else {
        var sfFree = prev && !prev.dropped ? busyUntil(prev) : sfFreeA;
        f.latch = gridAtOrAfter(Math.max(f.ready, f.sfTarget, sfFree || 0), S0, P);
      }
      var ls = (k === p.loadFrame ? p.load.sf : 1);
      f.latch1 = f.latch + C.latch * ls;
      f.comp1 = f.latch1 + C.comp * ls;
      f.hwc1 = f.comp1 + C.hwc * ls;
      f.present = gridAtOrAfter(Math.max(f.latch + sD - EPS, f.hwc1), 0, P);
      F.push(f);
    }
    // Releases, verdicts.
    F.forEach(function (f) {
      f.release = f.dropped ? f.latch : relOf(f.k);
      if (f.dropped) { f.verdict = 'dropped'; f.late = null; }
      else { f.late = Math.round((f.present - f.expected) / P); f.verdict = f.late > 0 ? 'late' : 'ontime'; }
      f.reason = f.dropped ? '被 F' + (f.k + 1) + ' 替换（同一次 SF 唤醒只 latch 最新 buffer）；起因：' + blame(f) : blame(f);
    });
    function blame(f) {
      if (f.verdict === 'ontime') return '按时：queueBuffer 与 fence 都早于本帧 SF 唤醒点';
      if (f.ready > f.sfTarget + EPS) {
        if (f.fence > f.sfTarget && f.txn <= f.sfTarget) return 'GPU 超时：acquire fence 晚于 SF 唤醒点';
        if (f.slotWait) return '等 buffer release：三个 slot 都被占用';
        if (f.doframe0 > f.wake + 0.5) return 'UI 线程被上一帧占用，doFrame 晚于 VSYNC-app';
        var ui = f.draw1 - f.doframe0, rt = f.queue1 - f.sync1;
        return ui >= rt ? 'UI 线程超时：measure / layout / draw 越过 SF 唤醒点' : 'RenderThread 超时：越过 SF 唤醒点';
      }
      if (f.latch > f.sfTarget + EPS) return 'SF backpressure：上一帧 present fence 还没 signal，本次唤醒跳过 commit';
      return 'SF 超时：合成 + HWC 越过预期的 HW VSYNC';
    }
    var shown = F.filter(function (f) { return !f.extra; });
    var endT = 0;
    shown.forEach(function (f) { endT = Math.max(endT, f.dropped ? f.latch : f.present); });
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

    // Slot timelines. Frame −1 is already on screen in slot 2.
    var slots = [[], [], []];
    slots[2].push({ k: -1, state: 'DISPLAY', t0: 0, t1: relOf(-1) });
    F.forEach(function (f) {
      var s = slots[f.k % 3], end = Math.min(f.release, 1e9);
      s.push({ k: f.k, state: 'DEQUEUED', t0: f.rt0, t1: f.queue1 });
      if (f.dropped) { s.push({ k: f.k, state: 'QUEUED', t0: f.queue1, t1: f.latch }); return; }
      s.push({ k: f.k, state: 'QUEUED', t0: f.queue1, t1: f.latch });
      s.push({ k: f.k, state: 'LATCHED', t0: f.latch, t1: f.present });
      s.push({ k: f.k, state: 'DISPLAY', t0: f.present, t1: end });
    });

    return { p: p, P: P, aD: aD, sD: sD, W0: W0, S0: S0, total: total, frames: F, shown: shown,
             display: display, misses: misses, slots: slots };
  };
})(window.FP);
