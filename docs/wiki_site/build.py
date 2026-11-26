#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Shell Transition 单文件 HTML 构建器。

输入：
  topics/shell-transition/topic.json          主题数据（步骤/时序/类/架构/流程）
  topics/shell-transition/annotations/*.json  逐行注释（带 quote 校验）

输出：
  dist/shell-transition.html                  自包含、离线、零外部依赖

构建期校验（任一失败只记录，不静默篡改）：
  - 每个 P0 函数必须能在源码中用签名唯一定位，且括号闭合可求；
  - 每条注释的 quote 必须等于对应源码行（允许 ±2 行内的唯一吸附）；
  - 每个 step 的 file:line 必须存在。
"""

import glob
import html
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.abspath(__file__))          # .../docs/wiki_site


def _find_repo(start):
    cur = start
    for _ in range(8):
        if os.path.isdir(os.path.join(cur, 'base', 'core', 'java', 'android', 'view')):
            return cur
        cur = os.path.dirname(cur)
    return os.path.dirname(start)


REPO = _find_repo(ROOT)                                     # 仓库根
TOPIC_SLUG = 'shell-transition'
for _i, _a in enumerate(sys.argv):
    if _a == '--topic' and _i + 1 < len(sys.argv):
        TOPIC_SLUG = sys.argv[_i + 1]
TOPIC = os.path.join(ROOT, 'topics', TOPIC_SLUG)
SRC = os.path.join(ROOT, 'src')
DIST = os.path.join(ROOT, 'dist')

PUNCT_ONLY = re.compile(r'^[)}\]};,]+$')


def load_json(path):
    with open(path, 'r', encoding='utf-8') as f:
        return json.load(f)


def read_lines(path):
    with open(path, 'r', encoding='utf-8', errors='ignore') as f:
        return f.read().split('\n')


def nontrivial(text):
    s = text.strip()
    if not s:
        return False
    if PUNCT_ONLY.match(s):
        return False
    if s.startswith('//') and len(s) < 4:
        return False
    return True


def find_function(lines, sig, hint_line=None):
    """用签名定位函数声明行，再用花括号深度求函数体结束行。返回 (start, end) 1-based。"""
    hits = [i for i, l in enumerate(lines) if sig in l]
    if not hits:
        raise SystemExit('[fatal] 找不到函数签名: %r' % sig)
    if len(hits) > 1 and hint_line:
        hits.sort(key=lambda i: abs(i + 1 - hint_line))
    i = hits[0]
    depth = 0
    started = False
    for j in range(i, min(len(lines), i + 12000)):
        depth += lines[j].count('{') - lines[j].count('}')
        if '{' in lines[j]:
            started = True
        if started and depth <= 0:
            return i + 1, j + 1
    raise SystemExit('[fatal] 括号未闭合: %r 从第 %d 行起' % (sig, i + 1))


def load_annotations(p0, problems):
    """加载并校验某 P0 函数的注释；返回 {abs_line: note} 与统计。"""
    path = os.path.join(TOPIC, 'annotations', p0['id'] + '.json')
    if not os.path.exists(path):
        problems.append('%s: 缺少注释文件 %s' % (p0['id'], os.path.relpath(path, ROOT)))
        return {}, {'annotated': 0, 'nontrivial': 0, 'snapped': 0, 'missing': True}

    data = load_json(path)
    lines = read_lines(os.path.join(REPO, p0['file']))
    start, end = p0['start'], p0['end']
    body = lines[start - 1:end]

    notes = {}
    snapped = 0
    lo = max(1, start - 80)
    hi = min(len(lines), end + 80)
    for n in data.get('notes', []):
        at = int(n.get('at', 0))
        quote = (n.get('quote') or '').strip()
        if not quote:
            continue
        # quote-first：以源码原文为准，at 只用来在多个命中里就近选择。
        key = quote[:50]
        cands = [k for k in range(lo, hi + 1) if lines[k - 1].strip().startswith(key)]
        if not cands:
            problems.append('%s: quote 在 %d..%d 内找不到：%r'
                            % (p0['id'], lo, hi, quote[:60]))
            continue
        center = start + max(at, 1) - 1
        abs_line = min(cands, key=lambda k: abs(k - center))
        if abs(abs_line - center) > 1:
            snapped += 1
            problems.append('%s: at=%d 吸附到第 %d 行（偏差 %d）'
                            % (p0['id'], at, abs_line, abs_line - center))
        if abs_line < start or abs_line > end:
            problems.append('%s: quote 命中函数外第 %d 行（函数 %d..%d）'
                            % (p0['id'], abs_line, start, end))
            continue
        notes[abs_line] = {
            'kind': n.get('kind', 'note'),
            'text': n.get('text', ''),
            'quote': lines[abs_line - 1].strip(),
        }

    total_nontrivial = sum(1 for l in body if nontrivial(l))
    cov = min(1.0, len(notes) / total_nontrivial) if total_nontrivial else 1.0
    if cov < 0.9:
        problems.append('%s: 覆盖率 %.1f%% 低于 90%%' % (p0['id'], cov * 100))
    return notes, {'annotated': len(notes), 'nontrivial': total_nontrivial,
                   'snapped': snapped, 'missing': False, 'coverage': round(cov * 100, 1)}


def build_p0(topic, problems):
    out = []
    for item in topic['p0']:
        path = os.path.join(REPO, item['file'])
        lines = read_lines(path)
        start, end = item['start'], item['end']
        if not any(item['sig'] in l for l in lines[start - 1:min(start + 8, end)]):
            problems.append('%s: 第 %d 行起 8 行内找不到签名 %r' % (item['id'], start, item['sig']))
        p0 = dict(item)
        p0['start'], p0['end'] = start, end
        p0['code'] = [l.rstrip() for l in lines[start - 1:end]]
        notes, stats = load_annotations(p0, problems)
        p0['notes'] = {str(k): v for k, v in notes.items()}
        p0['stats'] = stats
        out.append(p0)
    return out



# ---------------------------------------------------------------- inventory

DECL = re.compile(
    r'^\s{4}(?:@[\w.]+(?:\([^)]*\))?\s+)*'
    r'(?:(?:public|private|protected|static|final|abstract|synchronized|native|default|strictfp)\s+)*'
    r'[\w<>\[\],.?@ ]+?\s(\w+)\s*\([^;=]*\)\s*(?:throws [\w, .]+)?\{?\s*$')
SKIP_NAMES = {'if', 'for', 'while', 'switch', 'catch', 'return', 'new', 'do', 'else', 'try', 'synchronized', 'static'}


def extract_methods(path):
    lines = read_lines(path)
    if path.endswith(('.cpp', '.cc', '.h', '.hpp')):
        return extract_methods_cpp(lines)
    out = []
    for i, line in enumerate(lines):
        m = DECL.match(line)
        if not m:
            continue
        name = m.group(1)
        if name in SKIP_NAMES:
            continue
        depth, started, end = 0, False, i
        for j in range(i, min(len(lines), i + 4000)):
            depth += lines[j].count('{') - lines[j].count('}')
            if '{' in lines[j]:
                started = True
            if started and depth <= 0:
                end = j
                break
        if end - i < 2:
            continue
        out.append({'name': name, 'start': i + 1, 'end': end + 1, 'lines': end - i + 1,
                    'title': line.strip()[:120]})
    return out


# C++ 成员函数定义：返回类型在前、Class::method( 在后，可能跨多行；
# 缩进从 0 到 4 空格都接受（部分 SF 文件把成员函数定义顶格写）。
CPP_DECL = re.compile(
    r'^\s{0,4}(?:[\w:<>,*&\[\]]+\s+)+'                       # 返回类型
    r'(?P<name>~?\w+)::(?P<fn>~?\w+)\s*\('                   # Class::method(
    r'[^;{}]*\)\s*(?:const\s*)?(?:noexcept\s*)?'
    r'(?:REQUIRES|EXCLUDES|NO_THREAD_SAFETY_ANALYSIS)?[^;{]*\{?\s*$')


def extract_methods_cpp(lines):
    """C++ 成员函数：按 Class::method( 的缩进声明定位，再用花括号深度求函数体。"""
    out = []
    for i, line in enumerate(lines):
        m = CPP_DECL.match(line)
        if not m:
            continue
        name = m.group('fn')
        if name in SKIP_NAMES:
            continue
        depth, started, end = 0, False, i
        for j in range(i, min(len(lines), i + 4000)):
            depth += lines[j].count('{') - lines[j].count('}')
            if '{' in lines[j]:
                started = True
            if started and depth <= 0:
                end = j
                break
        if not started or end - i < 2:
            continue
        out.append({'name': name, 'start': i + 1, 'end': end + 1, 'lines': end - i + 1,
                    'title': line.strip()[:120]})
    return out


def build_inventory(topic, p0_list):
    spans = [(p['file'], p['start'], p['end'], p['id']) for p in p0_list]
    keep = set(topic.get('inventorySpans') or {})
    methods = []
    for f in topic.get('scope', []):
        for m in extract_methods(os.path.join(REPO, f)):
            status, owner = 'uncovered', None
            for pf, ps, pe, pid in spans:
                if pf != f:
                    continue
                if ps <= m['start'] and m['end'] <= pe:
                    status, owner = 'covered', pid
                    break
                if m['start'] <= pe and ps <= m['end']:
                    status, owner = 'partial', pid
            methods.append(dict(m, file=f, fileBase=os.path.basename(f), status=status, owner=owner))
    if keep:
        # inventorySpans 指定「只列这些方法名」时，超大文件也能保持清单可读
        kept = [m for m in methods if m['name'] in keep or m['status'] != 'uncovered']
        if kept:
            methods = kept
    methods.sort(key=lambda x: (-x['lines'], x['fileBase'], x['start']))
    covered = [m for m in methods if m['status'] == 'covered']
    return methods, {
        'methods': len(methods),
        'methodLoc': sum(m['lines'] for m in methods),
        'coveredMethods': len(covered),
        'coveredMethodLoc': sum(m['lines'] for m in covered),
    }



# ---------------------------------------------------------------- file tree

DIR_TOTAL_CACHE = {}


def dir_total(path):
    if path in DIR_TOTAL_CACHE:
        return DIR_TOTAL_CACHE[path]
    base = os.path.join(REPO, path)
    n = 0
    if os.path.isdir(base):
        for _, _, files in os.walk(base):
            n += sum(1 for f in files if f.endswith(('.java', '.cpp', '.h', '.kt', '.aidl', '.rs')))
    DIR_TOTAL_CACHE[path] = n
    return n


def build_file_tree(topic, p0_list, methods, deep):
    """把 steps / deep walk / p0 / scope 里出现的文件聚成一棵树，并附带统计。"""
    scope = set(topic.get('scope', []))
    info = {}

    def touch(path):
        if path not in info:
            fp = os.path.join(REPO, path)
            info[path] = {
                'path': path, 'lines': len(read_lines(fp)) if os.path.exists(fp) else 0,
                'steps': set(), 'p0': [], 'deep': set(), 'inScope': path in scope,
                'methods': 0, 'methodLoc': 0,
            }
        return info[path]

    for s in topic['steps']:
        for r in s.get('refs', []):
            touch(r['file'])['steps'].add(s['num'])
    for p0 in p0_list:
        touch(p0['file'])['p0'].append(p0['symbol'])
    for sid, d in deep.items():
        for w in d.get('walk', []):
            ref = (w.get('ref') or '').rsplit(':', 1)[0].strip('`').strip()
            if ref and os.path.exists(os.path.join(REPO, ref)):
                touch(ref)['deep'].add(sid)
    for f in scope:
        if os.path.exists(os.path.join(REPO, f)):
            touch(f)
    for m in methods:
        if m['file'] in info:
            info[m['file']]['methods'] += 1
            info[m['file']]['methodLoc'] += m['lines']

    root = {'name': '', 'type': 'dir', 'children': {}, 'path': ''}
    for path, meta in info.items():
        parts = path.split('/')
        node = root
        for part in parts[:-1]:
            node = node['children'].setdefault(part, {'name': part, 'type': 'dir', 'children': {}})
        node['children'][parts[-1]] = dict(meta, name=parts[-1], type='file')

    def finalize(node):
        if node['type'] == 'file':
            node['steps'] = sorted(node['steps'])
            node['deep'] = sorted(node['deep'])
            node['p0Count'] = len(node['p0'])
            node['hit'] = len(node['steps']) + len(node['deep']) + node['p0Count']
            return node
        # 目录在前、文件在后；再按名字排序
        items = sorted(node['children'].items(), key=lambda kv: (kv[1]['type'] != 'dir', kv[0]))
        children = [finalize(c) for _, c in items]
        node['children'] = children
        node['files'] = sum(c.get('files', 1) for c in children)
        node['lines'] = sum(c.get('lines', 0) for c in children)
        node['methods'] = sum(c.get('methods', 0) for c in children)
        node['p0Count'] = sum(c.get('p0Count', 0) for c in children)
        node['inScope'] = any(c.get('inScope') for c in children)
        # 单链目录合并：a/b/c 只有一条路径时拼成一行，避免 6 层缩进
        while len(node['children']) == 1 and node['children'][0]['type'] == 'dir':
            only = node['children'][0]
            merged = dict(only)
            merged['name'] = (node['name'] + '/' + only['name']) if node['name'] else only['name']
            node = merged
        node.setdefault('path', node.get('name', ''))
        node['dirTotal'] = dir_total(node['path'])
        return node

    tree = finalize(root)
    return tree, {
        'files': len(info),
        'lines': sum(v['lines'] for v in info.values()),
        'methods': sum(v['methods'] for v in info.values()),
        'inScope': sum(1 for v in info.values() if v['inScope']),
        'p0Files': sum(1 for v in info.values() if v['p0']),
    }


# ---------------------------------------------------------------- SVG helpers

KIND_COLOR = {
    'lock': '#ff6b6b', 'thread': '#4cd4ff', 'ipc': '#c792ea', 'state': '#ffb454',
    'branch': '#ff9f43', 'data': '#7bd88f', 'trace': '#9aa5b1', 'note': '#8b98a5',
}
EDGE_STYLE = {'call': ('#8b98a5', '5 4'), 'binder': ('#ffb454', '1 0'),
              'oneway': ('#c792ea', '6 5'), 'callback': ('#33d6c0', '2 4')}
SIDE_COLOR = {'WMS': '#ffb454', 'SHELL': '#33d6c0', 'AIDL': '#9aa5b1',
              'UI': '#4c8dff', 'RT': '#33d6c0'}


def esc(t):
    return html.escape(str(t), quote=True)


def wrap(text, width):
    """按显示宽度折行：CJK 逐字断，拉丁词按空格优先断，避免把单词劈开。"""
    tokens = re.findall(r'[\u2e80-\u9fff]|[A-Za-z0-9_@./:+\-]+|\s+|.', str(text))
    out, cur, w = [], '', 0
    for t in tokens:
        tw = sum(2 if ord(ch) > 0x2E80 else 1 for ch in t)
        if t.isspace():
            if cur:
                cur += ' '
                w += 1
            continue
        if w + tw > width and cur:
            out.append(cur.rstrip())
            cur, w = '', 0
            if tw > width:          # 超长单词硬断
                while tw > width and t:
                    out.append(t[:width])
                    t = t[width:]
                    tw = len(t)
        cur += t
        w += tw
    if cur:
        out.append(cur.rstrip())
    return out or ['']


def text_block(x, y, lines, size=12, cls='nb', anchor='start', lh=16):
    parts = []
    for i, l in enumerate(lines):
        parts.append('<tspan x="%d" dy="%d">%s</tspan>' % (x, 0 if i == 0 else lh, esc(l)))
    return ('<text class="%s" font-size="%d" text-anchor="%s" x="%d" y="%d">%s</text>'
            % (cls, size, anchor, x, y, ''.join(parts)))


def svg_arch(arch):
    """正交布线 + 通道错位 + 标签衬板。route=outer-right/outer-left 可强制绕行。"""
    W, H = arch['width'], arch['height']
    groups = {g['id']: g for g in arch['groups']}
    parts = ['<svg viewBox="0 0 %d %d" class="diagram" role="img" aria-label="架构图">' % (W, H)]
    parts.append('<defs><marker id="ah" markerWidth="9" markerHeight="9" refX="8" refY="4.5" orient="auto">'
                 '<path d="M0,0 L9,4.5 L0,9 z" fill="#8b98a5"/></marker></defs>')
    for g in arch['groups']:
        parts.append('<rect class="grp grp-%s" x="%d" y="%d" width="%d" height="%d" rx="16" stroke="%s"/>'
                     % (g['id'], g['x'], g['y'], g['w'], g['h'], g['color']))
        parts.append('<text class="grp-label grp-%s" x="%d" y="%d" fill="%s">%s</text>'
                     % (g['id'], g['x'] + 16, g['y'] + 24, g['color'], esc(g['label'])))
    boxes = {b['id']: b for b in arch['boxes']}
    bgroup = {b['id']: b['group'] for b in arch['boxes']}
    for b in arch['boxes']:
        parts.append('<g class="box"><rect x="%d" y="%d" width="%d" height="%d" rx="9"/>'
                     % (b['x'], b['y'], b['w'], b['h']))
        parts.append('<text class="bx" x="%d" y="%d">%s</text>'
                     % (b['x'] + 12, b['y'] + 22, esc(b['label'])))
        parts.append('<text class="bs" x="%d" y="%d">%s</text>'
                     % (b['x'] + 12, b['y'] + 40, esc(b['sub'])))
        parts.append('</g>')

    port_count = {}
    gap_count = {}

    def port(box_id, side):
        k = (box_id, side)
        n = port_count.get(k, 0)
        port_count[k] = n + 1
        return (n % 3 - 1) * 14

    def gap_channel(ga, gb):
        if ga['x'] < gb['x']:
            lo, hi = ga['x'] + ga['w'], gb['x']
        else:
            lo, hi = gb['x'] + gb['w'], ga['x']
        k = (lo, hi)
        n = gap_count.get(k, 0)
        gap_count[k] = n + 1
        return min(hi - 12, lo + 14 + n * 14)

    for e in arch['edges']:
        a, b = boxes[e['from']], boxes[e['to']]
        ga, gb = groups[bgroup[e['from']]], groups[bgroup[e['to']]]
        color, dash = EDGE_STYLE.get(e.get('kind', 'call'), ('#8b98a5', '5 4'))
        mode = e.get('route', 'auto')
        acx, acy = a['x'] + a['w'] / 2, a['y'] + a['h'] / 2
        bcx, bcy = b['x'] + b['w'] / 2, b['y'] + b['h'] / 2
        oa, ob = port(e['from'], 'r'), port(e['to'], 'l')

        if mode == 'below-right':
            xc = gb['x'] + gb['w'] - 12
            y1 = a['y'] + a['h'] + 16
            d = 'M%d,%d V%d H%d V%d H%d' % (acx, a['y'] + a['h'], y1, xc, bcy, b['x'] + b['w'])
            lx, ly = (acx + xc) / 2, y1 - 8
        elif mode == 'outer-right':
            xc = gb['x'] + gb['w'] - 10
            y1, y2 = acy + oa, bcy + ob
            d = 'M%d,%d H%d V%d H%d' % (a['x'] + a['w'], y1, xc, y2, b['x'] + b['w'])
            lx, ly = (a['x'] + a['w'] + xc) / 2, y1 - 9
        elif mode == 'outer-left':
            xc = ga['x'] + 10
            y1, y2 = acy + oa, bcy + ob
            d = 'M%d,%d H%d V%d H%d' % (a['x'], y1, xc, y2, b['x'])
            lx, ly = (a['x'] + xc) / 2, y1 - 9
        elif bgroup[e['from']] == bgroup[e['to']]:
            if abs(acx - bcx) < 40:
                d = 'M%d,%d V%d' % (acx + oa, a['y'] + a['h'], b['y'])
                lx, ly = acx + oa + 8, (a['y'] + a['h'] + b['y']) / 2
            else:
                my = (a['y'] + a['h'] + b['y']) / 2 if b['y'] > a['y'] else (b['y'] + b['h'] + a['y']) / 2
                if b['y'] > a['y']:
                    d = 'M%d,%d V%.0f H%d V%d' % (acx + oa, a['y'] + a['h'], my, bcx + ob, b['y'])
                else:
                    d = 'M%d,%d V%.0f H%d V%d' % (acx + oa, a['y'], my, bcx + ob, b['y'] + b['h'])
                lx, ly = (acx + bcx) / 2, my - 6
        else:
            xc = gap_channel(ga, gb)
            if ga['x'] < gb['x']:
                y1, y2 = acy + oa, bcy + ob
                d = 'M%d,%d H%.0f V%d H%d' % (a['x'] + a['w'], y1, xc, y2, b['x'])
            else:
                y1, y2 = acy + oa, bcy + ob
                d = 'M%d,%d H%.0f V%d H%d' % (a['x'], y1, xc, y2, b['x'] + b['w'])
            lx, ly = xc, y1 - 9

        parts.append('<path class="edge edge-%s" d="%s" stroke="%s" stroke-dasharray="%s" marker-end="url(#ah)"/>'
                     % (e.get('kind', 'call'), d, color, dash))
        label = e.get('label', '')
        if label:
            parts.append('<text class="el halo" x="%.0f" y="%.0f" text-anchor="middle">%s</text>'
                         % (lx, ly, esc(label)))
    parts.append('</svg>')
    return '\n'.join(parts)


def svg_seq(topic, steps_by_id):
    """时序图：泳道 + 消息 + 步骤号；消息标签带衬板，避免被泳道虚线穿透。"""
    lanes = topic['lanes']
    msgs = topic['sequence']
    lw, left, top, gap = 280, 120, 96, 58
    W = left + lw * len(lanes) + 40
    H = top + gap * len(msgs) + 70
    lane_x = {l['id']: left + i * lw + lw / 2 for i, l in enumerate(lanes)}
    parts = ['<svg viewBox="0 0 %d %d" class="diagram" role="img" aria-label="时序图">' % (W, H)]
    parts.append('<defs><marker id="sa" markerWidth="9" markerHeight="9" refX="8" refY="4.5" '
                 'orient="auto"><path d="M0,0 L9,4.5 L0,9 z" fill="#8b98a5"/></marker>'
                 '<marker id="sa2" markerWidth="9" markerHeight="9" refX="8" refY="4.5" '
                 'orient="auto"><path d="M0,0 L9,4.5 L0,9 z" fill="#c792ea"/></marker></defs>')
    for l in lanes:
        x = lane_x[l['id']]
        parts.append('<rect class="lane-hd lane-%s" x="%.0f" y="16" width="%d" height="42" rx="10" '
                     'stroke="%s"/>' % (l['id'], x - lw / 2 + 14, lw - 28, l['color']))
        parts.append('<text class="lane-t lane-%s" x="%.0f" y="43" text-anchor="middle" fill="%s">%s</text>'
                     % (l['id'], x, l['color'], esc(l['label'])))
        parts.append('<line class="lane-vl" x1="%.0f" y1="62" x2="%.0f" y2="%d" stroke="%s"/>'
                     % (x, x, H - 26, l['color']))
    for i, m in enumerate(msgs):
        y = top + i * gap
        x1, x2 = lane_x[m['from']], lane_x[m['to']]
        color, dash = EDGE_STYLE.get(m.get('kind', 'call'), ('#8b98a5', ''))
        step = steps_by_id.get(m['step'], {})
        prev = steps_by_id.get(msgs[i - 1]['step'], {}) if i else {}
        num = '' if prev.get('num') == step.get('num') else str(step.get('num', ''))
        parts.append('<g class="msg msg-%s" data-step="%s">' % (m.get('kind', 'call'), m['step']))
        if i % 2 == 0:
            parts.append('<rect class="band" x="8" y="%.0f" width="%d" height="%d"/>' % (y - 24, W - 16, gap))
        if num:
            parts.append('<text class="mn" x="20" y="%.0f">%s</text>' % (y + 4, esc(num)))
        marker = 'url(#sa2)' if m.get('kind') == 'oneway' else 'url(#sa)'
        if abs(x2 - x1) < 2:
            parts.append('<path class="self" d="M%.0f,%.0f h46 v22 h-46" fill="none" stroke="%s" '
                         'stroke-dasharray="%s" marker-end="%s"/>' % (x1, y - 11, color, dash, marker))
            label, lx = m['label'], x1 + 56
            anchor = 'start'
        else:
            parts.append('<line x1="%.0f" y1="%.0f" x2="%.0f" y2="%.0f" stroke="%s" '
                         'stroke-dasharray="%s" marker-end="%s"/>' % (x1, y, x2, y, color, dash, marker))
            label, lx, anchor = m['label'], (x1 + x2) / 2, 'middle'
        parts.append('<text class="ml halo" x="%.0f" y="%.0f" text-anchor="%s">%s</text>'
                     % (lx, y - 8, anchor, esc(label)))
        parts.append('<circle class="dot" cx="%.0f" cy="%.0f" r="2.6" fill="%s"/>'
                     % (min(x1, x2), y, color))
        parts.append('</g>')
    parts.append('</svg>')
    return '\n'.join(parts)


def svg_class(topic):
    """类图：分层列布局；职责两行、成员最多 6 条；关系不写长标签，避免压字。"""
    classes = topic['classes']
    tiers = sorted({c['tier'] for c in classes})
    colw, gapx = 330, 56
    W = 48 + len(tiers) * (colw + gapx)
    nodes = {}
    maxy = 0
    for ti, tier in enumerate(tiers):
        y = 96
        for c in [x for x in classes if x['tier'] == tier]:
            x = 48 + ti * (colw + gapx)
            role_lines = wrap(c['role'], 30)[:2]
            members = c.get('members', [])[:6]
            h = 54 + 15 * len(role_lines) + 17 * len(members) + 10
            nodes[c['name']] = dict(c, x=x, y=y, w=colw, h=h, role_lines=role_lines)
            y += h + 30
        maxy = max(maxy, y)
    H = maxy + 30
    parts = ['<svg viewBox="0 0 %d %d" class="diagram" role="img" aria-label="类图">' % (W, H)]
    parts.append('<defs><marker id="ca" markerWidth="10" markerHeight="10" refX="9" refY="5" '
                 'orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="#8b98a5"/></marker></defs>')
    labels = ['入口层', '核心层', '载体层', '底层原语']
    for ti, tier in enumerate(tiers):
        parts.append('<text class="tier" x="%d" y="62">%s</text>'
                     % (48 + ti * (colw + gapx), esc(labels[min(ti, 3)])))
    for c in nodes.values():
        color = SIDE_COLOR.get(c['side'], '#8b98a5')
        parts.append('<g class="cls side-%s"><rect x="%d" y="%d" width="%d" height="%d" rx="10" stroke="%s"/>'
                     % (c['side'], c['x'], c['y'], c['w'], c['h'], color))
        parts.append('<text class="cn side-%s" x="%d" y="%d" fill="%s">%s</text>'
                     % (c['side'], c['x'] + 16, c['y'] + 24, color, esc(c['name'])))
        yy = c['y'] + 42
        for line in c['role_lines']:
            parts.append('<text class="cs" x="%d" y="%d">%s</text>' % (c['x'] + 16, yy, esc(line)))
            yy += 15
        yy += 6
        for m in c.get('members', [])[:6]:
            parts.append('<text class="cm" x="%d" y="%d">+ %s()</text>' % (c['x'] + 16, yy, esc(m)))
            yy += 17
        parts.append('</g>')
    for rel in topic.get('relations', []):
        a, b = nodes.get(rel['from']), nodes.get(rel['to'])
        if not a or not b:
            continue
        if b['x'] > a['x'] + a['w']:
            x1, y1 = a['x'] + a['w'], a['y'] + 34
            x2, y2 = b['x'], b['y'] + 34
        elif b['x'] + b['w'] < a['x']:
            x1, y1 = a['x'], a['y'] + 34
            x2, y2 = b['x'] + b['w'], b['y'] + 34
        else:
            x1, y1 = a['x'] + a['w'] / 2, a['y'] + (a['h'] if a['y'] < b['y'] else 0)
            x2, y2 = b['x'] + b['w'] / 2, b['y'] + (0 if a['y'] < b['y'] else b['h'])
        mx = (x1 + x2) / 2
        parts.append('<path class="rel" d="M%.0f,%.0f C%.0f,%.0f %.0f,%.0f %.0f,%.0f" '
                     'marker-end="url(#ca)" stroke-dasharray="%s"/>'
                     % (x1, y1, mx, y1, mx, y2, x2, y2, '4 4' if rel.get('kind') == 'uses' else ''))
    parts.append('</svg>')
    return '\n'.join(parts)


def svg_flow(flow):
    nodes = {n['id']: n for n in flow['nodes']}
    W = 1060
    H = max(n['y'] for n in flow['nodes']) + 90
    parts = ['<svg viewBox="0 0 %d %d" class="diagram" role="img" aria-label="流程图">' % (W, H)]
    parts.append('<defs><marker id="fa" markerWidth="9" markerHeight="9" refX="8" refY="4.5" '
                 'orient="auto"><path d="M0,0 L9,4.5 L0,9 z" fill="#8b98a5"/></marker></defs>')
    for e in flow['edges']:
        a, b = nodes[e['from']], nodes[e['to']]
        x1, y1 = a['x'], a['y'] + (26 if a['kind'] != 'decision' else 42)
        x2, y2 = b['x'], b['y'] - (26 if b['kind'] != 'decision' else 42)
        if abs(x1 - x2) < 8:
            d = 'M%d,%d V%d' % (x1, y1, y2)
        else:
            my = (y1 + y2) / 2
            d = 'M%d,%d V%.0f H%d V%d' % (x1, y1, my, x2, y2)
        parts.append('<path class="fedge" d="%s" marker-end="url(#fa)"/>' % d)
        if e.get('label'):
            lx = (x1 + x2) / 2 if abs(x1 - x2) > 8 else x1 + 46
            ly = (y1 + y2) / 2 - 6 if abs(x1 - x2) > 8 else (y1 + y2) / 2
            parts.append('<text class="fl halo" x="%d" y="%d" text-anchor="middle">%s</text>'
                         % (lx, ly, esc(e['label'])))
    for n in flow['nodes']:
        kind, x, y = n['kind'], n['x'], n['y']
        if kind == 'decision':
            parts.append('<polygon class="fd" points="%d,%d %d,%d %d,%d %d,%d"/>'
                         % (x, y - 42, x + 150, y, x, y + 42, x - 150, y))
            lines = wrap(n['label'], 22)[:2]
            yy = y - (len(lines) - 1) * 8
            for line in lines:
                parts.append('<text class="fn" x="%d" y="%d" text-anchor="middle">%s</text>'
                             % (x, yy + 4, esc(line)))
                yy += 17
        else:
            w = 330 if kind == 'process' else 200
            rx = 26 if kind in ('start', 'end') else 12
            parts.append('<rect class="f%s" x="%d" y="%d" width="%d" height="54" rx="%d"/>'
                         % ('term' if kind in ('start', 'end') else 'proc', x - w // 2, y - 27, w, rx))
            lines = wrap(n['label'], 30)[:2]
            yy = y - (len(lines) - 1) * 8
            for line in lines:
                parts.append('<text class="fn" x="%d" y="%d" text-anchor="middle">%s</text>'
                             % (x, yy + 4, esc(line)))
                yy += 17
    parts.append('</svg>')
    return '\n'.join(parts)



# ---------------------------------------------------------------- detailed diagrams

def _tspans(x, y, lines, size, cls, lh):
    out = []
    for i, l in enumerate(lines):
        out.append('<tspan x="%d" dy="%d">%s</tspan>' % (x, 0 if i == 0 else lh, esc(l)))
    return ('<text class="%s" font-size="%d" x="%d" y="%d">%s</text>'
            % (cls, size, x, y, ''.join(out)))


def svg_arch_grid(g):
    """分层网格架构图：行=阶段（可多 slot 纵向排列），列=线程/进程；边上标注数据形态。"""
    rows, cols = g['rows'], g['cols']
    col_w = (g['width'] - g.get('left', 150) - 24) / len(cols)
    # 每个 row 的 slot 数决定行高
    slot_count = {}
    for n in g['nodes']:
        slot_count[n['row']] = max(slot_count.get(n['row'], 1), int(n.get('slot', 0)) + 1)
    row_y, row_h = {}, {}
    y = 96
    for r in rows:
        n = max(1, slot_count.get(r['id'], 1))
        row_h[r['id']] = 34 + n * 94 + 10
        row_y[r['id']] = y
        y += row_h[r['id']]
    H = y + 70
    W = g['width']
    left = g.get('left', 150)
    parts = ['<svg viewBox="0 0 %d %d" class="diagram" role="img" aria-label="详细架构图">' % (W, H)]
    parts.append('<defs>'
                 '<marker id="d1" markerWidth="9" markerHeight="9" refX="8" refY="4.5" orient="auto">'
                 '<path d="M0,0 L9,4.5 L0,9 z" fill="#8b98a5"/></marker>'
                 '<marker id="d2" markerWidth="9" markerHeight="9" refX="8" refY="4.5" orient="auto">'
                 '<path d="M0,0 L9,4.5 L0,9 z" fill="#c792ea"/></marker></defs>')
    for r in rows:
        yy = row_y[r['id']]
        parts.append('<rect class="rowband" x="%d" y="%d" width="%d" height="%d" rx="10"/>'
                     % (left - 128, yy + 6, W - left - 10, row_h[r['id']] - 12))
        parts.append('<text class="rn" x="18" y="%d">%s</text>' % (yy + 34, esc(r['label'])))
        for j, line in enumerate(wrap(r.get('note', ''), 15)[:3]):
            parts.append('<text class="rnote" x="18" y="%d">%s</text>' % (yy + 54 + j * 14, esc(line)))
    for j, c in enumerate(cols):
        x = left + j * col_w
        parts.append('<rect class="colhd" x="%.0f" y="52" width="%.0f" height="34" rx="8" stroke="%s"/>'
                     % (x + 10, col_w - 20, c['color']))
        parts.append('<text class="colt" x="%.0f" y="75" text-anchor="middle" fill="%s">%s</text>'
                     % (x + col_w / 2, c['color'], esc(c['label'])))
        parts.append('<line class="colseam" x1="%.0f" y1="92" x2="%.0f" y2="%d"/>' % (x, x, H - 40))
    pos = {}
    for n in g['nodes']:
        ci = next(i for i, c in enumerate(cols) if c['id'] == n['col'])
        x = left + ci * col_w + 14
        yy = row_y[n['row']] + 22 + int(n.get('slot', 0)) * 94
        w, h = col_w - 28, 82
        pos[n['id']] = (x, yy, w, h)
        color = cols[ci]['color']
        parts.append('<g class="anode"><rect x="%.0f" y="%.0f" width="%.0f" height="%.0f" rx="9" stroke="%s"/>'
                     % (x, yy, w, h, color))
        parts.append('<text class="anl" x="%.0f" y="%.0f">%s</text>' % (x + 12, yy + 20, esc(n['label'])))
        if n.get('sub'):
            parts.append('<text class="ans" x="%.0f" y="%.0f">%s</text>' % (x + 12, yy + 37, esc(n['sub'])))
        if n.get('badge'):
            parts.append('<text class="anb" x="%.0f" y="%.0f">%s</text>' % (x + 12, yy + 62, esc(n['badge'])))
        if n.get('thread'):
            parts.append('<text class="ant" x="%.0f" y="%.0f">%s</text>'
                         % (x + w - 10, yy + 20, esc(n['thread'])))
        parts.append('</g>')
    for e in g['edges']:
        if e['from'] not in pos or e['to'] not in pos:
            continue
        ax, ay, aw, ah = pos[e['from']]
        bx, by, bw, bh = pos[e['to']]
        color, dash = EDGE_STYLE.get(e.get('kind', 'call'), ('#8b98a5', '5 4'))
        marker = 'url(#d2)' if e.get('kind') == 'oneway' else 'url(#d1)'
        if bx > ax + aw - 4 and abs((ay + ah / 2) - (by + bh / 2)) < 14:
            d = 'M%.0f,%.0f H%.0f' % (ax + aw, ay + ah / 2, bx)
            lx, ly = (ax + aw + bx) / 2, ay + ah / 2 - 8
        elif by > ay + ah:
            x1, x2 = ax + aw / 2, bx + bw / 2
            my = (ay + ah + by) / 2
            d = 'M%.0f,%.0f V%.0f H%.0f V%.0f' % (x1, ay + ah, my, x2, by) if abs(x1 - x2) > 6 else \
                'M%.0f,%.0f V%.0f' % (x1, ay + ah, by)
            lx, ly = (x1 + x2) / 2, my - 4
        else:
            x1, x2 = ax + aw, bx
            my = (ay + ah / 2 + by + bh / 2) / 2
            d = 'M%.0f,%.0f H%.0f V%.0f H%.0f' % (x1, ay + ah / 2, (x1 + x2) / 2, by + bh / 2, x2)
            lx, ly = (x1 + x2) / 2, by + bh / 2 - 8
        parts.append('<path class="aedge" d="%s" stroke="%s" stroke-dasharray="%s" marker-end="%s"/>'
                     % (d, color, dash, marker))
        if e.get('label'):
            parts.append('<text class="ael halo" x="%.0f" y="%.0f" text-anchor="middle">%s</text>'
                         % (lx, ly, esc(e['label'])))
        if e.get('data'):
            parts.append('<text class="aed halo" x="%.0f" y="%.0f" text-anchor="middle">%s</text>'
                         % (lx, ly + 13, esc(e['data'])))
    ly = H - 26
    parts.append('<g class="legend">')
    lx = left - 128
    for txt, color, dash in (('同步调用', '#8b98a5', '5 4'), ('oneway / Binder', '#c792ea', '6 5'),
                             ('fence 等待', '#33d6c0', '2 4')):
        parts.append('<line x1="%d" y1="%d" x2="%d" y2="%d" stroke="%s" stroke-dasharray="%s"/>'
                     % (lx, ly, lx + 28, ly, color, dash))
        parts.append('<text class="lg" x="%d" y="%d">%s</text>' % (lx + 34, ly + 4, esc(txt)))
        lx += 160
    parts.append('</g>')
    parts.append('</svg>')
    return '\n'.join(parts)


def svg_seq_detail(d, steps_by_id=None):
    """分阶段时序图：泳道 + 阶段带 + 数据载荷 + 阻塞标注。"""
    lanes = d['lanes']
    phases = d.get('phases', [])
    msgs = d['messages']
    lw, left, top0, gap = 250, 150, 96, 52
    W = left + lw * len(lanes) + 40
    H = top0 + gap * len(msgs) + len(phases) * 34 + 90
    lane_x = {l['id']: left + i * lw + lw / 2 for i, l in enumerate(lanes)}
    parts = ['<svg viewBox="0 0 %d %d" class="diagram" role="img" aria-label="详细时序图">' % (W, H)]
    parts.append('<defs><marker id="s1" markerWidth="9" markerHeight="9" refX="8" refY="4.5" orient="auto">'
                 '<path d="M0,0 L9,4.5 L0,9 z" fill="#8b98a5"/></marker>'
                 '<marker id="s2" markerWidth="9" markerHeight="9" refX="8" refY="4.5" orient="auto">'
                 '<path d="M0,0 L9,4.5 L0,9 z" fill="#c792ea"/></marker></defs>')
    palette = ['#4c8dff', '#33d6c0', '#ffb454', '#c792ea', '#ff9f43', '#7bd88f']
    for i, l in enumerate(lanes):
        color = l.get('color', palette[i % len(palette)])
        x = lane_x[l['id']]
        parts.append('<rect class="lane-hd" x="%.0f" y="16" width="%d" height="40" rx="10" stroke="%s"/>'
                     % (x - lw / 2 + 12, lw - 24, color))
        parts.append('<text class="lane-t" x="%.0f" y="42" text-anchor="middle" fill="%s">%s</text>'
                     % (x, color, esc(l['label'])))
        parts.append('<line class="lane-vl" x1="%.0f" y1="58" x2="%.0f" y2="%d" stroke="%s"/>'
                     % (x, x, H - 30, color))
    y = top0
    cur_phase = None
    for i, m in enumerate(msgs):
        if m.get('phase') and m['phase'] != cur_phase:
            cur_phase = m['phase']
            ph = next((p for p in phases if p['id'] == cur_phase), {'label': cur_phase})
            parts.append('<rect class="phasebar" x="16" y="%d" width="%d" height="24" rx="5"/>'
                         % (y - 30, W - 32))
            parts.append('<text class="phasebar-t" x="26" y="%d">%s</text>'
                         % (y - 13, esc(ph['label'])))
        x1, x2 = lane_x[m['from']], lane_x[m['to']]
        color, dash = EDGE_STYLE.get(m.get('kind', 'call'), ('#8b98a5', ''))
        marker = 'url(#s2)' if m.get('kind') == 'oneway' else 'url(#s1)'
        if steps_by_id and m.get('step'):
            st = steps_by_id.get(m['step'], {})
            prev = steps_by_id.get(msgs[i - 1]['step'], {}) if i else {}
            if st.get('num') and prev.get('num') != st.get('num'):
                parts.append('<text class="mn" x="16" y="%.0f">%d</text>' % (y + 4, st['num']))
        parts.append('<g class="msg msg-%s" data-step="%s">' % (m.get('kind', 'call'), m.get('step', '')))
        if abs(x2 - x1) < 2:
            parts.append('<path d="M%.0f,%.0f h44 v20 h-44" fill="none" stroke="%s" stroke-dasharray="%s" marker-end="%s"/>'
                         % (x1, y - 10, color, dash, marker))
            # 右侧最后一列没有富余空间：把自消息标签放到环的左侧，避免越出 viewBox
            if x1 + 54 + 240 > W - 20:
                lx, anchor = x1 - 14, 'end'
            else:
                lx, anchor = x1 + 54, 'start'
        else:
            parts.append('<line x1="%.0f" y1="%.0f" x2="%.0f" y2="%.0f" stroke="%s" stroke-dasharray="%s" marker-end="%s"/>'
                         % (x1, y, x2, y, color, dash, marker))
            lx, anchor = (x1 + x2) / 2, 'middle'
        parts.append('<text class="ml halo" x="%.0f" y="%.0f" text-anchor="%s">%s</text>'
                     % (lx, y - 8, anchor, esc(m['label'])))
        if m.get('data'):
            parts.append('<text class="md halo" x="%.0f" y="%.0f" text-anchor="%s">%s</text>'
                         % (lx, y + 14, anchor, esc(m['data'])))
        if m.get('block'):
            bx = lane_x[m['blockLane']] if m.get('blockLane') else x1
            parts.append('<rect class="blockmark" x="%.0f" y="%.0f" width="150" height="24" rx="6"/>'
                         % (bx - 75, y + 22))
            parts.append('<text class="blockmark-t" x="%.0f" y="%.0f">%s</text>'
                         % (bx, y + 38, esc(m['block'])))
        parts.append('</g>')
        y += gap
    parts.append('</svg>')
    return '\n'.join(parts)


def svg_flow_regions(f):
    """分区流程图：每个分区内是独立判定链，分区之间用出/入边连接。"""
    W, H = f.get('width', 1500), f.get('height', 900)
    nodes = {n['id']: n for n in f['nodes']}
    parts = ['<svg viewBox="0 0 %d %d" class="diagram" role="img" aria-label="详细流程图">' % (W, H)]
    parts.append('<defs><marker id="f1" markerWidth="9" markerHeight="9" refX="8" refY="4.5" orient="auto">'
                 '<path d="M0,0 L9,4.5 L0,9 z" fill="#8b98a5"/></marker></defs>')
    for r in f.get('regions', []):
        parts.append('<rect class="region" x="%d" y="%d" width="%d" height="%d" rx="14"/>'
                     % (r['x'], r['y'], r['w'], r['h']))
        parts.append('<text class="region-t" x="%d" y="%d">%s</text>'
                     % (r['x'] + 16, r['y'] + 26, esc(r['label'])))
    for e in f['edges']:
        a, b = nodes[e['from']], nodes[e['to']]
        ax, ay = a['x'], a['y']
        bx, by = b['x'], b['y']
        if e.get('route') in ('left', 'right'):
            reg = next((r for r in f.get('regions', []) if r['id'] == a.get('region')), None)
            if reg:
                cx = reg['x'] + 18 if e['route'] == 'left' else reg['x'] + reg['w'] - 18
                sx = ax - 125 if e['route'] == 'left' else ax + 125
            else:
                cx = ax - 200 if e['route'] == 'left' else ax + 200
                sx = ax - 125 if e['route'] == 'left' else ax + 125
            tx = bx - 125 if e['route'] == 'left' else bx + 125
            d = 'M%d,%d H%d V%d H%d' % (sx, ay, cx, by, tx)
            lx, ly = cx, (ay + by) / 2
        elif e.get('route') == 'h':
            x1 = ax + 90
            x2 = bx - 90
            d = 'M%d,%d H%d V%d H%d' % (x1, ay, (x1 + x2) / 2, by, x2)
            lx, ly = (x1 + x2) / 2, by - 8
        else:
            y1 = ay + 26
            y2 = by - 26
            d = 'M%d,%d V%d' % (ax, y1, y2) if abs(ax - bx) < 6 else \
                'M%d,%d V%.0f H%d V%d' % (ax, y1, (y1 + y2) / 2, bx, y2)
            lx, ly = (ax + bx) / 2, (y1 + y2) / 2 - 6
        parts.append('<path class="fedge" d="%s" marker-end="url(#f1)"/>' % d)
        if e.get('label'):
            parts.append('<text class="fl halo" x="%.0f" y="%.0f" text-anchor="middle">%s</text>'
                         % (lx, ly, esc(e['label'])))
    for n in f['nodes']:
        kind, x, y = n['kind'], n['x'], n['y']
        if kind == 'decision':
            parts.append('<polygon class="fd" points="%d,%d %d,%d %d,%d %d,%d"/>'
                         % (x, y - 40, x + 140, y, x, y + 40, x - 140, y))
        elif kind in ('start', 'end'):
            parts.append('<rect class="fterm" x="%d" y="%d" width="240" height="48" rx="24"/>'
                         % (x - 120, y - 24))
        else:
            parts.append('<rect class="fproc" x="%d" y="%d" width="250" height="52" rx="10"/>'
                         % (x - 125, y - 26))
        for i, line in enumerate(wrap(n['label'], 26)[:2]):
            parts.append('<text class="fn" x="%d" y="%d" text-anchor="middle">%s</text>'
                         % (x, y + 4 + (i - 0.5) * 16, esc(line)))
        if n.get('note'):
            parts.append('<text class="fnote" x="%d" y="%d" text-anchor="middle">%s</text>'
                         % (x, y + (34 if kind == 'decision' else 44), esc(n['note'])))
    parts.append('</svg>')
    return '\n'.join(parts)


# ---------------------------------------------------------------- assemble

def main():
    topic = load_json(os.path.join(TOPIC, 'topic.json'))
    # 可选覆盖：同一主题的大块数据可以拆到独立 JSON（例如 topic_sf_commit.json），
    # 只需把顶层键写进配置文件的 "extends" 列表即可合并，避免 topic.json 膨胀到难以维护。
    ext = topic.get('extends', [])
    if isinstance(ext, str):
        ext = [ext]
    topic.pop('extends', None)
    for ov in ext:
        extra = load_json(os.path.join(TOPIC, ov))
        for k, v in extra.items():
            if k == 'meta' and 'meta' in topic:
                topic['meta'].update(v)   # 元信息按字段覆盖，保持 slug 与产物名一致
            else:
                topic[k] = v
    problems = []

    # 允许主题只给出自己用到的键；缺省值保证老主题与裁剪主题都能构建
    topic.setdefault('meta', {'slug': TOPIC_SLUG, 'title': TOPIC_SLUG, 'subtitle': '',
                              'version': '', 'built': '', 'git_ref': '', 'repo': ''})
    topic.setdefault('classes', [])
    topic.setdefault('relations', [])
    topic.setdefault('architecture', {'width': 100, 'height': 100, 'groups': [], 'boxes': [], 'edges': []})
    topic.setdefault('flow', {'nodes': [], 'edges': []})
    topic.setdefault('sequence', [])
    topic.setdefault('pitfalls', [])

    topic['steps'].sort(key=lambda s: s['num'])

    deep = {}
    for f in sorted(glob.glob(os.path.join(TOPIC, 'deep', '*.json'))):
        d = load_json(f)
        sid = d.get('step')
        if not sid:
            problems.append('deep: %s 缺少 step 字段' % os.path.basename(f))
            continue
        deep[sid] = d
    for s in topic['steps']:
        s['deep'] = deep.get(s['id'])
    order = {s['id']: s['num'] for s in topic['steps']}
    topic['sequence'].sort(key=lambda m: order.get(m['step'], 999))
    steps_by_id = {s['id']: s for s in topic['steps']}

    # 步骤引用校验
    for s in topic['steps']:
        for r in s.get('refs', []):
            p = os.path.join(REPO, r['file'])
            if not os.path.exists(p):
                problems.append('step %s: 文件不存在 %s' % (s['id'], r['file']))
                continue
            total = len(read_lines(p))
            if r['line'] > total:
                problems.append('step %s: 行号越界 %s:%d（共 %d 行）'
                                % (s['id'], r['file'], r['line'], total))

    p0_list = build_p0(topic, problems)
    methods, inv_stats = build_inventory(topic, p0_list)
    file_tree, file_stats = build_file_tree(topic, p0_list, methods, deep)
    # 逐行口径以 P0 函数区间为准（清单里的 method 边界是粗算）
    inv_stats['coveredMethods'] = len(p0_list)
    inv_stats['coveredMethodLoc'] = sum(p['end'] - p['start'] + 1 for p in p0_list)

    p0_by_step = {s.get('p0'): s['id'] for s in topic['steps'] if s.get('p0')}

    payload = {
        'meta': topic['meta'],
        'lanes': topic['lanes'],
        'phases': topic['phases'],
        'steps': topic['steps'],
        'p0': p0_list,
        'p0ByStep': p0_by_step,
        'classes': topic['classes'],
        'methods': methods,
        'cards': topic.get('cards', []),
        'glossary': topic.get('glossary', []),
        'scenario': topic.get('scenario', {}),
        'learningPath': topic.get('learningPath', []),
        'fileTree': file_tree,
        'detail': {'arch': bool(topic.get('archGrid')), 'seq': bool(topic.get('seqDetail')),
                   'flow': bool(topic.get('flowDetail'))},
        'fileStats': file_stats,
        'branchTables': topic.get('branchTables', []),
        'pitfalls': topic['pitfalls'],
        'diagrams': {
            'arch': svg_arch_grid(topic['archGrid']) if topic.get('archGrid') else svg_arch(topic['architecture']),
            'seq': svg_seq_detail(topic['seqDetail'], steps_by_id) if topic.get('seqDetail') else svg_seq(topic, steps_by_id),
            'cls': svg_class(topic),
            'flow': svg_flow_regions(topic['flowDetail']) if topic.get('flowDetail') else svg_flow(topic['flow']),
        },
        'stats': {
            'steps': len(topic['steps']),
            'p0': len(p0_list),
            'annotated': sum(p['stats']['annotated'] for p in p0_list),
            'loc': sum(p['end'] - p['start'] + 1 for p in p0_list),
            'coverage': round(sum(p['stats'].get('coverage', 0) for p in p0_list) / max(len(p0_list), 1), 1),
            'files': len({p['file'] for p in p0_list} | {r['file'] for s in topic['steps'] for r in s.get('refs', [])}),
            'deep': len(deep),
            'deepChars': sum(len(json.dumps(d, ensure_ascii=False)) for d in deep.values()),
            'relations': len(topic.get('relations', [])),
            'methods': inv_stats['methods'],
            'methodLoc': inv_stats['methodLoc'],
            'coveredMethods': inv_stats['coveredMethods'],
            'coveredMethodLoc': inv_stats['coveredMethodLoc'],
        },
    }

    tpl = open(os.path.join(SRC, 'template.html'), encoding='utf-8').read()
    css = open(os.path.join(SRC, 'app.css'), encoding='utf-8').read()
    js = open(os.path.join(SRC, 'app.js'), encoding='utf-8').read()

    doc = (tpl
           .replace('/*{{CSS}}*/', css)
           .replace('//{{JS}}', js)
           .replace('"{{PAYLOAD}}"', json.dumps(payload, ensure_ascii=False))
           .replace('{{TITLE}}', esc(topic['meta']['title']))
           .replace('{{SUBTITLE}}', esc(topic['meta']['subtitle']))
           .replace('{{KICKER}}', esc(topic['meta'].get('kicker', 'AOSP · ' + topic['meta']['version']))))

    os.makedirs(DIST, exist_ok=True)
    out = os.path.join(DIST, TOPIC_SLUG + '.html')
    with open(out, 'w', encoding='utf-8') as f:
        f.write(doc)

    print('built %s  (%.0f KB)' % (os.path.relpath(out, ROOT), os.path.getsize(out) / 1024))
    print('stats: steps=%d p0=%d annotated=%d loc=%d avg_cov=%.1f%%'
          % (payload['stats']['steps'], payload['stats']['p0'], payload['stats']['annotated'],
             payload['stats']['loc'], payload['stats']['coverage']))
    print('files: %d (%.0f lines), in-scope=%d, p0-files=%d'
          % (file_stats['files'], file_stats['lines'], file_stats['inScope'], file_stats['p0Files']))
    print('inventory: methods=%d (%.0f lines), line-covered=%d (%.0f lines, %.1f%% of method bodies)'
          % (inv_stats['methods'], inv_stats['methodLoc'], inv_stats['coveredMethods'],
             inv_stats['coveredMethodLoc'],
             100.0 * inv_stats['coveredMethodLoc'] / max(inv_stats['methodLoc'], 1)))
    try:
        if ROOT not in sys.path:
            sys.path.insert(0, ROOT)
        import make_index
        make_index.main()
    except Exception as e:
        print('index build skipped:', e)

    missing = [p['id'] for p in p0_list if p['stats'].get('missing')]
    if missing:
        print('missing annotations: %s' % ', '.join(missing))
    if problems:
        print('\n%d 个问题:' % len(problems))
        for p in problems[:40]:
            print(' -', p)
        return 1
    print('problems: 0')
    return 0


if __name__ == '__main__':
    sys.exit(main())
