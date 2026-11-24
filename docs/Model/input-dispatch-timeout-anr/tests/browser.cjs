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
  const data = JSON.parse(fs.readFileSync(path.join(root, 'data/sources.json'), 'utf8'));
  const errors = [];
  const external = [];
  const screenshots = [];
  const checks = [];
  const qa = page => page.evaluate(() => JSON.parse(document.documentElement.dataset.qa));
  async function seek(page, value) {
    await page.locator('#scrub').evaluate((input, time) => {
      input.value = String(time);
      input.dispatchEvent(new Event('input', { bubbles: true }));
    }, value);
  }
  async function slider(page, selector, value) {
    await page.locator(selector).evaluate((input, value) => {
      input.value = String(value);
      input.dispatchEvent(new Event('input', { bubbles: true }));
    }, value);
  }
  async function layout(page, label) {
    const result = await page.evaluate(() => {
      const selectors = 'h1, h2, h3, .phase-copy p, .actor, .channel, .queue, .watch-strip, .deadline-clock, .verdict, .policy-path, .parameter-row, .transport, .scenario-bar, .source-entry summary';
      return { document: document.documentElement.scrollWidth, viewport: innerWidth,
        clipped: [...document.querySelectorAll(selectors)].filter(node => node.getClientRects().length && node.scrollWidth > node.clientWidth + 2)
          .map(node => ({ className: node.className || node.tagName, width: node.clientWidth, scroll: node.scrollWidth, text: node.textContent.slice(0, 140) })) };
    });
    assert.ok(result.document <= result.viewport + 2, `${label}: page overflow ${JSON.stringify(result)}`);
    assert.deepEqual(result.clipped, [], `${label}: text or controls clipped`);
  }
  async function screenshot(page, name, element) {
    const target = path.join(os.tmpdir(), `input-timeout-anr-${name}.png`);
    if (element) await page.locator(element).screenshot({ path: target, style: '.topbar { visibility: hidden !important; }' });
    else await page.screenshot({ path: target });
    screenshots.push(target);
  }
  function observe(page) {
    page.on('pageerror', error => errors.push(error.message));
    page.on('request', request => { if (/^https?:/.test(request.url())) external.push(request.url()); });
  }
  try {
    for (const viewport of [{ width: 1440, height: 1050 }, { width: 768, height: 1024 }, { width: 390, height: 844 }, { width: 360, height: 800 }]) {
      const page = await browser.newPage({ viewport, reducedMotion: 'reduce', colorScheme: 'dark' });
      observe(page);
      await page.goto(url);
      await page.waitForFunction(() => Boolean(document.documentElement.dataset.qa));
      assert.deepEqual((await qa(page)).wait, ['E1']);
      await layout(page, `${viewport.width}/initial`);
      await screenshot(page, `${viewport.width}-initial-dark`);
      assert.equal(await page.locator('.source-entry').count(), data.stages.length);
      assert.equal(await page.locator('.snippet').count(), data.stages.reduce((n, stage) => n + stage.snippets.length, 0));
      const scenarios = await page.locator('#scenario option').evaluateAll(nodes => nodes.map(node => node.value));
      assert.equal(scenarios.length, 8);
      for (const scenario of scenarios) {
        await page.selectOption('#scenario', scenario);
        assert.equal((await qa(page)).t, 0);
        await seek(page, 900);
        assert.deepEqual((await qa(page)).tracker, []);
        assert.deepEqual((await qa(page)).outbound, ['E1']);
        await seek(page, 1100);
        assert.deepEqual((await qa(page)).tracker, ['E1']);
        for (const time of [2800, 4300, 6500, 7750]) {
          await seek(page, time);
          const state = await qa(page);
          assert.equal(state.scenario, scenario);
          if (scenario === 'multi' && time === 2800) assert.deepEqual(state.tracker, ['E2', 'E3']);
          if (scenario === 'changed-timeout' && time === 4300) {
            assert.equal(state.incident.trigger, 'E2');
            assert.equal(state.incident.reasonEntry, 'E1');
            assert.deepEqual(state.tracker, []);
          }
          if (scenario === 'multi' && time === 7750) {
            assert.equal(state.responsive, false);
            assert.deepEqual(state.wait, ['E3']);
          }
          await layout(page, `${viewport.width}/${scenario}/${time}`);
        }
        await seek(page, (await qa(page)).duration);
        const end = await qa(page);
        assert.equal(end.completed, true);
        assert.equal(end.responsive, true);
        assert.deepEqual(end.wait, []);
        assert.equal(end.incidents, ['normal', 'unhandled'].includes(scenario) ? 0 : 1);
        if (scenario === 'unhandled') assert.equal(end.entries[0].handled, false);
        await page.locator('#prevStep').click();
        const previous = (await qa(page)).t;
        await page.locator('#nextStep').click();
        assert.ok((await qa(page)).t > previous);
        await page.locator('#eventList button').nth(1).click();
        assert.equal((await qa(page)).playing, false);
        checks.push(`${viewport.width}/${scenario}`);
      }
      await page.selectOption('#scenario', 'normal');
      await slider(page, '#delayRange', 6500);
      assert.match(await page.locator('#scenarioDesc').textContent(), /自定义参数/);
      assert.doesNotMatch(await page.locator('#scenarioDesc').textContent(), /均在截止前/);
      await slider(page, '#delayRange', 600);
      await slider(page, '#ackRange', 6000);
      await seek(page, 6200);
      assert.equal((await qa(page)).entries[0].phase, 'ack-flight');
      assert.equal((await qa(page)).incidents, 1);
      await slider(page, '#ackRange', 100);
      await seek(page, 2000);
      assert.equal((await qa(page)).incidents, 0);
      await page.selectOption('#scenario', 'handler');
      await slider(page, '#delayRange', 4900);
      await slider(page, '#ackRange', 200);
      await seek(page, 6100);
      assert.equal((await qa(page)).recovered, true);
      assert.equal((await qa(page)).policyReached, true);
      assert.ok((await qa(page)).incident.policyTime < 6100);
      await slider(page, '#ackRange', 100);
      await slider(page, '#delayRange', 1000);
      await page.selectOption('#timeout', '2000');
      await seek(page, 5000);
      assert.equal((await qa(page)).incidents, 0);
      await slider(page, '#delayRange', 6500);
      await seek(page, 4000);
      assert.equal((await qa(page)).incidents, 1);
      await page.selectOption('#scenario', 'looper');
      await page.locator('#restartBtn').click();
      await page.selectOption('#speed', '2');
      await page.locator('#playBtn').click();
      await page.waitForFunction(() => JSON.parse(document.documentElement.dataset.qa).t > 200);
      await page.locator('#playBtn').click();
      const paused = (await qa(page)).t;
      await page.waitForTimeout(120);
      assert.equal((await qa(page)).t, paused);
      await seek(page, 6500);
      assert.equal((await qa(page)).policyReached, true);
      assert.equal(await page.locator('#wmsHop').evaluate(node => node.classList.contains('reached')), true);
      if (viewport.width === 1440 || viewport.width === 390) await screenshot(page, `${viewport.width}-timeout`, '#studio');
      if (viewport.width === 1440 || viewport.width === 390) {
        await page.selectOption('#scenario', 'changed-timeout');
        await seek(page, 4500);
        await screenshot(page, `${viewport.width}-different-deadlines`, '#studio');
        await page.selectOption('#scenario', 'looper');
      }
      await seek(page, 2800);
      await page.locator('#phaseRef').click();
      assert.equal(await page.locator('#source-publish').evaluate(node => node.open), true);
      await page.evaluate(() => document.querySelectorAll('details').forEach(node => { node.open = true; }));
      await layout(page, `${viewport.width}/all-sources-open`);
      const anchors = await page.locator('a[href^="#"]').evaluateAll(nodes => nodes.map(node => node.getAttribute('href')));
      for (const anchor of anchors) assert.equal(await page.locator(anchor).count(), 1, `invalid anchor ${anchor}`);
      await page.evaluate(() => document.querySelectorAll('details').forEach(node => { node.open = false; }));
      await page.locator('#themeBtn').click();
      assert.equal(await page.locator('html').getAttribute('data-theme'), 'light');
      await page.locator('#simulation').scrollIntoViewIfNeeded();
      await layout(page, `${viewport.width}/light`);
      await screenshot(page, `${viewport.width}-light`);
      if (viewport.width === 1440 || viewport.width === 390) await screenshot(page, `${viewport.width}-scene`, '#studio');
      if (viewport.width === 1440 || viewport.width === 390) {
        await screenshot(page, `${viewport.width}-mechanism`, '#mechanism');
        await screenshot(page, `${viewport.width}-diagnosis`, '#diagnosis');
      }
      await page.locator('#themeBtn').click();
      await page.reload();
      assert.equal(await page.locator('html').getAttribute('data-theme'), 'dark');
      await page.close();
    }
    const motion = await browser.newPage({ viewport: { width: 1440, height: 1050 }, reducedMotion: 'no-preference' });
    observe(motion);
    await motion.goto(url);
    await seek(motion, 800);
    const sendStart = await motion.locator('#sendPacket').boundingBox();
    await seek(motion, 950);
    const sendEnd = await motion.locator('#sendPacket').boundingBox();
    assert.ok(sendEnd.x > sendStart.x);
    assert.equal(await motion.locator('#sendPacket').evaluate(node => getComputedStyle(node).opacity), '1');
    await seek(motion, 1200);
    assert.equal(await motion.locator('#sendPacket').evaluate(node => getComputedStyle(node).opacity), '0');
    await motion.selectOption('#scenario', 'ack-delay');
    await seek(motion, 2000);
    const ackStart = await motion.locator('#ackPacket').boundingBox();
    await seek(motion, 6500);
    const ackEnd = await motion.locator('#ackPacket').boundingBox();
    assert.ok(ackEnd.x < ackStart.x);
    assert.equal(await motion.locator('#ackPacket').evaluate(node => getComputedStyle(node).opacity), '1');
    await seek(motion, 8000);
    assert.equal(await motion.locator('#ackPacket').evaluate(node => getComputedStyle(node).opacity), '0');
    await motion.close();
    assert.deepEqual(errors, []);
    assert.deepEqual(external, [], 'offline artifact requested remote assets');
    const report = { ok: true, artifact: 'index.html', artifactSha256: crypto.createHash('sha256').update(fs.readFileSync(path.join(root, 'index.html'))).digest('hex'),
      browser: await browser.version(), viewports: [1440, 768, 390, 360], scenarioChecks: checks.length,
      checks: ['all scenario outcomes', 'queue and tracker state', 'multi-event ACK isolation', 'different deadline ordering', 'late recovery', 'timing controls', 'play/pause/speed', 'step navigation', 'seeking', 'playhead-bound bidirectional motion', 'source anchors', 'theme persistence', 'text bounds', 'offline loading'],
      screenshots, errors, externalRequests: external,
      limits: ['illustrative local model', 'no Android device or vendor validation', 'no actual ANR dialog or timing validation'] };
    fs.writeFileSync(path.join(root, 'data/browser-verification.json'), JSON.stringify(report, null, 2) + '\n');
    console.log(`PASS: ${checks.length} viewport/scenario cases, controls, bidirectional motion, sources, themes, bounds, and offline loading.`);
    console.log(screenshots.join('\n'));
  } finally { await browser.close(); }
}

main().catch(error => { console.error(error); process.exitCode = 1; });
