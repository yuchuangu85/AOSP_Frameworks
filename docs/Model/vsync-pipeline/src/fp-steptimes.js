/* ============================================================
   Frame Pipeline Studio · per-frame step placement
   Maps one simulated frame (fp-model.js) onto the step catalogue:
   which lane, when, block vs tick. Shared by the time view, the step
   view and the state panel so all three always agree.
   ============================================================ */
window.FP = window.FP || {};
(function (FP) {
  'use strict';
  function fmt(t) { return (Math.round(t * 10) / 10).toFixed(1); }
  FP.fmt = fmt;

  FP.stepTimes = function (f) {
    var S = {};
    function blk(id, lane, t0, t1, label) { S[id] = { id: id, lane: lane, t0: t0, t1: Math.max(t1, t0), kind: 'block', label: label }; }
    function tick(id, lane, t, label) { S[id] = { id: id, lane: lane, t0: t, t1: t, kind: 'tick', label: label }; }
    if (f.k === 0) blk('req', 'UI', f.req, f.vreq, 'requestLayout'); else tick('req', 'UI', f.req, 'req');
    tick('vreq', 'UI', f.vreq, 'requestNextVsync');
    tick('vapp', 'SCHED', f.wake, 'VSYNC-app');
    blk('doframe', 'UI', f.doframe0, f.doframe1, 'doFrame');
    blk('trav', 'UI', f.doframe1, f.trav1, 'measure/layout');
    blk('draw', 'UI', f.trav1, f.draw1, 'draw');
    blk('sync', 'UI', f.sync0, f.sync1, 'sync');
    blk('commit', 'UI', f.sync1, f.commit1, 'COMMIT');
    blk('rtdraw', 'RT', f.rt0, f.rtdraw1, 'RT draw');
    blk('queue', 'RT', f.rtdraw1, f.queue1, 'queue+BLAST');
    blk('gpu', 'GPU', f.gpu0, f.fence, 'GPU');
    tick('txn', 'SF', f.txn, 'txn');
    tick('vsf', 'SCHED', f.latch, 'VSYNC-sf');
    if (f.dropped) {
      tick('latch', 'SF', f.latch, 'dropped');
      S.latch.dropped = true;
    } else {
      blk('latch', 'SF', f.latch, f.latch1, 'commit/latch');
      blk('comp', 'SF', f.latch1, f.comp1, 'composite');
      blk('hwc', 'SF', f.comp1, f.hwc1, 'HWC');
      tick('present', 'DISPLAY', f.present, 'present');
    }
    if (isFinite(f.release)) tick('release', 'BQ', f.release, 'release');
    return S;
  };

  // Blocks that are not steps but explain a wait.
  FP.waitBlocks = function (f) {
    var W = [];
    if (f.doframe0 > f.wake + 0.05) W.push({ lane: 'UI', t0: f.wake, t1: f.doframe0, label: 'doFrame 晚 ' + fmt(f.doframe0 - f.wake) + 'ms', why: 'UI 线程仍在处理上一帧' });
    if (f.sync0 > f.draw1 + 0.05) W.push({ lane: 'UI', t0: f.draw1, t1: f.sync0, label: '等 RenderThread', why: 'RenderThread 还在画上一帧' });
    if (f.dequeue0 > f.sync1 + 0.05) W.push({ lane: 'RT', t0: f.sync1, t1: f.dequeue0, label: '等 slot 归还', why: '本例三个 slot 尚未归还，dequeueBuffer 阻塞' });
    if (f.rt0 > f.dequeue0 + 0.05) W.push({ lane: 'RT', t0: f.dequeue0, t1: f.rt0, label: 'release fence 依赖', why: '已 dequeue，安全写入仍受 release fence 约束；等待或导入依赖由图形后端完成' });
    if (f.gpu0 > f.rtdraw1 + 0.05) W.push({ lane: 'GPU', t0: f.rtdraw1, t1: f.gpu0, label: 'GPU 忙', why: 'GPU 还在执行上一帧' });
    return W;
  };

  FP.predecessors = function (step, f) {
    var from = (step.from || []).slice();
    if (step.id === 'latch' && f.fence <= f.latch + 1e-6) from.push('gpu');
    return from;
  };

  // Start-time order keeps parallel GPU work from moving an early latch
  // to the later acquire-fence signal in step/process views.
  FP.stepSeq = function (f) {
    var S = FP.stepTimes(f), seq = [];
    FP.STEPS.forEach(function (st) {
      var x = S[st.id]; if (!x) return;
      seq.push({ id: st.id, step: st, at: x.t0, t0: x.t0, t1: x.t1, lane: x.lane, kind: x.kind, dropped: !!x.dropped });
    });
    seq.sort(function (a, b) { return a.at - b.at || a.step.index - b.step.index; });
    return seq;
  };
  FP.seqIndexAt = function (seq, t) {
    var i = 0;
    for (var j = 0; j < seq.length; j++) if (seq[j].at <= t + 1e-6) i = j;
    return i;
  };
})(window.FP);
