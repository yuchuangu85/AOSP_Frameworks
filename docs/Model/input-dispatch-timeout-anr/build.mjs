import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { createRequire } from 'node:module';

const dir = path.dirname(fileURLToPath(import.meta.url));
const root = path.resolve(dir, '../../..');
const read = name => fs.readFileSync(path.join(dir, name), 'utf8');
const escape = value => String(value).replaceAll('&', '&amp;').replaceAll('<', '&lt;').replaceAll('>', '&gt;').replaceAll('"', '&quot;').replaceAll("'", '&#39;');
const sha256 = value => crypto.createHash('sha256').update(value).digest('hex');
const manifest = JSON.parse(read('data/sources.json'));
const identity = { snapshot: execFileSync('git', ['rev-parse', 'HEAD'], { cwd: root, encoding: 'utf8' }).trim(),
  sourceRoot: 'repository root', inspectedDate: '2026-10-28', excerptOnly: true, files: {} };
let excerpts = 0;
const ids = new Set();
for (const stage of manifest.stages) {
  if (ids.has(stage.id)) throw new Error(`Duplicate stage ${stage.id}`);
  ids.add(stage.id);
  for (const snippet of stage.snippets) {
    const sourcePath = path.resolve(root, snippet.file);
    if (!sourcePath.startsWith(root + path.sep)) throw new Error(`Unsafe source ${snippet.file}`);
    const source = fs.readFileSync(sourcePath, 'utf8');
    const [start, end = start] = String(snippet.lines).split('-').map(Number);
    if (!Number.isInteger(start) || !Number.isInteger(end) || start < 1 || end < start) throw new Error(`Invalid range ${snippet.lines}`);
    const lines = source.split(/\r?\n/).slice(start - 1, end);
    let cursor = 0;
    for (const line of snippet.code.split(/\r?\n/)) {
      const target = line.trim();
      if (!target || target === '// ...' || target === '# ...') continue;
      const index = lines.findIndex((candidate, index) => index >= cursor && candidate.trim() === target);
      if (index < 0) throw new Error(`Excerpt mismatch ${snippet.file}:${snippet.lines}: ${target}`);
      cursor = index + 1;
    }
    identity.files[snippet.file] = { sha256: sha256(source), dirty: execFileSync('git', ['status', '--porcelain', '--', snippet.file], { cwd: root, encoding: 'utf8' }).trim() !== '' };
    excerpts++;
  }
}
if (!excerpts) throw new Error('No excerpts');
const model = read('src/model.js');
const api = createRequire(import.meta.url)('./src/model.js');
for (const scenario of api.scenarios) {
  for (const event of api.build({ scenario: scenario.id }).schedule) {
    if (!ids.has(event.ref)) throw new Error(`Missing source stage ${event.ref}`);
  }
}
const svg = body => `<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">${body}</svg>`;
const icons = {
  play: svg('<path d="M5 5a2 2 0 0 1 3.008-1.728l11.997 6.998a2 2 0 0 1 .003 3.458l-12 7A2 2 0 0 1 5 19z"/>'),
  pause: svg('<rect x="14" y="3" width="5" height="18" rx="1"/><rect x="5" y="3" width="5" height="18" rx="1"/>'),
  scan: svg('<path d="M3 7V5a2 2 0 0 1 2-2h2M17 3h2a2 2 0 0 1 2 2v2M21 17v2a2 2 0 0 1-2 2h-2M7 21H5a2 2 0 0 1-2-2v-2"/>'),
  restart: svg('<path d="M3 12a9 9 0 1 0 9-9 9.75 9.75 0 0 0-6.74 2.74L3 8M3 3v5h5"/>'),
  previous: svg('<path d="M13.971 4.285A2 2 0 0 1 17 6v12a2 2 0 0 1-3.029 1.715l-9.997-5.998a2 2 0 0 1-.003-3.432zM21 20V4"/>'),
  next: svg('<path d="M10.029 4.285A2 2 0 0 0 7 6v12a2 2 0 0 0 3.029 1.715l9.997-5.998a2 2 0 0 0 .003-3.432zM3 4v16"/>'),
  sun: svg('<circle cx="12" cy="12" r="4"/><path d="M12 2v2M12 20v2m-7.07-15.07 1.41 1.41m11.32 11.32 1.41 1.41M2 12h2m16 0h2M6.34 17.66l-1.41 1.41M19.07 4.93l-1.41 1.41"/>'),
  cpu: svg('<path d="M12 20v2M12 2v2M17 20v2M17 2v2M2 12h2M2 17h2M2 7h2M20 12h2M20 17h2M20 7h2M7 20v2M7 2v2"/><rect x="4" y="4" width="16" height="16" rx="2"/><rect x="8" y="8" width="8" height="8" rx="1"/>'),
};
const sources = manifest.stages.map(stage => {
  const snippets = stage.snippets.map(snippet => `<div class="snippet"><div class="snippet-head"><a href="../../../${escape(snippet.file)}" target="_blank" rel="noopener noreferrer">${escape(snippet.file)}</a><span>L${escape(snippet.lines)}</span></div><pre><code>${escape(snippet.code)}</code></pre><p class="snippet-note">${escape(snippet.note)}</p></div>`).join('\n');
  return `<details class="source-entry" id="source-${escape(stage.id)}"><summary><b>${escape(stage.title)}</b><span>${escape(stage.snippets[0].symbol)} / ${stage.snippets.length} 段</span></summary><p class="source-summary">${escape(stage.summary)}</p>${snippets}</details>`;
}).join('\n');
const values = { STYLE: read('src/style.css'), MODEL: model, APP: read('src/app.js'), SOURCES: sources,
  REVISION: identity.snapshot.slice(0, 12),
  DATA: JSON.stringify({ ...manifest, identity, icons, thirdPartyNotices: read('THIRD_PARTY_NOTICES.md') }).replaceAll('<', '\\u003c') };
Object.entries(icons).forEach(([name, svg]) => { values[`ICON_${name.toUpperCase()}`] = svg; });
const html = read('src/shell.html').replace('  <link rel="stylesheet" href="kit.css" data-ve-kit>\n', '').replace(/\{\{([A-Z_]+)\}\}/g, (_, key) => {
  if (!(key in values)) throw new Error(`Unknown placeholder ${key}`);
  return values[key];
});
if (/\{\{[A-Z_]+\}\}/.test(html) || /<(script|link)[^>]+(?:src|href)="https?:/i.test(html)) throw new Error('Non-offline artifact');
fs.writeFileSync(path.join(dir, 'index.html'), html);
fs.writeFileSync(path.join(dir, 'data/source-identity.json'), JSON.stringify(identity, null, 2) + '\n');
console.log(`Built index.html: ${Buffer.byteLength(html)} bytes; ${manifest.stages.length} source groups; ${excerpts}/${excerpts} excerpts match; zero external runtime assets.`);
