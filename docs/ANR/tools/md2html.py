#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""把 docs/ANR/*.md 转成 wiki 站点里的离线 HTML（复用 docs/wiki_site/src/app.css）。

用法：
    python3 docs/ANR/tools/md2html.py              # 全部转换 + 生成 anr/index.html
    python3 docs/ANR/tools/md2html.py ANRAnalysis.md

输出：docs/wiki_site/anr/<name>.html + index.html + assets/mermaid.min.js
"""

import html
import os
import re
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ANR = os.path.dirname(HERE)
ASSETS = os.path.normpath(os.path.join(HERE, '..', '..', 'wiki_site', 'assets'))
OUT = os.path.normpath(os.path.join(HERE, '..', '..', 'wiki_site', 'anr'))
WIKI_SRC = os.path.normpath(os.path.join(HERE, '..', '..', 'wiki_site', 'src'))

ORDER = [
    'ANRAnalysis.md',
    'ANR原理及渲染流程中的ANR风险点全面分析.md',
    'ANR日志分析完整实战指南.md',
    'ANR问题完整分析指南.md',
    'Broadcast-ANR放炸弹与拆炸弹机制源码深度解析.md',
    'Input-ANR放炸弹拆炸弹机制详解.md',
    'Input-ANR放炸弹与拆炸弹机制源码深度解析.md',
    'Provider-ANR放炸弹与拆炸弹机制源码深度解析.md',
    'Service-ANR放炸弹与拆炸弹机制源码深度解析.md',
]

ANR_CSS = r"""
/* ---------- ANR 文档：沿用 wiki 的 hdr/rail/stage/panel，只补正文排版 ---------- */
.anr-toc { padding: 4px 0 40px; }
.anr-toc a { display: block; padding: 5px 10px 5px 12px; border-left: 2px solid transparent;
  color: var(--tx-2); font-size: 12.5px; line-height: 1.55; text-decoration: none;
  overflow-wrap: anywhere; word-break: break-word; }
.anr-toc a:hover { color: var(--tx); background: rgba(255,255,255,.04); }
[data-theme="light"] .anr-toc a:hover { background: rgba(62,50,34,.05); }
.anr-toc a.lv3 { padding-left: 24px; font-size: 12px; color: var(--tx-3); }
.anr-toc a.active { border-left-color: var(--wms); color: var(--tx); background: rgba(255,180,84,.08); }
.anr-doc { padding: 24px 30px 30px; }
.anr-doc > h2:first-child { margin-top: 0; padding-top: 0; border-top: 0; }
.anr-doc h2 { margin: 30px 0 10px; padding-top: 20px; border-top: 1px solid var(--line);
  font-size: 20px; letter-spacing: 0; color: var(--tx); text-transform: none; }
.anr-doc h3 { margin: 22px 0 8px; font-size: 15.5px; color: var(--tx); letter-spacing: 0; text-transform: none; }
.anr-doc h4 { margin: 16px 0 6px; font-size: 13.5px; color: var(--tx-2); }
.anr-doc p { margin: 9px 0; color: var(--tx-2); line-height: 1.85; }
.anr-doc strong { color: var(--tx); }
.anr-doc ul, .anr-doc ol { margin: 8px 0; padding-left: 22px; color: var(--tx-2); }
.anr-doc li { margin: 4px 0; line-height: 1.75; }
.anr-doc blockquote { margin: 14px 0; padding: 10px 16px; border-left: 3px solid var(--wms);
  background: rgba(255,180,84,.07); border-radius: 0 8px 8px 0; color: var(--tx-2); }
[data-theme="light"] .anr-doc blockquote { background: rgba(138,82,0,.07); }
.anr-doc code { font: 12.5px/1.6 var(--mono); background: #191c21; padding: 1px 5px; border-radius: 4px; color: var(--tx); }
[data-theme="light"] .anr-doc code { background: #f3ecdf; }
.anr-doc pre { position: relative; background: #191c21; border: 1px solid var(--line); border-radius: 12px;
  padding: 14px 16px; overflow: auto; margin: 14px 0; }
[data-theme="light"] .anr-doc pre { background: #faf6eb; }
.anr-doc pre code { background: none; padding: 0; font-size: 12.5px; line-height: 1.75; }
.anr-doc hr { border: 0; border-top: 1px solid var(--line); margin: 24px 0; }
.anr-doc a { color: var(--wms); }
.anr-doc img { max-width: 100%; }
.anr-doc .table-wrap { overflow: auto; margin: 14px 0; border: 1px solid var(--line); border-radius: 12px; }
.anr-doc table { border-collapse: collapse; width: 100%; font-size: 13px; }
.anr-doc th, .anr-doc td { border-bottom: 1px solid var(--line); padding: 8px 12px; text-align: left; vertical-align: top; }
.anr-doc th { background: var(--panel-2); color: var(--tx); font-weight: 600; white-space: nowrap; }
.anr-doc tr:last-child td { border-bottom: 0; }
.mermaid-box { margin: 16px 0; border: 1px solid var(--line); border-radius: 12px; background: var(--panel-2); overflow: hidden; }
.mermaid-hd { display: flex; justify-content: space-between; gap: 10px; padding: 8px 12px; border-bottom: 1px solid var(--line);
  font: 10.5px/1.6 var(--mono); color: var(--tx-3); letter-spacing: .06em; }
.mermaid { padding: 16px; margin: 0; background: none; border: 0; overflow: auto; text-align: center; }
pre.mermaid { white-space: pre-wrap; text-align: left; font: 12px/1.7 var(--mono); color: var(--tx-3); }
.mermaid svg { max-width: 100%; height: auto; }
.anr-pager { display: flex; justify-content: space-between; gap: 14px; margin: 4px 0 40px; font-size: 12.5px; }
.anr-pager a { color: var(--tx-2); text-decoration: none; border: 1px solid var(--line); border-radius: 999px;
  padding: 7px 14px; background: var(--panel); }
.anr-pager a:hover { border-color: var(--line-2); color: var(--tx); }
.anr-cards { display: grid; gap: 12px; margin-top: 18px; }
.anr-card { display: block; padding: 16px 18px; border: 1px solid var(--line); border-radius: 14px;
  background: var(--panel); color: inherit; text-decoration: none; transition: border-color .16s, transform .16s; }
.anr-card:hover { border-color: var(--wms); transform: translateY(-1px); }
.anr-card .hd { display: flex; align-items: baseline; gap: 12px; }
.anr-card .no { font: 600 11px/1 var(--mono); color: var(--tx-3); }
.anr-card h2 { margin: 0; font-size: 16px; letter-spacing: 0; text-transform: none; border: 0; padding: 0; }
.anr-card .meta { display: flex; gap: 7px; flex-wrap: wrap; margin-top: 10px; }
.anr-card .pill { font: 10.5px/1.8 var(--mono); padding: 0 8px; border-radius: 5px; border: 1px solid var(--line); color: var(--tx-3); }
"""


def wiki_css():
    try:
        base = open(os.path.join(WIKI_SRC, 'app.css'), encoding='utf-8').read()
    except OSError:
        base = ''
    return base + '\n' + ANR_CSS


SHELL = """<!DOCTYPE html>
<html lang="zh-CN" data-theme="dark">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta name="color-scheme" content="dark light">
<title>__TITLE__ · ANR</title>
<script>
(function(){try{
  var s=localStorage.getItem('st-theme');
  var sys=window.matchMedia&&window.matchMedia('(prefers-color-scheme: light)').matches?'light':'dark';
  var f=null;try{f=new URLSearchParams(location.search).get('theme');}catch(e){}
  document.documentElement.dataset.theme=(f==='light'||f==='dark')?f:(s||sys);
}catch(e){document.documentElement.dataset.theme='dark';}})();
</script>
<style>__CSS__</style>
</head>
<body>
<div class="bg-grid" aria-hidden="true"></div>

<header class="hdr">
  <div class="hdr-main">
    <div class="kicker">AOSP · android17-release · ANR</div>
    <h1>__TITLE__</h1>
    <p class="sub">__SOURCE__ · __STAT_LINE__</p>
  </div>
  <div class="hdr-right">
    <button class="theme-toggle" id="theme-toggle" type="button" aria-pressed="false" title="切换亮/暗色">
      <svg class="ico-moon" viewBox="0 0 24 24" aria-hidden="true"><path d="M21 12.8A8.5 8.5 0 1 1 11.2 3a7 7 0 0 0 9.8 9.8z"/></svg>
      <svg class="ico-sun" viewBox="0 0 24 24" aria-hidden="true"><circle cx="12" cy="12" r="4.2"/><path d="M12 2v3M12 19v3M2 12h3M19 12h3M4.9 4.9l2.1 2.1M17 17l2.1 2.1M19.1 4.9L17 7M7 17l-2.1 2.1"/></svg>
      <span class="tt-label">亮色</span>
    </button>
    <div class="hdr-stats">__STATS__</div>
  </div>
</header>

<main class="app">
  <aside class="rail">
    <div class="phase">返回 <a href="../dist/index.html">站点索引</a> · <a href="index.html">ANR 索引</a></div>
    <nav class="anr-toc" id="toc">__TOC__</nav>
  </aside>
  <section class="stage">
    <div class="panel">
      <div class="panel-hd"><h2>正文</h2><span class="hint">__SOURCE__ · __STAT_LINE__</span></div>
      <article class="deep-body anr-doc">__BODY__</article>
    </div>
    <div class="anr-pager">__PAGER__</div>
  </section>
</main>

<footer class="ftr">
  <span>docs/ANR/__SOURCE__</span><span class="dot">·</span>
  <span>由 docs/ANR/tools/md2html.py 生成，样式复用 wiki（docs/wiki_site/src/app.css）</span>
</footer>

<script src="../assets/mermaid.min.js"></script>
<script>
(function(){
  var srcs=[];
  document.querySelectorAll('pre.mermaid').forEach(function(el,i){ srcs[i]=el.textContent; });
  function themeName(){ return document.documentElement.dataset.theme==='light'?'default':'dark'; }
  function render(){
    if(!window.mermaid) return;
    document.querySelectorAll('pre.mermaid').forEach(function(el,i){
      el.removeAttribute('data-processed'); el.textContent = srcs[i];
    });
    var dark = themeName()==='dark';
    try{
      mermaid.initialize({startOnLoad:false,theme:themeName(),securityLevel:'loose',
        themeVariables: dark ? {
          background:'transparent', primaryColor:'#30343a', primaryTextColor:'#f5f8fc',
          primaryBorderColor:'#5a6068', lineColor:'#9aa4b2', secondaryColor:'#2a2e34',
          tertiaryColor:'#2a2e34', textColor:'#e8eef6', mainBkg:'#30343a',
          nodeBorder:'#5a6068', clusterBkg:'#2a2e34', clusterBorder:'#4a5058',
          edgeLabelBackground:'#23262b', actorBkg:'#30343a', actorBorder:'#5a6068',
          signalColor:'#9aa4b2'
        } : {},
        flowchart:{htmlLabels:true,useMaxWidth:true},sequence:{useMaxWidth:true}});
      mermaid.run({nodes:document.querySelectorAll('pre.mermaid')});
    }catch(e){ console.warn('mermaid render failed', e); }
  }
  render();

  var btn=document.getElementById('theme-toggle');
  function apply(){
    var light=document.documentElement.dataset.theme==='light';
    btn.setAttribute('aria-pressed',String(light));
    var lab=btn.querySelector('.tt-label'); if(lab) lab.textContent = light?'暗色':'亮色';
  }
  btn.addEventListener('click',function(){
    var root=document.documentElement;
    root.dataset.theme = root.dataset.theme==='light'?'dark':'light';
    try{ localStorage.setItem('st-theme',root.dataset.theme); }catch(e){}
    apply(); render();
  });
  apply();

  var links=[].slice.call(document.querySelectorAll('#toc a'));
  var heads=links.map(function(a){return document.getElementById(a.getAttribute('href').slice(1));}).filter(Boolean);
  if('IntersectionObserver' in window){
    var io=new IntersectionObserver(function(es){
      es.forEach(function(e){ if(e.isIntersecting){
        links.forEach(function(a){a.classList.toggle('active',a.getAttribute('href')==='#'+e.target.id);});
      }});
    },{rootMargin:'-8% 0px -82% 0px'});
    heads.forEach(function(h){io.observe(h);});
  }
})();
</script>
</body>
</html>"""


def run_pandoc(md_path):
    out = subprocess.run(
        ['pandoc', '-f', 'gfm', '-t', 'html5', '--syntax-highlighting=none', '--wrap=none', md_path],
        capture_output=True, text=True, check=True)
    return out.stdout


REPO_PATH = re.compile(r'(?:href|src)="((?:base|native|frameworks|docs|system|packages)/[^"]*)"')


def fix_repo_links(body):
    return REPO_PATH.sub(lambda m: m.group(0).replace('="' + m.group(1), '="../../../' + m.group(1)), body)


def wrap_tables(body):
    return re.sub(r'<table>', '<div class="table-wrap"><table>', body).replace('</table>', '</table></div>')


def wrap_mermaid(body):
    def repl(m):
        return ('<div class="mermaid-box"><div class="mermaid-hd"><span>mermaid 图</span>'
                '<span>本地渲染</span></div>' + m.group(1) + '</div>')
    return re.sub(r'(<pre class="mermaid">.*?</pre>)', repl, body, flags=re.S)


def strip_code_class(body):
    body = re.sub(r'<div class="sourceCode"[^>]*>', '', body)
    body = body.replace('</div>\n</div>', '</div>')
    body = re.sub(r'<pre class="([^"]*)"',
                  lambda m: '<pre class="mermaid"' if 'mermaid' in m.group(1) else '<pre', body)
    return body


def strip_first_h1(body):
    body = re.sub(r'^\s*<h1[^>]*>.*?</h1>\s*', '', body, count=1, flags=re.S)
    body = re.sub(r'^(?:\s*<p>\s*</p>|<hr\s*/?>|\s)+', '', body, count=1)
    return body


def build_toc(body):
    items = []
    for m in re.finditer(r'<h([23]) id="([^"]+)"[^>]*>(.*?)</h\1>', body, re.S):
        level, hid, text = m.group(1), m.group(2), re.sub(r'<[^>]+>', '', m.group(3))
        cls = 'lv3' if level == '3' else 'lv2'
        items.append('<a class="%s" href="#%s">%s</a>' % (cls, hid, html.escape(text.strip())))
    return '\n'.join(items)


def title_of(md_text):
    m = re.search(r'^#\s+(.+)$', md_text, re.M)
    return m.group(1).strip() if m else 'ANR'


def stats_of(md):
    return {
        'h2': len(re.findall(r'^##\s+', md, re.M)),
        'diagrams': md.count('```mermaid'),
        'tables': len([l for l in md.split('\n') if l.startswith('|')]),
        'chars': len(md),
    }


def stat_chips(st):
    return ''.join(
        '<div class="stat"><b>%s</b><span>%s</span></div>' % (v, k)
        for v, k in ((st['h2'], '小节'), (st['diagrams'], 'mermaid 图'),
                     (st['tables'], '表格行'), ('%dK' % (st['chars'] // 1000), '字数')))


def convert(md_path, prev=None, nxt=None):
    name = os.path.basename(md_path)
    md = open(md_path, encoding='utf-8').read()
    st = stats_of(md)
    body = run_pandoc(md_path)
    body = strip_code_class(body)
    body = strip_first_h1(body)
    body = fix_repo_links(body)
    body = wrap_tables(body)
    body = wrap_mermaid(body)
    toc = build_toc(body)
    title = title_of(md)
    pager = []
    pager.append('<a class="prv" href="%s">← %s</a>' % (prev[1], html.escape(prev[0])) if prev else '<span></span>')
    pager.append('<a class="nxt" href="%s">%s →</a>' % (nxt[1], html.escape(nxt[0])) if nxt else '<span></span>')
    stat_line = '%d 个小节 · %d 张 mermaid 图 · %d 行表格 · %dK 字' % (
        st['h2'], st['diagrams'], st['tables'], st['chars'] // 1000)
    doc = (SHELL.replace('__CSS__', wiki_css())
                .replace('__TITLE__', html.escape(title))
                .replace('__TOC__', toc)
                .replace('__BODY__', body)
                .replace('__SOURCE__', html.escape(name))
                .replace('__STATS__', stat_chips(st))
                .replace('__STAT_LINE__', stat_line)
                .replace('__PAGER__', '\n'.join(pager)))
    os.makedirs(OUT, exist_ok=True)
    out = os.path.join(OUT, os.path.splitext(name)[0] + '.html')
    with open(out, 'w', encoding='utf-8') as f:
        f.write(doc)
    return out


def build_index(files):
    rows = []
    for i, md_path in enumerate(files, 1):
        name = os.path.basename(md_path)
        md = open(md_path, encoding='utf-8').read()
        rows.append((i, title_of(md), name, stats_of(md)))
    cards = []
    for i, title, name, st in rows:
        cards.append('      <a class="anr-card" href="%s.html">'
                     '<div class="hd"><span class="no">%02d</span><h2>%s</h2></div>'
                     '<div class="meta"><span class="pill">%d 小节</span><span class="pill">%d 张图</span>'
                     '<span class="pill">%d 行表格</span><span class="pill">%dK 字</span></div></a>'
                     % (html.escape(os.path.splitext(name)[0]), i, html.escape(title),
                        st['h2'], st['diagrams'], st['tables'], st['chars'] // 1000))
    doc = '''<!DOCTYPE html>
<html lang="zh-CN" data-theme="dark">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta name="color-scheme" content="dark light">
<title>Android ANR · 文档索引</title>
<script>
(function(){try{
  var s=localStorage.getItem('st-theme');
  var sys=window.matchMedia&&window.matchMedia('(prefers-color-scheme: light)').matches?'light':'dark';
  var f=null;try{f=new URLSearchParams(location.search).get('theme');}catch(e){}
  document.documentElement.dataset.theme=(f==='light'||f==='dark')?f:(s||sys);
}catch(e){document.documentElement.dataset.theme='dark';}})();
</script>
<style>__CSS__</style>
</head>
<body>
<div class="bg-grid" aria-hidden="true"></div>
<header class="hdr">
  <div class="hdr-main">
    <div class="kicker">AOSP · android17-release · ANR</div>
    <h1>Android ANR 文档</h1>
    <p class="sub">%d 篇分析，全部转换为 wiki 风格离线 HTML：左侧目录、代码块、mermaid 本地渲染、上下篇导航。</p>
  </div>
  <div class="hdr-right">
    <button class="theme-toggle" id="theme-toggle" type="button" title="切换亮/暗色">
      <svg class="ico-moon" viewBox="0 0 24 24" aria-hidden="true"><path d="M21 12.8A8.5 8.5 0 1 1 11.2 3a7 7 0 0 0 9.8 9.8z"/></svg>
      <svg class="ico-sun" viewBox="0 0 24 24" aria-hidden="true"><circle cx="12" cy="12" r="4.2"/><path d="M12 2v3M12 19v3M2 12h3M19 12h3M4.9 4.9l2.1 2.1M17 17l2.1 2.1M19.1 4.9L17 7M7 17l-2.1 2.1"/></svg>
      <span class="tt-label">亮色</span>
    </button>
  </div>
</header>
<main class="app">
  <aside class="rail">
    <div class="phase">返回 <a href="../dist/index.html">站点索引</a></div>
    <nav class="anr-toc">
      <a href="#list" class="lv2 active">9 篇文档</a>
    </nav>
  </aside>
  <section class="stage">
    <div class="panel">
      <div class="panel-hd"><h2>ANR 文档</h2><span class="hint">docs/ANR/*.md</span></div>
      <div class="anr-doc">
        <div class="anr-cards" id="list">__CARDS__</div>
      </div>
    </div>
  </section>
</main>
<footer class="ftr"><span>docs/wiki_site/anr/</span><span class="dot">·</span><span>由 docs/ANR/tools/md2html.py 生成</span></footer>
<script>
(function(){
  var btn=document.getElementById('theme-toggle');
  function apply(){
    var light=document.documentElement.dataset.theme==='light';
    btnt=btn.querySelector('.tt-label'); if(btnt) btnt.textContent = light?'暗色':'亮色';
  }
  btn.addEventListener('click',function(){
    var root=document.documentElement;
    root.dataset.theme = root.dataset.theme==='light'?'dark':'light';
    try{ localStorage.setItem('st-theme',root.dataset.theme); }catch(e){}
    apply();
  });
  apply();
})();
</script>
</body>
</html>''' % len(files)
    doc = doc.replace('__CSS__', wiki_css()).replace('__CARDS__', '\n'.join(cards))
    out = os.path.join(OUT, 'index.html')
    with open(out, 'w', encoding='utf-8') as f:
        f.write(doc)
    return out


def copy_assets():
    # mermaid 统一放在 docs/wiki_site/assets/，ANR 页面用 ../assets 引用
    if not os.path.exists(os.path.join(ASSETS, 'mermaid.min.js')):
        print('warning: missing', os.path.join(ASSETS, 'mermaid.min.js'))


def main():
    args = sys.argv[1:]
    if args:
        files = [os.path.join(ANR, a) for a in args]
    else:
        found = [f for f in ORDER if os.path.exists(os.path.join(ANR, f))]
        extra = sorted(set(x for x in os.listdir(ANR) if x.endswith('.md')) - set(ORDER))
        files = [os.path.join(ANR, f) for f in found + extra]
    titles = [(title_of(open(p, encoding='utf-8').read()),
               os.path.splitext(os.path.basename(p))[0] + '.html') for p in files]
    for i, p in enumerate(files):
        prev = titles[i - 1] if i > 0 else None
        nxt = titles[i + 1] if i + 1 < len(titles) else None
        out = convert(p, prev, nxt)
        print('converted', os.path.relpath(out, OUT))
    copy_assets()
    print('index ->', os.path.relpath(build_index(files), OUT))
    return 0


if __name__ == '__main__':
    sys.exit(main())
