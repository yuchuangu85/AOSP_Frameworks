/* ============================================================
   Frame Pipeline Studio · info panel, state chips, slot cards
   Everything is derived from (focus frame, time t).
   ============================================================ */
window.FP = window.FP || {};
(function (FP) {
  'use strict';
  var $ = function (id) { return document.getElementById(id); };
  var CB = ['INPUT', 'ANIMATION', 'INSETS_ANIMATION', 'TRAVERSAL', 'COMMIT'];
  var VIEW = ['measure / layout', 'draw 录制', 'sync → RT', 'RT 绘制 + queue', 'GPU → fence'];
  var BUF = ['FREE', 'DEQUEUED', 'QUEUED', 'ACQUIRED · BLAST 持有', '已归还 slot'];
  var VERDICT = { ontime: '示意按时', late: '示意延后', dropped: '示意跳过' };

  function chips(host, items) {
    host.innerHTML = '';
    return items.map(function (t) { var c = FP.h('span', 'chip', host); c.textContent = t; return c; });
  }
  function set(cs, st) { cs.forEach(function (c, k) { c.className = 'chip' + (st[k] ? ' ' + st[k] : ''); }); }
  function phase(t, t0, t1) { return t >= t1 ? 'done' : t >= t0 ? 'act' : ''; }

  FP.Panel = function () {
    this.cb = chips($('rowCb'), CB); this.view = chips($('rowView'), VIEW); this.buf = chips($('rowBuf'), BUF);
    this.lastStep = null;
  };
  var PN = FP.Panel.prototype;

  PN.buildSlots = function (sim) {
    var host = $('slots'); host.innerHTML = ''; this.slotEls = [];
    for (var r = 0; r < 3; r++) {
      var e = FP.h('div', 'slot', host, '<div class="sh"><span class="sn">SLOT ' + r + '</span><span class="state">FREE</span></div>' +
        '<div class="sbar"><i></i></div><div class="owner"><span>frame</span><b>—</b></div>');
      e.dataset.state = 'FREE'; this.slotEls.push(e);
    }
    this.note = FP.h('div', 'slots-note', host);
    this.sim = sim;
  };

  PN.update = function (sim, f, cur, t, info) {
    var s = cur.step, fmt = FP.fmt;
    if (this.lastStep !== s.id + '/' + f.k) {
      this.lastStep = s.id + '/' + f.k;
      var stage = $('ppStage');
      stage.textContent = 'STAGE ' + String(s.stage).padStart(2, '0');
      stage.style.setProperty('--pc', cur.dropped ? 'var(--hot)' : FP.LANE[cur.lane].color);
      $('ppTitle').textContent = cur.dropped ? 'F' + f.k + ' 被同轮新事务覆盖（无 layer 背压示例）' : s.title;
      $('ppDesc').textContent = s.desc;
      var refs = $('ppRefs'); refs.innerHTML = '';
      s.chain.forEach(function (c) { var li = FP.h('li', '', refs); li.textContent = c; });
      $('ppPacket').textContent = s.packet;
      $('ppHint').textContent = s.hint;
    }
    $('ppPhase').textContent = 'STEP ' + String(info.index + 1).padStart(2, '0') + ' / ' + String(info.count).padStart(2, '0') + ' · F' + f.k;
    var lis = $('ppRefs').children, n = lis.length, on = Math.min(n, Math.floor(info.frac * n + 1e-6));
    for (var i = 0; i < n; i++) lis[i].className = i < on ? 'on' : i === on ? 'cur' : '';

    // stats + budget
    $('ppVsync').textContent = t >= f.wake ? '+' + fmt(t - f.wake) + ' ms' : '等待 ' + fmt(f.wake - t) + ' ms';
    $('ppPresent').textContent = 'V' + Math.round(f.expected / sim.P) + (f.dropped ? ' → 跳过' : f.present > f.expected + 0.01 ? ' → V' + Math.round(f.present / sim.P) : ' ✓');
    $('ppElapsed').textContent = fmt(t) + ' ms';
    var vd = $('ppFrame');
    vd.textContent = t >= (f.dropped ? f.latch : f.present) ? VERDICT[f.verdict] + (f.late > 0 ? ' ' + f.late + ' 帧' : '') : '进行中';
    vd.className = t >= (f.dropped ? f.latch : f.present) ? 'v-' + f.verdict : '';
    var win = Math.max(0.1, f.sfTarget - f.wake), ready = f.ready, late = ready > f.sfTarget + 1e-6;
    var fill = $('budgetFill'), txt = $('budgetTxt');
    if (t < f.wake) { fill.style.width = '0%'; fill.classList.remove('warn'); txt.textContent = '0.0 / ' + fmt(win) + ' ms'; }
    else if (t >= ready) {
      fill.style.width = '100%'; fill.classList.toggle('warn', late);
      txt.textContent = late ? '超时 +' + fmt(ready - f.sfTarget) + ' ms' : '就绪 ' + fmt(ready - f.wake) + ' / ' + fmt(win) + ' ms';
    } else {
      fill.style.width = Math.min(100, (t - f.wake) / win * 100) + '%'; fill.classList.toggle('warn', t > f.sfTarget);
      txt.textContent = fmt(t - f.wake) + ' / ' + fmt(win) + ' ms';
    }
    if (t >= f.expected) $('ppHint').textContent = f.reason;
    else if ($('ppHint').textContent === f.reason) $('ppHint').textContent = s.hint;

    // state chips
    var d3 = (f.doframe1 - f.doframe0) / 3;
    var cb = t < f.vreq ? ['', '', '', '', ''] : t < f.doframe0 ? ['', '', '', 'q', ''] : [
      phase(t, f.doframe0, f.doframe0 + d3), phase(t, f.doframe0 + d3, f.doframe0 + 2 * d3), phase(t, f.doframe0 + 2 * d3, f.doframe1),
      phase(t, f.doframe1, f.sync1), phase(t, f.sync1, f.commit1)];
    set(this.cb, cb);
    set(this.view, [phase(t, f.doframe1, f.trav1), phase(t, f.trav1, f.draw1), phase(t, f.sync0, f.sync1),
      phase(t, f.rt0, f.queue1), phase(t, f.gpu0, f.fence)]);
    var b = 0;
    if (t >= f.dequeue0) b = 1; if (t >= f.queue1) b = 2;
    if (t >= f.acquire) b = 3;
    if (t >= f.release) b = 4;
    set(this.buf, BUF.map(function (_, k) { return k === b ? (f.dropped && b === 4 ? 'act drop' : 'act') : k < b ? 'done' : ''; }));

    // slot cards
    if (this.slotEls) {
      var self = this;
      sim.slots.forEach(function (segs, r) {
        var cur2 = null; segs.forEach(function (sg) { if (t >= sg.t0 && t < sg.t1) cur2 = sg; });
        var e = self.slotEls[r], st = cur2 ? cur2.state : 'FREE';
        e.dataset.state = st; e.querySelector('.state').textContent = st;
        e.querySelector('.sbar i').style.width = ({ FREE: '10%', DEQUEUED: '38%', QUEUED: '66%', ACQUIRED: '86%' })[st];
        e.querySelector('.owner b').textContent = cur2 ? 'F' + cur2.k : '—';
        e.classList.toggle('is-focus', !!cur2 && cur2.k === f.k);
      });
      var waiting = sim.frames.filter(function (g) { return g.slotWait && t >= g.sync1 && t < g.dequeue0; })[0];
      var fenceWait = sim.frames.filter(function (g) { return g.fenceWait && t >= g.dequeue0 && t < g.rt0; })[0];
      this.note.innerHTML = '<div class="t"><span class="dot"></span>BUFFERQUEUE · 3 个示意 slot</div><p' + (waiting || fenceWait ? ' class="warn"' : '') + '>' +
        (waiting ? 'F' + waiting.k + ' 的 dequeueBuffer 正在阻塞：要等 F' + (waiting.k - 3) + ' 被下一帧替换后才有 FREE slot。'
          : fenceWait ? 'F' + fenceWait.k + ' 已取得 slot，安全写入仍受 release fence 约束。'
          : 'ACQUIRED 表示 BLAST 持有，与 SF latch / 显示状态分离；归还 slot、fence signal 与安全写入不是同一事件。') + '</p>';
    }
  };
})(window.FP);
