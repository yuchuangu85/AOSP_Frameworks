/* ============================================================
   Frame Pipeline Studio · hand-off arrows on the time view
   Perfetto-style flow arrows between lanes for the focus frame.
   ============================================================ */
window.FP = window.FP || {};
(function (FP) {
  'use strict';
  var NS = 'http://www.w3.org/2000/svg';
  // What crosses the boundary on each cross-lane edge.
  FP.EDGE_LABEL = {
    'req>vreq': '', 'vreq>vapp': 'requestNextVsync · binder', 'vapp>doframe': 'VSYNC 事件 · BitTube',
    'draw>sync': '', 'sync>rtdraw': 'syncFrameState', 'rtdraw>gpu': 'GPU 命令', 'queue>txn': 'Transaction · oneway binder',
    'txn>vsf': 'scheduleCommit', 'vsf>latch': 'commit()', 'gpu>latch': 'acquire fence', 'hwc>present': 'present fence',
    'present>release': 'release fence'
  };
  function el(tag, attrs, parent) {
    var e = document.createElementNS(NS, tag);
    for (var k in attrs) e.setAttribute(k, attrs[k]);
    if (parent) parent.appendChild(e); return e;
  }

  FP.drawArrows = function (tb) {
    var body = tb.body, svg = body.querySelector('svg.fp-arrows');
    var labels = body.querySelector('.fp-edge-labels');
    if (!svg) { svg = el('svg', { 'class': 'fp-arrows', 'aria-hidden': 'true' }, body); }
    if (!labels) { labels = FP.h('div', 'fp-edge-labels', body); labels.setAttribute('aria-hidden', 'true'); }
    svg.innerHTML = ''; labels.innerHTML = '';
    tb.arrowItems = [];
    if (!tb.opt || !tb.opt.arrows || !tb.geo) { svg.style.display = 'none'; return; }
    svg.style.display = '';
    svg.setAttribute('width', body.scrollWidth); svg.setAttribute('height', body.scrollHeight);
    var defs = el('defs', {}, svg);
    [['fpArr', 'var(--arrow)'], ['fpArrOn', 'var(--arrow-on)']].forEach(function (m) {
      var mk = el('marker', { id: m[0], viewBox: '0 0 10 10', refX: 9, refY: 5, markerWidth: 6, markerHeight: 6, orient: 'auto-start-reverse' }, defs);
      el('path', { d: 'M0,0 L10,5 L0,10 z' }, mk).style.fill = m[1];
    });

    var f = tb.sim.frames[tb.opt.focus]; if (!f) return;
    var S = FP.stepTimes(f), total = tb.sim.total;
    FP.STEPS.forEach(function (st) {
      var dst = S[st.id]; if (!dst || !st.from) return;
      st.from.forEach(function (fid) {
        var src = S[fid]; if (!src || src.lane === dst.lane) return;
        if (src.t1 > total || dst.t0 > total) return;
        var x1 = tb.xAt(src.t1), y1 = tb.laneY(src.lane), x2 = tb.xAt(dst.t0), y2 = tb.laneY(dst.lane);
        if (x2 < x1) x2 = x1;
        var dx = Math.max(14, (x2 - x1) * 0.5), ipc = st.ipc && fid !== 'gpu';
        var path = el('path', {
          d: 'M' + x1 + ',' + y1 + ' C' + (x1 + dx) + ',' + y1 + ' ' + (x2 - dx) + ',' + y2 + ' ' + x2 + ',' + y2,
          'class': 'fp-arrow' + (ipc ? ' ipc' : '') + (fid === 'gpu' ? ' fence' : ''), 'marker-end': 'url(#fpArr)'
        }, svg);
        var item = { el: path, t0: src.t1, t1: dst.t0, kind: 'arrow' };
        var text = FP.EDGE_LABEL[fid + '>' + st.id];
        if (text) {
          var lb = FP.h('span', 'fp-edge-label' + (ipc ? ' ipc' : ''), labels, FP.esc(text));
          lb.style.left = ((x1 + x2) / 2) + 'px'; lb.style.top = ((y1 + y2) / 2) + 'px';
          item.label = lb;
        }
        tb.arrowItems.push(item);
      });
    });
    if (tb.lastT != null) FP.updateArrows(tb, tb.lastT);
  };

  FP.updateArrows = function (tb, t) {
    tb.lastT = t;
    (tb.arrowItems || []).forEach(function (a) {
      var on = t >= a.t0 - 0.05 && t <= a.t1 + 0.6, past = t > a.t1 + 0.6;
      a.el.classList.toggle('is-active', on); a.el.classList.toggle('is-past', past);
      a.el.setAttribute('marker-end', on ? 'url(#fpArrOn)' : 'url(#fpArr)');
      if (a.label) { a.label.classList.toggle('is-active', on); a.label.classList.toggle('is-past', past); }
    });
  };
})(window.FP);
