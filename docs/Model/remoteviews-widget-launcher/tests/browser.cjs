'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const crypto = require('node:crypto');
const { pathToFileURL } = require('node:url');
const { chromium } = require(process.env.PLAYWRIGHT_MODULE || 'playwright-core');

async function main() {
  const browser = await chromium.launch({ headless: true,
    ...(process.env.CHROME_PATH ? { executablePath: process.env.CHROME_PATH } : {}) });
  const root = path.resolve(__dirname, '..');
  const url = pathToFileURL(path.join(root, 'index.html')).href;
  const sourceData = JSON.parse(fs.readFileSync(path.join(root, 'data/sources.json'), 'utf8'));
  const errors = [];
  const external = [];
  const screenshots = [];
  const cases = [];
  const qa = page => page.evaluate(() => JSON.parse(document.documentElement.dataset.qa));
  async function seek(page, value) {
    await page.locator('#scrub').evaluate((input, value) => {
      input.value = String(value); input.dispatchEvent(new Event('input', { bubbles: true }));
    }, value);
  }
  async function slider(page, id, value) {
    await page.locator(id).evaluate((input, value) => {
      input.value = String(value); input.dispatchEvent(new Event('input', { bubbles: true }));
    }, value);
  }
  async function layout(page, label) {
    const result = await page.evaluate(() => {
      const selector = 'h1, h2, h3, h4, .phase-copy p, .actor, .connector > span, .connector small, .descriptor, .descriptor dd, .action-list, .cache-record, .host-local, .tree-root, .tree-child, .widget-preview, .boundaries, .verdict, .render-chain, .transport, .parameters, .scenario-bar, .source-entry summary';
      return { page: document.documentElement.scrollWidth, viewport: innerWidth,
        clipped: [...document.querySelectorAll(selector)].filter(node => node.getClientRects().length && node.scrollWidth > node.clientWidth + 2)
          .map(node => ({ className: node.className || node.tagName, width: node.clientWidth, scroll: node.scrollWidth, text: node.textContent.slice(0, 140) })) };
    });
    assert.ok(result.page <= result.viewport + 2, `${label}: document overflow ${JSON.stringify(result)}`);
    assert.deepEqual(result.clipped, [], `${label}: clipped content`);
  }
  async function shot(page, name, element) {
    const target = path.join(os.tmpdir(), `remoteviews-widget-${name}.png`);
    if (element) await page.locator(element).screenshot({ path: target, style: '.topbar { visibility: hidden !important; }' });
    else await page.screenshot({ path: target });
    screenshots.push(target);
  }
  function observe(page) {
    page.on('pageerror', error => errors.push(error.message));
    page.on('request', request => { if (/^https?:/.test(request.url())) external.push(request.url()); });
  }
  try {
    for (const viewport of [{ width: 1440, height: 1050 }, { width: 1024, height: 900 }, { width: 768, height: 1024 }, { width: 390, height: 844 }, { width: 360, height: 800 }]) {
      const page = await browser.newPage({ viewport, colorScheme: 'dark', reducedMotion: 'reduce' });
      observe(page);
      await page.goto(url);
      await page.waitForFunction(() => Boolean(document.documentElement.dataset.qa));
      assert.equal((await qa(page)).callbackReceived, true);
      assert.equal((await qa(page)).hostReceived, false);
      assert.equal(await page.locator('.source-entry').count(), sourceData.stages.length);
      assert.equal(await page.locator('.snippet').count(), sourceData.stages.reduce((n, stage) => n + stage.snippets.length, 0));
      await layout(page, `${viewport.width}/initial`);
      await shot(page, `${viewport.width}-initial-dark`);
      const scenarios = await page.locator('#scenario option').evaluateAll(nodes => nodes.map(node => node.value));
      assert.equal(scenarios.length, 8);
      let state;
      for (const scenario of scenarios) {
        await page.selectOption('#scenario', scenario);
        assert.equal((await qa(page)).t, 0);
        await seek(page, 3000);
        state = await qa(page);
        assert.equal(state.cached, true);
        assert.equal(state.cacheRevision, 1);
        assert.equal(state.hostReceived, false);
        assert.equal(state.applied, false);
        assert.equal(state.serviceHasView, false);
        if (scenario === 'partial') {
          assert.equal(state.providerActionCount, 1);
          assert.equal(state.cacheActionCount, 3);
        }
        const times = state.times;
        await seek(page, 4200);
        assert.equal((await qa(page)).callbackReceived, scenario !== 'offline');
        assert.equal((await qa(page)).hostReceived, false);
        await seek(page, times.host + 100);
        assert.equal((await qa(page)).hostReceived, true);
        if (times.tree !== null && scenario !== 'error') {
          await seek(page, times.tree + 100);
          state = await qa(page);
          assert.equal(state.prepared, true);
          assert.equal(state.applied, false);
          assert.match(await page.locator('#treeIdentity').textContent(), /未挂入/);
        }
        await seek(page, times.commit + 100);
        state = await qa(page);
        assert.equal(state.committed, true);
        assert.equal(state.applied, scenario !== 'error');
        assert.equal(state.screenUpdated, false);
        assert.notEqual(state.visibleRevision, 1);
        assert.equal(state.listenerApplied, scenario === 'async');
        if (['reapply', 'partial', 'offline'].includes(scenario)) assert.equal(state.localTree, 'T0');
        else if (scenario === 'error') assert.equal(state.localTree, 'ERROR');
        else assert.equal(state.localTree, 'T1');
        await layout(page, `${viewport.width}/${scenario}/committed`);
        if ((viewport.width === 1440 || viewport.width === 390) && ['async', 'offline', 'error', 'partial'].includes(scenario)) await shot(page, `${viewport.width}-${scenario}-committed`, '#studio');
        await seek(page, state.duration);
        state = await qa(page);
        assert.equal(state.screenUpdated, true);
        assert.equal(state.visibleRevision, scenario === 'error' ? 'error' : 1);
        assert.equal(await page.locator('#widgetCount').textContent(), scenario === 'error' ? '!' : '3');
        await layout(page, `${viewport.width}/${scenario}/screen`);
        if ((viewport.width === 1440 || viewport.width === 390) && scenario === 'first') await shot(page, `${viewport.width}-screen-complete`, '#studio');
        await page.locator('#prevStep').click();
        const previous = (await qa(page)).t;
        await page.locator('#nextStep').click();
        assert.ok((await qa(page)).t > previous);
        await page.locator('#eventList button').nth(1).click();
        assert.equal((await qa(page)).playing, false);
        cases.push(`${viewport.width}/${scenario}`);
      }
      await page.selectOption('#scenario', 'offline');
      await seek(page, 5000);
      state = await qa(page);
      assert.equal(state.hostListening, false);
      assert.equal(state.visibleRevision, 0);
      assert.equal(state.cacheRevision, 1);
      await seek(page, state.times.host + 100);
      assert.equal((await qa(page)).callbackReceived, false);
      assert.equal((await qa(page)).hostReceived, true);
      await page.selectOption('#scenario', 'async');
      await slider(page, '#backgroundRange', 4000);
      await slider(page, '#renderRange', 2600);
      state = await qa(page);
      assert.equal(state.background, 4000);
      assert.equal(state.renderDelay, 2600);
      await seek(page, state.times.commit + 100);
      state = await qa(page);
      assert.equal(state.listenerApplied, true);
      assert.equal(state.screenUpdated, false);
      await page.selectOption('#scenario', 'first');
      await page.locator('#restartBtn').click();
      await page.selectOption('#speed', '2');
      await page.locator('#playBtn').click();
      await page.waitForFunction(() => JSON.parse(document.documentElement.dataset.qa).t > 200);
      await page.locator('#playBtn').click();
      const paused = (await qa(page)).t;
      await page.waitForTimeout(100);
      assert.equal((await qa(page)).t, paused);
      await seek(page, 3000);
      await page.locator('#phaseRef').click();
      assert.equal(await page.locator('#source-service').evaluate(node => node.open), true);
      await page.evaluate(() => document.querySelectorAll('details').forEach(node => { node.open = true; }));
      await layout(page, `${viewport.width}/sources-open`);
      const anchors = await page.locator('a[href^="#"]').evaluateAll(nodes => nodes.map(node => node.getAttribute('href')));
      for (const anchor of anchors) assert.equal(await page.locator(anchor).count(), 1, `invalid anchor ${anchor}`);
      await page.evaluate(() => document.querySelectorAll('details').forEach(node => { node.open = false; }));
      await page.locator('#themeBtn').click();
      assert.equal(await page.locator('html').getAttribute('data-theme'), 'light');
      await seek(page, (await qa(page)).times.commit + 100);
      await page.locator('#simulation').scrollIntoViewIfNeeded();
      await layout(page, `${viewport.width}/light`);
      await shot(page, `${viewport.width}-light`);
      if (viewport.width === 1440 || viewport.width === 390) {
        await shot(page, `${viewport.width}-scene-light`, '#studio');
        await shot(page, `${viewport.width}-mechanism`, '#mechanism');
        await shot(page, `${viewport.width}-triggers`, '#triggers');
      }
      await page.locator('#themeBtn').click();
      await page.reload();
      assert.equal(await page.locator('html').getAttribute('data-theme'), 'dark');
      await page.close();
    }
    for (const width of [1440, 390]) {
      const page = await browser.newPage({ viewport: { width, height: 1000 }, reducedMotion: 'no-preference' });
      observe(page);
      await page.goto(url);
      await seek(page, 2400);
      const sendStart = await page.locator('#appPacket').boundingBox();
      await seek(page, 2800);
      const sendEnd = await page.locator('#appPacket').boundingBox();
      assert.ok(sendEnd.x > sendStart.x);
      assert.equal(await page.locator('#appPacket').evaluate(node => getComputedStyle(node).opacity), '1');
      await seek(page, 3000);
      assert.equal(await page.locator('#appPacket').evaluate(node => getComputedStyle(node).opacity), '0');
      await page.selectOption('#scenario', 'offline');
      await seek(page, 6020);
      const requestStart = await page.locator('#hostPacket').boundingBox();
      await seek(page, 6180);
      const requestEnd = await page.locator('#hostPacket').boundingBox();
      assert.ok(width > 950 ? requestEnd.x < requestStart.x : requestEnd.y < requestStart.y);
      await seek(page, 6250);
      const responseStart = await page.locator('#hostPacket').boundingBox();
      await seek(page, 6450);
      const responseEnd = await page.locator('#hostPacket').boundingBox();
      assert.ok(width > 950 ? responseEnd.x > responseStart.x : responseEnd.y > responseStart.y);
      await page.close();
    }
    assert.deepEqual(errors, []);
    assert.deepEqual(external, [], 'offline HTML requested remote resources');
    const report = { ok: true, artifact: 'index.html', artifactSha256: crypto.createHash('sha256').update(fs.readFileSync(path.join(root, 'index.html'))).digest('hex'),
      browser: await browser.version(), viewports: [1440, 1024, 768, 390, 360], scenarioChecks: cases.length,
      checks: ['all eight scenario outcomes', 'descriptor-only ownership', 'cache versus host UI', 'Binder versus UI', 'view apply versus screen', 'partial merge', 'local view identity', 'async UI completion', 'offline return-list delivery', 'error placeholder', 'fresh apply fallback', 'timing controls', 'play/pause/speed', 'step and seeking', 'bidirectional marker motion on desktop and mobile', 'source anchors', 'theme persistence', 'text bounds', 'offline loading'],
      screenshots, errors, externalRequests: external,
      limits: ['framework Host contract only; no Launcher3 checkout', 'illustrative times and screen update', 'no Android device, present-fence or physical-screen validation'] };
    fs.writeFileSync(path.join(root, 'data/browser-verification.json'), JSON.stringify(report, null, 2) + '\n');
    console.log(`PASS: ${cases.length} viewport/scenario cases; controls, ownership, callback/apply/screen boundaries, offline recovery, motion, sources, themes and layout.`);
    console.log(screenshots.join('\n'));
  } finally { await browser.close(); }
}

main().catch(error => { console.error(error); process.exitCode = 1; });
