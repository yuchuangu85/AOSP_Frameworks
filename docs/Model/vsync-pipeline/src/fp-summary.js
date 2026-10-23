/* ============================================================
   Frame Pipeline Studio · stage overview table
   Generated from FP.STEPS so it can never drift from the animation.
   Timing column follows the studio's current scenario / focus frame;
   the row of the step under the playhead is highlighted.
   ============================================================ */
window.FP = window.FP || {};
(function (FP) {
  'use strict';
  var rows = {}, built = false, lastId = null;

  function entryOf(st) { return st.chain[0].replace(/\s*\/\/.*$/, ''); }

  FP.buildSummary = function () {
    var body = document.getElementById('summaryBody'); if (!body) return;
    body.innerHTML = ''; rows = {};
    FP.STEPS.forEach(function (st, i) {
      var tr = FP.h('tr', 'sum-row', body);
      tr.dataset.step = st.id;
      tr.style.setProperty('--lane', FP.LANE[st.lane].color);
      tr.innerHTML =
        '<td class="sum-n">' + String(i + 1).padStart(2, '0') + '</td>' +
        '<td class="sum-t"><button type="button" class="sum-go" data-step-go="' + st.id + '" aria-label="在动画中定位：' + FP.esc(st.title) + '">' +
          '<b>' + FP.esc(st.short) + '</b><span>' + FP.esc(st.title) + '</span></button></td>' +
        '<td class="sum-w"><span class="sum-lane"><i></i>' + FP.esc(FP.LANE[st.lane].name) + '</span><small>' + FP.esc(st.where) + '</small></td>' +
        '<td class="sum-e"><code>' + FP.esc(entryOf(st)) + '</code></td>' +
        '<td class="sum-o">' + FP.esc(st.packet) + (st.ipc ? '<em class="sum-ipc">跨进程 / 跨硬件</em>' : '') + '</td>' +
        '<td class="sum-time" data-time></td>' +
        '<td class="sum-s"><a href="#stage-' + String(st.stage).padStart(2, '0') + '">' + String(st.stage).padStart(2, '0') + '</a></td>';
      rows[st.id] = tr;
    });
    body.addEventListener('click', function (e) {
      var b = e.target.closest('[data-step-go]'); if (!b) return;
      if (FP.jumpToStep) FP.jumpToStep(b.dataset.stepGo);
    });
    built = true;
  };

  // Six common misconceptions, from claude/android-frame-relay. Last field = step id in FP.STEPS.
  FP.MYTHS = [
    ['invalidate() 会立刻重绘', '它只打脏标记并向上传递，真正绘制在下一个 VSync 的 doFrame 里。', 'req'],
    ['onDraw 直接产生像素', '硬件加速下 onDraw 只录制 DisplayList，像素由 GPU 稍后写入。', 'draw'],
    ['queueBuffer 时图已经画好了', 'buffer 带着未 signal 的 acquire fence 交出，GPU 可能还在写。', 'queue'],
    ['SF 直接消费应用的 BufferQueue', '现代窗口由应用进程内的 BLAST 消费，再经事务交给 SF。', 'queue'],
    ['SF 合成一定要用 GPU', 'DEVICE 层由显示硬件直接叠加，只有 CLIENT 层才动用 RenderEngine。', 'comp'],
    ['present 返回就上屏了', 'present / ON_COMPLETE 可以携带仍 pending 的 fence；回调、呈现 signal 与 buffer 安全复用需要分别判断。', 'hwc']
  ];
  FP.buildMyths = function () {
    var host = document.getElementById('mythList'); if (!host) return;
    host.innerHTML = '';
    FP.MYTHS.forEach(function (m) {
      var st = FP.STEP[m[2]], card = FP.h('div', 'myth', host);
      card.style.setProperty('--lane', FP.LANE[st.lane].color);
      card.innerHTML = '<s>' + FP.esc(m[0]) + '</s><span>' + FP.esc(m[1]) + '</span>' +
        '<button type="button" data-step-go="' + st.id + '">看第 ' + (st.index + 1) + ' 步 · ' + FP.esc(st.short) + ' →</button>';
    });
    host.addEventListener('click', function (e) {
      var b = e.target.closest('[data-step-go]'); if (b && FP.jumpToStep) FP.jumpToStep(b.dataset.stepGo);
    });
  };

  // Called by the studio after every recompute.
  FP.updateSummary = function (sim, f) {
    if (!built) return;
    var S = FP.stepTimes(f), fmt = FP.fmt;
    var head = document.getElementById('summaryTimeHead');
      if (head) head.textContent = '示意时刻 · F' + f.k + '（' + (FP.PRESET[FP.currentPreset] ? FP.PRESET[FP.currentPreset].label : '自定义') + '）';
    FP.STEPS.forEach(function (st) {
      var td = rows[st.id].querySelector('[data-time]'), x = S[st.id];
      rows[st.id].classList.toggle('is-skip', !x || !!(x && x.dropped));
      if (!x) td.innerHTML = st.id === 'release' ? '<span class="sum-skip">本例未模拟后续归还</span>' : '<span class="sum-skip">跳过（dropped）</span>';
      else if (x.dropped) td.innerHTML = '<span class="sum-skip">' + fmt(x.t0) + ' · dropped</span>';
      else td.textContent = x.kind === 'block' ? fmt(x.t0) + '–' + fmt(x.t1) + ' ms' : fmt(x.t0) + ' ms';
    });
    lastId = null;
  };

  // Called by the studio when the step under the playhead changes.
  FP.markSummary = function (id) {
    if (!built || id === lastId) return;
    if (lastId && rows[lastId]) rows[lastId].classList.remove('is-cur');
    if (rows[id]) rows[id].classList.add('is-cur');
    lastId = id;
  };
})(window.FP);
