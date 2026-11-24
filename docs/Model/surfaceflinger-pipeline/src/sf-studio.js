/* ============================================================
   RenderThread studio: one frame, five lanes, nine stages.
   ============================================================ */
(function () {
  'use strict';

  var D = window.PIPELINE_DATA || { stages: [] };
  var STAGES = D.stages || [];
  var LANE_ORDER = ['APP', 'SCHED', 'LAYER', 'COMP', 'DISP'];
  var LANE_META = {
    APP:   { name: 'APP · BLAST', sub: 'Transaction / BufferQueue' },
    SCHED: { name: 'SF · Scheduler', sub: 'MessageQueue / commit / composite' },
    LAYER: { name: 'SF · Layer', sub: 'Snapshot / latch / fences' },
    COMP:  { name: 'Composition / HWC', sub: 'CompositionEngine / RenderEngine / present' },
    DISP:  { name: 'Display / Timeline', sub: 'scanout / retire / release' }
  };
  var LANE_COLORS = { APP: '#4ade80', SCHED: '#f472b6', LAYER: '#a78bfa', COMP: '#fb923c', DISP: '#fb7185' };
  var SCENARIOS = window.__SF_SCENARIOS__ || {};
  var scenario = SCENARIOS.normal;
  var reduceMotion = window.matchMedia && matchMedia('(prefers-reduced-motion: reduce)').matches;
  var TIME_SCALE = 0.006; // 1x: 34ms SF cycle ≈ 5.7s real time
  var t = 0, playing = false, lastTs = 0, speed = 1, rafId = null, autoplayDone = false;
  var els = {}, geo = null, blockEls = [], tokenEls = {}, pinEls = [], slotEls = [];

  function $(s, r) { return (r || document).querySelector(s); }
  function $all(s, r) { return Array.prototype.slice.call((r || document).querySelectorAll(s)); }
  function clamp(v, a, b) { return Math.max(a, Math.min(b, v)); }
  function pct(time) { return clamp(time / scenario.total, 0, 1) * 100; }
  function fmt(v) { return (Math.round(v * 10) / 10).toFixed(1); }
  function stageByNum(n) { for (var i = 0; i < STAGES.length; i++) if (STAGES[i].num === n) return STAGES[i]; return null; }

  /* ---------------- board ---------------- */
  function buildBoard() {
    var pins = $('#pins'), ruler = $('#ruler'), lanes = $('#lanes');
    pins.innerHTML = ''; ruler.innerHTML = ''; lanes.innerHTML = '';
    blockEls = []; tokenEls = {}; pinEls = [];

    // stage pins with collision-aware rows
    var pinRows = 0, pinEnds = [];
    scenario.pins.slice().sort(function (a, b) { return a.t - b.t; }).forEach(function (p) {
      var d = document.createElement('div');
      d.className = 'pin';
      d.style.left = pct(p.t) + '%';
      d.dataset.stage = p.stage;
      d.dataset.t = String(p.t);
      var r = 0;
      while (r < pinEnds.length && pinEnds[r] + 2.2 > pct(p.t)) r++;
      if (r === pinEnds.length) pinEnds.push(0);
      pinEnds[r] = pct(p.t);
      pinRows = Math.max(pinRows, r + 1);
      d.style.top = (r * 19) + 'px';
      var st = stageByNum(p.stage);
      d.innerHTML = '<span class="pin-n">' + String(p.stage).padStart(2, '0') + '</span><span class="pin-stem"></span>' +
        '<span class="pin-tip">Stage ' + String(p.stage).padStart(2, '0') + ' · ' + (st ? st.title : '') + '</span>';
      pins.appendChild(d); pinEls.push(d);
    });
    pins.style.height = Math.max(26, 20 + pinRows * 19) + 'px';

    // frame ruler
    var rline = document.createElement('div'); rline.className = 'ruler-line'; ruler.appendChild(rline);
    scenario.vsyncs.forEach(function (time, idx) {
      var v = document.createElement('div');
      v.className = 'vsync' + (idx === 0 ? ' first' : '') + (idx === scenario.vsyncs.length - 1 ? ' last' : '');
      v.style.left = pct(time) + '%';
      v.innerHTML = '<span class="vlab">' + scenario.vsyncLabels[idx] + '</span><span class="vdia"></span><span class="vline"></span>' +
        '<span class="vsub' + (idx === 0 ? '' : ' left') + '">' + fmt(time) + ' ms</span>';
      ruler.appendChild(v);
    });

    // callout row layout (shared by all lanes so blocks align)
    var laneCalls = {}, maxRows = 0;
    LANE_ORDER.forEach(function (lane) {
      var calls = (scenario.lanes[lane] || []).filter(function (b) { return b.callout; }).map(function (b) {
        return { b: b, x: pct(b.t0), w: (b.callout.length * 5.4 + 56) / 1100 * 100, row: 0 };
      }).sort(function (a, b) { return a.x - b.x; });
      var ends = [];
      calls.forEach(function (it) {
        var r = 0;
        while (r < ends.length && ends[r] + 0.8 > it.x) r++;
        if (r === ends.length) ends.push(0);
        ends[r] = it.x + it.w; it.row = r;
      });
      laneCalls[lane] = calls;
      maxRows = Math.max(maxRows, ends.length);
    });
    var blockTop = 26 + maxRows * 15;
    var laneH = Math.max(82, blockTop + 32 + 14);

    LANE_ORDER.forEach(function (lane) {
      var meta = LANE_META[lane];
      var laneEl = document.createElement('div');
      laneEl.className = 'lane'; laneEl.dataset.lane = lane;
      laneEl.style.height = laneH + 'px';
      laneEl.innerHTML = '<div class="lane-label"><span class="ll-top"><span class="dot"></span>' + meta.name +
        '</span><small>' + meta.sub + '</small></div><div class="lane-track" data-track="' + lane + '"></div>';
      lanes.appendChild(laneEl);
      var track = $('.lane-track', laneEl);

      var token = document.createElement('div');
      token.className = 'lane-token';
      token.style.top = (blockTop - 10) + 'px';
      track.appendChild(token); tokenEls[lane] = token;

      // event-only lanes
      if (lane === 'BQ' || lane === 'FENCE') {
        (scenario.lanes[lane] || []).forEach(function (ev) {
          var e = document.createElement('div'); e.className = 'ev'; e.style.left = pct(ev.t) + '%';
          var l = document.createElement('div'); l.className = 'ev-label ' + (ev.pos || 'above'); l.style.left = pct(ev.t) + '%';
          if (pct(ev.t) > 97) l.style.transform = 'translateX(-100%)';
          if (ev.hot) { e.style.setProperty('--lane', '#f87171'); l.style.color = '#fecaca'; }
          l.textContent = ev.label;
          track.appendChild(e); track.appendChild(l);
        });
        (scenario.misses || []).forEach(function (miss) {
          if ((miss.lane || 'RT') !== lane) return;
          var mk = document.createElement('div');
          mk.className = 'miss-marker' + (miss.label ? '' : ' unlabeled') + (pct(miss.t) > 88 ? ' right' : '');
          mk.style.left = pct(miss.t) + '%';
          if (miss.title) mk.title = miss.title;
          mk.innerHTML = '<span class="mm-line"></span>' + (miss.label ? '<span class="mm-label">' + miss.label + '</span>' : '');
          track.appendChild(mk);
        });
        return;
      }

      (laneCalls[lane] || []).forEach(function (it) {
        var co = document.createElement('div');
        co.className = 'blk-callout' + ((it.x + it.w > 99) ? ' right' : '');
        co.style.left = it.x + '%';
        co.style.top = (2 + it.row * 15) + 'px';
        co.innerHTML = it.b.callout + '<small>STAGE ' + String(it.b.stage).padStart(2, '0') + '</small>';
        track.appendChild(co);
      });

      (scenario.lanes[lane] || []).forEach(function (b) {
        var el = document.createElement('div');
        el.className = 'blk' + (b.dashed ? ' is-dashed' : '') + (b.hot ? ' is-hot' : '');
        el.style.left = pct(b.t0) + '%';
        el.style.width = Math.max(pct(b.t1) - pct(b.t0), 0.5) + '%';
        el.style.top = blockTop + 'px';
        if (b.hot) el.style.setProperty('--lane', '#f87171');
        if (b.hideLabel) el.classList.add('no-label');
        el.title = b.title || b.label || '';
        el.innerHTML = '<span class="blk-label">' + (b.label || '') + '</span>';
        track.appendChild(el);
        blockEls.push({ el: el, b: b, lane: lane });
      });

      (scenario.misses || []).forEach(function (miss) {
        if ((miss.lane || 'RT') !== lane) return;
        var mk = document.createElement('div');
        mk.className = 'miss-marker' + (miss.label ? '' : ' unlabeled') + (pct(miss.t) > 88 ? ' right' : '');
        mk.style.left = pct(miss.t) + '%';
        if (miss.title) mk.title = miss.title;
        mk.innerHTML = '<span class="mm-line"></span>' + (miss.label ? '<span class="mm-label">' + miss.label + '</span>' : '');
        track.appendChild(mk);
      });
    });

    rebuildSlots();
    els.boardBody = $('#boardBody');
    els.playhead = $('#playhead');
    els.playheadTime = $('.ph-time', els.playhead);
    measureGeometry();
  }

  function measureGeometry() {
    if (!els.boardBody) return;
    var track = $('.lane-track', els.boardBody);
    if (!track) return;
    var br = els.boardBody.getBoundingClientRect(), tr = track.getBoundingClientRect();
    geo = { pad: tr.left - br.left, width: tr.width };
    positionPlayhead();
  }
  function positionPlayhead() {
    if (!geo) return;
    els.playhead.style.left = (geo.pad + pct(t) / 100 * geo.width) + 'px';
  }

  /* ---------------- slots ---------------- */
  function slotState(slot, time) {
    var st = { state: 'FREE', owner: '—' };
    slot.ranges.forEach(function (r) { if (time >= r.from && time < r.to) st = r; });
    return st;
  }
  function rebuildSlots() {
    var host = $('#slots'); if (!host) return;
    host.innerHTML = ''; slotEls = [];
    scenario.slots.forEach(function (s) {
      var el = document.createElement('div');
      el.className = 'slot'; el.dataset.state = 'FREE';
      el.innerHTML = '<div class="sh"><span class="sn">' + s.name + '</span><span class="state">FREE</span></div>' +
        '<div class="sbar"><i></i></div><div class="owner"><span>owner</span><b>—</b></div>';
      host.appendChild(el); slotEls.push({ el: el, s: s });
    });
    var note = document.createElement('div'); note.className = 'slots-note';
    note.innerHTML = '<div class="t"><span class="dot"></span>BUFFERQUEUE · ' + (scenario.key === 'jank' ? '背压风险' : '三缓冲模型') + '</div><p>' + scenario.slotNote + '</p>';
    host.appendChild(note);
  }
  function updateSlots(time) {
    slotEls.forEach(function (o) {
      var st = slotState(o.s, time);
      o.el.dataset.state = st.state;
      $('.state', o.el).textContent = st.state;
      $('.sbar i', o.el).style.width = ({ FREE: '10%', DEQUEUED: '38%', QUEUED: '66%', LATCHED: '86%', DISPLAY: '100%' })[st.state] || '10%';
      $('.owner b', o.el).textContent = st.owner;
    });
  }

  /* ---------------- phase panel & budget ---------------- */
  function phaseAt(time) {
    for (var i = 0; i < scenario.phases.length; i++) {
      if (time >= scenario.phases[i].t0 && time < scenario.phases[i].t1) return scenario.phases[i];
    }
    return scenario.phases[scenario.phases.length - 1];
  }
  var currentPhase = -1;
  function updatePhasePanel() {
    var ph = phaseAt(t);
    var idx = scenario.phases.indexOf(ph); if (idx < 0) idx = scenario.phases.length - 1;
    if (idx !== currentPhase) {
      currentPhase = idx;
      $('#ppStage').textContent = 'STAGE ' + String(ph.stage).padStart(2, '0');
      $('#ppPhase').textContent = 'PHASE ' + String(idx + 1).padStart(2, '0') + ' / ' + String(scenario.phases.length).padStart(2, '0');
      $('#ppTitle').textContent = ph.title;
      $('#ppDesc').textContent = ph.desc;
      var color = ph.hot ? '#fca5a5' : (LANE_COLORS[ph.lane] || '#cfe0ff');
      $('#ppStage').style.setProperty('--pc', color);
      var refs = $('#ppRefs'); refs.innerHTML = '';
      (ph.refs || []).forEach(function (r) { var c = document.createElement('code'); c.textContent = r; refs.appendChild(c); });
      $('#ppHint').textContent = ph.hint || '';
    }

    var appV = (scenario.appVsync != null) ? scenario.appVsync : 0;
    var deadline = (scenario.deadline != null) ? scenario.deadline : scenario.vsyncs[scenario.vsyncs.length - 1];
    var qd = (scenario.queueDone != null) ? scenario.queueDone : deadline;
    var late = qd > deadline;
    var win = Math.max(1, deadline - appV);
    var progress = Math.max(0, Math.min(t, qd) - appV);
    var ratio = clamp(progress / win, 0, 1.35);
    $('#ppVsync').textContent = t >= appV ? '+' + fmt(t - appV) + ' ms' : '等待中';
    $('#ppElapsed').textContent = '+' + fmt(t) + ' ms';
    $('#ppPresent').textContent = scenario.present;
    var fill = $('#budgetFill');
    if (t >= qd) {
      fill.style.width = '100%';
      fill.classList.toggle('warn', late);
      $('#budgetTxt').textContent = late
        ? '超时 +' + fmt(qd - deadline) + ' ms'
        : '已交付 ' + fmt(qd - appV) + ' / ' + fmt(win) + ' ms';
    } else {
      fill.style.width = Math.min(ratio * 100, 100) + '%';
      fill.classList.toggle('warn', late || t > deadline);
      $('#budgetTxt').textContent = t > deadline
        ? '已超预算 +' + fmt(t - deadline) + ' ms'
        : fmt(Math.max(0, t - appV)) + ' / ' + fmt(win) + ' ms';
    }
    $('#ppFrame').textContent = 'FRAME F';
  }

  /* ---------------- time ---------------- */
  function setTime(next) {
    t = clamp(next, 0, scenario.total);
    $('#scrub').value = String(t);
    $('#timeNow').textContent = fmt(t);
    positionPlayhead();
    els.playheadTime.textContent = fmt(t) + ' ms';
    blockEls.forEach(function (o) {
      var active = t >= o.b.t0 && t < o.b.t1;
      o.el.classList.toggle('is-active', active && !o.b.dashed);
      o.el.classList.toggle('is-done', t >= o.b.t1);
    });
    var ph = phaseAt(t);
    LANE_ORDER.forEach(function (lane) { if (tokenEls[lane]) tokenEls[lane].classList.remove('on'); });
    if (ph && tokenEls[ph.lane]) {
      tokenEls[ph.lane].style.left = pct(t) + '%';
      tokenEls[ph.lane].classList.add('on');
    }
    pinEls.forEach(function (p) { p.classList.toggle('is-past', t >= Number(p.dataset.t || 0)); });
    updateSlots(t);
    updatePhasePanel();
    document.documentElement.setAttribute('data-qa', JSON.stringify({
      scenario: scenario.key, time: Math.round(t * 100) / 100,
      lanes: $('#lanes').querySelectorAll('.lane').length,
      blocks: blockEls.length, phases: scenario.phases.length
    }));
  }
  function tick(ts) {
    if (!playing) return;
    if (!lastTs) lastTs = ts;
    var dt = Math.min((ts - lastTs) / 1000, 0.1) * speed;
    lastTs = ts;
    t += dt * 1000 * TIME_SCALE;
    if (t >= (scenario.loopAt || scenario.total)) t = 0;
    setTime(t);
    rafId = requestAnimationFrame(tick);
  }
  function play() {
    if (playing) return;
    playing = true; lastTs = 0;
    $('#playIcon').innerHTML = '<path d="M7 5h4v14H7zM13 5h4v14h-4z"/>';
    rafId = requestAnimationFrame(tick);
  }
  function pause() {
    playing = false;
    if (rafId) cancelAnimationFrame(rafId);
    rafId = null;
    $('#playIcon').innerHTML = '<path d="M8 5v14l11-7z"/>';
  }
  function togglePlay() { playing ? pause() : play(); }

  /* ---------------- scenario switch ---------------- */
  function setChrome() {
    var live = $('.studio-title .live');
    if (live) live.innerHTML = '<i></i>' + (scenario.key === 'jank' ? 'SLOW SF · CUSTOM WORKDURATION' : 'SF CYCLE · CUSTOM WORKDURATION');
    var host = $('.studio-badges');
    if (host) {
      host.innerHTML = scenario.key === 'jank'
        ? '<span class="tag"><b>APP</b> 0ms</span><span class="tag"><b>SF</b> +8.3ms</span><span class="tag"><b>present</b> 18.2ms</span><span class="tag"><b>HW VSYNC</b> MISS</span><span class="tag" style="color:#fca5a5;border-color:rgba(248,113,113,.4)">CUSTOM WD</span>'
        : '<span class="tag"><b>APP</b> VSYNC 0</span><span class="tag"><b>SF</b> VSYNC +8.3ms</span><span class="tag"><b>commit</b> / latch</span><span class="tag"><b>HW</b> VSYNC 16.7ms</span><span class="tag"><b>custom</b> WorkDuration</span>';
    }
    $('#scrub').max = String(scenario.total);
    $('#timeTotal').textContent = fmt(scenario.total);
    $('#jankToggle').checked = scenario.key === 'jank';
  }
  function switchScenario(key, opts) {
    opts = opts || {};
    if (scenario.key === key) return;
    pause();
    scenario = SCENARIOS[key] || SCENARIOS.normal;
    currentPhase = -1;
    setChrome();
    buildBoard();
    setTime(opts.time || 0);
    if (opts.play) play();
  }

  /* ---------------- interactions ---------------- */
  function jumpToStage(n) {
    autoplayDone = true;
    pause();
    var pin = scenario.pins.filter(function (p) { return p.stage === n; })[0];
    if (pin) setTime(pin.t);
    var anatomy = document.getElementById('anatomy');
    if (anatomy) anatomy.scrollIntoView({ behavior: reduceMotion ? 'auto' : 'smooth', block: 'start' });
  }

  function bind() {
    $('#playBtn').addEventListener('click', function () { autoplayDone = true; togglePlay(); });
    $('#restartBtn').addEventListener('click', function () { autoplayDone = true; setTime(0); play(); });
    var scrub = $('#scrub');
    scrub.addEventListener('input', function () { pause(); setTime(Number(scrub.value)); });
    $('#speedSeg').addEventListener('click', function (e) {
      var b = e.target.closest('button'); if (!b) return;
      $all('#speedSeg button').forEach(function (x) { x.classList.toggle('on', x === b); });
      speed = Number(b.dataset.speed) || 1;
    });
    $('#jankToggle').addEventListener('change', function (e) {
      switchScenario(e.target.checked ? 'jank' : 'normal', { time: 0 });
    });
    var jd = $('#jankDemoBtn');
    if (jd) jd.addEventListener('click', function () { autoplayDone = true; switchScenario('jank', { time: 0 }); play(); document.getElementById('anatomy').scrollIntoView({ behavior: reduceMotion ? 'auto' : 'smooth', block: 'start' }); });
    document.addEventListener('keydown', function (e) {
      if (e.code !== 'Space') return;
      var tag = (e.target.tagName || '').toLowerCase();
      if (tag === 'input' || tag === 'textarea' || tag === 'button') return;
      var studio = document.getElementById('studio');
      if (!studio) return;
      var r = studio.getBoundingClientRect();
      if (r.bottom < 100 || r.top > window.innerHeight) return;
      e.preventDefault(); togglePlay();
    });
    var ro = new ResizeObserver(function () { measureGeometry(); });
    if (els.boardBody) ro.observe(els.boardBody);
    window.addEventListener('resize', measureGeometry);
    document.addEventListener('visibilitychange', function () { if (document.hidden) pause(); });
  }

  function initAutoplay() {
    if (reduceMotion) { setTime(0); return; }
    var io = new IntersectionObserver(function (entries) {
      entries.forEach(function (en) {
        if (en.isIntersecting && !autoplayDone) { autoplayDone = true; play(); io.disconnect(); }
      });
    }, { threshold: 0.3 });
    var studio = document.getElementById('studio');
    if (studio) io.observe(studio);
  }

  function deepLink() {
    var qs = null;
    try { qs = new URLSearchParams(location.search); } catch (e) {}
    if (!qs) return;
    if (qs.get('jank') === '1') switchScenario('jank', { time: Number(qs.get('t')) || 0 });
    else if (qs.has('t')) setTime(Number(qs.get('t')) || 0);
    if (qs.get('pause') === '1') { autoplayDone = true; pause(); }
    if (qs.get('play') === '1') { autoplayDone = true; play(); }
    var target = qs.get('scroll');
    if (target) {
      var el = document.getElementById(target);
      if (el) window.scrollTo({ top: el.getBoundingClientRect().top + window.scrollY - 80, behavior: 'instant' });
    }
  }

  function initStudio() {
    if (!scenario) return;
    els.boardBody = $('#boardBody');
    setChrome();
    buildBoard();
    setTime(0);
    bind();
    initAutoplay();
    deepLink();
  }

  window.FP = {
    initStudio: initStudio,
    pause: pause,
    jumpToStage: jumpToStage,
    runPreset: function (name) {
      autoplayDone = true;
      switchScenario(name === 'jank' || name === 'ui' ? 'jank' : 'normal', { time: 0 });
      play();
      var el = document.getElementById('anatomy');
      if (el) el.scrollIntoView({ behavior: reduceMotion ? 'auto' : 'smooth', block: 'start' });
    }
  };
})();