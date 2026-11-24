/* ============================================================
   Frame Pipeline Explorer — page chrome: highlighting, copy, scroll UI.
   The animated studio lives in fp-*.js (window.FP).
   ============================================================ */
(function () {
  'use strict';

  var reduceMotion = window.matchMedia('(prefers-reduced-motion: reduce)').matches;

  /* ---------- helpers ---------- */
  function $(sel, root) { return (root || document).querySelector(sel); }
  function $all(sel, root) { return Array.prototype.slice.call((root || document).querySelectorAll(sel)); }

  /* ---------- syntax highlighting ---------- */
  var KW = new Set(('abstract assert boolean break byte case catch char class const continue default do double else enum extends final finally float for goto if implements import instanceof int interface long native new package private protected public return short static strictfp super switch synchronized this throw throws transient try void volatile while true false null ' +
    'auto bool delete explicit friend inline mutable namespace nullptr operator override reinterpret_cast sizeof static_cast struct template typename union using virtual constexpr decltype noexcept thread_local size_t status_t nsecs_t int32_t int64_t uint32_t uint64_t sp wp String8 std vector string').split(/\s+/));
  var TYPES = new Set(('int void char bool float double long short unsigned signed size_t status_t nsecs_t int32_t int64_t uint32_t uint64_t sp wp void* DisplayList RenderNode CanvasContext Surface Frame BufferItem ' +
    'std::vector std::string std::unique_ptr std::shared_ptr').split(/\s+/));
  function esc(s) { return s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;'); }
  function highlight(code) {
    var out = '', i = 0, n = code.length;
    while (i < n) {
      var c = code[i];
      if (c === '/' && code[i + 1] === '/') {
        var j = code.indexOf('\n', i); if (j < 0) j = n;
        out += '<span class="tok-c">' + esc(code.slice(i, j)) + '</span>'; i = j; continue;
      }
      if (c === '/' && code[i + 1] === '*') {
        var k = code.indexOf('*/', i + 2); k = k < 0 ? n : k + 2;
        out += '<span class="tok-c">' + esc(code.slice(i, k)) + '</span>'; i = k; continue;
      }
      if (c === '"' || c === "'") {
        var q = i + 1;
        while (q < n) {
          if (code[q] === '\\') { q += 2; continue; }
          if (code[q] === c) { q++; break; }
          if (c === "'" && code[q] === '\n') break;
          q++;
        }
        out += '<span class="tok-s">' + esc(code.slice(i, q)) + '</span>'; i = q; continue;
      }
      if (/[0-9]/.test(c)) {
        var m = i + 1; while (m < n && /[0-9a-fA-FxX._]/.test(code[m])) m++;
        out += '<span class="tok-n">' + esc(code.slice(i, m)) + '</span>'; i = m; continue;
      }
      if (/[A-Za-z_$]/.test(c)) {
        var w = i + 1; while (w < n && /[A-Za-z0-9_$]/.test(code[w])) w++;
        var word = code.slice(i, w), cls = null;
        if (KW.has(word)) cls = 'tok-k'; else if (TYPES.has(word)) cls = 'tok-t';
        else {
          var z = w; while (z < n && (code[z] === ' ' || code[z] === '\t')) z++;
          if (code[z] === '(') cls = 'tok-f';
        }
        out += cls ? '<span class="' + cls + '">' + esc(word) + '</span>' : esc(word);
        i = w; continue;
      }
      if (c === '@') {
        var a = i + 1; while (a < n && /[A-Za-z0-9_]/.test(code[a])) a++;
        out += '<span class="tok-a">' + esc(code.slice(i, a)) + '</span>'; i = a; continue;
      }
      if (c === '#') {
        var h = code.indexOf('\n', i); if (h < 0) h = n;
        out += '<span class="tok-p">' + esc(code.slice(i, h)) + '</span>'; i = h; continue;
      }
      out += esc(c); i++;
    }
    return out;
  }
  function highlightAll() {
    $all('pre code[data-highlight]').forEach(function (el) {
      if (el.dataset.hlDone) return;
      el.innerHTML = highlight(el.textContent || '');
      el.dataset.hlDone = '1';
    });
  }

  /* ---------- interactions ---------- */
  function initEvents() {
    // "在动画中查看" buttons → jump the studio to that stage
    document.addEventListener('click', function (e) {
      var b = e.target.closest('[data-stage-jump]'); if (!b) return;
      if (window.FP && FP.jumpToStage) FP.jumpToStage(Number(b.dataset.stageJump));
    });
    // timeline pins scroll to their stage section
    document.addEventListener('click', function (e) {
      var p = e.target.closest('#pins .pin'); if (!p) return;
      var sec = document.getElementById('stage-' + String(p.dataset.stage).padStart(2, '0'));
      if (window.FP && FP.pause) FP.pause();
      if (sec) sec.scrollIntoView({ behavior: reduceMotion ? 'auto' : 'smooth', block: 'start' });
    });
    var jd = $('#jankDemoBtn');
    if (jd) jd.addEventListener('click', function () { if (window.FP && FP.runPreset) FP.runPreset('ui'); });
    // flow-strip cards scroll to their stage
    document.addEventListener('click', function (e) {
      var f = e.target.closest('.flow-step[data-goto]');
      if (!f) return;
      var el = document.getElementById(f.dataset.goto);
      if (el) el.scrollIntoView({ behavior: reduceMotion ? 'auto' : 'smooth', block: 'start' });
    });
    // copy code buttons
    document.addEventListener('click', function (e) {
      var btn = e.target.closest('.copy-btn'); if (!btn) return;
      var fig = btn.closest('.snippet'); if (!fig) return;
      var code = $('pre code', fig);
      if (!code) return;
      var text = code.getAttribute('data-raw') || code.textContent || '';
      var done = function () {
        btn.classList.add('copied');
        var old = btn.innerHTML;
        btn.innerHTML = '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round"><path d="M20 6 9 17l-5-5"/></svg>';
        setTimeout(function () { btn.classList.remove('copied'); btn.innerHTML = old; }, 1300);
      };
      if (navigator.clipboard && navigator.clipboard.writeText) {
        navigator.clipboard.writeText(text).then(done).catch(function () { fallbackCopy(text, done); });
      } else fallbackCopy(text, done);
    });
  }

  function fallbackCopy(text, done) {
    var ta = document.createElement('textarea'); ta.value = text; ta.style.position = 'fixed'; ta.style.opacity = '0';
    document.body.appendChild(ta); ta.select();
    try { document.execCommand('copy'); done(); } catch (err) { /* noop */ }
    document.body.removeChild(ta);
  }

  /* ---------- scroll UI ---------- */
  function initScrollUI() {
    var prog = $('#scrollProgress');
    function onScroll() {
      var h = document.documentElement;
      var max = h.scrollHeight - h.clientHeight;
      var v = max > 0 ? h.scrollTop / max : 0;
      prog.style.width = (v * 100) + '%';
      // nav active
      var links = $all('#nav a[data-nav]');
      var current = null;
      links.forEach(function (a) {
        var id = a.getAttribute('href').slice(1);
        var sec = document.getElementById(id);
        if (sec && sec.getBoundingClientRect().top <= 120) current = a;
      });
      links.forEach(function (a) { a.classList.toggle('is-active', a === current); });
    }
    var railLinks = $all('#stageRail a');
    function syncRail() {
      var active = null;
      var stages = document.getElementById('stages');
      var inStages = stages && stages.getBoundingClientRect().top <= 140 && stages.getBoundingClientRect().bottom > 140;
      if (inStages) {
        railLinks.forEach(function (a) {
          var id = a.getAttribute('href').slice(1);
          var sec = document.getElementById(id);
          if (sec && sec.getBoundingClientRect().top <= 140) active = a;
        });
      }
      railLinks.forEach(function (a) { a.classList.toggle('is-active', a === active); });
    }
    var toTop = $('#toTop');
    function syncTop() { if (toTop) toTop.classList.toggle('on', window.scrollY > 1400); }
    window.addEventListener('scroll', function () { syncRail(); syncTop(); }, { passive: true });
    if (toTop) toTop.addEventListener('click', function () { window.scrollTo({ top: 0, behavior: reduceMotion ? 'auto' : 'smooth' }); });
    syncRail(); syncTop();
    window.addEventListener('scroll', onScroll, { passive: true });
    onScroll();
    // reveal
    var ro = new IntersectionObserver(function (entries) {
      entries.forEach(function (en) { if (en.isIntersecting) { en.target.classList.add('in'); ro.unobserve(en.target); } });
    }, { threshold: 0.12 });
    $all('.reveal, .latency-grid .ladder').forEach(function (el) { ro.observe(el); });
  }

  /* ---------- theme ---------- */
  var THEME_ORDER = ['auto', 'light', 'dark'], THEME_NAME = { auto: '跟随系统', light: '浅色', dark: '深色' };
  function applyTheme(pref) {
    var mq = window.matchMedia && matchMedia('(prefers-color-scheme: light)');
    var light = pref === 'light' || (pref === 'auto' && mq && mq.matches);
    var root = document.documentElement;
    root.setAttribute('data-theme', light ? 'light' : 'dark');
    root.setAttribute('data-theme-pref', pref);
    var b = $('#themeBtn'); if (b) b.title = '配色：' + THEME_NAME[pref];
    document.dispatchEvent(new CustomEvent('fp-theme'));
  }
  function initTheme() {
    var btn = $('#themeBtn'); if (!btn) return;
    btn.addEventListener('click', function () {
      var cur = document.documentElement.getAttribute('data-theme-pref') || 'auto';
      var next = THEME_ORDER[(THEME_ORDER.indexOf(cur) + 1) % THEME_ORDER.length];
      try { localStorage.setItem('fp-theme', next); } catch (e) { /* private mode */ }
      applyTheme(next);
    });
    var mq = window.matchMedia && matchMedia('(prefers-color-scheme: light)');
    var onOs = function () { if ((document.documentElement.getAttribute('data-theme-pref') || 'auto') === 'auto') applyTheme('auto'); };
    if (mq && mq.addEventListener) mq.addEventListener('change', onOs); else if (mq && mq.addListener) mq.addListener(onOs);
    try { var qs = new URLSearchParams(location.search).get('theme'); if (THEME_NAME[qs]) applyTheme(qs); } catch (e2) {}
  }

  /* ---------- boot ---------- */
  function boot() {
    initTheme();
    highlightAll();
    initEvents();
    initScrollUI();
    if (window.FP && FP.initStudio) FP.initStudio();
    try {
      var qs = new URLSearchParams(location.search), target = qs.get('scroll');
      var el = target && document.getElementById(target);
      if (el) window.scrollTo({ top: el.getBoundingClientRect().top + window.scrollY - 80, behavior: 'instant' });
    } catch (e2) { /* deep-link params are optional */ }
  }
  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', boot);
  else boot();
})();
