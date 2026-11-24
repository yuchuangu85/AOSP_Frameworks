"""Build an offline reading edition from a pinned local source snapshot."""
from functools import cache
from hashlib import sha256
from html import escape
from pathlib import Path
import json
import os
import re
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
REV = '24bc82b5d25dd6ebc97e8c3401fa1678cd9f3ba5'
KIT = Path(os.environ.get('VISUAL_EXPLAINER_KIT', '~/.codex/skills/visual-explainer/kit')).expanduser()
VIEW = 'base/core/java/android/view/'
SCHED = 'native/services/surfaceflinger/Scheduler/'
GUIDE_REFS = {
    'CLOCKREF': [('native/services/surfaceflinger/SurfaceFlinger.cpp', 2535, 2556),
                 (SCHED+'VSyncDispatchTimerQueue.cpp', 155, 180),
                 (SCHED+'EventThread.cpp', 472, 481), (SCHED+'EventThread.cpp', 595, 603),
                 (SCHED+'EventThread.cpp', 769, 784), (SCHED+'MessageQueue.cpp', 59, 75),
                 (SCHED+'MessageQueue.cpp', 111, 123)],
    'TRACEID': [(SCHED+'FrameTimeline.cpp', 1139, 1149), (SCHED+'FrameTimeline.cpp', 1210, 1227),
                (VIEW+'FrameMetrics.java', 182, 190)],
    'TRACEUI': [(VIEW+'Choreographer.java', 1106, 1114), (VIEW+'Choreographer.java', 1199, 1217),
                (VIEW+'ViewRootImpl.java', 5472, 5487), (VIEW+'ViewRootImpl.java', 5548, 5558)],
    'TRACERT': [('base/libs/hwui/renderthread/DrawFrameTask.cpp', 82, 94), ('native/libs/gui/BLASTBufferQueue.cpp', 705, 720),
                (VIEW+'FrameMetrics.java', 115, 128), (VIEW+'FrameMetrics.java', 286, 315), (VIEW+'FrameMetrics.java', 349, 361)],
    'TRACESF': [(SCHED+'Scheduler.cpp', 470, 475), (SCHED+'FrameTimeline.cpp', 1420, 1437),
                (SCHED+'FrameTimeline.cpp', 2121, 2138)]
}

def data(name):
    return json.loads((HERE / (name + '.json')).read_text())

def git(*args):
    return subprocess.check_output(['git', '-C', str(REPO), *args])

@cache
def lines(path):
    p = (REPO / path).resolve()
    if not any(p.is_relative_to(REPO / x) for x in ['base', 'native']):
        raise ValueError('outside source roots: ' + path)
    raw = p.read_bytes()
    if raw != git('show', f'{REV}:{path}'):
        raise ValueError(f'cited file differs from pinned revision: {path}')
    return raw.decode('utf-8').splitlines()

def chip(i, label):
    return f'<a class="chiplink" href="#{i}">{escape(label)}</a>'

def dl(fields):
    return '<dl>'+''.join(f'<dt>{k}</dt><dd>{escape(v)}</dd>' for k,v in fields)+'</dl>'

def steps(items):
    if not items:
        return ''
    return '<ol class="stage-steps">'+''.join(
        f'<li><b>{escape(item[0])}</b><span>{escape(item[1])}</span></li>'
        for item in items
    )+'</ol>'

def main():
    datasets = [data(n) for n in ('app', 'render', 'sf', 'guide')]
    stages = [s for d in datasets for s in d.get('stages', [])]
    factors = [f for d in datasets for f in d['factors']]
    cases = datasets[-1]['cases']
    ids = [s['id'] for s in stages]
    if len(set(ids)) != len(ids):
        raise ValueError('duplicate stage')
    number = {k:f'{i:02}' for i,k in enumerate(ids, 1)}
    short = dict(zip(ids, ['接收事件','帧回调与动画','窗口遍历','测量与布局','绘制录制','交接 RT','渲染树同步','GPU 绘窗','缓冲入队','BLAST 事务','SF 帧唤醒','事务准入','状态与 latch','可见层与策略','执行合成','present 送显','归还与复用'],strict=True))
    refid, evidence, report = {}, [], []
    def refs(rr):
        if not rr:
            raise ValueError('source references required')
        out=[]
        for path, start, end in rr:
            key=(path,start,end)
            if key not in refid:
                src=lines(path)
                if not 1<=start<=end<=len(src):
                    raise ValueError(key)
                excerpt=src[start-1:end]
                if not any(x.strip() for x in excerpt):
                    raise ValueError(key)
                i=refid[key]=f'E{len(refid)+1:03}'
                digest=sha256(('\n'.join(excerpt)+'\n').encode()).hexdigest()
                report.append([i,path,start,end,digest])
                text='\n'.join(f'{n:>5}  {t}' for n,t in enumerate(excerpt,start))
                evidence.append(f'<details class="refitem" id="{i}"><summary>{i}　{escape(path)}:{start}–{end}</summary><pre>{escape(text)}</pre></details>')
            out.append(chip(refid[key],f'{Path(path).name}:{start}–{end}'))
        return '<div class="refrow"><span class="mini">源码</span>'+''.join(out)+'</div>'
    def related(ss):
        if not ss: raise ValueError('no stage links')
        return '<div class="links"><span class="mini">定位阶段</span>'+''.join(chip(s, number[s]+' '+short[s]) for s in ss)+'</div>'
    parts={name:refs(rr) for name,rr in GUIDE_REFS.items()}
    stage_html=[]
    for s in stages:
        i=s['id']
        if i in ['a1','r1','s1']:
            group={'a1':'应用 UI · 产出绘制指令','r1':'RenderThread / GPU / BLAST · 产出窗口缓冲与事务','s1':'SurfaceFlinger / HWC · 选择图层、合成与呈现'}[i]
            stage_html.append(f'<h3 class="groupname">{group}</h3>')
        ff=[chip(f'f{n}',f['name']) for n,f in enumerate(factors,1) if i in f['stages']]
        if not ff: raise ValueError('missing factors for '+i)
        fields=[('执行者',s['actor']),('输入',s['input']),('实际动作',s['action']),('产物与交接',s['output']),('分支与例外',s['branch']),('视图影响',s['impact'])]
        stage_html.append(f'<details class="stage" id="{i}"'+(' open' if i=='a1' else '')+f'><summary><span class="id">{number[i]}</span>{escape(s["title"])}</summary><div class="stagegrid">{dl(fields)}{steps(s.get("steps"))}<div class="links"><span class="mini">相关因素</span>{"".join(ff)}</div>{refs(s["refs"])}</div></details>')
    fact_html=[]
    for n,f in enumerate(factors,1):
        fields=[('作用机制',f['mechanism']),('不要误判',f['boundary']),('怎么验证',f['probe'])]
        fact_html.append(f'<details class="factor" id="f{n}" data-stages="{" ".join(f["stages"])}"><summary><span class="tag">{escape(f["category"])}</span>{escape(f["name"])}</summary><div class="factorbody">{dl(fields)}{related(f["stages"])}{refs(f["refs"])}</div></details>')
    case_html=[]
    for n,c in enumerate(cases,1):
        items=''.join(f'<article><h4>{escape(title)}</h4><p>{escape(desc)}</p>{related(ss.split())}</article>' for title,desc,ss in c['items'])
        case_html.append(f'<details class="case" id="case{n}"'+(' open' if n==1 else '')+f'><summary>{escape(c["title"])}</summary><p class="note">{escape(c["note"])}</p><div class="caseitems">{items}</div></details>')
    parts.update(STAGES=''.join(stage_html),CASES=''.join(case_html),FACTORS=''.join(fact_html),
                 NREF=str(len(refid)),NFILE=str(lines.cache_info().currsize),REV=REV[:9],COMMIT=REV,
                 OPTIONS=''.join(f'<option value="{i}">{number[i]} · {short[i]}</option>' for i in ids),
                 EVIDENCE=''.join(evidence))
    src=re.sub(r'@@([A-Z]+)@@',lambda m:parts[m[1]],(HERE/'shell.html').read_text())
    with tempfile.TemporaryDirectory() as tmp:
        t=Path(tmp)
        (t/'index.src.html').write_text(src)
        subprocess.run(['node',str(KIT/'render.mjs'),str(t/'index.src.html')],check=True)
        page=(t/'index.html').read_text()
        # The shared visual kit suggests external web fonts. This edition uses only local fonts.
        page=re.sub(r'<link\b[^>]*href="https://fonts\.(?:googleapis|gstatic)\.com[^"]*"[^>]*>','',page)
        page=page.replace("style-src 'unsafe-inline' https://fonts.googleapis.com;", "style-src 'unsafe-inline';").replace("font-src https://fonts.gstatic.com;", "font-src 'none';")
        (HERE/'index.html').write_text(page)
        if os.environ.get('ATLAS_SHEET') and (t/'index.sheet.png').exists():
            Path(os.environ['ATLAS_SHEET']).write_bytes((t/'index.sheet.png').read_bytes())
    (HERE/'source-verification.json').write_text(json.dumps({
        'scope':'Exact cited ranges embedded from working tree; cited file bytes equal the pinned git blobs. Not semantic verification or device evidence.',
        'revision':REV,'files':lines.cache_info().currsize,'ranges':len(report),'columns':['id','path','first','last','excerpt_sha256_lf'],'records':report
    },ensure_ascii=False,indent=1)+'\n')
    print(f'Built {len(stages)} stages, {len(factors)} factors, {len(cases)} cases; {len(report)} distinct verified ranges in {lines.cache_info().currsize} files.')

if __name__=='__main__': main()
