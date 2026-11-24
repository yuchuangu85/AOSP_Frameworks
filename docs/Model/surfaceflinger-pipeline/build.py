#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Assemble the self-contained SurfaceFlinger Deep Dive HTML.

Reads:  surfaceflinger-pipeline/data/stage-*.json, surfaceflinger-pipeline/src/*
Writes: surfaceflinger-pipeline/index.html
Also verifies each code snippet against the real AOSP sources.
"""
import json, os, re, sys, html, datetime, collections, subprocess

ROOT = os.path.dirname(os.path.abspath(__file__))          # .../vsync-pipeline
# Locate the AOSP checkout by walking up until base/core/java/android/view exists.
REPO = os.path.dirname(ROOT)
_probe = REPO
for _ in range(6):
    if os.path.isfile(os.path.join(_probe, 'base/core/java/android/view/ViewRootImpl.java')):
        REPO = _probe
        break
    _probe = os.path.dirname(_probe)
DATA = os.path.join(ROOT, 'data')
SRC  = os.path.join(ROOT, 'src')
OUT  = os.path.join(ROOT, 'index.html')
GH_BASE = ''

def _git(*args):
    try:
        return subprocess.check_output(['git', '-C', REPO] + list(args), stderr=subprocess.DEVNULL, text=True).strip()
    except Exception:
        return ''

def github_base():
    """https://github.com/<owner>/<repo>/blob/<ref>/ — pinned to HEAD when HEAD is on a remote branch
    (stable line numbers), otherwise the branch name."""
    url = _git('remote', 'get-url', 'origin')
    m = re.search(r'github\.com[:/]([^/]+)/([^/]+?)(?:\.git)?$', url)
    owner, repo = (m.group(1), m.group(2)) if m else ('yuchuangu85', 'AOSP_Frameworks')
    head = _git('rev-parse', 'HEAD')
    ref = head if head and _git('branch', '-r', '--contains', head) else ''
    if not ref:
        # A local docs-only commit can still cite the identical published source.
        ancestor = _git('merge-base', 'HEAD', 'origin/HEAD')
        if ancestor and not _git('diff', '--name-only', ancestor, '--', 'base', 'native'):
            ref = ancestor
    ref = ref or (_git('branch', '--show-current') or 'main')
    return 'https://github.com/%s/%s/blob/%s/' % (owner, repo, ref)

# Relative path from the generated page back to the repo root, for "本地文件" links.
LOCAL_PREFIX = os.path.relpath(REPO, ROOT).replace(os.sep, '/') + '/'

def git_head():
    try:
        return subprocess.check_output(['git', '-C', REPO, 'rev-parse', '--short=9', 'HEAD'],
                                       stderr=subprocess.DEVNULL, text=True).strip() or 'unknown'
    except Exception:
        return 'unknown'

LANE_NAMES = {
    'APP': 'APP · BLAST', 'SCHED': 'SF · Scheduler', 'LAYER': 'SF · Layer', 'COMP': 'Composition / HWC',
    'DISP': 'Display / Timeline', 'GPU': 'GPU', 'BQ': 'BufferQueue / BLAST', 'RT': 'RenderThread', 'UI': 'UI Thread',
    'SF': 'SurfaceFlinger', 'HWC': 'HWC / Display', 'DISPLAY': 'HWC / Display'
}
LANE_SHORT = {'APP': 'APP / BLAST', 'SCHED': 'SF Scheduler', 'LAYER': 'SF Layer', 'COMP': 'Composition / HWC', 'DISP': 'Display / Timeline', 'GPU': 'GPU', 'BQ': 'BufferQueue / BLAST', 'RT': 'RenderThread', 'UI': 'UI 线程', 'SF': 'SurfaceFlinger', 'HWC': 'HWC / Display', 'DISPLAY': 'HWC / Display'}
# Studio modules, in dependency order (all attach to window.FP), then page chrome.
SCRIPTS = ['scenarios.js', 'sf-studio.js', 'app.js']
SHORT = {1: '输入 / BLAST', 2: 'Scheduler / VSYNC', 3: 'commit / 快照', 4: 'latch / fence', 5: '合成决策',
         6: 'GPU client comp', 7: 'present', 8: 'scanout / retire', 9: '回调 / 回收'}
ALLOWED_TAGS = {'p','ul','ol','li','strong','em','code','br','blockquote','table','thead','tbody','tr','th','td','div','span','small','sub','sup','h4','h5'}
DANGEROUS = re.compile(r'<\s*(script|style|iframe|object|embed|link|meta|img|svg|video|audio|form|input|button)\b[^>]*>', re.I)
TAG_RE = re.compile(r'<\s*/?\s*([a-zA-Z][a-zA-Z0-9-]*)\b[^>]*>')

def esc(s): return html.escape(str(s), quote=True)

def norm(s): return re.sub(r'\s+', ' ', s).strip()

def load_stages():
    stages = []
    for n in range(1, 10):
        path = os.path.join(DATA, 'stage-%02d.json' % n)
        if not os.path.exists(path):
            print('  ! missing %s' % path)
            continue
        with open(path, encoding='utf-8') as f:
            d = json.load(f)
        d['num'] = int(d.get('num', n))
        stages.append(d)
    stages.sort(key=lambda x: x['num'])
    return stages

def parse_lines(spec):
    m = re.match(r'^\s*(\d+)\s*(?:[-~]\s*(\d+))?\s*$', str(spec))
    if not m:
        return None
    a = int(m.group(1)); b = int(m.group(2) or a)
    if b < a: a, b = b, a
    return a, b

def verify_snippet(s):
    """Return (ok, ratio, message, matched_symbol)."""
    rel = s.get('file', '')
    path = os.path.join(REPO, rel)
    if not rel or not os.path.isfile(path):
        return (False, 0.0, 'file missing: %s' % rel, False)
    with open(path, encoding='utf-8', errors='replace') as f:
        src = f.readlines()
    rng = parse_lines(s.get('lines', ''))
    if not rng:
        return (False, 0.0, 'bad lines spec: %r' % s.get('lines'), False)
    a, b = rng
    if a < 1 or b > len(src):
        return (False, 0.0, 'range %d-%d outside file (%d lines)' % (a, b, len(src)), False)
    region = src[a-1:b]
    sym = s.get('symbol', '')
    sym_in_range = bool(sym) and any(sym in ln or (sym.split('::')[-1] in ln) for ln in region)
    code_lines = [ln for ln in str(s.get('code', '')).splitlines() if norm(ln) and '// ...' not in ln]
    if not code_lines:
        return (False, 0.0, 'empty code', sym_in_range)
    # Exact (whitespace-normalized) line match, in order. Blank source lines are skipped:
    # an empty string is a substring of everything and must never count as a hit.
    region_norm = [norm(ln) for ln in region]
    matched, pos = 0, 0
    for cl in code_lines:
        target = norm(cl)
        found = -1
        for k in range(pos, len(region_norm)):
            if region_norm[k] and region_norm[k] == target:
                found = k; break
        if found >= 0:
            matched += 1; pos = found + 1
    ratio = matched / float(len(code_lines))
    msg = '%d/%d lines' % (matched, len(code_lines))
    # A cited range much wider than the quoted code hides what is actually being shown.
    span_ok = (b - a + 1) <= max(60, 3 * len(code_lines))
    if not span_ok:
        msg += ', range %d lines too wide for %d code lines' % (b - a + 1, len(code_lines))
    return (ratio == 1.0 and span_ok, ratio, msg, sym_in_range)

def sanitize_html(fragment):
    fragment = DANGEROUS.sub('', str(fragment))
    def keep(m):
        tag = m.group(1).lower()
        return m.group(0) if tag in ALLOWED_TAGS else ''
    return TAG_RE.sub(keep, fragment)

def render_snippet(s, lang_by_ext):
    rel = s.get('file', '')
    base = os.path.basename(rel)
    folder = os.path.dirname(rel)
    ext = os.path.splitext(base)[1].lstrip('.').lower()
    lang = lang_by_ext.get(ext, 'text')
    code = html.escape(str(s.get('code', '')), quote=False)
    note = sanitize_html(s.get('note', ''))
    rng = parse_lines(s.get('lines', ''))
    anchor = ('#L%d-L%d' % rng if rng[0] != rng[1] else '#L%d' % rng[0]) if rng else ''
    gh = GH_BASE + rel + anchor
    local = LOCAL_PREFIX + rel
    return f'''<figure class="snippet">
  <figcaption class="snippet-head">
    <span class="dots"><i></i><i></i><i></i></span>
    <span class="fpath" title="{esc(rel)}"><b>{esc(base)}</b><span class="fp-dir">{esc(folder)}</span></span>
    <span class="ln">L{esc(s.get('lines',''))}</span>
    <span class="src-links"><a href="{esc(gh)}" target="_blank" rel="noopener" title="在 GitHub 打开（固定版本，带行号）">GitHub 源码 ↗</a><span aria-hidden="true">·</span><a href="{esc(local)}" title="{esc(local)}">本地文件</a></span>
    <button class="copy-btn" type="button" aria-label="复制这段源码"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><rect x="9" y="9" width="11" height="11" rx="2"/><path d="M5 15V5a2 2 0 0 1 2-2h10"/></svg></button>
  </figcaption>
  <pre><code data-highlight="{lang}">{code}</code></pre>
  <div class="snippet-note">{note}</div>
</figure>'''

def render_stage(d, lang_by_ext):
    n = d['num']
    lane = d.get('lane', 'UI')
    lane_name = LANE_NAMES.get(lane, lane)
    lane_short = LANE_SHORT.get(lane, lane)
    dur_full = str(d.get('durationHint', ''))
    dur_short = re.split(r'[；;]', dur_full)[0].strip()
    if len(dur_short) > 88:
        dur_short = dur_short[:88].rstrip() + '…'
    callchain = d.get('callchain') or []
    cc = ''.join('<span class="cc">%s</span>%s' % (esc(x), '<span class="arr">→</span>' if i < len(callchain)-1 else '') for i, x in enumerate(callchain))

    sections_html = ''.join('<h3>%s</h3>\n%s\n' % (esc(sec.get('heading','')), sanitize_html(sec.get('html',''))) for sec in d.get('sections', []))
    snippets_html = ''.join(render_snippet(s, lang_by_ext) for s in d.get('snippets', []))

    keys_html = ''
    for k in d.get('keys', []):
        keys_html += '<div class="row"><span class="n">%s</span><span class="t">%s</span><span class="d">%s</span></div>' % (
            esc(k.get('name','')), esc(k.get('type','')), sanitize_html(k.get('desc','')))
    pitfalls_html = ''.join('<div class="warn-item">%s</div>' % sanitize_html(p) for p in d.get('pitfalls', []))
    metrics_html = ''
    for m in d.get('metrics', []):
        metrics_html += '<div class="metric"><div class="m-top"><span class="m-name">%s</span><span class="m-val">%s</span></div><span class="m-d">%s</span></div>' % (
            esc(m.get('name','')), esc(m.get('value','')), sanitize_html(m.get('desc','')))
    trace_html = ''.join('<span>%s</span>' % esc(t) for t in d.get('traceTags', []))

    aside = f'''<aside class="stage-aside">
  <div class="card"><div class="card-h"><span class="ico">K</span>关键结构 / 字段</div><div class="card-b"><div class="kv">{keys_html}</div></div></div>
  <div class="card"><div class="card-h"><span class="ico">!</span>坑点与边界</div><div class="card-b"><div class="warn-list">{pitfalls_html}</div></div></div>
  <div class="card"><div class="card-h"><span class="ico">T</span>观测点</div><div class="card-b"><div class="metric-list">{metrics_html}</div></div></div>
  <div class="card"><div class="card-h"><span class="ico">#</span>Trace tags</div><div class="card-b"><div class="taglist">{trace_html}</div></div></div>
</aside>'''

    return f'''<section class="stage" id="stage-{n:02d}" data-lane="{esc(lane)}">
  <div class="stage-head">
    <div class="stage-headtxt">
      <div class="stage-kicker"><span class="lane-badge"><i></i>{esc(lane_short)}</span><span class="stage-dur" title="{esc(dur_full)}">{esc(dur_short)}</span></div>
      <h2><span class="stage-num">{n:02d}</span><span class="stage-title">{esc(d.get('title',''))}</span></h2>
      <p class="stage-sub">{esc(d.get('subtitle',''))}</p>
    </div>
    <button class="btn small ghost jump" type="button" data-stage-jump="{n}">
      <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"><path d="M5 3v18l15-9z"/></svg>
      在动画中查看
    </button>
  </div>
  <p class="stage-summary">{sanitize_html(d.get('summary',''))}</p>
  <div class="callchain">{cc}</div>
  <div class="stage-grid">
    <div class="stage-main">
      <div class="prose">
        {sections_html}
        {snippets_html}
      </div>
    </div>
    {aside}
  </div>
</section>'''

def build():
    global GH_BASE
    GH_BASE = github_base()
    stages = load_stages()
    if len(stages) < 9:
        print('  ! only %d/9 stages present' % len(stages))

    # ---- verify snippets ----
    lang_by_ext = {'java': 'java', 'cpp': 'cpp', 'cc': 'cpp', 'c': 'cpp', 'h': 'cpp', 'hpp': 'cpp', 'aidl': 'java'}
    report = []
    total = passed = 0
    file_stat = collections.defaultdict(lambda: {'syms': [], 'stages': set(), 'count': 0})
    for d in stages:
        for s in d.get('snippets', []):
            ok, ratio, msg, sym_ok = verify_snippet(s)
            total += 1; passed += 1 if ok else 0
            report.append({'stage': d['num'], 'file': s.get('file'), 'lines': s.get('lines'), 'symbol': s.get('symbol'),
                           'ok': ok, 'ratio': round(ratio, 3), 'msg': msg, 'symbolInRange': sym_ok})
            st = file_stat[s.get('file','')]
            st['count'] += 1; st['stages'].add(d['num'])
            if s.get('symbol') and s['symbol'] not in st['syms']:
                st['syms'].append(s['symbol'])
            if not ok:
                print('  ~ stage-%02d %s:%s -> %s (symbol in range: %s)' % (d['num'], s.get('file'), s.get('lines'), msg, sym_ok))
    with open(os.path.join(DATA, 'verify-report.json'), 'w', encoding='utf-8') as f:
        json.dump(report, f, ensure_ascii=False, indent=1)

    # ---- nav & flow ----
    nav = '''<a href="#overview" data-nav>总览</a>
      <a href="#anatomy" data-nav>时间轴</a>
      <a href="#stages" data-nav>九个阶段</a>
      <a href="#composition" data-nav>合成路径</a>
      <a href="#observe" data-nav>观测</a>
      <a href="#sources" data-nav>源码</a>
      <span class="sep"></span>
      <a href="#anatomy" class="nav-cta">▶ 播放</a>'''
    flow = ''
    for d in stages:
        n = d['num']
        flow += '<div class="flow-step" data-goto="stage-%02d"><span class="n">STAGE %02d</span><span class="t">%s</span><span class="l">%s</span></div>' % (
            n, n, esc(SHORT.get(n, d.get('title',''))), esc(LANE_SHORT.get(d.get('lane','UI'), d.get('lane',''))))

    # ---- sources ----
    rows = ''
    for rel in sorted(file_stat, key=lambda f: (min(file_stat[f]['stages']), f)):
        info = file_stat[rel]
        syms = info['syms']
        sym_html = ''.join('<span class="sym">%s</span>' % esc(x) for x in syms[:4])
        if len(syms) > 4:
            sym_html += '<span class="sym">… +%d</span>' % (len(syms) - 4)
        stage_links = ''.join('<a href="#stage-%02d">%02d</a>' % (n, n) for n in sorted(info['stages']))
        rows += '<tr><td><span class="fp">%s</span>%s</td><td>%d</td><td><span class="stg">%s</span></td></tr>' % (
            esc(rel), sym_html, info['count'], stage_links)
    sources = '''<table class="sources-table">
  <thead><tr><th>源码文件</th><th>片段</th><th>阶段</th></tr></thead>
  <tbody>%s</tbody>
</table>''' % rows

    trace = []
    for d in stages:
        for t in d.get('traceTags', []):
            if t not in trace: trace.append(t)
    trace_html = ''.join('<span>%s</span>' % esc(t) for t in trace)

    rail = ''.join('<a href="#stage-%02d" data-rail="%d" title="%s"><i></i><span>%02d · %s</span></a>' % (
        d['num'], d['num'], esc(d.get('title','')), d['num'], esc(SHORT.get(d['num'], d.get('title','')))) for d in stages)
    stage_sections = '\n'.join(render_stage(d, lang_by_ext) for d in stages)
    snippet_count = total
    cited_count = len(file_stat)
    verify_pct = (100.0 * passed / total) if total else 0
    verify_summary = '%d/%d 段片段与源码逐行精确匹配（去除空白差异后 100%% 行命中、引用范围不过宽），覆盖 %d 个文件。' % (passed, total, cited_count)
    if passed < total:
        verify_summary += ' 有 %d 段需要人工复核（见 data/verify-report.json）。' % (total - passed)

    data = [{'num': d['num'], 'title': d.get('title',''), 'subtitle': d.get('subtitle',''), 'lane': d.get('lane','UI'),
             'durationHint': d.get('durationHint',''), 'summary': d.get('summary',''), 'traceTags': d.get('traceTags', [])} for d in stages]
    data_json = json.dumps({'stages': data, 'version': 'android17-release', 'head': git_head()}, ensure_ascii=False, separators=(',', ':')).replace('</', '<\\/')

    shell = open(os.path.join(SRC, 'shell.html'), encoding='utf-8').read()
    style = open(os.path.join(SRC, 'style.css'), encoding='utf-8').read()
    script = '\n'.join(open(os.path.join(SRC, name), encoding='utf-8').read() for name in SCRIPTS)
    for name in ('fp-studio.css', 'theme-dark.css', 'theme-light.css'):
        style += '\n' + open(os.path.join(SRC, name), encoding='utf-8').read()

    rep = {
        '{{STYLE}}': style,
        '{{NAV}}': nav,
        '{{FLOW}}': flow,
        '{{STAGE_SECTIONS}}': stage_sections,
        '{{RAIL}}': rail,
        '{{SOURCES}}': sources,
        '{{TRACETAGS}}': trace_html,
        '{{DATA}}': data_json,
        '{{SCRIPT}}': script,
        '{{SNIPPET_COUNT}}': str(snippet_count),
        '{{CITED_FILE_COUNT}}': str(cited_count),
        '{{VERIFY_SUMMARY}}': verify_summary,
        '{{HEAD}}': git_head(),
        '{{BUILD_DATE}}': datetime.datetime.now().strftime('%Y-%m-%d %H:%M'),
    }
    for k, v in rep.items():
        shell = shell.replace(k, v)
    leftovers = re.findall(r'\{\{[A-Z_]+\}\}', shell)
    if leftovers:
        print('  ! unresolved placeholders:', set(leftovers))

    with open(OUT, 'w', encoding='utf-8') as f:
        f.write(shell)
    size = os.path.getsize(OUT)
    print('built %s (%.1f KB)' % (OUT, size / 1024.0))
    print('stages=%d snippets=%d files=%d verified=%d/%d (%.1f%%)' % (len(stages), total, cited_count, passed, total, verify_pct))
    bad = [r for r in report if not r['ok']]
    if bad:
        print('verification warnings:')
        for r in bad:
            print('  stage-%02d %s:%s %s' % (r['stage'], r['file'], r['lines'], r['msg']))
    nosym = [r for r in report if not r['symbolInRange']]
    if nosym:
        print('symbols not found in cited range (review):')
        for r in nosym:
            print('  stage-%02d %s:%s %r' % (r['stage'], r['file'], r['lines'], r['symbol']))
    return 0 if passed == total else 1

if __name__ == '__main__':
    sys.exit(build())