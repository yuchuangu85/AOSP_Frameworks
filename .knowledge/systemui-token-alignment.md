---
schema_version: 3
id: systemui-token-alignment
title: "SystemUI Token Alignment 工具链"
summary: "SystemUI 令牌对齐脚本的入口、辅助函数职责，以及与源码字节绑定的证据声明。"
module: systemui
source_map:
  - section: "Overview"
    path: "base/packages/SystemUI/scripts/token_alignment/helpers/textFuncs.ts"
    symbol: "groupReplace"
    source_sha256: "b773e76a72c04f97bacfa83bf1ceecee2e1d66946ebce1876e8d1c21816da4e4"
  - section: "Flow"
    path: "base/packages/SystemUI/scripts/token_alignment/index.ts"
    symbol: "notDeclaredSymbol"
    source_sha256: "450f5c8389882b0a6bd49e9b1bb50a0680cd7ccb4ecebc8440fc8d29f606fed6"
  - section: "Limits"
    path: "base/packages/SystemUI/scripts/token_alignment/helpers/FileIO.ts"
    symbol: "readText"
    source_sha256: "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
---
# SystemUI Token Alignment 工具链

## Overview

### Summary

SystemUI 的 token alignment 脚本用于把设计令牌批量替换到资源与代码中，入口是脚本目录下的 index.ts，辅助函数按文本、文件与 XML 三个方向拆分。

### Detail

脚本以 Node.js 运行，输入是令牌清单与待处理路径，输出是替换后的文件内容。
`helpers/textFuncs.ts` 负责纯文本替换，`helpers/FileIO.ts` 负责读写，
`helpers/processXML.ts` 负责 XML 结构内的替换。声明只绑定当前 Revision 的源码
字节，替换结果仍需人工复核后再提交 Git。

## Flow

### Summary

执行顺序是：解析参数、读取令牌定义、遍历目标文件、按类型分派替换、写回并汇总变更。

### Detail

参数解析失败会直接终止，不产生部分写入。遍历阶段按文件类型选择 text/XML 处理
器，任一文件替换失败会记录并在总结中报告。脚本不会自动执行 Git add 或 commit。

## Limits

### Summary

该工具链只覆盖脚本自身的 JS/TS 实现，不证明 SystemUI 运行时的行为。

### Detail

令牌语义、资源合并与最终渲染结果属于 Android 构建与运行时，超出本文档的
证据范围。源码关系声明为 stale 或 unresolved 时不能作为依赖证明。
