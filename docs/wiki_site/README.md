# wiki_site · 源码级 HTML 解剖页

把一条系统链路做成**单文件、离线、可交互**的 HTML：左侧步骤时间轴，中间逐行注释的真实源码，右侧架构/时序/类图/流程图四张图，三处联动。

主题（每个主题一套独立数据与产物）：

| 主题 | 产物 | 规模 |
| --- | --- | --- |
| `binder-ipc` | `dist/binder-ipc.html` | 24 步 / 12 函数逐行（811 条，行覆盖 100%）/ 24 篇精读 / 11 文件 |
| `vsync` | `dist/vsync.html` | 24 步 / 12 函数逐行（406 条，行覆盖 100%）/ 24 篇精读 / 18 文件 |
| `input-dispatch` | `dist/input-dispatch.html` | 24 步 / 11 函数逐行（1248 条，行覆盖 100%）/ 24 篇精读 / 20 文件 |
| `wms` | `dist/wms.html` | 24 步 / 12 函数逐行（864 条，行覆盖 100%）/ 24 篇精读 / 14 文件 |
| `transaction` | `dist/transaction.html` | 24 步 / 12 函数逐行（615 条，行覆盖 100%）/ 24 篇精读 / 8 文件 |
| `view-traversal` | `dist/view-traversal.html` | 24 步 / 10 函数逐行（544 条，行覆盖 100%）/ 24 篇精读 / 48 方法清单 / 13 文件 |
| `render-thread-to-sf` | `dist/render-thread-to-sf.html` | 36 步 / 10 函数逐行（561 条）/ 36 篇精读 / 234 方法清单 / 36 文件 |
| `sf-compose-to-display` | `dist/sf-compose-to-display.html` | 24 步 / 10 函数逐行（1043 条，行覆盖 100%）/ 24 篇精读 / 9 文件 |
| `shell-transition` | `dist/shell-transition.html` | 42 步 / 13 函数逐行 / 732 条注释 / 42 篇精读 |

> 上表按**一帧的真实发生顺序**排列（导航页 `dist/index.html` 同序）：输入事件 → View 请求刷新 → 交棒 RenderThread → RT 回放并交给 SF → SF 合成上屏；`shell-transition` 是跨窗口的一层；`binder-ipc` 是贯穿所有链路的进程间通道，两者放在最后。

ANR 文档：`anr/`（由 `docs/ANR/tools/md2html.py` 从 `docs/ANR/*.md` 生成，9 篇 + index，含本地 mermaid）。

导航页：`dist/index.html` —— 由 `make_index.py` 扫描 `dist/*.html` 的 payload 自动生成（9 个主题卡 + 15 个 `docs/Model` 模型页），每次构建结束会自动刷新；也可单独跑 `python3 docs/wiki_site/make_index.py`。构建时用 `--topic <slug>` 指定主题。

当前主题细节：**Shell Transition（android17-release）**
产物：`dist/shell-transition.html`（约 440 KB，双击即开，零外部依赖）

覆盖口径（重要）：

| 维度 | 现状 |
| --- | --- |
| 流程完整性 | **42 步**覆盖全部路径：注册 player、触发、采集、SyncSet/超时、请求播放、合并、sync/sleep 闸门、handler 分派、动画落地、finish 顺序、cleanup、abort、旋转/睡眠、PiP/legacy、诊断 |
| 方法清单 | **496 个方法 / 6,395 行**全部可见（"方法清单"页），可按文件/状态/关键字过滤，点开看签名与卡片 |
| 逐行注释 | **13 个函数 / 933 行**（占方法体 14.6%），只覆盖决定流程走向的热点，不做全量 |
| 函数级卡片 | 10 张（finishTransition、dispatchReady、getChangeFlags、onTimeout、RemoteTransitionHandler.startAnimation 等） |
| 精读文章 | **42 步**每步一篇：一句话结论、为什么需要、术语、逐调用 walk（带 `文件:行`）、状态变化表、线程/锁、新手常踩、验证命令、下一步 |
| 学习辅助 | 18 条术语表（正文悬停即解释）、一次真实转场时间线（0→250ms）、4 条学习路径（5 分钟 / 30 分钟 / 90 分钟 / 半天）、5 张分支决策表 |

## 页面顺序（四个主题一致）

顶部标签栏按其阅读顺序固定为：

```
全景·架构 → 时序 → 类图 → 流程 → 文件结构 → 方法清单 → 精读 → 代码逐行 → 降级与坑
```

先建立整体印象与调用拓扑，再看清单与文件分布，最后进入「精读」（每步一篇的讲解）与「代码逐行」（带 quote 校验的逐行注释）。移动端标签栏可横向滚动。

## 方法清单的交互

清单最后新增了「操作」列，**整行点击只展开/收起详情卡片，不会跳转**：

| 行状态 | 右侧按钮 | 行为 |
| --- | --- | --- |
| 逐行已覆盖 | `逐行 →` | 切到「代码逐行」并定位到该函数 |
| 未逐行 / 部分覆盖 | `详情` | 就地展开卡片（线程 / 锁 / 职责 / 风险）；点行本身同效 |

这样浏览清单时不会因为误点行而跳出当前页；需要看逐行注释时点按钮即可。清单最多渲染前 400 条，超出时右上角会提示。

## 图的缩放与拖拽

架构图、时序图、类图、流程图四张图**都是构建期生成的矢量 SVG**（页面里没有 `<img>`、没有 base64 位图）。缩放通过**直接放大 SVG 的布局尺寸**实现，浏览器会按新尺寸重新做矢量光栅化，因此放大后文字与线条依然锐利——不要改回 CSS `transform: scale()`，那会把已光栅化的结果当位图拉伸而发虚。

| 操作 | 效果 |
| --- | --- |
| 滚轮 | 上下/左右平移（默认行为，不缩放） |
| **Ctrl/⌘ + 滚轮** | 以光标为锚点缩放（0.4×–6×）；触控板双指捏合同样生效 |
| 拖拽 | 平移；拖拽结束后的 click 会被抑制，不会误触发「点消息跳步骤」 |
| 双击 / Shift+双击 | 放大 / 缩小 |
| 右上角控件 | `−` `+` `适应` `还原`，中间显示当前缩放比例 |
| 键盘 | 容器获得焦点后 `+` `-` `0` |

容器高度固定为 `62vh`（最小 360px）。**默认显示比例 100%（1:1，即 SVG 原始尺寸）**，需要看全貌点「适应」，需要复位点「还原」。

## 为什么这样组织

| 决策 | 原因 |
| --- | --- |
| 代码由构建期从源码抽取 | 页面里的每一行都是源码原文，不是抄来的 |
| 注释绑定 `quote` 原文而不是行号 | 源码一变，构建就报错，不会静默指错代码 |
| 图在构建期生成为 SVG | 离线可用、无 CDN、无 mermaid 运行时 |
| 详细度分级 | 13 个 P0 函数逐行（933 行 / 732 条注释）；10 个函数给卡片；其余 473 个方法在清单里可见、可跳转 |

## 目录

```
wiki_site/
├─ build.py                     # 抽取源码 + 校验注释 + 生成四图 + 组装单文件 HTML
├─ verify.py                    # 独立校验：step 引用 / quote 一致 / 离线 / 体积
├─ topics/shell-transition/
│  ├─ topic.json                # 26 步、时序消息、类图、架构图、流程图、降级表
│  └─ annotations/*.json        # 13 个 P0 函数的逐行注释（带 quote）
├─ src/{template.html,app.css,app.js}
└─ dist/shell-transition.html   # 产物
```

## 用法

```bash
python3 docs/wiki_site/build.py --topic shell-transition        # 生成 dist/shell-transition.html
python3 docs/wiki_site/build.py --topic render-thread-to-sf     # 生成 dist/render-thread-to-sf.html
python3 docs/wiki_site/build.py --topic view-traversal          # 生成 dist/view-traversal.html
python3 docs/wiki_site/build.py --topic sf-compose-to-display    # 生成 dist/sf-compose-to-display.html
python3 docs/wiki_site/verify.py    # 校验，必须输出 problems: 0
open wiki_site/dist/shell-transition.html
```

Windows 下先设 `$env:PYTHONUTF8="1"`，否则 GBK 解码会直接抛 UnicodeDecodeError。

深链：`#s14` 跳到第 14 步，`#deep` / `#code` / `#seq` / `#cls` / `#flow` / `#files` / `#methods` / `#risk` 直接打开对应视图；`#files:<路径>` 直接选中某个文件，例如
`#files:base/services/core/java/com/android/server/wm/Transition.java`。

文件结构页：按包层级展开本页引用到的 24 个文件（单链目录自动合并、目录在前），每个文件标注行数、逐行函数数、被多少步/精读引用；点击查看"直接引用的步骤 / 精读 walk 里提到 / 逐行函数 / 主要方法"，并可跳到对应步骤或代码。

主题：

- 右上角按钮切换亮/暗色，快捷键 `t`，选择写入 `localStorage.st-theme`；
- 首次打开跟随系统 `prefers-color-scheme`；
- 临时覆盖：`?theme=light` / `?theme=dark`（手动切换后会清掉该参数）。

## 注释文件格式

```json
{
  "id": "tr-collect",
  "title": "Transition.collect",
  "file": "base/services/core/java/com/android/server/wm/Transition.java",
  "sig": "void collect(@NonNull WindowContainer wc)",
  "span": [903, 939],
  "notes": [
    { "at": 12, "quote": "if (mParticipants.contains(wc)) {", "kind": "branch",
      "text": "同一容器重复 collect 直接返回，避免重复加入 SyncSet 造成事务泄漏。" }
  ]
}
```

- `at`：函数体内 1-based 偏移；`quote`：该行源码去掉首尾空白的原文（构建期按 quote 优先吸附，`at` 仅作就近提示）。
- `kind`：`lock / thread / ipc / state / branch / data / trace / note`，决定行注释的色点与筛选。

## 新增一条链路

1. 复制 `topics/shell-transition/` → `topics/<slug>/`，改 `topic.json` 的 meta/steps/p0/sequence/classes/architecture/flow。
2. 用 `python3 docs/wiki_site/build.py` 反复构建，`problems` 会指出缺失的注释与 quote 不一致。
3. 逐行注释可以并行生成：每个 P0 函数一个 agent，按上面的 JSON 结构产出，构建期自动校验。

## 图数据格式（详细版）

`render-thread-to-sf` 用了三张更细的图，数据在 topic.json 里用独立键描述，构建时自动生成 SVG（其余主题仍用旧结构，渲染器自动回退）：

| 键 | 结构 | 渲染 |
| --- | --- | --- |
| `archGrid` | `rows`（阶段）/ `cols`（线程）/ `nodes{row,col,slot,label,sub,thread,badge}` / `edges{from,to,label,data,kind}` | 行=时间阶段、列=线程；同格多节点按 `slot` 纵向堆叠；边上标注数据形态与 oneway |
| `seqDetail` | `lanes` / `phases` / `messages{phase,step,from,to,label,data,kind,block}` | 分阶段时序；灰字=数据、红块=阻塞（如 UI 阻塞在 mSignal）、左侧数字=步骤号 |
| `flowDetail` | `regions` / `nodes{region,kind,x,y,note}` / `edges{route:left/right/h}` | 分区流程图（判定 / 入队 / 合成），侧边通道承载重试回路 |

`view-traversal` 另外用到两个可选键：

| 键 | 作用 |
| --- | --- |
| `scope` | 方法清单要抽取的文件列表；不含 `View.java` 这类 3.5 万行的巨型文件（否则清单会被淹没） |
| `inventorySpans` | 当 scope 里有超大类时，只把这些方法名放进清单（`covered` 状态的方法始终保留） |
| `extends` | 主题数据可以拆到独立 JSON（如 `sf-compose-to-display/topic_sf_commit.json`），顶层键写进该列表即自动合并；`meta` 按字段覆盖 |

## 精读文章格式（deep/<step-id>.json）

```json
{
  "step": "s16",
  "title": "…",
  "tldr": "一句话，≤60 字",
  "why": "没有这一步会怎样，≥80 字",
  "concepts": [{"term": "SyncSet", "text": "…"}],
  "walk": [{"call": "BLASTSyncEngine.SyncSet.tryFinish()", "ref": "…:198", "what": "…"}],
  "state": [{"field": "mState", "before": "STARTED", "after": "PLAYING", "note": "…"}],
  "threads": "…", "pitfalls": ["…"], "debug": ["…"], "next": "…"
}
```

`verify.py` 会检查：42 步是否齐全、`walk` ≥3 条且 `ref` 真实存在、`state`/`pitfalls`/`debug` 非空、`tldr` 与 `why` 长度。

## 两处容易讲错的事实（页面已按代码纠正）

- **SYNC_METHOD 默认是 METHOD_NONE**（`TransitionController.java:117-119`）：默认构建下 `setSyncId` 只是序号与顺序约束，并不要求 App 重传 buffer；BLAST 模式需要 `persist.wm.debug.shell_transit_blast=true`。
- **FallbackPlayer 是 flag 行为**（`TransitionController.java:290`）：只有 `Flags.fallbackTransitionPlayer()` 打开才预注册；否则 player 为空期间 `requestTransitionIfNeeded` 直接返回 null，连转场都不建。

## 已知边界

- 只覆盖 `base/` 与 `native/` 里的代码；kernel binder 驱动、composer HAL、Launcher 的 `RemoteTransition` 实现均在仓库外，页面里标注为“接口假设”。
- `DefaultTransitionHandler.startAnimation`（362 行）与 `Transition.finishTransition`（323 行）只做到函数级卡片，没有逐行——它们的分支密度高但回归频率低，按需再补。
