const P = JSON.parse(document.getElementById('payload').textContent);
const KIND = {
  lock: '#ff6b6b', thread: '#4cd4ff', ipc: '#c792ea', state: '#ffb454',
  branch: '#ff9f43', data: '#7bd88f', trace: '#9aa1ad', note: '#8b98a5'
};
const KIND_LABEL = {
  lock: '锁', thread: '线程', ipc: '跨进程', state: '状态', branch: '分支',
  data: '数据', trace: 'trace', note: '说明'
};
const LANE_COLOR = {};
P.lanes.forEach(l => LANE_COLOR[l.id] = l.color);

const state = { tab: 'overview', step: null, p0: null };

const $ = sel => document.querySelector(sel);
const el = (tag, cls, htmlText) => {
  const n = document.createElement(tag);
  if (cls) n.className = cls;
  if (htmlText !== undefined) n.innerHTML = htmlText;
  return n;
};
const esc = s => String(s).replace(/[&<>"]/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));
const shortName = sym => {
  const parts = String(sym).split('.');
  return parts.length >= 2 ? parts.slice(-2).join('.') : sym;
};
const KW = new Set(('abstract assert boolean break byte case catch char class const continue default do ' +
  'double else enum extends final finally float for goto if implements import instanceof int interface ' +
  'long native new package private protected public return short static strictfp super switch synchronized ' +
  'this throw throws transient try void volatile while var record yield sealed permits null true false ' +
  'auto bool delete explicit friend inline namespace noexcept nullptr operator sizeof struct template ' +
  'typename using virtual override constexpr static_cast reinterpret_cast').split(' '));
const TOKEN = /(\/\/.*$)|(\/\*[\s\S]*?\*\/)|("(?:\\.|[^"\\])*")|('(?:\\.|[^'\\])*')|(@[A-Za-z_][\w.]*)|(\b\d+(?:\.\d+)?\b)|([A-Za-z_][A-Za-z0-9_]*)/g;
function highlight(src) {
  let out = '', last = 0, m;
  TOKEN.lastIndex = 0;
  while ((m = TOKEN.exec(src)) !== null) {
    out += esc(src.slice(last, m.index));
    const t = m[0];
    let cls = '';
    if (m[1] || m[2]) cls = 'c1';
    else if (m[3] || m[4]) cls = 's';
    else if (m[5]) cls = 'a';
    else if (m[6]) cls = 'n';
    else if (KW.has(t)) cls = 'k';
    out += cls ? `<span class="${cls}">${esc(t)}</span>` : esc(t);
    last = m.index + t.length;
  }
  out += esc(src.slice(last));
  return out;
}


/* ------------------------------ theme */
function applyTheme() {
  const root = document.documentElement;
  const light = root.dataset.theme === 'light';
  const btn = $('#theme-toggle');
  if (!btn) return;
  btn.setAttribute('aria-pressed', String(light));
  btn.title = light ? '切换到暗色（快捷键 t）' : '切换到亮色（快捷键 t）';
  const label = btn.querySelector('.tt-label');
  if (label) label.textContent = light ? '暗色' : '亮色';
}

function toggleTheme() {
  const root = document.documentElement;
  root.dataset.theme = root.dataset.theme === 'light' ? 'dark' : 'light';
  try { localStorage.setItem('st-theme', root.dataset.theme); } catch (e) { /* 忽略隐私模式 */ }
  // ?theme= 是一次性覆盖；用户手动切换后去掉参数，后续以 localStorage 为准
  try {
    const u = new URL(location.href);
    if (u.searchParams.has('theme')) {
      u.searchParams.delete('theme');
      history.replaceState(null, '', u.pathname + (u.searchParams.toString() ? '?' + u.searchParams : '') + u.hash);
    }
  } catch (e) { /* file:// 下忽略 */ }
  applyTheme();
}

function initTheme() {
  applyTheme();
  const btn = $('#theme-toggle');
  if (btn) btn.addEventListener('click', toggleTheme);
  document.addEventListener('keydown', (e) => {
    if (e.key !== 't' && e.key !== 'T') return;
    if (e.metaKey || e.ctrlKey || e.altKey) return;
    const tag = (e.target.tagName || '').toLowerCase();
    if (tag === 'input' || tag === 'textarea' || tag === 'select') return;
    e.preventDefault();
    toggleTheme();
  });
}

/* ------------------------------ header */
function renderHeader() {
  const s = P.stats;
  const stats = [
    [s.steps, '转场步骤'],
    [s.p0, 'P0 函数'],
    [s.annotated, '逐行注释'],
    [s.loc, '代码行'],
    [s.coverage + '%', '行覆盖率'],
    [s.methods, '方法总量']
  ];
  $('#hdr-stats').innerHTML = stats.map(([v, k]) =>
    `<div class="stat"><b>${esc(v)}</b><span>${esc(k)}</span></div>`).join('');
  $('#ftr-meta').textContent =
    `android17-release · 构建 ${P.meta.built} · 源码 ${P.meta.git_ref} · ` +
    `${P.stats.p0} 个函数 / ${P.stats.annotated} 条逐行注释`;
}

/* ------------------------------ rail */
function renderRail() {
  const rail = $('#rail');
  rail.innerHTML = '';
  P.phases.forEach(ph => {
    const steps = P.steps.filter(s => s.phase === ph.id);
    if (!steps.length) return;
    rail.appendChild(el('div', 'phase', `${ph.id} · ${esc(ph.label)} <span style="opacity:.55">${esc(ph.hint)}</span>`));
    steps.forEach(s => {
      const b = el('div', 'step');
      b.dataset.step = s.id;
      b.innerHTML =
        `<span class="lane-dot" style="background:${LANE_COLOR[s.lane] || '#888'}"></span>` +
        `<span class="n">${s.num}</span>` +
        `<span class="t">${esc(s.title)}${s.p0 ? '<span class="p0tag">逐行</span>' : ''}</span>`;
      b.addEventListener('click', () => selectStep(s.id, { scrollRail: false }));
      rail.appendChild(b);
    });
  });
}

/* ------------------------------ step card */
function renderStepCard() {
  const card = $('#step-card');
  if (!state.step) { card.classList.remove('is-on'); return; }
  const s = P.steps.find(x => x.id === state.step);
  card.classList.add('is-on');
  card.style.borderLeftColor = LANE_COLOR[s.lane] || 'var(--wms)';
  card.innerHTML =
    `<div class="sc-hd"><span class="sc-n">${s.num} · ${esc(s.phase)}</span>` +
    `<h3>${esc(s.title)}</h3>` +
    `<span class="hint">${esc((P.lanes.find(l => l.id === s.lane) || {}).label || '')}</span></div>` +
    `<p>${esc(s.brief)}</p>` +
    `<div class="refs">${(s.refs || []).map(r =>
      `<span class="ref"><b>${shortName(r.symbol || '')}</b> ${esc(r.file.split('/').pop())}:${r.line}</span>`).join('')}</div>` +
    (s.p0 ? `<div class="refs" style="margin-top:10px"><span class="ref" id="jump-p0" style="cursor:pointer;border-color:var(--wms);color:var(--wms)">→ 跳到逐行注释</span></div>` : '');
  const j = $('#jump-p0');
  if (j) j.addEventListener('click', () => { setTab('code'); selectP0(s.p0); });
}

/* ------------------------------ overview */
function renderOverview() {
  $('#diagram-arch').innerHTML = P.diagrams.arch;
  const lp = P.learningPath || [];
  const box = $('#learning-path');
  if (box && lp.length) {
    box.innerHTML = lp.map(x =>
      `<div class="lp-row"><span class="lp-t">${esc(x.time)}</span>` +
      `<span class="lp-g">${esc(x.goal)}</span>` +
      `<span class="lp-r">${esc(x.route)}</span></div>`).join('');
  }
  $('#facts').innerHTML = (P.facts || []).map(f => `<li>${f}</li>`).join('');
  const list = $('#p0-list');
  list.innerHTML = '';
  P.p0.forEach(p0 => {
    const item = el('div', 'p0-item');
    const cov = p0.stats.missing ? '—' : p0.stats.coverage + '%';
    item.innerHTML =
      `<div><div class="nm">${esc(p0.symbol)}</div><div class="wh">${esc(p0.why)}</div></div>` +
      `<div class="cov ${p0.stats.missing || p0.stats.coverage < 90 ? 'low' : ''}">${cov}</div>`;
    item.addEventListener('click', () => { setTab('code'); selectP0(p0.id); });
    list.appendChild(item);
  });
}


/* ------------------------------ deep read */
function renderScenario() {
  const sc = P.scenario || {};
  if (!sc.events) return;
  $('#scenario').innerHTML = sc.events.map(e =>
    `<div class="ev"><div class="t">${esc(e.t)}</div><div class="l">${esc(e.label)}</div>` +
    `<div class="d">${esc(e.detail)}</div></div>`).join('');
}

function renderGlossary() {
  $('#glossary').innerHTML = '<dl>' + (P.glossary || []).map(g =>
    `<dt>${esc(g.term)}</dt><dd>${esc(g.text)}</dd>`).join('') + '</dl>';
}

function deepStepIndex() {
  return P.steps.findIndex(s => s.id === state.step);
}

function renderDeep() {
  const idx = deepStepIndex();
  const s = P.steps[idx] || P.steps[0];
  const d = s.deep;
  $('#deep-progress').textContent = `第 ${s.num} / ${P.steps.length} 步 · ${s.title}`;
  $('#deep-prev').disabled = idx <= 0;
  $('#deep-next').disabled = idx >= P.steps.length - 1;

  const body = $('#deep-body');
  if (!d) {
    body.innerHTML = `<div class="kicker">第 ${s.num} 步 · ${esc(s.phase)}</div>` +
      `<h1>${esc(s.title)}</h1><p class="tldr">${esc(s.brief)}</p>` +
      `<p class="missing">这一步的精读文章还没写（agent 正在生成）。先看左栏步骤卡与「代码逐行」。</p>` +
      (s.refs || []).length ? `<h2>代码位置</h2>` + s.refs.map(r =>
        `<p><span class="ref">${esc(r.file)}:${r.line}</span></p>`).join('') : '';
    return;
  }

  const walk = (d.walk || []).map(w =>
    `<li><div class="call">${esc(w.call)}<span class="ref">${esc(w.ref)}</span></div>` +
    `<div class="what">${esc(w.what)}</div></li>`).join('');

  const state = (d.state || []).length
    ? `<h2>状态变化</h2><table class="state"><thead><tr><th>字段/对象</th><th>之前</th><th>之后</th><th>说明</th></tr></thead><tbody>` +
      d.state.map(r => `<tr><td><code>${esc(r.field)}</code></td><td>${esc(r.before)}</td>` +
        `<td>${esc(r.after)}</td><td>${esc(r.note)}</td></tr>`).join('') + '</tbody></table>'
    : '';

  const concepts = (d.concepts || []).length
    ? `<h2>先懂这几个词</h2>` + d.concepts.map(c =>
      `<span class="concept"><b>${esc(c.term)}</b>：${esc(c.text)}</span>`).join('')
    : '';

  const bt = (P.branchTables || []).find(t => t.step === s.id);
  const branch = bt ? `<h2>本步的分支</h2><div class="branch-tbl">` +
    `<div class="row head"><div>条件</div><div>动作</div><div>结果</div></div>` +
    bt.rows.map(r => `<div class="row"><div class="cond">${esc(r.cond)}</div>` +
      `<div class="then">${esc(r.then)}</div><div class="eff">${esc(r.effect)}</div></div>`).join('') +
    `</div><p class="hint">源码：${esc(bt.ref)}</p>` : '';

  body.innerHTML =
    `<div class="kicker">第 ${s.num} 步 · ${esc(s.phase)} · ${esc(s.lane)}</div>` +
    `<h1>${esc(d.title || s.title)}</h1>` +
    `<p class="tldr">${esc(d.tldr || s.brief)}</p>` +
    `<h2>为什么需要这一步</h2><p class="why">${esc(d.why || '')}</p>` +
    concepts +
    `<h2>到底发生了什么</h2><ol class="walk">${walk}</ol>` +
    state +
    `<h2>谁在跑</h2><p class="threads">${esc(d.threads || '')}</p>` +
    ((d.pitfalls || []).length ? `<h2>新手常踩</h2><ul class="plain">` +
      d.pitfalls.map(x => `<li>${esc(x)}</li>`).join('') + '</ul>' : '') +
    ((d.debug || []).length ? `<h2>怎么验证</h2><ul class="plain">` +
      d.debug.map(x => `<li>${esc(x)}</li>`).join('') + '</ul>' : '') +
    branch +
    (d.next ? `<div class="next">下一步 → ${esc(d.next)}</div>` : '');

  annotateTerms(body);
}

function annotateTerms(root) {
  const terms = (P.glossary || []).slice().sort((a, b) => b.term.length - a.term.length);
  const used = new Set();
  const walker = document.createTreeWalker(root, NodeFilter.SHOW_TEXT, null);
  const nodes = [];
  while (walker.nextNode()) nodes.push(walker.currentNode);
  nodes.forEach(node => {
    if (!node.nodeValue.trim()) return;
    for (const g of terms) {
      if (used.has(g.term)) continue;
      const i = node.nodeValue.indexOf(g.term);
      if (i < 0) continue;
      const before = node.nodeValue.slice(0, i);
      const after = node.nodeValue.slice(i + g.term.length);
      const abbr = document.createElement('abbr');
      abbr.className = 'term';
      abbr.title = g.text;
      abbr.textContent = g.term;
      const parent = node.parentNode;
      const tail = document.createTextNode(after);
      parent.insertBefore(document.createTextNode(before), node);
      parent.insertBefore(abbr, node);
      parent.insertBefore(tail, node);
      parent.removeChild(node);
      used.add(g.term);
      return;
    }
  });
}

function deepGo(delta) {
  const i = deepStepIndex() + delta;
  if (i < 0 || i >= P.steps.length) return;
  const s = P.steps[i];
  state.step = s.id;
  document.querySelectorAll('.step').forEach(n => n.classList.toggle('is-on', n.dataset.step === s.id));
  renderStepCard();
  renderDeep();
  const n = document.querySelector(`.step[data-step="${s.id}"]`);
  if (n) n.scrollIntoView({ block: 'nearest' });
  history.replaceState(null, '', '#' + s.id);
}

/* ------------------------------ code */
function renderChips() {
  const box = $('#p0-chips');
  box.innerHTML = '';
  P.p0.forEach(p0 => {
    const c = el('button', 'chip');
    c.dataset.p0 = p0.id;
    c.textContent = shortName(p0.symbol);
    c.title = p0.file + ':' + p0.start;
    c.addEventListener('click', () => selectP0(p0.id));
    box.appendChild(c);
  });
}

function renderCode() {
  const p0 = P.p0.find(p => p.id === state.p0) || P.p0[0];
  if (!p0) return;
  state.p0 = p0.id;
  document.querySelectorAll('.chip').forEach(c => c.classList.toggle('is-on', c.dataset.p0 === p0.id));

  const wrap = $('#code-wrap');
  const rows = [];
  rows.push(`<div class="code">`);
  p0.code.forEach((line, i) => {
    const ln = p0.start + i;
    const note = p0.notes[String(ln)];
    const k = note ? (KIND[note.kind] || KIND.note) : '';
    const cls = ['crow', note ? 'has-note' : ''].join(' ');
    rows.push(
      `<div class="${cls}" data-line="${ln}">` +
      `<span class="ln">${ln}</span>` +
      `<span class="lc">${highlight(line) || '&nbsp;'}</span>` +
      `<span class="la">${note ? `<span class="k" style="background:${k}"></span>${esc(note.text)}` : ''}</span>` +
      `</div>`);
  });
  rows.push('</div>');
  wrap.innerHTML = rows.join('');

  const stats = p0.stats;
  const hint = $('#code-step-hint');
  if (state.step) {
    const s = P.steps.find(x => x.id === state.step);
    hint.classList.add('is-on');
    hint.innerHTML = `<b>${s.num}</b> ${esc(s.title)} · ${esc(p0.symbol)}` +
      `　<span class="hint">${p0.start}–${p0.end} 行 · 注释 ${stats.annotated}/${stats.nontrivial}` +
      `（${stats.missing ? '缺失' : stats.coverage + '%'}）</span>`;
  } else {
    hint.classList.add('is-on');
    hint.innerHTML = `<b>${esc(p0.symbol)}</b> · ${esc(p0.why)}` +
      `　<span class="hint">${p0.start}–${p0.end} 行 · 覆盖 ${stats.missing ? '缺失' : stats.coverage + '%'}</span>`;
  }
  applyFilters();
}

function applyFilters() {
  const q = ($('#code-search').value || '').trim().toLowerCase();
  const onlyNotes = $('#only-notes').checked;
  document.querySelectorAll('.crow').forEach(r => {
    const note = r.querySelector('.la').textContent.toLowerCase();
    const code = r.querySelector('.lc').textContent.toLowerCase();
    let show = true;
    if (q && !(code.includes(q) || note.includes(q))) show = false;
    if (onlyNotes && !r.classList.contains('has-note')) show = false;
    r.classList.toggle('hidden', !show);
  });
}

function selectP0(id, opts = {}) {
  state.p0 = id;
  renderCode();
  const p0 = P.p0.find(p => p.id === id);
  // 同步选中有对应步骤的 step
  const stepId = Object.keys(P.p0ByStep).find(k => P.p0ByStep[k] === id) || null;
  const ownerStep = P.steps.find(s => s.p0 === id);
  if (ownerStep) selectStep(ownerStep.id, { keepTab: true, silent: true });
  if (!opts.keepScroll) $('#code-wrap').scrollTop = 0;
}

/* ------------------------------ diagrams */
function renderDiagramHints() {
  const d = P.detail || {};
  const set = (id, txt) => { const n = $('#' + id); if (n && txt) n.textContent = txt; };
  set('hint-arch', d.arch
    ? '按行从上到下 = 时间推进；每列 = 谁在干活；箭头灰字 = 数据形态'
    : '实线=同步调用 · 虚线=oneway / Binder 单程');
  set('hint-seq', d.seq
    ? '横轴 = 参与者，纵轴 = 时间；灰字 = 数据；红块 = 阻塞；点消息跳步骤'
    : '点击任意消息 → 跳到对应步骤');
  set('hint-flow', d.flow
    ? 'A 判定 / B 入队 / C 合成；侧边通道是重试回路'
    : '含无 player / 异常两条降级路径');
}

function renderDiagrams() {
  renderDiagramHints();
  $('#diagram-seq').innerHTML = P.diagrams.seq;
  $('#diagram-cls').innerHTML = P.diagrams.cls;
  $('#diagram-flow').innerHTML = P.diagrams.flow;
  document.querySelectorAll('#diagram-seq .msg').forEach(g => {
    g.addEventListener('click', () => selectStep(g.dataset.step));
  });
}



/* ------------------------------ file tree */
const FOLDER_ICO = '<svg class="ico" viewBox="0 0 16 16"><path d="M1.5 4.2c0-.6.5-1.1 1.1-1.1h3l1.3 1.5h6.5c.6 0 1.1.5 1.1 1.1v6.1c0 .6-.5 1.1-1.1 1.1H2.6c-.6 0-1.1-.5-1.1-1.1z" fill="none" stroke="currentColor" stroke-width="1.1"/></svg>';
const FILE_ICO = '<svg class="ico" viewBox="0 0 16 16"><path d="M4 1.8h5l3 3v8.4c0 .5-.4.9-.9.9H4c-.5 0-.9-.4-.9-.9V2.7c0-.5.4-.9.9-.9z" fill="none" stroke="currentColor" stroke-width="1.1"/><path d="M9 1.8v3h3" fill="none" stroke="currentColor" stroke-width="1.1"/></svg>';
const collapsed = new Set();
let fileSel = null;

function filesSummary() {
  const s = P.fileStats || {};
  $('#files-summary').textContent =
    `${s.files} 个文件 / ${(s.lines || 0).toLocaleString()} 行 · 深挖范围 ${s.inScope} 个 · 含逐行函数 ${s.p0Files} 个`;
}

function fileMatches(node, q, onlyHit) {
  if (!q && !onlyHit) return true;
  if (node.type === 'file') {
    const hit = (node.steps || []).length || (node.p0 || []).length || (node.deep || []).length;
    if (onlyHit && !hit) return false;
    return !q || node.path.toLowerCase().includes(q);
  }
  return node.children.some(c => fileMatches(c, q, onlyHit));
}

function renderTree() {
  const q = ($('#files-search').value || '').trim().toLowerCase();
  const onlyHit = $('#files-only-hit').checked;
  const box = $('#file-tree');
  box.innerHTML = '';
  const root = P.fileTree;
  const walk = (node, depth) => {
    if (!fileMatches(node, q, onlyHit)) return null;
    const el = document.createElement('div');
    el.className = 'tnode ' + (node.type === 'dir' ? 'dir' : 'file');
    const path = node.path || (node.type === 'dir' ? node.name : '');
    if (collapsed.has(path)) el.classList.add('collapsed');
    if (fileSel === path) el.classList.add('sel');
    const row = document.createElement('div');
    row.className = 'row';
    const caret = node.type === 'dir' ? (collapsed.has(path) ? '▸' : '▾') : '';
    const badges = [];
    if (node.type === 'file') {
      if ((node.p0Count || 0) > 0) badges.push(`<span class="badge-mini p0">逐行 ${node.p0Count}</span>`);
      const hits = (node.steps || []).length + (node.deep || []).length;
      if (hits) badges.push(`<span class="badge-mini hit">涉及 ${hits} 处</span>`);
      if (!node.inScope) badges.push('<span class="badge-mini">仅引用</span>');
    }
    row.innerHTML = `<span class="caret">${caret}</span>${node.type === 'dir' ? FOLDER_ICO : FILE_ICO}` +
      `<span class="nm">${esc(node.name || '')}</span>` +
      `<span class="meta">${node.type === 'dir'
        ? `涉及 ${node.files} / 共 ${node.dirTotal || node.files} 个文件 · ${node.lines.toLocaleString()} 行`
        : `${node.lines} 行`}</span>` +
      badges.join('');
    el.appendChild(row);
    if (node.type === 'file') {
      row.addEventListener('click', () => {
        fileSel = path;
        document.querySelectorAll('.tnode.sel').forEach(n => n.classList.remove('sel'));
        el.classList.add('sel');
        renderFileDetail(node);
      });
    } else {
      row.addEventListener('click', () => {
        if (collapsed.has(path)) collapsed.delete(path); else collapsed.add(path);
        renderTree();
      });
    }
    if (node.type === 'dir' && node.children.length) {
      const kids = document.createElement('div');
      kids.className = 'children';
      node.children.forEach(c => {
        const k = walk(c, depth + 1);
        if (k) kids.appendChild(k);
      });
      el.appendChild(kids);
    }
    return el;
  };
  const rootEl = walk(root, 0);
  if (rootEl) box.appendChild(rootEl);
}

function renderFileDetail(node) {
  const box = $('#file-detail');
  const steps = (node.steps || []).map(n => P.steps.find(s => s.num === n)).filter(Boolean);
  const deeps = (node.deep || []).map(id => P.steps.find(s => s.id === id)).filter(Boolean);
  const meths = (P.methods || []).filter(m => m.file === node.path).slice(0, 10);
  box.innerHTML =
    `<h3>${esc(node.name)}</h3><div class="fp">${esc(node.path)}</div>` +
    `<dl>` +
    `<dt>规模</dt><dd>${node.lines} 行 · ${node.methods || 0} 个方法（清单口径）${node.inScope ? ' · 属于深挖范围' : ' · 仅被引用'}</dd>` +
    `<dt>直接引用的步骤（${steps.length}）</dt><dd><div class="chip-list">` +
      steps.map(s => `<span class="stepchip" data-step="${s.id}">${s.num} ${esc(s.title)}</span>`).join('') + '</div></dd>' +
    `<dt>精读 walk 里提到（${deeps.length}）</dt><dd><div class="chip-list">` +
      deeps.map(s => `<span class="stepchip" data-step="${s.id}">${s.num} ${esc(s.title)}</span>`).join('') + '</div></dd>' +
    ((node.p0 || []).length ? `<dt>逐行函数</dt><dd><div class="chip-list">` +
      node.p0.map(id => `<span class="stepchip" data-p0="${esc(id)}">${esc(id)}</span>`).join('') + '</div></dd>' : '') +
    `<dt>主要方法（前 ${meths.length}，按行数）</dt><dd>` +
      meths.map(m => `<div class="meth"><b>${esc(m.name)}()</b> · ${m.start}–${m.end} · ${m.lines} 行</div>`).join('') +
      (meths.length ? '' : '<span class="hint">该文件不在方法清单的扫描范围（不在 scope 里）</span>') + '</dd>' +
    `</dl>`;
  box.querySelectorAll('[data-step]').forEach(n =>
    n.addEventListener('click', () => {
      const s = P.steps.find(x => x.id === n.dataset.step);
      setTab('deep');
      selectStep(s.id);
    }));
  box.querySelectorAll('[data-p0]').forEach(n =>
    n.addEventListener('click', () => { setTab('code'); selectP0(n.dataset.p0); }));
}

function findFileNode(node, path) {
  if (node.type === 'file') return node.path === path ? node : null;
  for (const c of node.children) {
    const r = findFileNode(c, path);
    if (r) return r;
  }
  return null;
}

function selectFileByPath(path) {
  const node = findFileNode(P.fileTree, path);
  if (!node) return false;
  fileSel = path;
  renderTree();
  renderFileDetail(node);
  return true;
}

function initFiles() {
  filesSummary();
  renderTree();
  $('#files-search').addEventListener('input', renderTree);
  $('#files-only-hit').addEventListener('change', renderTree);
  $('#files-expand').addEventListener('click', () => { collapsed.clear(); renderTree(); });
  $('#files-collapse').addEventListener('click', () => {
    const walk = (n) => {
      if (n.type === 'dir') { collapsed.add(n.path || n.name); n.children.forEach(walk); }
    };
    walk(P.fileTree);
    renderTree();
  });
}

/* ------------------------------ inventory */
const cardOf = (m) => (P.cards || []).find(c =>
  c.file === m.file && (c.fn.endsWith('.' + m.name) || Math.abs(c.line - m.start) < 6));

function renderInventory() {
  const s = P.stats;
  const pct = (100 * s.coveredMethodLoc / Math.max(s.methodLoc, 1)).toFixed(1);
  $('#inv-summary').textContent =
    `${s.methods} 个方法 / ${s.methodLoc.toLocaleString()} 行方法体 · 逐行 ${s.coveredMethods} 个 / ${s.coveredMethodLoc} 行（${pct}%）`;

  const files = [...new Set(P.methods.map(m => m.fileBase))].sort();
  const sel = $('#inv-file');
  if (!sel.dataset.init) {
    sel.innerHTML = '<option value="all">全部文件</option>' +
      files.map(f => `<option value="${f}">${f}</option>`).join('');
    sel.dataset.init = '1';
    sel.addEventListener('change', renderRows);
    $('#inv-status').addEventListener('change', renderRows);
    $('#inv-search').addEventListener('input', renderRows);
  }
  renderRows();
}

function renderRows() {
  const file = $('#inv-file').value || 'all';
  const status = $('#inv-status').value || 'all';
  const q = ($('#inv-search').value || '').trim().toLowerCase();
  let rows = P.methods.filter(m =>
    (file === 'all' || m.fileBase === file) &&
    (status === 'all' || m.status === status) &&
    (!q || m.name.toLowerCase().includes(q) || m.fileBase.toLowerCase().includes(q)));
  const shown = rows.slice(0, 400);
  const html = ['<thead><tr><th>方法</th><th>所在文件</th><th>行范围</th><th>行数</th><th>状态</th></tr></thead><tbody>'];
  shown.forEach((m, i) => {
    const b = m.status === 'covered' ? '<span class="badge covered">逐行</span>'
      : m.status === 'partial' ? '<span class="badge partial">部分</span>'
      : '<span class="badge uncovered">未逐行</span>';
    html.push(`<tr class="row" data-i="${i}"><td><span class="fn">${esc(m.name)}()</span></td>` +
      `<td><span class="fl">${esc(m.fileBase)}</span></td>` +
      `<td><span class="rg">${m.start}–${m.end}</span></td>` +
      `<td><span class="rg">${m.lines}</span></td><td>${b}</td></tr>`);
  });
  html.push('</tbody>');
  const table = $('#inv-table');
  table.innerHTML = html.join('');
  $('#inv-count').textContent = `${rows.length} 条` + (rows.length > 400 ? '（显示前 400）' : '');
  table.querySelectorAll('tr.row').forEach(tr => {
    tr.addEventListener('click', () => openMethod(shown[+tr.dataset.i], tr));
  });
}

function openMethod(m, tr) {
  const prev = tr.nextElementSibling;
  if (prev && prev.classList.contains('card-row')) { prev.remove(); return; }
  if (m.status === 'covered') {
    setTab('code');
    selectP0(m.owner, { keepScroll: true });
    return;
  }
  const c = cardOf(m);
  const row = document.createElement('tr');
  row.className = 'card-row';
  const cell = document.createElement('td');
  cell.colSpan = 5;
  cell.innerHTML = c
    ? `<div class="card-box">
         <div><span class="k">线程</span><b>${esc(c.thread)}</b></div>
         <div><span class="k">锁</span><b>${esc(c.lock)}</b></div>
         <div><span class="k">规模</span><b>${c.size} 行</b></div>
         <div><span class="k">性质</span><b>函数级卡片</b></div>
         <div class="wide"><span class="k">职责</span>${esc(c.purpose)}</div>
         <div class="wide"><span class="k">风险</span>${esc(c.risk)}</div>
       </div>`
    : `<div class="card-box"><div class="wide"><span class="k">签名</span>${esc(m.title)}</div>
       <div class="wide" style="color:var(--tx-3)">该函数尚未逐行注释；它已在清单中可见，按需可补。</div></div>`;
  row.appendChild(cell);
  tr.after(row);
}

/* ------------------------------ risk */
function renderRisk() {
  const rows = P.pitfalls.map(p =>
    `<tr><td>${esc(p.case)}</td><td>${esc(p.behavior)}</td><td><code>${esc(p.ref)}</code></td></tr>`);
  $('#risk-table').innerHTML =
    `<thead><tr><th>场景</th><th>实际行为</th><th>证据</th></tr></thead><tbody>${rows.join('')}</tbody>`;
}

/* ------------------------------ selection */
function selectStep(id, opts = {}) {
  state.step = id;
  document.querySelectorAll('.step').forEach(n => n.classList.toggle('is-on', n.dataset.step === id));
  document.querySelectorAll('#diagram-seq .msg').forEach(g =>
    g.classList.toggle('is-on', g.dataset.step === id));
  renderStepCard();
  const s = P.steps.find(x => x.id === id);
  if (s && s.p0 && !opts.keepTab && !opts.silent) {
    setTab('code');
    selectP0(s.p0, { keepScroll: true });
  } else if (s && s.p0 && opts.keepTab) {
    // 已经切到 code，只换函数
    if (state.tab === 'code' && state.p0 !== s.p0) selectP0(s.p0, { keepScroll: true });
  }
  if (state.tab === 'deep') renderDeep();
  if (opts.scrollRail !== false) {
    const n = document.querySelector(`.step[data-step="${id}"]`);
    if (n) n.scrollIntoView({ block: 'nearest' });
  }
  history.replaceState(null, '', '#' + id);
}

function setTab(name) {
  state.tab = name;
  document.querySelectorAll('.tab').forEach(t => t.classList.toggle('is-on', t.dataset.tab === name));
  document.querySelectorAll('.view').forEach(v => v.classList.toggle('is-on', v.id === 'view-' + name));
  if (name === 'code') renderCode();
  if (name === 'deep') renderDeep();
}

/* ------------------------------ boot */
function boot() {
  initTheme();
  renderHeader();
  renderRail();
  renderChunks();
  renderOverview();
  renderDiagrams();
  renderRisk();
  renderInventory();
  initFiles();
  renderChips();
  renderCode();
  renderScenario();
  renderGlossary();

  document.querySelectorAll('.tab').forEach(t =>
    t.addEventListener('click', () => setTab(t.dataset.tab)));
  $('#deep-prev').addEventListener('click', () => deepGo(-1));
  $('#deep-next').addEventListener('click', () => deepGo(1));
  document.addEventListener('keydown', (e) => {
    if (state.tab !== 'deep') return;
    if (e.target.tagName === 'INPUT' || e.target.tagName === 'TEXTAREA') return;
    if (e.key === 'j' || e.key === 'ArrowRight') { e.preventDefault(); deepGo(1); }
    if (e.key === 'k' || e.key === 'ArrowLeft') { e.preventDefault(); deepGo(-1); }
  });
  $('#code-search').addEventListener('input', applyFilters);
  $('#only-notes').addEventListener('change', applyFilters);

  const hash = (location.hash || '').replace('#', '');
  const TABS = ['overview', 'deep', 'code', 'seq', 'cls', 'flow', 'files', 'methods', 'risk'];
  if (TABS.includes(hash)) {
    setTab(hash);
    selectStep(P.steps[0].id, { silent: true, scrollRail: false });
    return;
  }
  if (hash.startsWith('files:')) {
    setTab('files');
    selectStep(P.steps[0].id, { silent: true, scrollRail: false });
    const ok = selectFileByPath(decodeURIComponent(hash.slice(6)));
    if (!ok) setTab('files');
    return;
  }
  if (hash && P.steps.some(s => s.id === hash)) {
    setTab(hash === 's00' ? 'deep' : (P.steps.find(x => x.id === hash).deep ? 'deep' : 'code'));
    selectStep(hash);
  } else if (hash.startsWith('p0-')) {
    setTab('code');
    selectP0(hash);
  } else {
    selectStep(P.steps[0].id, { silent: true, scrollRail: false });
  }
}

function renderChunks() {
  // 预留：分块渲染（当前 code 最多数百行，直接渲染）
}

boot();
