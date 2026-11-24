'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { pathToFileURL } = require('node:url');
const { chromium } = require(process.env.PLAYWRIGHT_MODULE || 'playwright-core');

async function main() {
  const browser = await chromium.launch({ headless: true,
    ...(process.env.CHROME_PATH ? { executablePath: process.env.CHROME_PATH } : {}) });
  const errors = [];
  const requests = [];
  const results = [];
  const screenshots = [];
  const root = path.resolve(__dirname, '..');
  const url = pathToFileURL(path.join(root, 'index.html')).href;
  const qa = page => page.evaluate(() => JSON.parse(document.documentElement.dataset.qa));
  async function seek(page, time) {
    await page.locator('#scrub').evaluate((input, value) => {
      input.value = String(value);
      input.dispatchEvent(new Event('input', { bubbles: true }));
    }, time);
  }
  async function layout(page, name) {
    const bounds = await page.evaluate(() => {
      const elements = [...document.querySelectorAll('h1, h2, h3, .phase-copy p, .register, .focus-reason, .decision-strip, .anr-checks, .policy-path, .parameter-row, .scenario-bar, .transport, .actor, .source-entry summary')];
      return { page: document.documentElement.scrollWidth, viewport: innerWidth,
        clipped: elements.filter(element => element.getClientRects().length && element.scrollWidth > element.clientWidth + 2)
          .map(element => ({ tag: element.className || element.tagName, text: element.textContent.slice(0, 140), width: element.clientWidth, scroll: element.scrollWidth })) };
    });
    assert.ok(bounds.page <= bounds.viewport + 2, `${name}: document overflow ${JSON.stringify(bounds)}`);
    assert.deepEqual(bounds.clipped, [], `${name}: clipped content`);
  }
  try {
    for (const viewport of [{ width: 1440, height: 1050 }, { width: 768, height: 1024 }, { width: 390, height: 844 }, { width: 360, height: 800 }]) {
      const page = await browser.newPage({ viewport, reducedMotion: 'reduce', colorScheme: 'dark' });
      page.on('pageerror', error => errors.push(error.message));
      page.on('request', request => { if (/^https?:/.test(request.url())) requests.push(request.url()); });
      await page.goto(url);
      await page.waitForFunction(() => Boolean(document.documentElement.dataset.qa));
      assert.equal((await qa(page)).waiting, true);
      assert.equal(await page.locator('.source-entry').count(), 11);
      assert.equal(await page.locator('.snippet').count(), 60);
      await layout(page, `${viewport.width}/initial`);
      const initial = path.join(os.tmpdir(), `no-focus-anr-${viewport.width}-dark.png`);
      await page.screenshot({ path: initial });
      screenshots.push(initial);
      const scenarios = await page.locator('#scenario option').evaluateAll(nodes => nodes.map(node => node.value));
      assert.equal(scenarios.length, 7);
      for (const scenario of scenarios) {
        await page.selectOption('#scenario', scenario);
        const initialState = await qa(page);
        await seek(page, initialState.duration);
        const end = await qa(page);
        assert.equal(end.scenario, scenario);
        assert.equal(end.timedOut, ['timeout', 'not-focusable'].includes(scenario));
        assert.equal(end.waiting, false);
        if (scenario === 'pointer') assert.equal(end.focusedWindow, 'null');
        if (scenario === 'switch') {
          assert.equal(end.application, 'B');
          assert.equal(end.event, '\u5f00\u59cb\u5206\u53d1');
          assert.equal(await page.locator('#outgoing').evaluate(node => node.classList.contains('pass')), true);
        }
        await layout(page, `${viewport.width}/${scenario}`);
        await page.locator('#prevStep').click();
        const previous = await qa(page);
        await page.locator('#nextStep').click();
        assert.ok((await qa(page)).t > previous.t);
        const target = Math.min(1, await page.locator('#eventList button').count() - 1);
        await page.locator('#eventList button').nth(target).click();
        assert.equal((await qa(page)).playing, false);
        results.push(`${viewport.width}/${scenario}`);
      }
      await page.selectOption('#scenario', 'timeout');
      await page.locator('#preAnr').check();
      await seek(page, 3500);
      assert.equal((await qa(page)).preNotified, true);
      assert.equal((await qa(page)).waiting, true);
      assert.equal(await page.locator('#preStatus').isVisible(), true);
      await page.locator('#preAnr').uncheck();
      assert.equal((await qa(page)).preNotified, false);
      await seek(page, 6500);
      assert.match(await page.locator('#sameAppCheck').textContent(), /token/);
      assert.equal(await page.locator('#wmsHop').evaluate(node => node.classList.contains('reached')), true);
      await layout(page, `${viewport.width}/timeout-report`);
      if (viewport.width === 1440 || viewport.width === 390) {
        const reportShot = path.join(os.tmpdir(), `no-focus-anr-report-${viewport.width}.png`);
        await page.locator('#studio').screenshot({ path: reportShot, style: '.topbar { visibility: hidden !important; }' });
        screenshots.push(reportShot);
      }
      await page.selectOption('#scenario', 'recovery');
      await page.locator('#readyRange').evaluate(input => { input.value = '8000'; input.dispatchEvent(new Event('input', { bubbles: true })); });
      await seek(page, 6500);
      assert.equal((await qa(page)).policyReached, true);
      assert.match(await page.locator('#scenarioOutcome').textContent(), /ANR/);
      await page.locator('#readyRange').evaluate(input => { input.value = '500'; input.dispatchEvent(new Event('input', { bubbles: true })); });
      await seek(page, 2000);
      assert.equal((await qa(page)).deadline, null);
      assert.equal((await qa(page)).focusedWindow, 'A / Window');
      await page.selectOption('#scenario', 'switch');
      await page.locator('#eventRange').evaluate(input => { input.value = '4000'; input.dispatchEvent(new Event('input', { bubbles: true })); });
      await seek(page, 9500);
      const switched = await qa(page);
      assert.equal(switched.waiting, false);
      assert.equal(switched.timedOut, false);
      assert.equal(switched.deadline, null);
      await page.selectOption('#scenario', 'timeout');
      await page.locator('#eventRange').evaluate(input => { input.value = '1000'; input.dispatchEvent(new Event('input', { bubbles: true })); });
      await page.locator('#restartBtn').click();
      assert.equal((await qa(page)).t, 0);
      await page.selectOption('#speed', '2');
      await page.locator('#playBtn').click();
      await page.waitForFunction(() => JSON.parse(document.documentElement.dataset.qa).t > 200);
      await page.locator('#playBtn').click();
      assert.equal((await qa(page)).playing, false);
      const paused = (await qa(page)).t;
      await page.waitForTimeout(100);
      assert.equal((await qa(page)).t, paused);
      await seek(page, 2200);
      await page.locator('#phaseRefs a').first().click();
      assert.equal(await page.locator('#source-arm').evaluate(node => node.open), true);
      await layout(page, `${viewport.width}/source-open`);
      const links = await page.locator('a[href^="#"]').evaluateAll(nodes => nodes.map(node => node.getAttribute('href')));
      for (const href of links) assert.equal(await page.locator(href).count(), 1, `invalid anchor ${href}`);
      await page.evaluate(() => document.querySelectorAll('details').forEach(node => { node.open = true; }));
      await layout(page, `${viewport.width}/all-sources-open`);
      await page.evaluate(() => document.querySelectorAll('details').forEach(node => { node.open = false; }));
      await page.locator('#themeBtn').click();
      assert.equal(await page.locator('html').getAttribute('data-theme'), 'light');
      await page.locator('#simulation').scrollIntoViewIfNeeded();
      await layout(page, `${viewport.width}/light`);
      const light = path.join(os.tmpdir(), `no-focus-anr-${viewport.width}-light.png`);
      await page.screenshot({ path: light });
      screenshots.push(light);
      if (viewport.width === 1440 || viewport.width === 390) {
        const scene = path.join(os.tmpdir(), `no-focus-anr-scene-${viewport.width}.png`);
        await page.locator('#studio').screenshot({ path: scene, style: '.topbar { visibility: hidden !important; }' });
        screenshots.push(scene);
      }
      await page.locator('#themeBtn').click();
      assert.equal(await page.locator('html').getAttribute('data-theme'), 'dark');
      await page.reload();
      assert.equal(await page.locator('html').getAttribute('data-theme'), 'dark');
      await page.close();
    }
    const motionPage = await browser.newPage({ viewport: { width: 1440, height: 1050 }, reducedMotion: 'no-preference' });
    motionPage.on('pageerror', error => errors.push(error.message));
    motionPage.on('request', request => { if (/^https?:/.test(request.url())) requests.push(request.url()); });
    await motionPage.goto(url);
    await seek(motionPage, 750);
    const arrivalStart = await motionPage.locator('#incoming .packet').boundingBox();
    assert.equal(await motionPage.locator('#incoming .packet').evaluate(node => getComputedStyle(node).opacity), '1');
    await seek(motionPage, 950);
    const arrivalEnd = await motionPage.locator('#incoming .packet').boundingBox();
    assert.ok(arrivalEnd.x > arrivalStart.x, 'arrival marker did not follow the playhead');
    await seek(motionPage, 1200);
    assert.equal(await motionPage.locator('#incoming .packet').evaluate(node => getComputedStyle(node).opacity), '0');
    assert.equal(await motionPage.locator('#pendingSlot').evaluate(node => node.classList.contains('occupied')), true);
    await motionPage.selectOption('#scenario', 'recovery');
    await seek(motionPage, 3450);
    const deliveryStart = await motionPage.locator('#outgoing .packet').boundingBox();
    assert.equal(await motionPage.locator('#outgoing .packet').evaluate(node => getComputedStyle(node).opacity), '1');
    await seek(motionPage, 3750);
    const deliveryEnd = await motionPage.locator('#outgoing .packet').boundingBox();
    assert.ok(deliveryEnd.x > deliveryStart.x, 'delivery marker did not follow the playhead');
    await seek(motionPage, 4000);
    assert.equal(await motionPage.locator('#outgoing .packet').evaluate(node => getComputedStyle(node).opacity), '0');
    await motionPage.close();
    assert.deepEqual(errors, []);
    assert.deepEqual(requests, [], 'the offline artifact loaded external assets');
    const report = { ok: true, artifact: 'index.html', artifactSha256: require('node:crypto').createHash('sha256').update(fs.readFileSync(path.join(root, 'index.html'))).digest('hex'),
      browser: await browser.version(), viewports: [1440, 768, 390, 360], scenarioChecks: results.length,
      checks: ['scenario outcomes', 'step navigation', 'event list', 'scrubbing', 'pre-ANR toggle', 'ready timing', 'event timing', 'app switch before event', 'play/pause', 'speed', 'playhead-bound packet motion', 'source anchors', 'theme persistence', 'text bounds', 'offline requests'],
      screenshots, pageErrors: errors, externalRequests: requests,
      limits: ['local illustrative model only', 'no Android device or vendor behavior validation', 'no actual ANR dialog validation', 'article body unavailable'] };
    fs.writeFileSync(path.join(root, 'data/browser-verification.json'), JSON.stringify(report, null, 2) + '\n');
    console.log(`PASS ${results.length} viewport/scenario cases; controls, anchors, themes, overflow and offline loading; ${screenshots.length} screenshots.`);
    console.log(screenshots.join('\n'));
  } finally { await browser.close(); }
}

main().catch(error => { console.error(error); process.exitCode = 1; });
