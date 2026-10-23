const assert = require('node:assert/strict');
const os = require('node:os');
const path = require('node:path');
const { pathToFileURL } = require('node:url');
// Use an existing Playwright installation; no page dependency is added.
const { chromium } = require(process.env.PLAYWRIGHT_MODULE || 'playwright-core');

async function main() {
  const browser = await chromium.launch({ headless: true,
    ...(process.env.CHROME_PATH ? { executablePath: process.env.CHROME_PATH } : {}) });
  const errors = [];
  let checks = 0;
  try {
    for (const viewport of [{ width: 1440, height: 1000 }, { width: 390, height: 844 }]) {
      const page = await browser.newPage({ viewport, reducedMotion: 'reduce' });
      page.on('pageerror', error => errors.push(error.message));
      const url = pathToFileURL(path.join(__dirname, '../index.html')).href;
      await page.goto(url + '?pause=1&zoom=fit&theme=light');
      await page.waitForFunction(() => document.documentElement.hasAttribute('data-qa'));
      const scenarios = await page.locator('#presetSeg button:not([disabled])').evaluateAll(buttons =>
        buttons.map(button => button.dataset.preset));
      for (const key of scenarios) {
        await page.locator(`#presetSeg [data-preset="${key}"]`).click();
        for (const mode of ['time', 'step', 'proc']) {
          await page.locator(`#modeSeg [data-mode="${mode}"]`).click();
          for (const focus of [0, 1, 2]) {
            const control = page.locator(`#focusSeg [data-focus="${focus}"]`);
            if (await control.isDisabled()) continue;
            await control.click();
            const qa = await page.evaluate(() => JSON.parse(document.documentElement.dataset.qa));
            assert.equal(qa.preset, key);
            assert.equal(qa.mode, mode);
            assert.equal(qa.focus, focus);
            assert.equal(qa.stages, 8);
            assert.equal(qa.snippets, 32);
            assert.ok(qa.blocks > 0);
            await page.locator('#nextStep').click();
            await page.locator('#prevStep').click();
            await page.locator('#scrub').evaluate(input => {
              input.value = String(Number(input.max) * 0.7);
              input.dispatchEvent(new Event('input', { bubbles: true }));
            });
            assert.equal(await page.locator('#slots .slot').count(), 3);
            const clipped = await page.locator('.pp').evaluate(panel => {
              const bounds = panel.getBoundingClientRect();
              return [...panel.querySelectorAll('h3, p, li, .pp-hint, .pp-packet')]
                .filter(element => element.scrollWidth > element.clientWidth + 2 ||
                  element.getBoundingClientRect().right > bounds.right + 2)
                .map(element => element.textContent);
            });
            assert.deepEqual(clipped, [], `${key}/${mode}/${focus}: clipped panel text`);
            checks++;
          }
        }
      }
      await page.locator('#presetSeg [data-preset="uicont"]').click();
      await page.locator('.fp-params summary').click();
      await page.locator('label:has(#backpressureToggle)').click();
      assert.equal(await page.locator('#backpressureToggle').isChecked(), false);
      assert.ok(await page.evaluate(() => JSON.parse(document.documentElement.dataset.qa).verdicts.includes('dropped')));
      await page.locator('label:has(#backpressureToggle)').click();
      assert.equal(await page.locator('#backpressureToggle').isChecked(), true);
      assert.ok(await page.evaluate(() => !JSON.parse(document.documentElement.dataset.qa).verdicts.includes('dropped')));
      await page.locator('#presetSeg [data-preset="gpu"]').click();
      await page.locator('label:has(#unsignaledToggle)').click();
      assert.equal(await page.locator('#unsignaledToggle').isChecked(), false);
      assert.equal(await page.evaluate(() => JSON.parse(document.documentElement.dataset.qa).verdicts[1]), 'late');
      await page.locator('label:has(#unsignaledToggle)').click();
      assert.equal(await page.locator('#unsignaledToggle').isChecked(), true);
      assert.equal(await page.evaluate(() => JSON.parse(document.documentElement.dataset.qa).verdicts[1]), 'ontime');

      for (const mode of ['time', 'step', 'proc']) {
        await page.locator('#presetSeg [data-preset="relay"]').click();
        await page.locator(`#modeSeg [data-mode="${mode}"]`).click();
        await page.locator('#restartBtn').click();
        await page.waitForFunction(() => Number(document.getElementById('timeNow').textContent) > 1);
        await page.locator('#playBtn').click();
      }
      await page.locator('#presetSeg [data-preset="relay"]').click();
      await page.locator('#modeSeg [data-mode="time"]').click();
      await page.locator('#zoomSeg [data-zoom="fit"]').click();
      await page.locator('.fp-params summary').click();
      await page.locator('#anatomy').scrollIntoViewIfNeeded();
      const screenshot = path.join(os.tmpdir(), `vsync-pipeline-${viewport.width}.png`);
      await page.screenshot({ path: screenshot });
      await page.locator('#studio').screenshot({ path: path.join(os.tmpdir(), `vsync-pipeline-studio-${viewport.width}.png`) });
      const overflow = await page.evaluate(() => ({ page: document.documentElement.scrollWidth,
        viewport: window.innerWidth,
        headings: [...document.querySelectorAll('h2, #ppTitle')].filter(element =>
          element.scrollWidth > element.clientWidth + 2).map(element => element.textContent) }));
      assert.ok(overflow.page <= overflow.viewport + 2, JSON.stringify(overflow));
      assert.deepEqual(overflow.headings, []);
      await page.locator('#themeBtn').click();
      await page.locator('#themeBtn').click();
      await page.close();
      console.log(`viewport=${viewport.width} screenshot=${screenshot}`);
    }
    assert.deepEqual(errors, []);
    console.log(`PASS: ${checks} preset/mode/focus cases, policy controls, step navigation, scrubbing, themes and overflow`);
  } finally {
    await browser.close();
  }
}
main().catch(error => { console.error(error); process.exitCode = 1; });
