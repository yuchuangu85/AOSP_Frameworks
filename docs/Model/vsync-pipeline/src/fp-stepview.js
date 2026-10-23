/* ============================================================
   Frame Pipeline Studio · step view (causal swim-lane, one frame)
   Equally spaced nodes; a packet walks the hand-offs. Same data
   (FP.stepSeq) as the time view, so both views share one playhead.
   ============================================================ */
window.FP = window.FP || {};
(function (FP) {
  'use strict';
  var NS = 'http://www.w3.org/2000/svg';
  // Fixed pixel geometry: the SVG grows with the number of steps and the wrap scrolls (lane names stay sticky).
  var LABEL = 150, X0 = LABEL + 60, PITCH = 112, NW = 92, NH = 34, TOP = 70, GAP = 66;
  FP.DWELL = 0.7;
  function el(tag, attrs, parent, text) {
    var e = document.createElementNS(NS, tag);
    // Paint values given as CSS variables must go through style (presentation attributes ignore var()).
    for (var k in attrs) {
      var v = attrs[k];
      if ((k === 'fill' || k === 'stroke') && String(v).indexOf('var(') === 0) e.style[k] = v; else e.setAttribute(k, v);
    }
    if (text != null) e.textContent = text;
    if (parent) parent.appendChild(e); return e;
  }

  FP.StepView = function (svg) { this.svg = svg; };
  var SV = FP.StepView.prototype;

  SV.build = function (seq, frame, onJump) {
    var svg = this.svg, self = this;
    svg.innerHTML = '';
    var laneY = {}; FP.LANES.forEach(function (l, i) { laneY[l.key] = TOP + i * GAP; });
    var H = TOP + FP.LANES.length * GAP;
    var n = seq.length, step = PITCH, W = X0 + (n - 1) * PITCH + NW / 2 + 40;
    svg.setAttribute('viewBox', '0 0 ' + W + ' ' + H);
    svg.setAttribute('width', W); svg.setAttribute('height', H);
    svg.style.width = W + 'px'; svg.style.height = H + 'px';
    this.W = W;
    // sticky lane-name column (HTML, so it can stay put while the SVG scrolls)
    var col = document.getElementById('stepLanes');
    if (col) {
      col.style.height = H + 'px';
      col.innerHTML = '<span class="sv-lane vs" style="top:' + (32 - 8) + 'px">VSYNC</span>' + FP.LANES.map(function (l) {
        return '<span class="sv-lane" style="top:' + (laneY[l.key] - 9) + 'px;color:' + l.color + '">' + FP.esc(l.name) + '</span>';
      }).join('');
    }
    var pos = seq.map(function (s, i) { return { x: X0 + i * step, y: laneY[s.lane] }; });
    this.seq = seq; this.pos = pos;

    var defs = el('defs', {}, svg);
    var mk = el('marker', { id: 'svArr', viewBox: '0 0 10 10', refX: 9, refY: 5, markerWidth: 7, markerHeight: 7, orient: 'auto-start-reverse' }, defs);
    el('path', { d: 'M0,0 L10,5 L0,10 z', fill: 'var(--sv-dim)' }, mk);
    var flt = el('filter', { id: 'svGlow', x: '-50%', y: '-50%', width: '200%', height: '200%' }, defs);
    el('feGaussianBlur', { stdDeviation: 3.5, result: 'b' }, flt);
    var fm = el('feMerge', {}, flt); el('feMergeNode', { in: 'b' }, fm); el('feMergeNode', { in: 'SourceGraphic' }, fm);

    FP.LANES.forEach(function (l) {
      el('line', { x1: LABEL, x2: W - 10, y1: laneY[l.key], y2: laneY[l.key], stroke: l.color, 'stroke-opacity': 0.22, 'stroke-dasharray': '2 4' }, svg);
    });
    el('text', { x: W - 12, y: H - 8, fill: 'var(--sv-dim)', 'font-size': 11, 'text-anchor': 'end' }, svg,
      'F' + frame.k + ' · 因果顺序（等距，不按时间比例）');

    this.vs = [];
    seq.forEach(function (s, i) {
      if (!s.step.vsync) return;
      var g = el('g', { opacity: 0.4 }, svg), x = pos[i].x;
      el('line', { x1: x, x2: x, y1: 44, y2: H - 22, stroke: 'var(--lane-sched)', 'stroke-dasharray': '4 4' }, g);
      el('rect', { x: x - 54, y: 20, width: 108, height: 18, rx: 9, fill: 'var(--sv-vs-bg)', stroke: 'var(--lane-sched)' }, g);
      el('text', { x: x, y: 33, fill: 'var(--sv-vs-text)', 'font-size': 11, 'text-anchor': 'middle', 'font-weight': 600 }, g, s.step.vsync + ' ' + FP.fmt(s.t0));
      self.vs.push({ i: i, g: g });
    });

    // causal edges (from the catalogue), drawn under the nodes
    var idx = {}; seq.forEach(function (s, i) { idx[s.id] = i; });
    this.edges = [];
    seq.forEach(function (s, i) {
      (s.step.from || []).forEach(function (fid) {
        var j = idx[fid]; if (j == null) return;
        var a = pos[j], b = pos[i], ipc = s.step.ipc && fid !== 'gpu';
        var d = a.y === b.y ? 'M' + (a.x + NW / 2) + ',' + a.y + ' H' + (b.x - NW / 2)
          : 'M' + (a.x + NW / 2) + ',' + a.y + ' C' + (a.x + NW / 2 + 30) + ',' + a.y + ' ' + (b.x - NW / 2 - 30) + ',' + b.y + ' ' + (b.x - NW / 2) + ',' + b.y;
        var p = el('path', { d: d, fill: 'none', stroke: 'var(--sv-edge)', 'stroke-width': 1.6, 'marker-end': 'url(#svArr)', 'stroke-dasharray': ipc ? '5 4' : 'none' }, svg);
        self.edges.push({ el: p, to: i });
      });
    });

    this.nodes = seq.map(function (s, i) {
      var c = FP.LANE[s.lane].color, x = pos[i].x, y = pos[i].y;
      var g = el('g', { 'class': 'sv-node', tabindex: 0, role: 'button', 'aria-label': '第 ' + (i + 1) + ' 步：' + s.step.title }, svg);
      var r = el('rect', { x: x - NW / 2, y: y - NH / 2, width: NW, height: NH, rx: 7, fill: 'var(--sv-node)', stroke: s.dropped ? 'var(--hot)' : c, 'stroke-width': 1.4 }, g);
      el('text', { x: x, y: y + 4, fill: s.dropped ? 'var(--hot)' : 'var(--sv-text)', 'font-size': 10.5, 'text-anchor': 'middle' }, g, s.dropped ? 'dropped' : s.step.short);
      el('circle', { cx: x - NW / 2 + 1, cy: y - NH / 2 + 1, r: 8, fill: c }, g);
      el('text', { x: x - NW / 2 + 1, y: y - NH / 2 + 4.5, fill: 'var(--sv-badge-text)', 'font-size': 9.5, 'font-weight': 700, 'text-anchor': 'middle' }, g, String(i + 1));
      el('text', { x: x, y: y + NH / 2 + 13, fill: 'var(--sv-dim)', 'font-size': 9.5, 'text-anchor': 'middle' }, g, FP.fmt(s.t0));
      g.addEventListener('click', function () { onJump(i); });
      g.addEventListener('keydown', function (e) { if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); onJump(i); } });
      return { g: g, r: r };
    });
    this.packet = el('circle', { r: 7, fill: 'var(--sv-text)', 'class': 'sv-packet', filter: 'url(#svGlow)' }, svg);
    this.last = -1;
  };

  // Keep the moving packet ~35% into the visible area (right of the sticky lane names).
  SV.follow = function (smooth) {
    var wrap = document.getElementById('stepScroll'); if (!wrap || !this.packet) return;
    var x = Number(this.packet.getAttribute('cx')) || 0, view = wrap.clientWidth;
    var target = Math.max(0, x - LABEL - (view - LABEL) * 0.35);
    if (Math.abs(wrap.scrollLeft - target) < 2) return;
    if (smooth) wrap.scrollTo({ left: target, behavior: 'smooth' }); else wrap.scrollLeft = target;
  };

  SV.setPos = function (p) {
    if (!this.seq || !this.seq.length) return;
    var n = this.seq.length, i = Math.min(n - 1, Math.max(0, Math.floor(p))), f = p - i;
    if (i !== this.last) {
      this.last = i;
      this.nodes.forEach(function (nd, k) {
        nd.r.style.fill = k === i ? 'var(--sv-node-cur)' : k < i ? 'var(--sv-node-done)' : 'var(--sv-node)';
        nd.r.setAttribute('stroke-width', k === i ? 3 : 1.4);
        if (k === i) nd.r.setAttribute('filter', 'url(#svGlow)'); else nd.r.removeAttribute('filter');
        nd.g.setAttribute('aria-current', k === i ? 'step' : 'false');
      });
      this.edges.forEach(function (e) { e.el.style.stroke = e.to <= i ? 'var(--sv-edge-on)' : 'var(--sv-edge)'; });
    }
    this.vs.forEach(function (v) { v.g.setAttribute('opacity', v.i === i ? 0.6 + 0.4 * Math.abs(Math.sin(f * Math.PI * 3)) : 0.35); });
    var a = this.pos[i], b = this.pos[Math.min(n - 1, i + 1)], q = f > FP.DWELL ? (f - FP.DWELL) / (1 - FP.DWELL) : 0;
    this.packet.setAttribute('cx', a.x + (b.x - a.x) * q);
    this.packet.setAttribute('cy', a.y + (b.y - a.y) * q - NH / 2 - 2);
    this.packet.style.fill = FP.LANE[this.seq[i].lane].color;
  };
})(window.FP);
