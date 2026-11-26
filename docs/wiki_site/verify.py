#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Shell Transition 页面的独立校验（不依赖 build.py 的中间状态）。

检查项：
  A. topic.json 里每个 step 的 file:line 真实存在；
  B. 每个 P0 函数的 start/end 落在文件内、签名在声明区、注释 quote 与源码逐行一致；
  C. 产出的 HTML 自包含（0 处外链资源）、无未替换的模板占位符、体积在预算内；
  D. 统计并打印覆盖率表。

用法：python3 wiki_site/verify.py   退出码 0=全绿
"""

import glob
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
TOPIC_SLUG = 'shell-transition'
for _i, _a in enumerate(sys.argv):
    if _a == '--topic' and _i + 1 < len(sys.argv):
        TOPIC_SLUG = sys.argv[_i + 1]
TOPIC = os.path.join(HERE, 'topics', TOPIC_SLUG)
OUT = os.path.join(HERE, 'dist', TOPIC_SLUG + '.html')

PUNCT = re.compile(r'^[)}\]};,]+$')


def nontrivial(line):
    s = line.strip()
    return bool(s) and not PUNCT.match(s)


def lines_of(path):
    with open(path, encoding='utf-8', errors='ignore') as f:
        return f.read().split('\n')


def main():
    problems = []
    topic = json.load(open(os.path.join(TOPIC, 'topic.json'), encoding='utf-8'))
    # 与 build.py 一致：支持 extends 拆分布局（meta 按字段覆盖）
    _ext = topic.get('extends')
    for _ov in ([_ext] if isinstance(_ext, str) else (_ext or [])):
        _extra = json.load(open(os.path.join(TOPIC, _ov), encoding='utf-8'))
        for _k, _v in _extra.items():
            if _k == 'meta' and 'meta' in topic:
                topic['meta'].update(_v)
            else:
                topic[_k] = _v
    topic.pop('extends', None)
    vcfg = topic.get('verify', {})
    min_steps = int(vcfg.get('minSteps', 30))
    min_methods = int(vcfg.get('minMethods', 150))
    min_cards = int(vcfg.get('minCards', 4))
    min_pitfalls = int(vcfg.get('minPitfalls', 8))

    # A. steps
    step_refs = 0
    for s in topic['steps']:
        for r in s.get('refs', []):
            step_refs += 1
            p = os.path.join(REPO, r['file'])
            if not os.path.exists(p):
                problems.append('step %s: 缺文件 %s' % (s['id'], r['file']))
                continue
            n = len(lines_of(p))
            if not (1 <= r['line'] <= n):
                problems.append('step %s: 行号越界 %s:%d（共 %d）' % (s['id'], r['file'], r['line'], n))

    # B. p0 + annotations
    cov_rows = []
    for item in topic['p0']:
        path = os.path.join(REPO, item['file'])
        src = lines_of(path)
        start, end = item['start'], item['end']
        if not (1 <= start < end <= len(src)):
            problems.append('%s: span 非法 %d..%d（文件 %d 行）' % (item['id'], start, end, len(src)))
            continue
        if not any(item['sig'] in l for l in src[start - 1:min(start + 8, end)]):
            problems.append('%s: 声明区找不到签名 %r' % (item['id'], item['sig']))
        body = src[start - 1:end]
        ann_path = os.path.join(TOPIC, 'annotations', item['id'] + '.json')
        if not os.path.exists(ann_path):
            problems.append('%s: 缺注释文件' % item['id'])
            continue
        ann = json.load(open(ann_path, encoding='utf-8'))
        bad_quote = 0
        for n in ann['notes']:
            at = int(n['at'])
            q = (n.get('quote') or '').strip()
            if 1 <= at <= len(body) and q and not body[at - 1].strip().startswith(q[:40]):
                bad_quote += 1
        if bad_quote:
            problems.append('%s: %d 条 quote 与 at 行不一致（build 会按 quote 吸附提醒）' % (item['id'], bad_quote))
        ntr = sum(1 for l in body if nontrivial(l))
        cov = min(1.0, len(ann['notes']) / ntr) if ntr else 1.0
        if cov < 0.9:
            problems.append('%s: 覆盖率 %.1f%% < 90%%' % (item['id'], cov * 100))
        cov_rows.append((item['id'], len(ann['notes']), ntr, cov))

    # B2. deep articles
    import glob as _glob
    deep_files = sorted(_glob.glob(os.path.join(TOPIC, 'deep', '*.json')))
    deep = {}
    for f in deep_files:
        try:
            d = json.load(open(f, encoding='utf-8'))
        except Exception as e:
            problems.append('deep %s 不是合法 JSON: %s' % (os.path.basename(f), e))
            continue
        deep[d.get('step')] = d
    step_ids = {s['id'] for s in topic['steps']}
    if len(deep) < len(step_ids):
        problems.append('精读文章只有 %d / %d 步（缺 %s）'
                        % (len(deep), len(step_ids), ','.join(sorted(step_ids - set(deep)))))
    req = ('tldr', 'why', 'concepts', 'walk', 'state', 'threads', 'pitfalls', 'debug', 'next')
    for sid, d in deep.items():
        for k in req:
            if k not in d:
                problems.append('deep %s 缺字段 %s' % (sid, k))
        if len((d.get('walk') or [])) < 3:
            problems.append('deep %s: walk < 3 条' % sid)
        if len((d.get('state') or [])) < 1:
            problems.append('deep %s: state 为空' % sid)
        if len((d.get('pitfalls') or [])) < 2:
            problems.append('deep %s: pitfalls < 2 条' % sid)
        if len((d.get('debug') or [])) < 1:
            problems.append('deep %s: debug 为空' % sid)
        if len(d.get('tldr', '')) > 90:
            problems.append('deep %s: tldr 过长（%d 字）' % (sid, len(d['tldr'])))
        if len(d.get('why', '')) < 50:
            problems.append('deep %s: why 过短（%d 字）' % (sid, len(d.get('why', ''))))
        for w in d.get('walk') or []:
            ref = (w.get('ref') or '').strip()
            if ':' not in ref:
                problems.append('deep %s: walk 缺 ref' % sid)
                continue
            path, _, ln = ref.rpartition(':')
            if path.startswith('`'):
                path = path.strip('`')
            fp = os.path.join(REPO, path)
            if not os.path.exists(fp):
                problems.append('deep %s: ref 文件不存在 %s' % (sid, ref))
                continue
            try:
                n = len(lines_of(fp))
            except Exception:
                continue
            if not (1 <= int(ln) <= n):
                problems.append('deep %s: ref 行号越界 %s' % (sid, ref))

    # C. html
    if not os.path.exists(OUT):
        problems.append('缺产物 %s（先跑 build.py）' % os.path.relpath(OUT, REPO))
    else:
        html_text = open(OUT, encoding='utf-8').read()
        size_kb = os.path.getsize(OUT) / 1024
        if size_kb > 2048:
            problems.append('产物 %.0f KB 超过 2 MB 预算' % size_kb)
        ext = re.findall(r'(?:src|href)="(https?:)?//', html_text)
        if ext:
            problems.append('产物存在 %d 处外链资源' % len(ext))
        for ph in ('{{TITLE}}', '{{SUBTITLE}}', '{{PAYLOAD}}', '/*{{CSS}}*/', '//{{JS}}'):
            if ph in html_text:
                problems.append('模板占位符未替换：%s' % ph)
        if html_text.count('<svg') < 4:
            problems.append('产物内 SVG 少于 4 张（架构/时序/类/流程）')
        payload_m = re.search(r'<script id="payload"[^>]*>(.*?)</script>', html_text, re.S)
        if not payload_m:
            problems.append('产物内找不到 payload JSON')
        else:
            pl = json.loads(payload_m.group(1))
            if len(pl.get('steps', [])) < min_steps:
                problems.append('步骤少于 %d（完整性要求）：%d' % (min_steps, len(pl.get('steps', []))))
            if len(pl.get('methods', [])) < min_methods:
                problems.append('方法清单少于 %d 条：%d' % (min_methods, len(pl.get('methods', []))))
            ft = pl.get('fileTree') or {}
            min_files = int(vcfg.get('minFiles', 10))
            if not ft.get('children') or (pl.get('fileStats') or {}).get('files', 0) < min_files:
                problems.append('文件树缺失或文件数过少：%s' % (pl.get('fileStats') or {}))
            if len(pl.get('cards', [])) < min_cards:
                problems.append('函数级卡片少于 8 张：%d' % len(pl.get('cards', [])))
            if len(pl.get('pitfalls', [])) < min_pitfalls:
                problems.append('降级条目少于 10 条：%d' % len(pl.get('pitfalls', [])))

    print('== Shell Transition 校验 ==')
    print('steps=%d  refs=%d  p0=%d  deep=%d' % (len(topic['steps']), step_refs, len(topic['p0']), len(deep)))
    print('%-22s %8s %10s %8s' % ('function', 'notes', 'nontrivial', 'cov'))
    for fid, n, ntr, cov in cov_rows:
        print('%-22s %8d %10d %7.1f%%' % (fid, n, ntr, cov * 100))
    if os.path.exists(OUT):
        print('artifact: %s  (%.0f KB)' % (os.path.relpath(OUT, REPO), os.path.getsize(OUT) / 1024))

    if problems:
        print('\n%d 个问题：' % len(problems))
        for p in problems:
            print(' -', p)
        return 1
    print('\nproblems: 0')
    return 0


if __name__ == '__main__':
    sys.exit(main())
