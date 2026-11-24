import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const dir = path.dirname(fileURLToPath(import.meta.url));
const root = path.resolve(dir, '../../..');
const read = name => fs.readFileSync(path.join(dir, name), 'utf8');
const escape = value => String(value).replaceAll('&', '&amp;').replaceAll('<', '&lt;').replaceAll('>', '&gt;').replaceAll('"', '&quot;').replaceAll("'", '&#39;');
const hash = text => crypto.createHash('sha256').update(text).digest('hex');
const manifest = JSON.parse(read('data/sources.json'));
const identity = { snapshot: execFileSync('git', ['rev-parse', 'HEAD'], { cwd: root, encoding: 'utf8' }).trim(),
  sourceRoot: 'repository root', inspectedDate: '2026-10-28', excerptOnly: true, files: {} };
let count = 0;
for (const stage of manifest.stages) {
  for (const snippet of stage.snippets) {
    const sourcePath = path.resolve(root, snippet.file);
    if (!sourcePath.startsWith(root + path.sep)) throw new Error(`Unsafe source: ${snippet.file}`);
    const content = fs.readFileSync(sourcePath, 'utf8');
    const [start, end = start] = String(snippet.lines).split('-').map(Number);
    if (!Number.isInteger(start) || !Number.isInteger(end) || start < 1 || end < start) throw new Error(`Invalid range: ${snippet.file}`);
    const lines = content.split(/\r?\n/).slice(start - 1, end);
    let cursor = 0;
    for (const line of snippet.code.split(/\r?\n/)) {
      const target = line.trim();
      if (!target || target === '// ...' || target === '# ...') continue;
      const index = lines.findIndex((candidate, i) => i >= cursor && candidate.trim() === target);
      if (index < 0) throw new Error(`Excerpt mismatch: ${snippet.file}:${snippet.lines}: ${target}`);
      cursor = index + 1;
    }
    identity.files[snippet.file] = { sha256: hash(content), dirty: execFileSync('git', ['status', '--porcelain', '--', snippet.file], { cwd: root, encoding: 'utf8' }).trim() !== '' };
    count++;
  }
}
if (!count) throw new Error('No source excerpts');

const titles = {
  'focused-application-route': ['application', '焦点应用：独立的应用身份更新', 'WMS 将 Activity 的 InputApplicationHandle 通知 InputDispatcher，和窗口焦点 token 分开管理。当前 JNI 对 null Java handle 直接返回；不可据此声称 Java 清空应用时一定清除了 native 状态。'],
  'focused-window-route': ['window-route', '焦点窗口：请求与输入窗口信息汇合', 'WMS 把焦点请求写入 SurfaceControl 事务；SurfaceFlinger 分发窗口信息与焦点请求，InputDispatcher 再让 FocusResolver 解析。请求已发出，不代表窗口已经获得输入焦点。'],
  'focus-eligibility': ['focusability', '焦点资格：存在、可聚焦、可见', 'FocusResolver 对匹配 token 的窗口检查 NO_WINDOW、NOT_FOCUSABLE、NOT_VISIBLE 等条件；窗口画面存在，不足以证明它能承接焦点事件。'],
  'focus-dependent-event-routing': ['routing', '事件分流：焦点目标与触摸命中', 'KeyEvent 与 non-pointer motion 查找焦点窗口；普通 pointer 触摸采用触摸目标查找。示意中的 ENTER 事件已通过前置 policy 拦截，不把所有按键、返回手势等同处理。'],
  'arm-focus-wait': ['arm', '计时启动：currentTime + 应用 timeout', '焦点应用存在、焦点窗口为空，且焦点事件进入目标查找时才创建 mNoFocusedWindowAnrState。事件保持 PENDING，保存的 entry.eventTime 与计时起点并不相同。'],
  'pre-anr-warning': ['pre-anr', '可选预警：不是正式 ANR 的开关', 'feature flag 控制 pre-ANR 通知；正式超时检查仍独立运行。预警本身不重置或延长 deadline，也不自动意味着 ANR 对话框。示意开关不宣称任何设备默认 flag 值。'],
  'deadline-and-native-recheck': ['timeout', '超时复查：应用身份与窗口仍满足条件', 'processAnrsLocked 在 currentTime >= timeoutEndTime 时调用复查；只在同一应用 token 仍为焦点、对应 display 仍无焦点窗口时继续上报。'],
  'reset-entry-points': ['reset', '解除等待：新焦点、应用变化与调度重置', '新窗口焦点、应用身份改变、解除冻结、全量重置等路径会清除等待状态。清除状态不撤回已排队的 policy 通知；触摸切换的队列裁剪也不等于任意触摸都直接 reset。'],
  'policy-jni-to-wms': ['report', '上报边界：排队 command，再经 JNI 到 WMS', 'native 排入 command，执行时释放 dispatcher 锁后调用 policy，经 NativeInputManager / JNI、InputManagerService、InputManagerCallback 转交 WMS。JNI 是同一 system_server 内的语言边界，不是这一步跨进程 Binder。'],
  'wms-attribution-and-recovery': ['policy', 'Java 复核：Activity 状态、焦点请求归因及恢复', 'WMS 过滤未知或 stopped Activity，可能将问题归因到持续超时的焦点请求窗口，否则经 ActivityRecord 进入 AMS 超时处理。是否弹窗与后续进程策略是另一个边界。'],
  'timeout-configuration': ['configuration', '超时配置：5000 ms 是基础默认值', '默认值会乘以 HW timeout multiplier，并可由应用的 dispatchingTimeout 覆盖。模型采用基础 5 s 和倍率 1，不声称所有设备固定等待 5 s。'],
};
const svg = body => `<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">${body}</svg>`;
const icons = {
  play: svg('<path d="M5 5a2 2 0 0 1 3.008-1.728l11.997 6.998a2 2 0 0 1 .003 3.458l-12 7A2 2 0 0 1 5 19z"/>'),
  pause: svg('<rect x="14" y="3" width="5" height="18" rx="1"/><rect x="5" y="3" width="5" height="18" rx="1"/>'),
  scan: svg('<path d="M3 7V5a2 2 0 0 1 2-2h2M17 3h2a2 2 0 0 1 2 2v2M21 17v2a2 2 0 0 1-2 2h-2M7 21H5a2 2 0 0 1-2-2v-2"/>'),
  restart: svg('<path d="M3 12a9 9 0 1 0 9-9 9.75 9.75 0 0 0-6.74 2.74L3 8M3 3v5h5"/>'),
  previous: svg('<path d="M13.971 4.285A2 2 0 0 1 17 6v12a2 2 0 0 1-3.029 1.715l-9.997-5.998a2 2 0 0 1-.003-3.432zM21 20V4"/>'),
  next: svg('<path d="M10.029 4.285A2 2 0 0 0 7 6v12a2 2 0 0 0 3.029 1.715l9.997-5.998a2 2 0 0 0 .003-3.432zM3 4v16"/>'),
  sun: svg('<circle cx="12" cy="12" r="4"/><path d="M12 2v2M12 20v2m-7.07-15.07 1.41 1.41m11.32 11.32 1.41 1.41M2 12h2m16 0h2M6.34 17.66l-1.41 1.41M19.07 4.93l-1.41 1.41"/>'),
  key: svg('<path d="M10 8h.01M12 12h.01M14 8h.01M16 12h.01M18 8h.01M6 8h.01M7 16h10M8 12h.01"/><rect width="20" height="16" x="2" y="4" rx="2"/>'),
  window: svg('<rect width="18" height="18" x="3" y="3" rx="2"/><path d="M3 9h18M9 21V9"/>'),
};
const sourceHTML = manifest.stages.map(stage => {
  const [id, title, summary] = titles[stage.id];
  const snippets = stage.snippets.map(snippet => `<div class="snippet"><div class="snippet-head"><a href="../../../${escape(snippet.file)}" target="_blank" rel="noopener noreferrer">${escape(snippet.file)}</a><span>L${escape(snippet.lines)}</span></div><pre><code>${escape(snippet.code)}</code></pre><p class="snippet-note">${escape(snippet.note)}</p></div>`).join('\n');
  return `<details class="source-entry" id="source-${id}"><summary><strong>${escape(title)}</strong><span>${escape(stage.snippets[0].symbol)} / ${stage.snippets.length} 段</span></summary><p class="source-summary">${escape(summary)}</p>${snippets}</details>`;
}).join('\n');
const values = { STYLE: read('src/style.css'), MODEL: read('src/model.js'), APP: read('src/app.js'),
  DATA: JSON.stringify({ ...manifest, icons, identity, thirdPartyNotices: read('THIRD_PARTY_NOTICES.md') }).replaceAll('<', '\\u003c'),
  SOURCES: sourceHTML, REVISION: identity.snapshot.slice(0, 12) };
for (const [key, icon] of Object.entries(icons)) values[`ICON_${key.toUpperCase()}`] = icon;
const html = read('src/shell.html').replace('  <link rel="stylesheet" href="kit.css" data-ve-kit>\n', '').replace(/\{\{([A-Z_]+)\}\}/g, (_, name) => {
  if (!(name in values)) throw new Error(`Unknown placeholder: ${name}`);
  return values[name];
});
if (/\{\{[A-Z_]+\}\}/.test(html) || /<(script|link)[^>]+(?:src|href)="https?:/i.test(html)) throw new Error('Artifact is not self-contained');
fs.writeFileSync(path.join(dir, 'index.html'), html);
fs.writeFileSync(path.join(dir, 'data/source-identity.json'), JSON.stringify(identity, null, 2) + '\n');
console.log(`Built index.html: ${Buffer.byteLength(html)} bytes; ${manifest.stages.length} groups; ${count}/${count} excerpts match; no external runtime assets.`);
