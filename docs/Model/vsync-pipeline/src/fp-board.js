/* ============================================================
   Frame Pipeline Studio · time view (ms Gantt, multi-frame)
   ============================================================ */
window.FP = window.FP || {};
(function (FP) {
  'use strict';
  var LANE_H = { UI: 58, RT: 48, GPU: 36, BQ: 72, SCHED: 42, SF: 48, DISPLAY: 44 };
  FP.LANE_H = LANE_H;
  var STATE_SHORT = { DEQUEUED: 'DEQ', QUEUED: 'QUEUED', ACQUIRED: 'ACQUIRED' };
  function h(tag, cls, parent, html) {
    var e = document.createElement(tag); if (cls) e.className = cls;
    if (html != null) e.innerHTML = html; if (parent) parent.appendChild(e); return e;
  }
  FP.h = h;
  function esc(s) { return String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;'); }
  FP.esc = esc;

  FP.TimeBoard = function () {
    this.board = document.getElementById('board');
    this.body = document.getElementById('boardBody');
    this.items = [];
  };
  var TB = FP.TimeBoard.prototype;

  TB.build = function (sim, opt) {
    var self = this, total = sim.total, P = sim.P, fmt = FP.fmt;
    function pct(t) { return Math.max(0, Math.min(1, t / total)) * 100; }
    this.sim = sim; this.opt = opt; this.pct = pct; this.items = []; this.tracks = {}; this.laneEls = {};
    var pins = document.getElementById('pins'), ruler = document.getElementById('ruler'), lanes = document.getElementById('lanes');
    pins.innerHTML = ''; ruler.innerHTML = ''; lanes.innerHTML = '';
    // Width = lane-label column + total ms × px/ms. zoom 'fit' squeezes the timeline into the visible width.
    var wrap = document.getElementById('boardWrap'), avail = Math.max(600, (wrap.clientWidth || 1200) - 60);
    var pxms = opt.zoom === 'fit' ? Math.max(8, (avail - 220) / total) : (Number(opt.zoom) || 4) * 14;
    this.pxms = pxms;
    this.board.style.minWidth = this.board.style.width = Math.max(avail, Math.round(220 + total * pxms)) + 'px';
    var focus = sim.frames[opt.focus] || sim.frames[0];
    var FS = FP.stepTimes(focus);

    // ruler: HW VSYNC + focus-frame phase brackets
    h('div', 'ruler-line', ruler);
    for (var v = 0; v * P <= total + 1e-6; v++) {
      var vs = h('div', 'vsync' + (v === 0 ? ' first' : ''), ruler,
        '<span class="vlab">V' + v + '</span><span class="vdia"></span><span class="vline"></span><span class="vsub' + (v ? ' left' : '') + '">' + fmt(v * P) + '</span>');
      vs.style.left = pct(v * P) + '%';
    }
    function band(t0, t1, cls, text) {
      var b = h('div', 'fp-band ' + cls, ruler, '<span>' + text + '</span>');
      b.style.left = pct(t0) + '%'; b.style.width = Math.max(0.5, pct(t1) - pct(t0)) + '%';
    }
    band(focus.wake, focus.sfTarget, 'app', 'F' + focus.k + ' app ' + fmt(sim.aD));
    band(focus.sfTarget, focus.expected, 'sf', 'sf ' + fmt(sim.sD));

    // stage pins (focus frame)
    var seen = {}, ends = [], rows = 0;
    FP.STEPS.forEach(function (st) {
      var x = FS[st.id]; if (!x || seen[st.stage]) return; seen[st.stage] = 1;
      var r = 0, px = pct(x.t0), gap = 26 / (total * pxms) * 100;   // keep pins ≥ 26px apart at any zoom
      while (r < ends.length && ends[r] + gap > px) r++;
      if (r === ends.length) ends.push(0); ends[r] = px; rows = Math.max(rows, r + 1);
      var d = h('div', 'pin', pins, '<span class="pin-n">' + String(st.stage).padStart(2, '0') + '</span><span class="pin-stem"></span><span class="pin-tip">Stage ' + String(st.stage).padStart(2, '0') + ' · ' + esc(st.title) + '</span>');
      d.style.left = px + '%'; d.style.top = (r * 19) + 'px'; d.dataset.stage = st.stage; d.dataset.t = x.t0;
      self.items.push({ el: d, t0: x.t0, t1: x.t0, kind: 'pin' });
    });
    pins.style.height = Math.max(26, 20 + rows * 19) + 'px';

    // lanes
    FP.LANES.forEach(function (ln) {
      var el = h('div', 'lane fp-lane', lanes);
      el.dataset.lane = ln.key; el.style.height = LANE_H[ln.key] + 'px'; el.style.setProperty('--lane', ln.color);
      var sub = ln.key === 'SCHED' ? 'app ' + fmt(sim.aD) + ' · sf ' + fmt(sim.sD) + ' ms' : ln.sub;
      h('div', 'lane-label', el, '<span class="ll-top"><span class="dot"></span>' + ln.name + '</span><small>' + sub + '</small>');
      self.tracks[ln.key] = h('div', 'lane-track', el); self.laneEls[ln.key] = el;
    });
    var T = this.tracks;

    // SCHED grid: every VSYNC-app / VSYNC-sf wakeup in range
    for (var a = sim.W0 % P; a <= total; a += P) h('div', 'fp-grid app', T.SCHED).style.left = pct(a) + '%';
    for (var s = sim.S0 % P; s <= total; s += P) h('div', 'fp-grid sf', T.SCHED).style.left = pct(s) + '%';

    // BQ slot rows
    var slotRows = [0, 1, 2].map(function (r) {
      var row = h('div', 'fp-slotrow', T.BQ, '<span class="fp-slotname">slot ' + r + '</span>');
      row.style.top = (6 + r * 21) + 'px'; return row;
    });
    sim.slots.forEach(function (segs, r) {
      segs.forEach(function (sg) {
        if (sg.t0 >= total) return;
        var e = h('div', 'fp-seg st-' + sg.state + frameCls(sg.k), slotRows[r], 'F' + (sg.k < 0 ? '-1' : sg.k) + ' ' + STATE_SHORT[sg.state]);
        e.style.left = pct(sg.t0) + '%'; e.style.width = Math.max(0.3, pct(Math.min(sg.t1, total)) - pct(sg.t0)) + '%';
        e.title = 'slot ' + r + ' · F' + sg.k + ' ' + sg.state + ' ' + fmt(sg.t0) + '–' + (isFinite(sg.t1) ? fmt(sg.t1) : '…') + ' ms';
        self.items.push({ el: e, t0: sg.t0, t1: sg.t1, kind: 'block' });
      });
    });

    function frameCls(k) { return k === opt.focus ? ' is-focus' : ' is-other'; }
    // per-frame blocks / ticks
    sim.frames.forEach(function (f) {
      var isFocus = f.k === opt.focus;
      if (!isFocus && !opt.showOthers) return;
      var cls = isFocus ? ' is-focus' : (f.extra ? ' is-extra' : ' is-other');
      var color = FP.FRAME_COLORS[f.k % FP.FRAME_COLORS.length];
      var S = FP.stepTimes(f);
      Object.keys(S).forEach(function (id) {
        var x = S[id]; if (x.lane === 'BQ' || x.t0 >= total) return;
        var st = FP.STEP[id], tr = T[x.lane], e;
        if (x.kind === 'block') {
          e = h('div', 'blk fp-blk' + cls, tr, '<span class="blk-label">' + (isFocus ? '' : 'F' + f.k + ' ') + x.label + '</span>');
          e.style.left = pct(x.t0) + '%'; e.style.width = Math.max(0.25, pct(x.t1) - pct(x.t0)) + '%';
        } else {
          e = h('div', 'fp-tick' + cls + (x.dropped ? ' is-drop' : '') + (st.vsync ? ' is-vs' : ''), tr, '<span>' + (isFocus || x.dropped ? (isFocus ? '' : 'F' + f.k + ' ') + x.label : 'F' + f.k) + '</span>');
          e.style.left = pct(x.t0) + '%';
        }
        e.style.setProperty('--fc', color);
        e.title = 'F' + f.k + ' · ' + st.title + ' · ' + fmt(x.t0) + (x.kind === 'block' ? '–' + fmt(x.t1) : '') + ' ms';
        e.dataset.step = id; e.dataset.frame = f.k;
        self.items.push({ el: e, t0: x.t0, t1: x.t1, kind: x.kind });
      });
      FP.waitBlocks(f).forEach(function (w) {
        var e = h('div', 'blk fp-blk is-wait is-dashed' + cls, T[w.lane], '<span class="blk-label">' + w.label + '</span>');
        e.style.left = pct(w.t0) + '%'; e.style.width = Math.max(0.25, pct(w.t1) - pct(w.t0)) + '%';
        e.title = 'F' + f.k + ' · ' + w.why;
        self.items.push({ el: e, t0: w.t0, t1: w.t1, kind: 'block' });
      });
    });

    // display occupancy + misses
    sim.display.forEach(function (d) {
      var e = h('div', 'blk fp-blk fp-disp' + (d.k === opt.focus ? ' is-focus' : ' is-other') + (d.k < 0 ? ' is-prev' : ''), T.DISPLAY,
        '<span class="blk-label">' + (d.k < 0 ? '上一帧内容' : '扫描 F' + d.k) + '</span>');
      e.style.left = pct(d.t0) + '%'; e.style.width = (pct(d.t1) - pct(d.t0)) + '%';
      if (d.k >= 0) e.style.setProperty('--fc', FP.FRAME_COLORS[d.k % FP.FRAME_COLORS.length]);
      self.items.push({ el: e, t0: d.t0, t1: d.t1, kind: 'block' });
    });
    sim.misses.forEach(function (m) {
      var mk = h('div', 'miss-marker', T.DISPLAY, '<span class="mm-line"></span><span class="mm-label">' + esc(m.label) + '</span>');
      mk.style.left = pct(m.t) + '%';
    });
    this.measure();
  };

  TB.measure = function () {
    var tr = this.tracks && this.tracks.UI; if (!tr) return;
    var br = this.body.getBoundingClientRect(), r = tr.getBoundingClientRect();
    this.geo = { left: r.left - br.left, width: r.width, top: br.top };
    if (FP.drawArrows) FP.drawArrows(this);
  };
  TB.xAt = function (t) { return this.geo.left + this.pct(t) / 100 * this.geo.width; };
  TB.laneY = function (lane) {
    var el = this.laneEls[lane], br = this.body.getBoundingClientRect(), r = el.getBoundingClientRect();
    return r.top - br.top + r.height / 2;
  };

  // Keep the playhead visible: park it ~1/3 into the visible track area.
  TB.follow = function (t, smooth) {
    var wrap = document.getElementById('boardWrap'); if (!this.geo || !wrap || wrap.hidden) return;
    // xAt is relative to .board-body; .board adds 24px padding. Lane names cover the first 196px of the view.
    var label = 196, x = this.xAt(t) + 24, view = wrap.clientWidth - 44;
    var target = Math.max(0, x - label - (view - label) * 0.33);
    if (Math.abs(wrap.scrollLeft - target) < 2) return;
    if (smooth) wrap.scrollTo({ left: target, behavior: 'smooth' }); else wrap.scrollLeft = target;
  };

  TB.setTime = function (t) {
    this.items.forEach(function (it) {
      var st = it.kind === 'block' ? (t >= it.t1 ? 2 : t >= it.t0 ? 1 : 0) : (t >= it.t0 ? 2 : 0);
      if (st === it.st) return; it.st = st;
      it.el.classList.toggle('is-active', st === 1);
      it.el.classList.toggle(it.kind === 'pin' ? 'is-past' : 'is-done', st === 2);
    });
    var ph = document.getElementById('playhead');
    if (this.geo) ph.style.left = this.xAt(t) + 'px';
    ph.querySelector('.ph-time').textContent = FP.fmt(t) + ' ms';
  };
})(window.FP);
