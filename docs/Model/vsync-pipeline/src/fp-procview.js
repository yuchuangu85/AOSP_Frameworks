/* ============================================================
   Frame Pipeline Studio · process stage view
   Layout adapted from claude/android-frame-relay: three bands
   (APP / SURFACEFLINGER / HARDWARE), one box per executor, the
   focus frame as a red token, the current hand-off as a flowing link.
   Driven by the same step sequence + time as the other two views.
   ============================================================ */
window.FP = window.FP || {};
(function (FP) {
  'use strict';
  var NS = 'http://www.w3.org/2000/svg';
  var BANDS = [{ p: 'app', y: 10, h: 168, label: 'APP PROCESS', sub: '应用进程 · Java + HWUI native + BLAST' },
               { p: 'sf', y: 186, h: 168, label: 'SURFACEFLINGER PROCESS', sub: '系统合成服务 · 调度 + 事务 + 合成' },
               { p: 'hw', y: 362, h: 172, label: 'HARDWARE / HAL', sub: 'GPU、Composer HAL / DPU、面板：不是一个进程' }];
  var COLS = [30, 325, 620, 915], SW = 255, SH = 96;
  var STATIONS = [
    { id: 'ui', p: 'app', c: 0, name: 'UI 主线程', role: 'Looper · ViewRootImpl', idle: '等待消息' },
    { id: 'rt', p: 'app', c: 1, name: 'RenderThread', role: 'HWUI · native 线程', idle: '等待 DrawFrameTask' },
    { id: 'blast', p: 'app', c: 2, name: 'BLASTBufferQueue', role: '应用侧 BQ 消费者', idle: '等待新 buffer' },
    { id: 'vsync', p: 'sf', c: 0, name: 'Scheduler / EventThread', role: '软件 VSYNC 派发', idle: '按预测模型计时' },
    { id: 'binder', p: 'sf', c: 1, name: 'Binder 线程', role: 'setTransactionState 入队', idle: '等待事务' },
    { id: 'sf', p: 'sf', c: 2, name: 'SurfaceFlinger 主线程', role: 'commit / composite', idle: '等待 VSYNC-sf' },
    { id: 'hwv', p: 'hw', c: 0, name: '硬件 VSYNC', role: '面板 / DPU 刷新节拍', idle: '' },
    { id: 'gpu', p: 'hw', c: 1, name: 'GPU', role: '异步执行命令', idle: '空闲' },
    { id: 'hwc', p: 'hw', c: 2, name: 'Composer HAL / DPU', role: '叠加与呈现', idle: '保持上一帧配置' },
    { id: 'display', p: 'hw', c: 3, name: '显示面板', role: '逐行扫描', idle: '' }];
  var ST = {}; STATIONS.forEach(function (s) { ST[s.id] = s; });
  // step → executor; extra link targets, blocked executors, VSYNC pulses
  var AT = { req: 'ui', vreq: 'ui', vapp: 'vsync', doframe: 'ui', trav: 'ui', draw: 'ui', sync: 'rt', commit: 'ui', rtdraw: 'rt',
             queue: 'blast', gpu: 'gpu', txn: 'binder', vsf: 'vsync', latch: 'sf', comp: 'sf', hwc: 'hwc', present: 'display', release: 'blast' };
  var TO = { vreq: 'vsync' }, BLOCK = { sync: ['ui'] }, FROM = { release: ['sf'] }, PULSE = { vapp: 1, vsf: 1, present: 1 };
  var WAIT_AT = { UI: 'ui', RT: 'rt', GPU: 'gpu' };
  FP.PROC_AT = AT;

  function el(tag, attrs, parent, text) {
    var e = document.createElementNS(NS, tag);
    for (var k in attrs) {
      var v = attrs[k];
      if ((k === 'fill' || k === 'stroke') && String(v).indexOf('var(') === 0) e.style[k] = v; else e.setAttribute(k, v);
    }
    if (text != null) e.textContent = text;
    if (parent) parent.appendChild(e); return e;
  }
  function band(p) { for (var i = 0; i < BANDS.length; i++) if (BANDS[i].p === p) return i; return 0; }
  function geo(id) { var s = ST[id], b = band(s.p); return { x: COLS[s.c], y: BANDS[b].y + 46, w: SW, h: SH, c: s.c, band: b }; }
  // Same routing as the relay page: same band → horizontal, adjacent band → vertical, two bands → through the column gap.
  function linkPath(a, b) {
    var A = geo(a), B = geo(b);
    if (A.band === B.band) {
      var y = A.y + A.h / 2;
      if (Math.abs(A.c - B.c) === 1) return 'M' + (A.c < B.c ? A.x + A.w : A.x) + ',' + y + ' L' + (A.c < B.c ? B.x : B.x + B.w) + ',' + y;
      var yb = A.y + A.h, dip = yb + 16;
      return 'M' + (A.x + A.w / 2) + ',' + yb + ' C' + (A.x + A.w / 2) + ',' + dip + ' ' + (B.x + B.w / 2) + ',' + dip + ' ' + (B.x + B.w / 2) + ',' + yb;
    }
    var down = A.band < B.band, y1 = down ? A.y + A.h : A.y, y2 = down ? B.y : B.y + B.h, x1 = A.x + A.w / 2, x2 = B.x + B.w / 2;
    if (Math.abs(A.band - B.band) === 1) {
      var my = (y1 + y2) / 2;
      return A.c === B.c ? 'M' + x1 + ',' + y1 + ' L' + x2 + ',' + y2 : 'M' + x1 + ',' + y1 + ' C' + x1 + ',' + my + ' ' + x2 + ',' + my + ' ' + x2 + ',' + y2;
    }
    var gx = COLS[Math.max(A.c, B.c)] - (COLS[1] - COLS[0] - SW) / 2, yA = A.y + A.h / 2, yB = B.y + B.h / 2, sg = yB > yA ? 1 : -1;
    var ax = A.c < B.c ? A.x + A.w : A.x, bx = B.c < A.c ? B.x + B.w : B.x;
    return 'M' + ax + ',' + yA + ' Q' + gx + ',' + yA + ' ' + gx + ',' + (yA + sg * 20) + ' L' + gx + ',' + (yB - sg * 20) + ' Q' + gx + ',' + yB + ' ' + bx + ',' + yB;
  }

  FP.ProcView = function (svg) { this.svg = svg; };
  var PV = FP.ProcView.prototype;

  PV.build = function (sim, seq, f, onJump) {
    var svg = this.svg, self = this, n = this.n = {};
    svg.innerHTML = ''; this.sim = sim; this.seq = seq; this.f = f; this.lastId = null;
    var defs = el('defs', {}, svg), mk = el('marker', { id: 'pvArrow', viewBox: '0 0 10 10', refX: 8, refY: 5, markerWidth: 7, markerHeight: 7, orient: 'auto-start-reverse' }, defs);
    el('path', { d: 'M0,0 L10,5 L0,10 z', fill: 'var(--hot)' }, mk);
    var pat = el('pattern', { id: 'pvHatch', width: 8, height: 8, patternUnits: 'userSpaceOnUse', patternTransform: 'rotate(45)' }, defs);
    el('rect', { width: 8, height: 8, fill: 'var(--pv-hatch-bg)' }, pat); el('line', { x1: 0, y1: 0, x2: 0, y2: 8, stroke: 'var(--pv-hatch)', 'stroke-width': 3 }, pat);
    var gB = el('g', {}, svg); n.links = el('g', {}, svg); var gS = el('g', {}, svg), gW = el('g', {}, svg); n.rip = el('g', {}, svg);
    BANDS.forEach(function (b) {
      var g = el('g', { 'class': 'pv-band', 'data-p': b.p }, gB);
      el('rect', { 'class': 'bg', x: 8, y: b.y, width: 1184, height: b.h, rx: 14 }, g);
      el('text', { 'class': 'bl', x: 24, y: b.y + 26 }, g, b.label);
      el('text', { 'class': 'bs', x: b.p === 'sf' ? 232 : b.p === 'app' ? 150 : 170, y: b.y + 26 }, g, b.sub);
    });
    n.st = {};
    STATIONS.forEach(function (s) {
      var q = geo(s.id), g = el('g', { 'class': 'pv-st', tabindex: 0, role: 'button', 'aria-label': s.name + '：跳到它的第一个步骤' }, gS);
      el('rect', { 'class': 'box', x: q.x, y: q.y, width: q.w, height: q.h, rx: 10 }, g);
      el('text', { 'class': 'role', x: q.x + 14, y: q.y + 22 }, g, s.role);
      el('text', { 'class': 'name', x: q.x + 14, y: q.y + 46 }, g, s.name);
      var op = el('text', { 'class': 'op', x: q.x + 14, y: q.y + 78 }, g, s.idle);
      var pill = el('g', { 'class': 'pill', visibility: 'hidden' }, g);
      el('rect', { x: q.x + q.w - 84, y: q.y + 10, width: 72, height: 20, rx: 10 }, pill);
      var pt = el('text', { x: q.x + q.w - 48, y: q.y + 24, 'text-anchor': 'middle' }, pill, '');
      if (s.id === 'display') {
        el('rect', { 'class': 'scr', x: q.x + 150, y: q.y + 30, width: 88, height: 54, rx: 4 }, g);
        n.scrT = el('text', { 'class': 'scr-t', x: q.x + 194, y: q.y + 62, 'text-anchor': 'middle' }, g, '');
        n.scr = g;
      }
      var jump = function () { for (var i = 0; i < seq.length; i++) if (AT[seq[i].id] === s.id) return onJump(i); };
      g.addEventListener('click', jump);
      g.addEventListener('keydown', function (e) { if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); jump(); } });
      n.st[s.id] = { g: g, op: op, pill: pill, pt: pt };
    });
    // BufferQueue slots (app band) and SF frame inputs (sf band), as in the relay page
    var bx = COLS[3], by = BANDS[0].y + 46, wb = el('g', { 'class': 'pv-wg' }, gW);
    el('rect', { 'class': 'frame', x: bx, y: by, width: SW, height: 112, rx: 10 }, wb);
    el('text', { 'class': 'wt', x: bx + 14, y: by + 20 }, wb, 'BufferQueue 槽位（3 块）');
    n.slots = [0, 1, 2].map(function (i) {
      var sx = bx + 14 + i * 80, g = el('g', { 'class': 'pv-slot', 'data-s': 'FREE' }, wb);
      el('rect', { x: sx, y: by + 30, width: 70, height: 72, rx: 7 }, g);
      el('text', { 'class': 'sl', x: sx + 35, y: by + 52, 'text-anchor': 'middle' }, g, 'slot ' + i);
      return { g: g, ss: el('text', { 'class': 'ss', x: sx + 35, y: by + 72, 'text-anchor': 'middle' }, g, ''),
               so: el('text', { 'class': 'so', x: sx + 35, y: by + 90, 'text-anchor': 'middle' }, g, '') };
    });
    var ly = BANDS[1].y + 46, wl = el('g', { 'class': 'pv-wg' }, gW);
    el('rect', { 'class': 'frame', x: bx, y: ly, width: SW, height: 112, rx: 10 }, wl);
    el('text', { 'class': 'wt', x: bx + 14, y: ly + 20 }, wl, 'SF 本轮输入（焦点帧）');
    n.lines = [0, 1, 2].map(function (i) { return el('text', { 'class': 'lyr', x: bx + 14, y: ly + 46 + i * 22 }, wl, ''); });
    n.packet = el('g', { 'class': 'pv-packet', 'aria-hidden': 'true' }, svg);
    el('circle', { r: 17 }, n.packet); n.packetT = el('text', { 'text-anchor': 'middle', dy: 5 }, n.packet, 'F');
    // per-executor activity intervals over all frames (for 并行中 / 阻塞等待)
    var iv = this.iv = [];
    sim.frames.forEach(function (g) {
      var S = FP.stepTimes(g);
      Object.keys(S).forEach(function (id) { var x = S[id]; if (x.kind === 'block' && AT[id]) iv.push({ st: AT[id], t0: x.t0, t1: x.t1, k: g.k, op: FP.STEP[id].short, kind: 'busy' }); });
      FP.waitBlocks(g).forEach(function (w) { if (WAIT_AT[w.lane]) iv.push({ st: WAIT_AT[w.lane], t0: w.t0, t1: w.t1, k: g.k, op: w.label, kind: 'blocked' }); });
    });
  };
  // t = playhead time, i = index of the current step in the focus-frame sequence
  PV.update = function (t, i) {
    var n = this.n, sim = this.sim, f = this.f, cur = this.seq[i], fmt = FP.fmt, self = this;
    if (!cur) return;
    var actor = AT[cur.id], blockedNow = BLOCK[cur.id] || [];
    STATIONS.forEach(function (s) {
      var o = n.st[s.id], active = s.id === actor, hit = null;
      self.iv.forEach(function (x) { if (x.st === s.id && t >= x.t0 && t < x.t1 && (!hit || x.kind === 'blocked')) hit = x; });
      var blocked = !active && (blockedNow.indexOf(s.id) >= 0 || (hit && hit.kind === 'blocked'));
      var busy = !active && !blocked && !!hit;
      o.g.classList.toggle('active', active); o.g.classList.toggle('blocked', blocked); o.g.classList.toggle('busy', busy);
      if (active) o.g.setAttribute('aria-current', 'step'); else o.g.removeAttribute('aria-current');
      o.pill.setAttribute('visibility', active || busy || blocked ? 'visible' : 'hidden');
      o.pt.textContent = active ? '当前 F' + f.k : blocked ? '阻塞等待' : hit ? '并行 F' + hit.k : '';
      var op = active ? (cur.dropped ? 'dropped：被下一帧替换' : cur.step.short) : hit ? 'F' + hit.k + ' · ' + hit.op : s.idle;
      if (s.id === 'hwv') op = 'V' + Math.floor(t / sim.P + 1e-6) + ' @ ' + fmt(Math.floor(t / sim.P + 1e-6) * sim.P) + ' ms · 周期 ' + fmt(sim.P);
      if (s.id === 'display') {
        var on = null; sim.display.forEach(function (d) { if (t >= d.t0 && t < d.t1) on = d; });
        op = on && on.k >= 0 ? '扫描 F' + on.k : '显示上一帧内容';
        n.scrT.textContent = on && on.k >= 0 ? 'F' + on.k : 'F−1';
        n.scr.classList.toggle('is-focus', !!on && on.k === f.k);
      }
      o.op.textContent = op;
    });
    if (cur.id !== this.lastId) {
      this.lastId = cur.id;
      var q = geo(actor);
      n.packet.setAttribute('transform', 'translate(' + (q.x + q.w - 24) + ',' + (q.y + q.h - 6) + ')');
      n.packet.classList.toggle('is-drop', !!cur.dropped);
      n.packetT.textContent = 'F' + f.k;
      n.links.innerHTML = '';
      var pairs = [];
      (FROM[cur.id] || (cur.step.from || []).map(function (id) { return AT[id]; })).forEach(function (a) { if (a && a !== actor) pairs.push([a, actor]); });
      if (TO[cur.id]) pairs.push([actor, TO[cur.id]]);
      pairs.forEach(function (pr) {
        var d = linkPath(pr[0], pr[1]);
        el('path', { 'class': 'pv-link-glow', d: d }, n.links);
        el('path', { 'class': 'pv-link' + (cur.step.ipc ? ' ipc' : ''), d: d, 'marker-end': 'url(#pvArrow)' }, n.links);
      });
      n.rip.innerHTML = '';
      if (PULSE[cur.id]) [cur.id === 'present' ? 'hwv' : 'vsync', actor].forEach(function (id) {
        var g = geo(id); el('circle', { 'class': 'pv-ripple go', cx: g.x + g.w / 2, cy: g.y + g.h / 2, r: g.w * 0.6 }, n.rip);
      });
    }
    // slots + SF inputs
    sim.slots.forEach(function (segs, r) {
      var c = null; segs.forEach(function (sg) { if (t >= sg.t0 && t < sg.t1) c = sg; });
      var o = n.slots[r];
      o.g.setAttribute('data-s', c ? c.state : 'FREE'); o.g.classList.toggle('hot', !!c && c.k === f.k);
      o.ss.textContent = c ? c.state : 'FREE'; o.so.textContent = c ? 'F' + c.k : '空闲';
    });
    var acq = t < f.queue1 ? '尚未产生' : t < f.fence ? 'pending：GPU 还在写' : 'signaled';
    n.lines[0].textContent = 'F' + f.k + ' 事务 · ' + (t < f.txn ? '未到达' : t < f.latch ? '已入队，等 VSYNC-sf' : f.dropped ? 'dropped' : '已 latch');
    n.lines[1].textContent = 'acquire fence · ' + acq;
    n.lines[2].textContent = '预期上屏 V' + Math.round(f.expected / sim.P) + (f.dropped ? ' · 被跳过' : ' · 实际 V' + Math.round(f.present / sim.P));
    n.lines.forEach(function (l, k) { l.classList.toggle('hot', (k === 0 && cur.id === 'latch') || (k === 1 && cur.id === 'gpu')); });
    // relay-style lamps
    var prev = sim.frames[f.k - 1], rel = f.k === 0 ? null : prev;
    lamp('pvArt', 'on', cur.step.packet);
    lamp('pvAcq', t < f.queue1 ? '' : t < f.fence ? 'pending' : 'signaled', acq);
    var relT = rel ? rel.release : (sim.slots[2][0] || {}).t1;
    lamp('pvRel', t < relT ? (t >= f.latch ? 'pending' : '') : 'signaled',
      (rel ? 'F' + rel.k : 'F−1') + (t < relT ? ' 仍被持有' : ' 已 release，slot 可复用'));
    var on2 = null; sim.display.forEach(function (d) { if (t >= d.t0 && t < d.t1) on2 = d; });
    lamp('pvScr', on2 && on2.k === f.k ? 'on' : '', on2 && on2.k >= 0 ? '扫描 F' + on2.k : '显示上一帧内容');
  };
  function lamp(id, state, text) {
    var c = document.getElementById(id); if (!c) return;
    c.dataset.s = state || 'none'; c.querySelector('span').textContent = text; c.title = text;
  }
})(window.FP);
