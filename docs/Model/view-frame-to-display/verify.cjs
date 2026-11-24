/* Run against the delivered file:// page, without a server or network. */
const { strict: assert } = require('node:assert');
const { createHash } = require('node:crypto');
const { readFileSync } = require('node:fs');
const path = require('node:path');
const { pathToFileURL } = require('node:url');
const { chromium } = require(process.env.PLAYWRIGHT_MODULE || 'playwright-core');
const url = pathToFileURL(path.join(__dirname, 'index.html')).href;
const report = JSON.parse(readFileSync(path.join(__dirname, 'source-verification.json')));
async function main() {
  const browser = await chromium.launch({ headless: true,
    ...(process.env.CHROME_PATH ? { executablePath: process.env.CHROME_PATH } : {}) });
  try {
    for (const width of [390, 1280]) {
      const ctx = await browser.newContext({ offline: true, viewport: { width, height: 900 } });
      const page = await ctx.newPage(), errors = [], external = [];
      page.on('pageerror', e => errors.push(e.message));
      page.on('console', m => { if (m.type() === 'error') errors.push(m.text()); });
      page.on('request', r => { if (/^https?:/.test(r.url())) external.push(r.url()); });
      await page.goto(url);
      const scan = await page.evaluate(() => ({
        ids: [...document.querySelectorAll('[id]')].map(el => el.id),
        missing: [...document.querySelectorAll('main a[href^="#"]')].filter(a => !document.getElementById(a.hash.slice(1))).map(a => a.hash),
        overflow: document.documentElement.scrollWidth > innerWidth,
        stages: document.querySelectorAll('.stage').length, factors: document.querySelectorAll('.factor').length,
        cases: document.querySelectorAll('.case').length,
        spaces: ['main','header','h1','figure','nav'].map(s=>{const x=document.querySelector(s),c=getComputedStyle(x);return [s,Math.round(x.getBoundingClientRect().height),c.rowGap,c.paddingBottom,c.maxWidth]})
      }));
      assert.deepEqual(scan.missing,[]); assert.equal(new Set(scan.ids).size,scan.ids.length); assert.equal(scan.overflow,false);
      assert(scan.stages && scan.factors && scan.cases);
      const snippets = await page.locator('.refitem').evaluateAll(els => els.map(el=>[el.id,el.querySelector('pre').textContent]));
      assert.equal(snippets.length,report.ranges);
      for(let i=0;i<snippets.length;i++){
        const [id,file,first,last,digest] = report.records[i], [actualID,raw] = snippets[i];
        assert.equal(id,actualID);
        const decoded=raw.split('\n').map((line,k)=>{const p=String(first+k).padStart(5)+'  ';assert(line.startsWith(p));return line.slice(p.length);});
        assert.equal(decoded.length,last-first+1);
        const source=readFileSync(path.join(__dirname,'../../..',file),'utf8').split(/\r?\n/).slice(first-1,last);
        assert.deepEqual(decoded,source, id);
        assert.equal(createHash('sha256').update(decoded.join('\n')+'\n').digest('hex'),digest);
      }
      for(const selector of ['.stage','.case','.factor']) {
        for(const el of await page.locator(selector).all()) {
          await el.evaluate(x=>x.open=true);
          assert.deepEqual(await el.evaluate(x=>[...x.querySelectorAll('p,h4,dt,dd,a,summary')].filter(y=>y.getBoundingClientRect().width&&y.getBoundingClientRect().right>innerWidth+1).map(y=>y.textContent)),[]);
          await el.evaluate(x=>x.open=false);
        }
      }
      await page.locator('.lane a[href="#s1"]').click();
      await page.waitForFunction(()=>document.getElementById('s1').open);
      const citation=page.locator('#s1 .refrow a').first();
      const evidenceID=await citation.getAttribute('href');
      await citation.click(); await page.waitForFunction(id=>document.querySelector(id).open,evidenceID,{timeout:4000});
      assert(await page.locator('#evidence').evaluate(e=>e.open));
      await page.locator('#where').selectOption('s7'); const found=await page.locator('.factor:visible').count();
      assert(found>0 && found<scan.factors);
      assert((await page.locator('.factor:visible').evaluateAll(es=>es.map(e=>e.dataset.stages.split(' ').includes('s7')))).every(Boolean));
      await page.locator('#query').fill('no-such-render-stage');assert.equal(await page.locator('.factor:visible').count(),0);
      await page.locator('#clear').click();assert.equal(await page.locator('.factor:visible').count(),scan.factors);
      await page.locator('#query').fill('纹理');assert(await page.locator('.factor:visible').count()>0);
      await page.goto(url+'#f1'); await page.waitForFunction(()=>{const e=document.getElementById('f1');return e.open&&!e.hidden;});
      assert.deepEqual(errors,[]); assert.deepEqual(external,[]);
      console.log(width+'px offline: links, text bounds, search, stage → source, deep link, '+snippets.length+' exact excerpts OK.',JSON.stringify(scan.spaces));
      await ctx.close();
    }
  } finally { await browser.close(); }
}
main().catch(e=>{console.error(e); process.exitCode=1});
