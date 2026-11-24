#!/usr/bin/env python3
"""校验 omx_wiki/ 下所有页面的引用：
1) 形如 path/to/File.java:123 的代码引用——文件必须存在、行号必须在文件长度内；
2) [[page-slug]] 交叉引用——目标页必须存在于 omx_wiki/；
3) 相对链接 ../docs/xxx.md——目标必须存在。

用法（仓库根目录）：
    python3 omx_wiki/tools/verify_refs.py
退出码：0 全部通过；1 存在失效引用（逐条打印）。
"""

import glob
import os
import re
import sys

WIKI = 'omx_wiki'
REF = re.compile(r'([\w./\-]+\.(?:java|cpp|h|hpp|cc|rs|aidl|bp|xml|mk|c)):(\d+)')
LINK = re.compile(r'\[\[([^\]]+)\]\]')
DOC_LINK = re.compile(r'\]\((\.\./[^)\s]+)\)')


def line_count(path):
    try:
        with open(path, errors='ignore') as f:
            return sum(1 for _ in f)
    except OSError:
        return -1


def main():
    pages = sorted(glob.glob(os.path.join(WIKI, '*.md')))
    if not pages:
        print('no pages found under %s/' % WIKI)
        return 1
    slugs = {os.path.basename(p)[:-3] for p in pages}
    problems = []
    refs = 0

    for page in pages:
        with open(page) as f:
            text = f.read()

        for m in REF.finditer(text):
            refs += 1
            path, line = m.group(1), int(m.group(2))
            if not os.path.exists(path):
                problems.append('%s: 文件不存在 %s' % (page, m.group(0)))
                continue
            total = line_count(path)
            if line < 1 or line > total:
                problems.append('%s: 行号越界(%s 共 %d 行) %s' % (page, path, total, m.group(0)))

        for m in LINK.finditer(text):
            if m.group(1).strip() not in slugs:
                problems.append('%s: 失效 wiki 链接 [[%s]]' % (page, m.group(1)))

        for m in DOC_LINK.finditer(text):
            target = os.path.normpath(os.path.join(WIKI, m.group(1)))
            if not os.path.exists(target):
                problems.append('%s: 失效文档链接 %s' % (page, m.group(1)))

    print('pages=%d refs=%d problems=%d' % (len(pages), refs, len(problems)))
    for p in problems:
        print(' -', p)
    return 0 if not problems else 1


if __name__ == '__main__':
    sys.exit(main())
