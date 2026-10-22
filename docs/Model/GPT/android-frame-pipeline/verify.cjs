#!/usr/bin/env node
'use strict';

// Optional browser QA only. The delivered HTML has no package/runtime dependencies.
// NODE_PATH=/tmp/android-frame-pipeline-test/node_modules node docs/android-frame-pipeline/verify.cjs
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { pathToFileURL } = require('node:url');
const vm = require('node:vm');
const { chromium } = require('playwright');

const artifactDir = process.env.ARTIFACT_DIR || path.join(os.tmpdir(), 'android-frame-pipeline-qa');
fs.mkdirSync(artifactDir, { recursive: true });
const htmlPath = path.join(__dirname, 'index.html');
const html = fs.readFileSync(htmlPath, 'utf8');
new vm.Script(html.match(/<script>([\s\S]*?)<\/script>/)[1]);

async function snapshot(page) {
  return page.evaluate(() => window.frameLab.snapshot());
}
async function scrub(page, value) {
  await page.locator('#scrubber').evaluate((el, v) => {
    el.value = String(v);
    el.dispatchEvent(new Event('input', { bubbles: true }));
  }, value);
}
async function selectEvent(page, id) {
  const s = await snapshot(page);
  const index = s.events.findIndex(e => e.id === id);
  assert(index >= 0, `Missing event: ${id}`);
  await page.locator('#event-list .event-list-item').nth(index).click();
}
async function timelineGeometry(page) {
  return page.locator('#timeline-scroll').evaluate(container => {
    const rows = [...container.querySelectorAll('.lane')];
    const clipped = [];
    const collisions = [];
    for (const row of rows) {
      const tracks = new Map();
      for (const event of row.querySelectorAll('.event')) {
        if (event.scrollWidth > event.clientWidth + 1) clipped.push(event.dataset.event);
        const top = event.offsetTop;
        const items = tracks.get(top) || [];
        items.push({ id: event.dataset.event, left: event.offsetLeft, right: event.offsetLeft + event.offsetWidth });
        tracks.set(top, items);
      }
      for (const items of tracks.values()) {
        items.sort((a, b) => a.left - b.left);
        for (let i = 1; i < items.length; i++) {
          if (items[i].left < items[i - 1].right + 6) collisions.push(`${items[i - 1].id}/${items[i].id}`);
        }
      }
    }
    return { viewport: container.clientWidth, content: container.scrollWidth, clipped, collisions };
  });
}

(async () => {
  const browser = await chromium.launch({
    executablePath: process.env.CHROME_PATH || '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
    headless: true,
  });
  try {
    const page = await browser.newPage({ viewport: { width: 1600, height: 1160 }, deviceScaleFactor: 1 });
    const errors = [];
    const network = [];
    const checks = [];
    page.on('pageerror', error => errors.push(error.message));
    page.on('request', request => { if (/^https?:/.test(request.url())) network.push(request.url()); });
    await page.goto(pathToFileURL(htmlPath).href);
    assert.equal((await snapshot(page)).eventCount, 45);
    assert.equal(await page.locator('#callback-queue .callback').count(), 5);
    assert.equal(await page.locator('#journey-grid .journey-step').count(), 8);
    assert.equal(await page.locator('#journey-grid .journey-step[aria-current="step"]').count(), 1);
    assert(await page.locator('#journey-code-list .journey-code-item').count() > 0);
    assert.match(await page.locator('#journey-code-detail').textContent(), /源码执行与状态变化/);
    assert.equal(await page.locator('#scene-map .scene-node').count(), 8);
    assert.equal(await page.locator('#scene-map .scene-node.current').count(), 1);
    assert.equal(await page.locator('#scene-map .scene-domain').count(), 3);
    assert(await page.locator('.scene').evaluate(el => el.getBoundingClientRect().top < document.querySelector('.journey').getBoundingClientRect().top));
    assert.equal(await page.locator('#scene-handoff .transfer').count(), 6);
    assert.equal(await page.locator('#timeline-details').evaluate(el => el.open), false);
    checks.push('Offline file:// startup; 45 default events; five callback queues');

    // Every scenario / composition / refresh combination must preserve causal invariants.
    let combinations = 0;
    for (const scenario of ['layout', 'invalidate', 'ui-late', 'gpu-late', 'sf-late']) {
      for (const composition of ['mixed', 'device', 'client']) {
        for (const refresh of ['60', '90', '120']) {
          await page.selectOption('#scenario', scenario);
          await page.selectOption('#composition', composition);
          await page.selectOption('#refresh', refresh);
          const s = await snapshot(page);
          const get = id => s.events.find(e => e.id === id);
          assert(Math.abs(s.periodMs - 1000 / Number(refresh)) < 1e-8);
          assert.equal(new Set(s.events.map(e => e.id)).size, s.eventCount);
          assert(s.events.every((e, i) => e.start >= 0 && e.end > e.start && e.end < s.duration && (!i || e.start >= s.events[i - 1].start)));
          assert.equal(s.target, scenario.endsWith('late') ? 3 : 2);
          assert.equal(!!get('measure'), scenario !== 'invalidate');
          assert.equal(!!get('layout'), scenario !== 'invalidate');
          assert.equal(!!get('sfGpu'), composition !== 'device');
          assert(get('queue').start < get('gpuSignal').start, 'Queue must illustrate asynchronous GPU completion');
          assert(get('queue').end <= get('blastAcquire').start);
          assert(get('blastAcquire').end <= get('transaction').start);
          assert(get('gpuSignal').start < get('apply').start);
          assert(get('latch').end <= get('composite').start);
          assert(get('rtSync').end <= get('syncUI').end);
          assert(get('syncUI').end <= get('commitCallback').start);
          assert(get('commitCallback').start < get('gpuSignal').start);
          assert(get('present').end < get('presentSignal').start);
          assert.equal(get('presentSignal').start, s.target);
          if (composition === 'client') assert.equal(get('releaseSignal').start, get('release').start);
          for (const [a, b] of [['input', 'animation'], ['animation', 'insets'], ['insets', 'traversal'], ['traversal', 'commitCallback']]) assert(get(a).end <= get(b).start);
          await scrub(page, 1000);
          const end = await snapshot(page);
          assert(Math.abs(end.time - end.duration) < 1e-8, 'Range input must not reset itself before reading the value');
          assert.equal(await page.locator('#screen-state').textContent(), 'F 已完整扫描');
          assert.equal(await page.locator('#buffer-current').getAttribute('data-state'), 'ACQUIRED');
          assert.equal(await page.locator('#buffer-old').getAttribute('data-state'), 'FREE');
          combinations++;
        }
      }
    }
    checks.push(`${combinations} scenario × composition × refresh combinations; causal timing, fence, callback and final-state checks`);

    await page.selectOption('#scenario', 'layout');
    await page.selectOption('#composition', 'mixed');
    await page.selectOption('#refresh', '60');
    for (let phase = 0; phase < 8; phase++) {
      await page.locator('#journey-grid .journey-step').nth(phase).click();
      const selected = await snapshot(page);
      assert.equal(selected.events.find(e => e.id === selected.eventId).phase, phase);
      assert.equal(await page.locator('#journey-grid .journey-step[aria-current="step"]').count(), 1);
      assert.match(await page.locator('#journey-marker').textContent(), new RegExp(`^0${phase + 1} / 08$`));
      assert.equal(await page.locator('#journey-current').textContent(), await page.locator('#event-title').textContent());
      const currentPhaseEvents = selected.events.filter(e => e.phase === phase);
      assert.equal(await page.locator('#journey-code-list .journey-code-item').count(), currentPhaseEvents.length);
      assert.equal(await page.locator('#journey-code-list [aria-current="step"]').count(), 1);
      assert(await page.locator('#journey-code-detail .journey-code-sources a').count() > 0);
      await page.locator('#journey-code-list .journey-code-item').last().click();
      assert.equal((await snapshot(page)).eventId, currentPhaseEvents.at(-1).id);
    }
    await page.selectOption('#scenario', 'invalidate');
    assert.match(await page.locator('#journey-grid .journey-step').nth(3).textContent(), /跳过 Measure \/ Layout/);
    await page.selectOption('#composition', 'device');
    assert.match(await page.locator('#journey-grid .journey-step').nth(6).textContent(), /全 DEVICE/);
    await page.selectOption('#scenario', 'layout');
    await page.selectOption('#composition', 'mixed');
    await page.locator('#journey-grid .journey-step').nth(2).click();
    assert.equal(await page.locator('#journey-grid .journey-step').nth(2).locator('.step-visual .lit').count(), 0);
    await selectEvent(page, 'insets');
    assert.equal(await page.locator('#journey-grid .journey-step').nth(2).locator('.step-visual .lit').count(), 1);
    await selectEvent(page, 'visible');
    assert.notEqual(await page.locator('#journey-grid .screen-mini').evaluate(el => el.style.getPropertyValue('--scan')), '0%');
    assert.equal(await page.locator('#scene-map .scene-node.current').getAttribute('data-scene'), 'display');
    await selectEvent(page, 'queue');
    assert.equal(await page.locator('#scene-map .scene-node.current').getAttribute('data-scene'), 'rt');
    assert.match(await page.locator('#scene-title').textContent(), /queueBuffer/);
    assert.match(await page.locator('#scene-symbol').textContent(), /queueBuffer/);
    assert.match(await page.locator('#scene-handoff').textContent(), /acquire fence/);
    await selectEvent(page, 'sfCommit');
    assert.equal(await page.locator('#scene-map .scene-node.current').getAttribute('data-scene'), 'sf');
    assert.equal(await page.locator('.scene-domain.active').getAttribute('data-domain'), 'sf');
    await page.locator('#scene-map .scene-node[data-scene="display"]').click();
    assert.equal(await page.locator('#scene-map .scene-node.current').getAttribute('data-scene'), 'display');
    await page.locator('#timeline-details > summary').click();
    assert.equal(await page.locator('#timeline-details').evaluate(el => el.open), true);
    checks.push('Eight-stage journey, in-flow code details, actor-scene playback and optional timing view');
    const wideTimeline = await timelineGeometry(page);
    assert(wideTimeline.content > wideTimeline.viewport * 2, 'The timeline must provide substantial horizontal scroll width');
    assert.deepEqual(wideTimeline.clipped, [], 'Event text must fit inside each label');
    assert.deepEqual(wideTimeline.collisions, [], 'Labels in one track must not overlap');
    assert.match(await page.locator('[data-event="transaction"]').textContent(), /buffer.*fence/);
    await page.locator('#timeline-pan').evaluate(el => { el.value = '1000'; el.dispatchEvent(new Event('input', { bubbles: true })); });
    assert.equal(await page.locator('#follow').getAttribute('aria-pressed'), 'false');
    const panned = await page.locator('#timeline-scroll').evaluate(el => ({ left: el.scrollLeft, max: el.scrollWidth - el.clientWidth }));
    assert(Math.abs(panned.left - panned.max) <= 2, 'Pan slider must reach the end of the timeline');
    assert.equal(await page.locator('#timeline-pan-value').textContent(), '100%');
    await page.locator('#follow').click();
    checks.push('Wide horizontal timeline, complete labels, non-overlapping label tracks and end-to-end pan control');
    const initial = await snapshot(page);
    for (const e of initial.events) {
      await selectEvent(page, e.id);
      assert.equal((await snapshot(page)).eventId, e.id);
      assert((await page.locator('#event-title').textContent()).length > 0);
      assert(await page.locator('#event-sources a').count() > 0);
      assert.equal(await page.locator('#journey-code-detail h4').textContent(), await page.locator('#event-title').textContent());
      assert.equal(await page.locator('#journey-code-list [aria-current="step"]').count(), 1);
      assert(await page.locator('#journey-code-detail ol li').count() > 0);
      assert(await page.locator('#journey-code-detail .journey-code-sources a').count() > 0);
    }
    await selectEvent(page, 'queue');
    await page.locator('#journey-code-detail .journey-code-handoff div').last().locator('button').first().click();
    assert.equal((await snapshot(page)).eventId, 'blastAcquire');
    checks.push('All 45 event navigation targets, in-flow code details, causal handoffs and per-event source links');

    await selectEvent(page, 'syncUI');
    assert.match(await page.locator('#view-status').textContent(), /等待 RT/);
    await selectEvent(page, 'commitCallback');
    assert.match(await page.locator('#view-status').textContent(), /同步完成/);
    await selectEvent(page, 'queue');
    assert.match(await page.locator('#acquire-label').textContent(), /未 signal/);
    await selectEvent(page, 'gpuSignal');
    assert.match(await page.locator('#acquire-label').textContent(), /已 signal/);
    checks.push('UI/RT wait boundary and unsignaled → signaled acquire fence state');

    await page.locator('#event-search').fill('BLAST');
    assert(await page.locator('#event-list .event-list-item:visible').count() > 0);
    await page.locator('#event-search').fill('unmatched-zzzz');
    assert.equal(await page.locator('#event-list .event-list-item:visible').count(), 0);
    assert(await page.locator('#no-results').isVisible());
    await page.locator('#event-search').fill('');
    await page.locator('#open-sources').click();
    assert(await page.locator('#sources-dialog').isVisible());
    const localLinks = await page.locator('#source-catalog a[href^="../../"]').evaluateAll(links => links.map(a => a.getAttribute('href')));
    for (const link of localLinks) assert(fs.existsSync(path.resolve(__dirname, link)), `Missing local source ${link}`);
    await page.keyboard.press('Escape');
    await page.locator('#open-help').click();
    assert(await page.locator('#help-dialog').isVisible());
    await page.keyboard.press('Escape');
    checks.push(`Search, dialogs and ${localLinks.length} local source paths`);

    await page.locator('h1').click();
    await page.keyboard.press('Home');
    assert.equal((await snapshot(page)).eventId, 'invalidate');
    await page.keyboard.press('ArrowRight');
    assert.equal((await snapshot(page)).eventId, 'requestLayout');
    await page.keyboard.press('End');
    assert.equal((await snapshot(page)).eventId, 'visible');
    await page.locator('#reset').click();
    assert.equal((await snapshot(page)).time, 0);
    await page.locator('#focus').click();
    assert.equal(await page.locator('#focus').getAttribute('aria-pressed'), 'true');
    await page.locator('#focus').click();
    checks.push('Keyboard stepping, reset and focus mode');

    // Simulated browser time verifies animation rather than only static rendering.
    await page.clock.install();
    await page.selectOption('#speed', '4');
    await page.locator('#play').click();
    await page.clock.runFor(1600);
    assert((await snapshot(page)).time > 0);
    assert((await snapshot(page)).eventId !== 'invalidate');
    await page.locator('#play').click();
    const paused = (await snapshot(page)).time;
    await page.clock.runFor(500);
    assert.equal((await snapshot(page)).time, paused);
    await page.locator('#reset').click();
    await page.selectOption('#mode', 'continuous');
    await page.locator('#play').click();
    await page.clock.runFor(5000);
    const completed = await snapshot(page);
    assert.equal(completed.playing, false);
    assert.equal(completed.time, completed.duration);
    checks.push('Explain playback advances; pause freezes; continuous playback reaches full scanout and stops');

    await page.locator('#reset').click();
    await selectEvent(page, 'rtSync');
    assert(await page.locator('#timeline-connectors path[marker-end]').count() > 0);
    await page.locator('#zoom').evaluate(el => { el.value = '3'; el.dispatchEvent(new Event('input', { bubbles: true })); });
    assert(await page.locator('#timeline-connectors path[marker-end]').count() > 0);
    await page.locator('#zoom').evaluate(el => { el.value = '1.4'; el.dispatchEvent(new Event('input', { bubbles: true })); });
    await selectEvent(page, 'sfGpu');
    await page.locator('.workspace').scrollIntoViewIfNeeded();
    await page.screenshot({ path: path.join(artifactDir, 'desktop.png'), fullPage: true });
    const desktopOverflow = await page.evaluate(() => document.documentElement.scrollWidth > innerWidth);
    assert(!desktopOverflow);
    await page.setViewportSize({ width: 390, height: 844 });
    await page.screenshot({ path: path.join(artifactDir, 'mobile.png'), fullPage: true });
    assert(!await page.evaluate(() => document.documentElement.scrollWidth > innerWidth));
    const mobileTimeline = await timelineGeometry(page);
    assert.deepEqual(mobileTimeline.clipped, []);
    assert.deepEqual(mobileTimeline.collisions, []);
    await page.locator('#timeline-pan').evaluate(el => { el.value = '1000'; el.dispatchEvent(new Event('input', { bubbles: true })); });
    const lastEventVisible = await page.locator('#timeline-scroll').evaluate(container => {
      const edge = container.getBoundingClientRect();
      const last = container.querySelector('[data-event="visible"]').getBoundingClientRect();
      return last.left < edge.right && last.right <= edge.right + 2;
    });
    assert(lastEventVisible, 'The final event must be reachable on mobile');
    await page.setViewportSize({ width: 768, height: 1024 });
    assert(!await page.evaluate(() => document.documentElement.scrollWidth > innerWidth));
    checks.push('Cross-lane dependency arrows, zoom and 1600 / 768 / 390px responsive layouts without document overflow');

    const reduced = await browser.newPage({ reducedMotion: 'reduce', viewport: { width: 1280, height: 900 } });
    await reduced.goto(pathToFileURL(htmlPath).href);
    assert.equal((await snapshot(reduced)).playing, false);
    await reduced.close();
    assert.deepEqual(errors, []);
    assert.deepEqual(network, []);
    checks.push('Reduced-motion starts paused; no page errors; zero HTTP(S) runtime requests');
    const report = { passed: true, combinations, checks, pageErrors: errors, runtimeNetworkRequests: network, artifactDir, browser: await browser.version() };
    fs.writeFileSync(path.join(artifactDir, 'report.json'), JSON.stringify(report, null, 2) + '\n');
    console.log(JSON.stringify(report, null, 2));
  } finally {
    await browser.close();
  }
})().catch(error => { console.error(error); process.exitCode = 1; });
