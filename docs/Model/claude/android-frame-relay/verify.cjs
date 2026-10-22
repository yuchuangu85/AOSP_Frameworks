// 可选浏览器验证：node docs/claude/android-frame-relay/verify.cjs
// 依赖：NODE_PATH 指向装有 playwright 的目录；默认使用系统 Chrome（可用 CHROME_PATH 覆盖）。
const assert = require('assert');
const fs = require('fs');
const path = require('path');
const os = require('os');
const { chromium } = require('playwright');

const here = __dirname;
const repo = path.resolve(here, '../../..');
const url = 'file://' + path.join(here, 'index.html');
const out = path.join(os.tmpdir(), 'android-frame-relay-qa');

(async () => {
  fs.mkdirSync(out, { recursive: true });
  const browser = await chromium.launch({
    executablePath: process.env.CHROME_PATH || '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
  });
  const checks = [], errors = [], requests = [];
  const page = await browser.newPage({ viewport: { width: 1440, height: 1000 } });
  page.on('pageerror', e => errors.push(String(e)));
  page.on('console', m => { if (m.type() === 'error') errors.push(m.text()); });
  page.on('request', r => { if (/^https?:/.test(r.url())) requests.push(r.url()); });
  await page.goto(url);

  // 1. 数据结构
  const data = await page.evaluate(() => ({ steps: STEPS, files: F, chapters: CHAPTERS.length, stations: STATIONS.map(s => s.id) }));
  assert.equal(data.steps.length, 25);
  assert.equal(data.chapters, 7);
  assert.equal(new Set(data.steps.map(s => s.id)).size, 25, 'step ids unique');
  for (let k = 0; k < 7; k++) assert(data.steps.some(s => s.ch === k), 'chapter ' + k + ' has steps');
  for (const s of data.steps) {
    assert(data.stations.includes(s.a), s.id + ' actor');
    for (const f of s.from || []) assert(data.stations.includes(f), s.id + ' from ' + f);
    if (s.to) assert(data.stations.includes(s.to), s.id + ' to');
    assert(s.t[0] < s.t[1], s.id + ' interval');
  }
  for (let i = 1; i < 25; i++) assert(data.steps[i].ch >= data.steps[i - 1].ch, 'chapters monotonic');
  checks.push('25 steps / 7 chapters / valid actors and intervals');

  // 2. 每一行带行号的源码都必须与本地文件逐字一致
  let lines = 0;
  for (const s of data.steps) for (const c of s.code) {
    if (c.pseudo) continue;
    const file = path.join(repo, data.files[c.f]);
    assert(fs.existsSync(file), 'missing ' + file);
    const src = fs.readFileSync(file, 'utf8').split('\n');
    let prev = 0;
    for (const l of c.ln) {
      const p = l.indexOf('|'), n = +l.slice(0, p), text = l.slice(p + 1);
      assert(n > prev, s.id + ' line order ' + n); prev = n;
      assert.equal(src[n - 1], text, `${s.id}: ${data.files[c.f]}:${n}`);
      lines++;
    }
    for (const n of c.hl || []) assert(c.ln.some(l => +l.split('|')[0] === n), s.id + ' highlight ' + n);
  }
  checks.push(`${lines} quoted source lines match the working tree verbatim`);

  // 3. 每一步的舞台、讲解和代码同步
  for (let i = 0; i < 25; i++) {
    await page.evaluate(i => go(i), i);
    const s = data.steps[i];
    assert.equal(await page.locator('.st.active').count(), 1);
    assert.equal(await page.locator('.st.active').getAttribute('data-id'), s.a);
    assert.equal(await page.locator('#s-title').textContent(), s.title);
    assert.equal(await page.locator('#c-blocks .cblock').count(), s.code.length);
    assert.equal(await page.locator('#c-blocks .ln.hl').count(), s.code.reduce((n, c) => n + (c.hl || []).length, 0));
    const want = (s.from || []).filter(f => f !== s.a).length + (s.to ? 1 : 0);
    assert.equal(await page.locator('#links path.link').count(), want, s.id + ' links');
    assert.equal(await page.locator('.tl-bar.cur').count(), 1);
    assert.equal(await page.locator('.chap.active').getAttribute('data-ch'), String(s.ch));
  }
  checks.push('Every step: one active actor, matching story / code / links / timeline / chapter');

  // 4. 派生状态：fence、槽位、屏幕
  const at = async id => { await page.evaluate(id => go(I[id]), id); };
  await at('queue');
  assert.equal(await page.locator('#chip-acq').getAttribute('data-s'), 'pending');
  assert.equal(await page.locator('.slot.hot .sl').textContent(), 'B');
  assert.equal(await page.locator('.st[data-id=gpu]').getAttribute('class'), 'st busy');
  await at('sync');
  assert.match(await page.locator('.st[data-id=ui]').getAttribute('class'), /blocked/);
  await at('gpuDone');
  assert.equal(await page.locator('#chip-acq').getAttribute('data-s'), 'signaled');
  await at('release');
  assert.equal(await page.locator('#chip-rel').getAttribute('data-s'), 'pending');
  assert.equal(await page.locator('#chip-scr').getAttribute('data-s'), 'none');
  await at('reuse');
  assert.equal(await page.locator('#chip-rel').getAttribute('data-s'), 'signaled');
  assert.equal(await page.locator('#chip-scr').getAttribute('data-s'), 'on');
  await at('client');
  assert.equal(await page.locator('.st[data-id=gpu]').getAttribute('class'), 'st busy');
  assert.match(await page.locator('.st[data-id=gpu] .op').textContent(), /CLIENT/);
  checks.push('Fence lamps, BufferQueue slots, blocked/parallel actors and screen state follow the step');

  // 5. 键盘、章节、播放
  await page.evaluate(() => go(0));
  await page.locator('body').click({ position: { x: 5, y: 5 } });
  await page.keyboard.press('ArrowRight'); assert.equal(await page.evaluate(() => cur), 1);
  await page.keyboard.press('End'); assert.equal(await page.evaluate(() => cur), 24);
  await page.keyboard.press('Home'); assert.equal(await page.evaluate(() => cur), 0);
  await page.keyboard.press('6'); assert.equal(await page.evaluate(() => STEPS[cur].ch), 5);
  await page.locator('.chap').nth(3).click(); assert.equal(await page.evaluate(() => STEPS[cur].id), 'sync');
  await page.locator('.st[data-id=hwc]').click(); assert.equal(await page.evaluate(() => STEPS[cur].id), 'validate');
  await page.locator('#myths button').first().click(); assert.equal(await page.evaluate(() => STEPS[cur].id), 'invalidate');
  await page.selectOption('#speed', '0.6');
  await page.locator('#play').click(); await page.waitForTimeout(2400);
  assert(await page.evaluate(() => cur) >= 1, 'autoplay advances');
  await page.locator('#play').click(); const frozen = await page.evaluate(() => cur);
  await page.waitForTimeout(2400); assert.equal(await page.evaluate(() => cur), frozen, 'pause freezes');
  await page.keyboard.press('?'); assert(await page.locator('#help').evaluate(d => d.open));
  await page.keyboard.press('Escape');
  await page.locator('#open-sources').click();
  assert.equal(await page.locator('#src-files li').count(), new Set(data.steps.flatMap(s => s.code.filter(c => !c.pseudo).map(c => c.f))).size);
  await page.keyboard.press('Escape');
  checks.push('Keyboard, chapter / station / myth navigation, autoplay + pause, dialogs');

  // 6. 深链接与响应式
  const deep = await browser.newPage({ viewport: { width: 1440, height: 900 } });
  await deep.goto(url + '#latch-miss'); assert.equal(await deep.evaluate(() => cur), 0);
  await deep.goto(url + '#commit'); await deep.reload(); assert.equal(await deep.evaluate(() => STEPS[cur].id), 'commit');
  await deep.close();
  await page.evaluate(() => go(I.client));
  await page.screenshot({ path: path.join(out, 'desktop.png'), fullPage: true });
  for (const w of [1024, 768, 390]) {
    await page.setViewportSize({ width: w, height: 900 });
    const overflow = await page.evaluate(() => document.documentElement.scrollWidth - document.documentElement.clientWidth);
    assert(overflow <= 1, `document overflow at ${w}px: ${overflow}`);
  }
  await page.screenshot({ path: path.join(out, 'mobile.png'), fullPage: true });
  checks.push('Hash deep links; 1440 / 1024 / 768 / 390px without document overflow');

  assert.deepEqual(errors, []);
  assert.deepEqual(requests, []);
  checks.push('No page errors; zero HTTP(S) requests');
  await browser.close();
  console.log(JSON.stringify({ passed: true, checks, artifactDir: out }, null, 2));
})().catch(e => { console.error(e); process.exit(1); });
