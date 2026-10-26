const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { pathToFileURL, fileURLToPath } = require('node:url');
const { chromium } = require(process.env.PLAYWRIGHT_MODULE || 'playwright-core');
const model = require('../src/model.js');
const root = path.join(__dirname, '..');
const stages = fs.readdirSync(path.join(root, 'data')).filter(file => file.endsWith('stages.json')).sort()
  .flatMap(file => JSON.parse(fs.readFileSync(path.join(root, 'data', file), 'utf8')).stages);
const screenshots = path.join(os.tmpdir(), 'remoteviews-qa');

async function main() {
  fs.mkdirSync(screenshots, { recursive: true });
  const browser = await chromium.launch({ headless: true,
    ...(process.env.CHROME_PATH ? { executablePath: process.env.CHROME_PATH } : {}) });
  const errors = [];
  const network = [];
  const report = { scope: 'isolated local browser; not Android runtime or physical display',
    scenarioSteps: 0, sourceTopics: 0, viewports: [], screenshots: [] };
  try {
    for (const viewport of [{ width: 1440, height: 1000 }, { width: 1920, height: 1080 },
      { width: 390, height: 844 }, { width: 320, height: 760 }]) {
      const page = await browser.newPage({ viewport, reducedMotion: 'reduce' });
      page.on('pageerror', error => errors.push(error.message));
      page.on('request', request => { if (/^https?:/.test(request.url())) network.push(request.url()); });
      await page.goto(pathToFileURL(path.join(root, 'index.html')).href);
      await page.waitForFunction(() => Boolean(document.documentElement.dataset.qa));
      assert.equal(await page.locator('#scenario').inputValue(), 'remoteviews');
      assert.equal(await page.locator('.actor').count(), 3);
      const firstViewport = path.join(screenshots, `first-${viewport.width}.png`);
      await page.screenshot({ path: firstViewport });
      report.screenshots.push(firstViewport);
      for (const scenario of model.scenarios) {
        await page.locator('#scenario').selectOption(scenario.id);
        assert.equal(await page.locator('#prev').isDisabled(), true);
        for (let index = 0; index < scenario.events.length; index++) {
          await page.locator('#seek').evaluate((input, value) => {
            input.value = value;
            input.dispatchEvent(new Event('input', { bubbles: true }));
          }, String(index));
          const expected = model.snapshot(scenario.id, index);
          const actual = await page.evaluate(() => JSON.parse(document.documentElement.dataset.qa));
          for (const key of ['index', 'applied', 'callback', 'error', 'fallback', 'attached', 'screen', 'tree',
            'widgetId', 'bound', 'hostViewCreated', 'workspaceAttached', 'initialContent', 'listening', 'renderSubmitted', 'composition']) {
            assert.deepEqual(actual[key], expected[key], `${scenario.id}/${index}: ${key}`);
          }
          assert.equal(await page.locator('#events .active').count(), 1);
          assert.equal(await page.locator('.actor.active').getAttribute('data-actor'), expected.current.actor);
          assert.equal(await page.locator('[data-topic]:visible').getAttribute('data-topic'), expected.current.stage);
          assert.equal(await page.locator('#execution-thread').textContent(), expected.current.thread);
          assert.equal(await page.locator('#execution-handoff').textContent(), expected.current.handoff);
          report.scenarioSteps++;
        }
        assert.equal(await page.locator('#next').isDisabled(), true);
        await page.locator('#prev').click();
        await page.locator('#next').click();
        await page.locator('#events button').first().click();
        assert.equal(await page.locator('#seek').inputValue(), '0');
      }

      await page.locator('#scenario').selectOption('click');
      const attached = model.scenario('click').events.findIndex(step => step.id === 'attached');
      await page.locator(`#events [data-step="${attached}"]`).click();
      await page.locator('.widget-action').click();
      assert.equal(await page.evaluate(() => JSON.parse(document.documentElement.dataset.qa).event), 'clicked');
      await page.locator('#next').click();
      assert.equal(await page.locator('.widget-action').isDisabled(), true);

      for (const stage of stages) {
        await page.locator(`#stage-nav [data-stage="${stage.id}"]`).click();
        assert.equal(await page.locator('[data-topic]:visible').getAttribute('data-topic'), stage.id);
        const clipped = await page.locator('h1,h2,h3,p,dd,.stage-nav a,.event-name,.payload-meta,.widget-title,.actions-list li').evaluateAll(elements =>
          elements.filter(element => element.getClientRects().length && element.scrollWidth > element.clientWidth + 2)
            .map(element => ({ tag: element.tagName, text: element.textContent, width: element.clientWidth, scroll: element.scrollWidth })));
        assert.deepEqual(clipped, [], `clipped text at ${viewport.width}/${stage.id}`);
        report.sourceTopics++;
      }

      const links = await page.locator('a').evaluateAll(elements => elements.map(element => element.href));
      for (const href of links) {
        const url = new URL(href);
        if (url.hash) {
          assert.equal(await page.evaluate(hash => Boolean(document.getElementById(decodeURIComponent(hash.slice(1)))), url.hash), true, href);
        } else if (url.protocol === 'file:') assert.ok(fs.existsSync(fileURLToPath(url)), href);
        else assert.fail(`Unexpected external link: ${href}`);
      }
      assert.ok(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth + 2), `page overflow at ${viewport.width}px`);

      await page.locator('#scenario').selectOption('async');
      await page.locator('#play').click();
      await page.waitForFunction(() => JSON.parse(document.documentElement.dataset.qa).index > 0);
      await page.locator('#play').click();
      const paused = await page.locator('#seek').inputValue();
      await page.waitForTimeout(1100);
      assert.equal(await page.locator('#seek').inputValue(), paused);
      await page.locator('#reset').click();
      assert.equal(await page.locator('#seek').inputValue(), '0');
      await page.locator('#play').focus();
      await page.keyboard.press('Space');
      assert.equal(await page.locator('#play').getAttribute('aria-label'), '暂停');
      await page.keyboard.press('Space');
      assert.equal(await page.locator('#play').getAttribute('aria-label'), '播放');

      await page.locator('#scenario').selectOption('partial');
      await page.locator('#seek').evaluate(input => { input.value = input.max; input.dispatchEvent(new Event('input', { bubbles: true })); });
      await page.locator('#explorer').scrollIntoViewIfNeeded();
      for (const dark of [false, true]) {
        await page.locator('#theme').setChecked(dark);
        const file = path.join(screenshots, `process-${viewport.width}-${dark ? 'dark' : 'light'}.png`);
        await page.locator('#explorer').screenshot({ path: file });
        report.screenshots.push(file);
      }
      await page.locator('#theme').uncheck();
      await page.locator('#scenario').selectOption('launcher');
      await page.locator('#seek').evaluate(input => { input.value = input.max; input.dispatchEvent(new Event('input', { bubbles: true })); });
      const launcherScreenshot = path.join(screenshots, `launcher-${viewport.width}.png`);
      await page.locator('#explorer').screenshot({ path: launcherScreenshot });
      report.screenshots.push(launcherScreenshot);
      await page.locator('#stage-nav [data-stage="launcher-workspace"]').click();
      const sourceScreenshot = path.join(screenshots, `source-${viewport.width}.png`);
      await page.screenshot({ path: sourceScreenshot });
      report.screenshots.push(sourceScreenshot);
      report.viewports.push(viewport);
      await page.close();
    }
    assert.deepEqual(errors, []);
    assert.deepEqual(network, []);
    report.errors = errors;
    report.externalRequests = network;
    report.ok = true;
    fs.writeFileSync(path.join(root, 'data/browser-verification.json'), JSON.stringify(report, null, 2) + '\n');
    console.log(JSON.stringify(report, null, 2));
  } finally {
    await browser.close();
  }
}

main().catch(error => { console.error(error); process.exitCode = 1; });
