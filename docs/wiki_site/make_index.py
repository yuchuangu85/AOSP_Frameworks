#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""扫描 dist/*.html 的 payload 与 docs/Model，生成站点索引 dist/index.html。

用法：python3 docs/wiki_site/make_index.py
build.py 在每次构建结束时也会自动调用。
"""

import glob
import html
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
def _find_repo(start):
    cur = start
    for _ in range(8):
        if os.path.isdir(os.path.join(cur, 'base', 'core', 'java', 'android', 'view')):
            return cur
        cur = os.path.dirname(cur)
    return os.path.dirname(start)


REPO = _find_repo(HERE)
DIST = os.path.join(HERE, 'dist')

CSS = """
  :root, [data-theme="dark"] {
    --bg: #23262b; --bg-2: #2a2e34; --panel: #2d3138;
    --line: rgba(255,255,255,0.16); --line-2: rgba(255,255,255,0.30);
    --tx: #f5f8fc; --tx2: #c8d1dd; --tx3: #98a5b5;
    --amber: #ffb454; --teal: #33d6c0; --card-shadow: 0 1px 0 rgba(0,0,0,.25);
  }
  [data-theme="light"] {
    --bg: #f3efe5; --bg-2: #ece5d7; --panel: #fcf9f1;
    --line: rgba(62,50,34,0.16); --line-2: rgba(62,50,34,0.30);
    --tx: #2b2620; --tx2: #564f42; --tx3: #7d7362;
    --amber: #8a5200; --teal: #0a6b5f; --card-shadow: 0 1px 2px rgba(62,50,34,.10);
  }
  * { box-sizing: border-box; }
  body {
    margin: 0; padding: 52px 40px 80px;
    background: var(--bg);
    color: var(--tx); min-height: 100vh;
    font: 14px/1.7 -apple-system, BlinkMacSystemFont, "PingFang SC", "Segoe UI", "Noto Sans CJK SC", sans-serif;
    -webkit-font-smoothing: antialiased;
  }
  [data-theme="light"] body {
    background: radial-gradient(1100px 520px at 12% -10%, rgba(176,116,30,0.075), transparent 62%),
                radial-gradient(900px 460px at 92% 0%, rgba(10,115,101,0.05), transparent 58%),
                var(--bg);
  }
  .wrap { max-width: 1240px; margin: 0 auto; }
  .top { display: flex; align-items: flex-start; justify-content: space-between; gap: 20px; }
  .kicker { font: 11px/1 ui-monospace, Menlo, monospace; letter-spacing: .16em; color: var(--amber); text-transform: uppercase; }
  h1 { margin: 12px 0 8px; font-size: 32px; letter-spacing: -0.01em; }
  .sub { margin: 0 0 26px; color: var(--tx2); max-width: 820px; }
  .theme-toggle { display: inline-flex; align-items: center; gap: 7px; padding: 8px 12px; border-radius: 999px;
    border: 1px solid var(--line); background: var(--panel); color: var(--tx2); cursor: pointer; font: 12px/1 inherit; white-space: nowrap; }
  .theme-toggle:hover { border-color: var(--line-2); color: var(--tx); }
  .theme-toggle svg { width: 15px; height: 15px; }
  .theme-toggle .moon { display: block; } .theme-toggle .sun { display: none; }
  [data-theme="light"] .theme-toggle .moon { display: none; } [data-theme="light"] .theme-toggle .sun { display: block; }
  .theme-toggle svg path, .theme-toggle svg circle { fill: none; stroke: currentColor; stroke-width: 1.6; stroke-linecap: round; stroke-linejoin: round; }
  .theme-toggle .moon path { fill: currentColor; stroke: none; }
  .totals { display: flex; gap: 10px; flex-wrap: wrap; margin: 0 0 30px; }
  .total { padding: 9px 14px; border: 1px solid var(--line); border-radius: 10px; background: var(--panel); }
  .total b { display: block; font: 600 18px/1.2 ui-monospace, Menlo, monospace; }
  .total span { font-size: 11px; color: var(--tx3); }
  h3.section { margin: 34px 0 14px; font-size: 15px; color: var(--tx2); letter-spacing: .02em; }
  .cards { display: grid; grid-template-columns: repeat(auto-fit, minmax(380px, 1fr)); gap: 16px; }
  .card { display: block; padding: 22px 24px; border-radius: 14px; border: 1px solid var(--line);
    background: var(--panel); box-shadow: var(--card-shadow); color: inherit;
    transition: border-color .18s, transform .18s; }
  .card:hover { border-color: var(--amber); transform: translateY(-2px); }
  .card a.ttl { text-decoration: none; color: inherit; }
  .card a.ttl:hover h2 { color: var(--amber); }
  .card-top { display: flex; align-items: baseline; justify-content: space-between; gap: 12px; }
  .card h2 { margin: 0 0 6px; font-size: 17px; }
  .ver { font: 10px/1 ui-monospace, Menlo, monospace; color: var(--tx3); white-space: nowrap; }
  .card p { margin: 0 0 14px; color: var(--tx2); font-size: 12.5px; }
  .meta { display: flex; gap: 7px; flex-wrap: wrap; margin-bottom: 12px; }
  .pill { font: 10.5px/1.8 ui-monospace, Menlo, monospace; padding: 0 8px; border-radius: 5px;
    border: 1px solid var(--line); color: var(--tx3); }
  .pill.hot { color: var(--amber); border-color: rgba(255,180,84,.35); background: rgba(255,180,84,.06); }
  .pill.ok { color: var(--teal); border-color: rgba(51,214,192,.30); background: rgba(51,214,192,.05); }
  [data-theme="light"] .pill.hot { border-color: rgba(138,82,0,.35); background: rgba(138,82,0,.06); }
  [data-theme="light"] .pill.ok { border-color: rgba(10,107,95,.30); background: rgba(10,107,95,.05); }
  .views { display: flex; gap: 10px; flex-wrap: wrap; align-items: center; font-size: 11.5px; color: var(--tx3); }
  .views a { color: var(--tx2); text-decoration: none; border-bottom: 1px dotted var(--line-2); }
  .views a:hover { color: var(--amber); border-color: var(--amber); }
  ul.models { list-style: none; margin: 0; padding: 0; display: grid;
    grid-template-columns: repeat(auto-fit, minmax(260px, 1fr)); gap: 8px; }
  ul.models li { border: 1px solid var(--line); border-radius: 8px; background: var(--panel); }
  ul.models a { display: block; padding: 9px 12px; color: var(--tx2); text-decoration: none;
    font: 11.5px/1.5 ui-monospace, Menlo, monospace; }
  ul.models a:hover { color: var(--amber); }
  footer { margin-top: 40px; color: var(--tx3); font-size: 11.5px; line-height: 1.9; }
  code { font-family: ui-monospace, Menlo, monospace; color: var(--tx2); }
  @media (max-width: 720px) { body { padding: 32px 20px 60px; } .top { flex-direction: column; } }
"""

SHELL = """<!DOCTYPE html>
<html lang="zh-CN" data-theme="dark">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta name="color-scheme" content="dark light">
<title>AOSP 源码级解剖 · 站点索引</title>
<script>
  (function () {
    try {
      var saved = localStorage.getItem('st-theme');
      var sys = window.matchMedia && window.matchMedia('(prefers-color-scheme: light)').matches ? 'light' : 'dark';
      var forced = null;
      try { forced = new URLSearchParams(location.search).get('theme'); } catch (e) {}
      document.documentElement.dataset.theme =
        (forced === 'light' || forced === 'dark') ? forced : (saved || sys);
    } catch (e) { document.documentElement.dataset.theme = 'dark'; }
  })();
</script>
<style>__CSS__</style>
</head>
<body>
<div class="wrap">
  <div class="top">
    <div>
      <div class="kicker">AOSP · android17-release · 源码级解剖</div>
      <h1>站点索引：__N__ 条链路</h1>
      <p class="sub">每个主题都是单文件、离线、可交互：精读 + 逐行注释 + 方法清单 + 文件结构 + 四类图，支持亮/暗色与深链。
        本页在构建时扫描 <code>dist/*.html</code>、<code>anr/*.html</code> 与 <code>docs/Model/*/index.html</code> 自动生成，新增主题后不需要手改。</p>
    </div>
    <button class="theme-toggle" id="theme-toggle" type="button" title="切换亮/暗色">
      <svg class="moon" viewBox="0 0 24 24" aria-hidden="true"><path d="M21 12.8A8.5 8.5 0 1 1 11.2 3a7 7 0 0 0 9.8 9.8z"/></svg>
      <svg class="sun" viewBox="0 0 24 24" aria-hidden="true"><circle cx="12" cy="12" r="4.2"/><path d="M12 2v3M12 19v3M2 12h3M19 12h3M4.9 4.9l2.1 2.1M17 17l2.1 2.1M19.1 4.9L17 7M7 17l-2.1 2.1"/></svg>
      <span id="theme-label">亮色</span>
    </button>
  </div>

  <div class="totals">__TOTALS__</div>

  <h3 class="section">主题页</h3>
  <div class="cards">__CARDS__</div>

  <h3 class="section">模型页（docs/Model）</h3>
  <ul class="models">__MODELS__</ul>

  <h3 class="section">ANR 文档（anr/）</h3>
  <ul class="models">__ANR__</ul>

  <footer>
    构建：<code>python3 docs/wiki_site/build.py --topic &lt;slug&gt;</code> · 校验：<code>python3 docs/wiki_site/verify.py --topic &lt;slug&gt;</code><br>
    主题数据：<code>docs/wiki_site/topics/&lt;slug&gt;/</code> · 索引由 <code>docs/wiki_site/make_index.py</code> 生成
  </footer>
</div>
<script>
  (function () {
    var root = document.documentElement;
    var btn = document.getElementById('theme-toggle');
    var label = document.getElementById('theme-label');
    function apply() {
      var light = root.dataset.theme === 'light';
      label.textContent = light ? '暗色' : '亮色';
      btn.title = light ? '切换到暗色' : '切换到亮色';
      btn.setAttribute('aria-pressed', String(light));
    }
    btn.addEventListener('click', function () {
      root.dataset.theme = root.dataset.theme === 'light' ? 'dark' : 'light';
      try { localStorage.setItem('st-theme', root.dataset.theme); } catch (e) {}
      try {
        var u = new URL(location.href);
        if (u.searchParams.has('theme')) {
          u.searchParams.delete('theme');
          history.replaceState(null, '', u.pathname + (u.searchParams.toString() ? '?' + u.searchParams : '') + u.hash);
        }
      } catch (e) {}
      apply();
    });
    apply();
  })();
</script>
</body>
</html>"""

CARD = """      <div class="card">
        <div class="card-top">
          <a class="ttl" href="__FILE__"><h2>__TITLE__</h2></a>
          <span class="ver">__VERSION__</span>
        </div>
        <p>__SUB__</p>
        <div class="meta">
          <span class="pill hot">__STEPS__ 步</span>
          <span class="pill hot">__P0__ 函数逐行</span>
          <span class="pill">__NOTES__ 条行注释</span>
          <span class="pill">__LOC__ 行代码</span>
          <span class="pill ok">__DEEP__ 篇精读</span>
          <span class="pill">__METHODS__ 方法清单</span>
        </div>
        <div class="views">
          <span>视图：</span>
          <a href="__FILE__#deep">精读</a>
          <a href="__FILE__#code">代码逐行</a>
          <a href="__FILE__#seq">时序</a>
          <a href="__FILE__#files">文件结构</a>
          <a href="__FILE__#methods">方法清单</a>
          <a href="__FILE__#risk">降级与坑</a>
        </div>
      </div>"""


def esc(t):
    return html.escape(str(t), quote=True)


def collect_topics():
    items = []
    for path in sorted(glob.glob(os.path.join(DIST, '*.html'))):
        name = os.path.basename(path)
        if name == 'index.html':
            continue
        try:
            text = open(path, encoding='utf-8').read()
        except OSError:
            continue
        m = re.search(r'<script id="payload" type="application/json">(.*?)</script>', text, re.S)
        if not m:
            continue
        try:
            data = json.loads(m.group(1))
        except ValueError:
            continue
        st = data.get('stats', {})
        meta = data.get('meta', {})
        items.append({
            'slug': meta.get('slug') or name[:-5],
            'file': name,
            'title': meta.get('title') or name[:-5],
            'subtitle': meta.get('subtitle', ''),
            'version': meta.get('version', ''),
            'steps': st.get('steps', 0), 'p0': st.get('p0', 0),
            'notes': st.get('annotated', 0), 'loc': st.get('loc', 0),
            'deep': st.get('deep', 0), 'methods': st.get('methods', 0),
        })
    order = {'shell-transition': 0, 'render-thread-to-sf': 1,
             'view-traversal': 2, 'wms': 3, 'transaction': 4,
             'sf-compose-to-display': 5, 'vsync': 6, 'input-dispatch': 7, 'binder-ipc': 8}
    items.sort(key=lambda x: (order.get(x['slug'], 50), x['slug']))
    return items


def collect_models():
    out = []
    for p in sorted(glob.glob(os.path.join(REPO, 'docs', 'Model', '*', 'index.html'))):
        name = os.path.basename(os.path.dirname(p))
        rel = os.path.relpath(p, DIST).replace(os.sep, '/')
        out.append((name, rel))
    return out


def main():
    topics = collect_topics()
    models = collect_models()
    totals = [
        ('n', len(topics), '主题'),
        ('steps', sum(t['steps'] for t in topics), '链路步骤'),
        ('p0', sum(t['p0'] for t in topics), '逐行函数'),
        ('notes', sum(t['notes'] for t in topics), '行注释'),
        ('deep', sum(t['deep'] for t in topics), '精读文章'),
        ('methods', sum(t['methods'] for t in topics), '方法清单'),
    ]
    totals_html = ''.join('<div class="total"><b>%s</b><span>%s</span></div>' % (v, esc(label))
                          for _, v, label in totals)
    cards = []
    for t in topics:
        c = CARD
        for k, v in (('__FILE__', t['file']), ('__TITLE__', esc(t['title'])),
                     ('__VERSION__', esc(t['version'])), ('__SUB__', esc(t['subtitle'])),
                     ('__STEPS__', str(t['steps'])), ('__P0__', str(t['p0'])),
                     ('__NOTES__', str(t['notes'])), ('__LOC__', str(t['loc'])),
                     ('__DEEP__', str(t['deep'])), ('__METHODS__', str(t['methods']))):
            c = c.replace(k, v)
        cards.append(c)
    models_html = ''.join('<li><a href="%s">%s</a></li>' % (esc(rel), esc(name))
                          for name, rel in models)
    anr_dir = os.path.join(HERE, 'anr')
    anr_items = []
    if os.path.isdir(anr_dir):
        for f in sorted(glob.glob(os.path.join(anr_dir, '*.html'))):
            if os.path.basename(f) == 'index.html':
                continue
            try:
                title = re.search(r'<title>(.*?)</title>', open(f, encoding='utf-8').read()).group(1)
            except Exception:
                title = os.path.basename(f)[:-5]
            title = title.replace(' · ANR', '')
            anr_items.append('<li><a href="%s">%s</a></li>' % (
                esc(os.path.relpath(f, DIST).replace(os.sep, '/')), esc(title)))
    if os.path.exists(os.path.join(anr_dir, 'index.html')):
        anr_items.insert(0, '<li><a href="%s">ANR 索引（%d 篇）</a></li>' % (
            esc(os.path.relpath(os.path.join(anr_dir, 'index.html'), DIST).replace(os.sep, '/')),
            len(anr_items)))
    anr_html = ''.join(anr_items)
    doc = (SHELL.replace('__CSS__', CSS)
                .replace('__N__', str(len(topics)))
                .replace('__TOTALS__', totals_html)
                .replace('__CARDS__', '\n'.join(cards))
                .replace('__MODELS__', models_html)
                .replace('__ANR__', anr_html))
    out = os.path.join(DIST, 'index.html')
    with open(out, 'w', encoding='utf-8') as f:
        f.write(doc)
    print('index: %d topics, %d model pages -> dist/index.html (%.0f KB)'
          % (len(topics), len(models), len(doc) / 1024))
    return 0


if __name__ == '__main__':
    sys.exit(main())
