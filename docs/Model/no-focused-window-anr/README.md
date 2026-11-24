# No Focus ANR

打开 `index.html` 即可运行，不需要服务、网络、npm 安装或构建工具。

## 内容与边界

- 基于当前仓库 AOSP 源码，使用与 `docs/Model` 现有页面相近的顶部栏、明暗主题、动画控制和折叠源码。
- 七种对照场景、统一播放位置、可调焦点查找与窗口就绪时刻，以及可选 pre-ANR 预警。
- 单 display、单焦点事件、默认基础 timeout 5000 ms、HW multiplier 为 1 的教学模型。调度与 policy 延迟均为示意。
- 输入事件已通过前置 policy 过滤；获得焦点后的窗口选择为未暂停、未丢弃输入的分支。ACK 路径只作概念对比，不模拟完整协议。
- 相同时刻窗口状态先更新、再检查超时，是本模型的确定性规则，不是平台并发顺序保证。
- 路径上的移动标记为绑定播放位置的短暂视觉过渡，不代表实际传输耗时；等待期间事件不会在路径上循环发送。
- 焦点切换场景排除触摸触发的 `mNextUnblockedEvent` 裁剪：A 的计时取消，原 pending 按键重新查找并向 B 分发。清除计时不等于丢弃输入。
- 当前源码字段为 `mNoFocusedWindowAnrState`，不沿用旧文档中的过时字段和行号。
- 用户提供的微信链接在 2026-10-28 返回“环境异常”验证页；备用提取超时。未获得正文，页面没有声称整合了文章的具体观点。

## 维护

只编辑 `src/` 和 `data/sources.json`，然后重新生成最终 HTML：

```sh
node docs/Model/no-focused-window-anr/build.mjs
node --test docs/Model/no-focused-window-anr/tests/model.test.cjs
```

构建无外部依赖，并验证全部摘录及记录源码文件 hash。它不修改 `base/` 或 `native/`。

独立摘录验证：

```sh
python3 -B ~/.codex/skills/source-pipeline-explorer/scripts/verify_sources.py \
  docs/Model/no-focused-window-anr/data/sources.json --source-root . \
  --report docs/Model/no-focused-window-anr/data/source-verification.json
```

浏览器检查使用已有的 Playwright 安装；不添加页面或项目依赖：

```sh
PLAYWRIGHT_MODULE=/path/to/playwright-core \
CHROME_PATH='/Applications/Google Chrome.app/Contents/MacOS/Google Chrome' \
node docs/Model/no-focused-window-anr/tests/browser.cjs
```

`data/source-verification.json` 仅证明摘录匹配，`data/browser-verification.json` 记录本地浏览器交互；两者都不等于设备、厂商系统或实际弹窗验证。

工具图标取自 Lucide（ISC license），图形内嵌以支持离线打开。页面不使用外部字体、图片或脚本；状态舞台是本主题专用的 HTML/CSS/SVG 机制图。
