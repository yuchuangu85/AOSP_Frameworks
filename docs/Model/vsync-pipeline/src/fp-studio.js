/* ============================================================
   Frame Pipeline Studio · controller
   One simulation, two views (time / step), one playhead.
   ============================================================ */
window.FP = window.FP || {};
(function (FP) {
  'use strict';
  var $ = function (id) { return document.getElementById(id); };
  var reduceMotion = window.matchMedia('(prefers-reduced-motion: reduce)').matches;
  var TIME_SCALE = 0.012, STEP_SEC = 1.7;
  var S = { preset: 'relay', params: null, mode: 'time', focus: 0, showOthers: true, arrows: true, zoom: '4', follow: true,
            t: 0, pos: 0, playing: false, speed: 1, lastTs: 0, raf: 0 };
  var sim, seq, board, stepView, procView, panel, autoplayDone = false;

  function seqTime(p) {
    var n = seq.length, i = Math.min(n - 1, Math.max(0, Math.floor(p))), f = p - i;
    var a = seq[i].at, b = i + 1 < n ? seq[i + 1].at : a, mid = Math.min(b, Math.max(a, seq[i].t1));
    return f <= FP.DWELL ? a + (mid - a) * (f / FP.DWELL) : mid + (b - mid) * ((f - FP.DWELL) / (1 - FP.DWELL));
  }
  // An explicit step jump wins while time stays on that step's anchor (several steps can share one instant).
  function curIndex() {
    if (S.mode !== 'time') return Math.min(seq.length - 1, Math.floor(S.pos));
    if (S.lock != null && seq[S.lock] && Math.abs(seq[S.lock].at - S.t) < 1e-9) return S.lock;
    return FP.seqIndexAt(seq, S.t);
  }

  // smooth: animate the follow-scroll (jumps); playback scrolls instantly every frame.
  function render(smooth) {
    var i = curIndex(), cur = seq[i], frac;
    if (S.mode === 'step') { stepView.setPos(S.pos); if (S.follow) stepView.follow(!!smooth); frac = Math.min(1, (S.pos - i) / FP.DWELL); }
    else if (S.mode === 'proc') { procView.update(S.t, i); frac = Math.min(1, (S.pos - i) / FP.DWELL); }
    else { board.setTime(S.t); FP.updateArrows(board, S.t); if (S.follow) board.follow(S.t, !!smooth); frac = cur.t1 > cur.t0 ? Math.min(1, Math.max(0, (S.t - cur.t0) / (cur.t1 - cur.t0))) : (S.t >= cur.t0 ? 1 : 0); }
    panel.update(sim, sim.frames[S.focus], cur, S.t, { index: i, count: seq.length, frac: frac });
    FP.markSummary(cur.id);
    $('scrub').value = String(S.t); $('timeNow').textContent = FP.fmt(S.t);
  }

  function recompute() {
    sim = FP.simulate(S.params);
    S.focus = Math.min(S.focus, sim.shown.length - 1); S.lock = null;
    var f = sim.frames[S.focus];
    seq = FP.stepSeq(f);
    board.build(sim, { focus: S.focus, showOthers: S.showOthers, arrows: S.arrows, zoom: S.zoom });
    $('zoomHint').textContent = FP.fmt(board.pxms) + ' px/ms · 时间轴 ' + Math.round(sim.total * board.pxms) + ' px';
    stepView.build(seq, f, jumpStepIndex);
    procView.build(sim, seq, f, jumpStepIndex);
    panel.buildSlots(sim); panel.lastStep = null;
    S.t = Math.min(S.t, sim.total);
    if (S.mode !== 'time') S.pos = FP.seqIndexAt(seq, S.t);
    $('scrub').max = String(sim.total); $('timeTotal').textContent = FP.fmt(sim.total);
    syncControls();
    FP.currentPreset = S.preset; FP.updateSummary(sim, f);
    var vt = { ontime: '按时上屏', late: '晚 ' + f.late + ' 帧', dropped: '被跳过' }[f.verdict];
    $('studioLive').innerHTML = '<i></i>F' + S.focus + ' · ' + sim.p.fps + ' Hz';
    $('studioBadges').innerHTML = '<span class="tag"><b>P</b> ' + FP.fmt(sim.P) + ' ms</span><span class="tag"><b>app</b> ' + FP.fmt(sim.aD) +
      ' ms</span><span class="tag"><b>sf</b> ' + FP.fmt(sim.sD) + ' ms</span><span class="tag v-' + f.verdict + '"><b>F' + S.focus + '</b> ' + vt + '</span>';
    render();
    try {
      document.documentElement.setAttribute('data-qa', JSON.stringify({ preset: S.preset, mode: S.mode, focus: S.focus, total: sim.total,
        verdicts: sim.shown.map(function (g) { return g.verdict; }), blocks: document.querySelectorAll('#lanes .blk').length,
        stages: document.querySelectorAll('section.stage').length, snippets: document.querySelectorAll('figure.snippet').length }));
    } catch (e) { /* QA hook only */ }
  }

  function seg(id, attr, val) {
    Array.prototype.forEach.call($(id).querySelectorAll('button'), function (b) { b.classList.toggle('on', b.dataset[attr] === String(val)); });
  }
  function syncControls() {
    var p = S.params;
    seg('presetSeg', 'preset', S.preset); seg('modeSeg', 'mode', S.mode); seg('focusSeg', 'focus', S.focus);
    seg('fpsSeg', 'fps', p.fps); seg('durSeg', 'dur', p.dur); seg('zoomSeg', 'zoom', S.zoom);
    $('followToggle').checked = S.follow; $('zoomSeg').closest('.fp-row').hidden = S.mode !== 'time' && S.mode !== 'step';
    $('zoomSeg').hidden = $('zoomHint').hidden = S.mode !== 'time';
    Array.prototype.forEach.call($('focusSeg').querySelectorAll('button'), function (b) { b.disabled = Number(b.dataset.focus) >= sim.shown.length; });
    $('presetNote').textContent = S.preset === 'custom' ? '自定义参数：' + (FP.PRESET[S.base] ? '基于「' + FP.PRESET[S.base].label + '」修改。' : '') : FP.PRESET[S.preset].note;
    [['aD', sim.aD], ['sD', sim.sD]].forEach(function (x) { var r = $(x[0] + 'Range'); r.value = x[1]; r.disabled = p.dur !== 'custom'; $(x[0] + 'V').textContent = FP.fmt(x[1]) + ' ms'; });
    ['ui', 'rt', 'gpu', 'sf'].forEach(function (k) { $(k + 'Load').value = p.load[k] || 1; $(k + 'LoadV').textContent = (p.load[k] || 1).toFixed(1) + '×'; });
    $('contToggle').checked = p.cont; $('othersToggle').checked = S.showOthers; $('arrowsToggle').checked = S.arrows;
    $('boardWrap').hidden = S.mode !== 'time'; $('stepWrap').hidden = S.mode !== 'step'; $('procWrap').hidden = S.mode !== 'proc';
    $('boardLegend').dataset.mode = S.mode;
  }

  function applyPreset(key) {
    var pr = FP.PRESET[key]; if (!pr) return;
    S.preset = key; S.base = key; S.params = FP.paramsFromPreset(key); S.focus = pr.focus; S.t = 0; S.pos = 0;
    recompute();
  }
  function custom(mut) { if (S.preset !== 'custom') { S.base = S.preset; S.preset = 'custom'; } mut(S.params); recompute(); }

  function setMode(m) {
    if (m === S.mode) return;
    S.mode = m; if (m !== 'time') S.pos = FP.seqIndexAt(seq, S.t);
    syncControls();
    if (m === 'time') { if (S.zoom === 'fit') recompute(); else board.measure(); }
    render(true);
  }
  function jumpStepIndex(i) {
    pause(); i = Math.max(0, Math.min(seq.length - 1, i));
    S.pos = i; S.t = seq[i].at; S.lock = i; render(true);
  }
  FP.jumpToStage = function (num) {
    var i = -1; seq.forEach(function (s, k) { if (i < 0 && s.step.stage === num) i = k; });
    autoplayDone = true; if (i >= 0) jumpStepIndex(i);
    var a = $('anatomy'); if (a) a.scrollIntoView({ behavior: reduceMotion ? 'auto' : 'smooth', block: 'start' });
  };
  FP.jumpToStep = function (id) {
    var i = -1; seq.forEach(function (s, k) { if (s.id === id) i = k; });
    if (i < 0) { FP.STEP[id] && FP.jumpToStage(FP.STEP[id].stage); return; }   // step skipped in this scenario
    autoplayDone = true; jumpStepIndex(i);
    var a = $('anatomy'); if (a) a.scrollIntoView({ behavior: reduceMotion ? 'auto' : 'smooth', block: 'start' });
  };
  FP.runPreset = function (key) {
    autoplayDone = true; applyPreset(key);
    var a = $('anatomy'); if (a) a.scrollIntoView({ behavior: reduceMotion ? 'auto' : 'smooth', block: 'start' });
    setTimeout(play, reduceMotion ? 0 : 450);
  };

  function tick(ts) {
    if (!S.playing) return;
    var dt = S.lastTs ? Math.min(0.1, (ts - S.lastTs) / 1000) * S.speed : 0; S.lastTs = ts;
    if (S.mode !== 'time') { S.pos += dt / STEP_SEC; if (S.pos >= seq.length) S.pos = 0; S.t = seqTime(S.pos); }
    else { S.t += dt * 1000 * TIME_SCALE; if (S.t >= sim.total) S.t = 0; }
    render(); S.raf = requestAnimationFrame(tick);
  }
  function play() { if (S.playing) return; S.playing = true; S.lastTs = 0; $('playIcon').innerHTML = '<path d="M7 5h4v14H7zM13 5h4v14h-4z"/>'; S.raf = requestAnimationFrame(tick); }
  function pause() { S.playing = false; cancelAnimationFrame(S.raf); $('playIcon').innerHTML = '<path d="M8 5v14l11-7z"/>'; }
  FP.pause = pause;

  function bindSeg(id, attr, fn) {
    $(id).addEventListener('click', function (e) { var b = e.target.closest('button'); if (b && !b.disabled) fn(b.dataset[attr]); });
  }
  function inView() { var r = $('studio').getBoundingClientRect(); return r.bottom > 100 && r.top < window.innerHeight; }

  FP.initStudio = function () {
    if (!$('studio')) return;
    board = new FP.TimeBoard(); stepView = new FP.StepView($('stepSvg')); procView = new FP.ProcView($('procSvg')); panel = new FP.Panel();
    FP.buildSummary(); FP.buildMyths();
    $('presetSeg').innerHTML = FP.PRESETS.map(function (p) { return '<button data-preset="' + p.key + '">' + p.label + '</button>'; }).join('') +
      '<button data-preset="custom" disabled>自定义</button>';
    var qs; try { qs = new URLSearchParams(location.search); } catch (e) { qs = null; }
    var key = qs && (qs.get('preset') || (qs.get('jank') === '1' ? 'ui' : null));
    S.mode = qs && (qs.get('mode') === 'step' || qs.get('mode') === 'proc') ? qs.get('mode') : 'time';
    if (qs && /^(fit|2|4|6)$/.test(qs.get('zoom') || '')) S.zoom = qs.get('zoom');
    if (qs && qs.get('follow') === '0') S.follow = false;
    applyPreset(FP.PRESET[key] ? key : 'relay');
    if (qs && qs.has('focus')) { S.focus = Number(qs.get('focus')) || 0; recompute(); }
    if (qs && qs.has('t')) { S.t = Math.min(sim.total, Number(qs.get('t')) || 0); S.pos = FP.seqIndexAt(seq, S.t); render(); }

    bindSeg('presetSeg', 'preset', function (k) { autoplayDone = true; applyPreset(k); });
    bindSeg('modeSeg', 'mode', setMode);
    bindSeg('focusSeg', 'focus', function (k) { S.focus = Number(k); recompute(); });
    bindSeg('fpsSeg', 'fps', function (v) { custom(function (p) { p.fps = Number(v); }); });
    bindSeg('durSeg', 'dur', function (v) { custom(function (p) { if (v === 'custom') { p.aD = sim.aD; p.sD = sim.sD; } p.dur = v; }); });
    ['aD', 'sD'].forEach(function (k) { $(k + 'Range').addEventListener('input', function (e) { custom(function (p) { p[k] = Number(e.target.value); }); }); });
    ['ui', 'rt', 'gpu', 'sf'].forEach(function (k) { $(k + 'Load').addEventListener('input', function (e) { custom(function (p) { p.load[k] = Number(e.target.value); }); }); });
    $('contToggle').addEventListener('change', function (e) { custom(function (p) { p.cont = e.target.checked; p.frames = e.target.checked ? 3 : 2; }); });
    $('othersToggle').addEventListener('change', function (e) { S.showOthers = e.target.checked; recompute(); });
    $('arrowsToggle').addEventListener('change', function (e) { S.arrows = e.target.checked; recompute(); });
    $('playBtn').addEventListener('click', function () { autoplayDone = true; S.playing ? pause() : play(); });
    $('restartBtn').addEventListener('click', function () { autoplayDone = true; S.t = 0; S.pos = 0; render(); play(); });
    $('prevStep').addEventListener('click', function () { jumpStepIndex(curIndex() - 1); });
    $('nextStep').addEventListener('click', function () { jumpStepIndex(curIndex() + 1); });
    $('scrub').addEventListener('input', function (e) { pause(); S.t = Number(e.target.value); S.pos = FP.seqIndexAt(seq, S.t); render(); });
    bindSeg('zoomSeg', 'zoom', function (z) { S.zoom = z; recompute(); render(true); });
    $('followToggle').addEventListener('change', function (e) { S.follow = e.target.checked; if (S.follow) render(true); });
    // Any manual horizontal scroll means the reader wants to look around: stop following.
    ['boardWrap', 'stepScroll'].forEach(function (id) {
      var w = $(id), off = function () { if (S.follow) { S.follow = false; $('followToggle').checked = false; } };
      w.addEventListener('wheel', function (e) { if (Math.abs(e.deltaX) > Math.abs(e.deltaY) || e.shiftKey) off(); }, { passive: true });
      w.addEventListener('pointerdown', function (e) { if (e.target === w || e.offsetY > w.clientHeight - 14) off(); });
      w.addEventListener('touchstart', off, { passive: true });
    });
    bindSeg('speedSeg', 'speed', function (v) { S.speed = Number(v) || 1; seg('speedSeg', 'speed', v); });
    document.addEventListener('keydown', function (e) {
      var tag = (e.target.tagName || '').toLowerCase();
      if (tag === 'input' || tag === 'textarea' || tag === 'select' || !inView()) return;
      if (e.code === 'Space' && tag !== 'button' && !(e.target.closest && e.target.closest('.sv-node'))) { e.preventDefault(); S.playing ? pause() : play(); }
      else if (e.key === 'ArrowRight') { e.preventDefault(); jumpStepIndex(curIndex() + 1); }
      else if (e.key === 'ArrowLeft') { e.preventDefault(); jumpStepIndex(curIndex() - 1); }
    });
    var lastW = 0;
    if (window.ResizeObserver) new ResizeObserver(function () {
      if (S.mode !== 'time') return;
      var w = $('boardWrap').clientWidth;
      if (S.zoom === 'fit' && Math.abs(w - lastW) > 40) { lastW = w; recompute(); } else board.measure();
    }).observe($('boardWrap'));
    document.addEventListener('visibilitychange', function () { if (document.hidden) pause(); });
    if (qs && qs.get('pause') === '1') autoplayDone = true;
    if (qs && qs.get('play') === '1') { autoplayDone = true; play(); }
    if (!reduceMotion && window.IntersectionObserver) {
      var io = new IntersectionObserver(function (en) {
        en.forEach(function (x) { if (x.isIntersecting && !autoplayDone) { autoplayDone = true; play(); io.disconnect(); } });
      }, { threshold: 0.35 });
      io.observe($('studio'));
    }
  };
})(window.FP);
