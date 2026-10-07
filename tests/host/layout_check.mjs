// Looks at every web page in a real browser the way a desktop and an iPhone would, and fails if the layout rules are broken.
//   NODE_PATH=<folder with 'ws' and puppeteer-core> node layout_check.mjs [screenshot-folder]
// Chrome: set CHROME_PATH, or install @sparticuz/chromium (it ships a headless Chromium inside the npm package).
// "Desktop" and "phone" are real browser settings (mouse vs touch-only), not just window sizes:
//   desktop: pointer:fine + hover:hover   ->  the iPhone bezel must appear, 430px wide
//   iPhone : pointer:coarse + hover:none  ->  NO bezel at all (portrait or sideways), nothing wider than the screen,
//                                              form fields 16px or larger (so Safari does not zoom in when tapped)
import puppeteer from 'puppeteer-core';
import { createRequire } from 'module';
import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';
const here = path.dirname(fileURLToPath(import.meta.url));
const require = createRequire(import.meta.url);
const { makeServer, WEB_PAGES, WS_VALUES } = require(path.join(here, 'mock_board.js'));
async function chromePath() {
  if (process.env.CHROME_PATH) return { exe: process.env.CHROME_PATH, args: [] };
  const mod = (await import('@sparticuz/chromium')).default;
  return { exe: await mod.executablePath(), args: mod.args };
}
const outDir = process.argv[2] || '/tmp/layout_shots'; fs.mkdirSync(outDir, { recursive: true });
const PAGES = [['/', 'front'], ['/can', 'can'], ['/gauges', 'gauges'], ['/web-config', 'webcfg'], ['/sd', 'sd'], ['/update', 'update'], ['/log-channels', 'logchans'], ['/simulator', 'simulator']];
const IPHONE_UA = 'Mozilla/5.0 (iPhone; CPU iPhone OS 17_5 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/17.5 Mobile/15E148 Safari/604.1';
const PROFILES = [
  { name: 'desktop', pointer: { type: 4, hover: 2 }, viewport: { width: 1280, height: 1000, deviceScaleFactor: 1 }, phone: false },
  { name: 'iphone', pointer: { type: 2, hover: 1 }, viewport: { width: 430, height: 932, deviceScaleFactor: 2, isMobile: true, hasTouch: true }, phone: true },
  { name: 'iphone-landscape', pointer: { type: 2, hover: 1 }, viewport: { width: 932, height: 430, deviceScaleFactor: 2, isMobile: true, hasTouch: true }, phone: true },
];
const { exe, args } = await chromePath();
const server = makeServer(); await new Promise(r => server.listen(0, r)); const base = 'http://localhost:' + server.address().port;
const problems = []; let checked = 0;
for (const prof of PROFILES) {
  const browser = await puppeteer.launch({ executablePath: exe, headless: 'shell',
    args: [...args, `--blink-settings=primaryPointerType=${prof.pointer.type},availablePointerTypes=${prof.pointer.type},primaryHoverType=${prof.pointer.hover},availableHoverTypes=${prof.pointer.hover}`] });
  for (const [route, label] of PAGES) {
    const page = await browser.newPage(); await page.setViewport(prof.viewport);
    if (prof.phone) await page.setUserAgent(IPHONE_UA);
    const errors = []; page.on('pageerror', e => errors.push(String(e)));
    await page.goto(base + route, { waitUntil: 'networkidle2', timeout: 20000 }); await new Promise(r => setTimeout(r, 600));
    const m = await page.evaluate(() => {
      const po = document.querySelector('.phone-outer'); const cs = po && getComputedStyle(po);
      const vw = document.documentElement.clientWidth; const wide = [];
      document.querySelectorAll('.phone-outer *').forEach(e => { const r = e.getBoundingClientRect(); if (r.width > 0 && (r.right > vw + 1 || r.left < -1)) wide.push(e.tagName.toLowerCase() + (e.id ? '#' + e.id : '')); });
      const smallInputs = [...document.querySelectorAll('input:not([type=checkbox]):not([type=color]):not([type=file]):not([type=range]),select,textarea')].filter(e => parseFloat(getComputedStyle(e).fontSize) < 16 && e.getBoundingClientRect().width > 0).length;
      const bulk = [...document.querySelectorAll('.bulk-colors input')].map(e => e.getBoundingClientRect());
      const box = document.querySelector('.bulk'), apply = document.querySelector('.bulk-apply');
      const bulkInfo = bulk.length ? { n: bulk.length, widthSpread: Math.max(...bulk.map(r => r.width)) - Math.min(...bulk.map(r => r.width)), topSpread: Math.max(...bulk.map(r => r.top)) - Math.min(...bulk.map(r => r.top)),
        applyFill: apply.getBoundingClientRect().width / (box.getBoundingClientRect().width - 28) } : null;
      return { bulkInfo, has: !!po, border: cs && cs.borderTopWidth, radius: cs && cs.borderTopLeftRadius, w: po && Math.round(po.getBoundingClientRect().width),
               scrollW: document.documentElement.scrollWidth, vw, wide: [...new Set(wide)].slice(0, 4), nav: !!document.getElementById('navmenu') && !!document.querySelector('.brand'), smallInputs };
    });
    // The front page's numbers must honour each cell's Value Text Size (1.25 x the size in px) but never be wider than the cell:
    // the font is capped at 26% of the cell's width. Measured on all three web dashboard pages of the mock board.
    m.valPages = []; m.lampPages = [];
    if (route === '/') {
      await page.waitForFunction(() => Object.keys(signalValues).length > 0, { timeout: 8000 });   // wait for the first live message
      const nPages = await page.evaluate(() => webPages.length);
      for (let i = 0; i < nPages; i++) {
        if (i) await page.evaluate(() => changeWebPage(1));
        const info = await page.evaluate(() => {
          const po = document.querySelector('.phone-outer').getBoundingClientRect();
          return { kind: webPages[currentWebPage].kind || 0,
            vals: [...document.querySelectorAll('.dcell .val')].map(v => { const cell = v.closest('.dcell');
              return { size: +v.dataset.size, px: parseFloat(getComputedStyle(v).fontSize), cellW: cell.clientWidth, fits: v.scrollWidth <= cell.clientWidth + 1 }; }),
            lamps: [...document.querySelectorAll('.lamp')].map(l => { const r = l.getBoundingClientRect();
              return { on: l.dataset.on, bg: getComputedStyle(l).backgroundColor, text: l.textContent, w: r.width, h: r.height, inside: r.left >= po.left - 1 && r.right <= po.right + 1 }; }) };
        });
        if (info.kind === 1) { m.lampPages.push(info.lamps); if (!prof.phone) await page.screenshot({ path: `${outDir}/${prof.name}_front_indicators.png` }); }
        else { m.valPages.push(info.vals); if (m.valPages.length === 3 && prof.phone === false) await page.screenshot({ path: `${outDir}/${prof.name}_front_bigtext.png` }); }
      }
      await page.evaluate(() => changeWebPage(1));   // one more step wraps back to the first page for the screenshot below
    }
    m.lampCards = await page.evaluate(() => document.querySelectorAll('.lamp-card').length);
    m.chips = await page.evaluate(() => document.querySelectorAll('.lamp-card .chip').length);
    await page.screenshot({ path: `${outDir}/${prof.name}_${label}.png`, fullPage: prof.phone });
    checked++;
    const bad = (why) => problems.push(`${prof.name} ${label}: ${why}`);
    if (!m.has) bad('no phone column (.phone-outer) on the page');
    else if (!prof.phone) { if (m.border !== '10px' || m.w !== 430) bad(`desktop should show the 430px iPhone bezel (border ${m.border}, width ${m.w})`); }
    else {
      if (m.border !== '0px' || m.radius !== '0px') bad(`a phone must NOT show the bezel (border ${m.border}, radius ${m.radius})`);
      if (m.w > 430) bad(`column wider than an iPhone (${m.w}px)`);
      if (m.smallInputs) bad(`${m.smallInputs} form field(s) under 16px: Safari would zoom in when tapped`);
    }
    if (prof.phone && m.scrollW > m.vw + 1) bad(`page scrolls sideways (${m.scrollW}px wide on a ${m.vw}px screen)`);
    if (prof.phone && m.wide.length) bad(`sticks out past the screen edge: ${m.wide.join(', ')}`);
    if (m.bulkInfo) {   // the "apply colours to all" box on the two editors
      if (m.bulkInfo.n !== 3) bad('colour box should have 3 pickers, found ' + m.bulkInfo.n);
      if (m.bulkInfo.widthSpread > 1) bad(`colour pickers are different widths (${m.bulkInfo.widthSpread.toFixed(0)}px apart)`);
      if (m.bulkInfo.topSpread > 1) bad(`colour pickers are not on one row (${m.bulkInfo.topSpread.toFixed(0)}px apart vertically)`);
      if (m.bulkInfo.applyFill < 0.95) bad('the Apply to All button does not span the box');
    }
    m.valPages.forEach((cells, pi) => cells.forEach((v, ci) => {
      const want = Math.min(v.size * 1.25, v.cellW * 0.26);
      if (!(v.size > 0)) bad(`front page ${pi + 1} cell ${ci + 1}: the number carries no text size`);
      else if (Math.abs(v.px - want) > 1.5) bad(`front page ${pi + 1} cell ${ci + 1}: size ${v.size} should draw at ${want.toFixed(1)}px (1.25 x the size, at most 26% of the ${v.cellW}px cell) but draws at ${v.px}px`);
      if (!v.fits) bad(`front page ${pi + 1} cell ${ci + 1}: the number is wider than its cell`);
    }));
    // The indicators page of the mock board: what the BROWSER draws must match what the mock's live data says should be lit.
    const mockLamps = WEB_PAGES[WEB_PAGES.length - 1].indicators, rgb = n => `rgb(${(n >> 16) & 255}, ${(n >> 8) & 255}, ${n & 255})`;
    m.lampPages.forEach(lamps => {
      if (lamps.length !== mockLamps.length) bad(`front page indicators: ${mockLamps.length} lamps expected, ${lamps.length} drawn`);
      lamps.forEach((d, i) => {
        const l = mockLamps[i], v = WS_VALUES[l.signal];
        const on = v !== undefined && (l.op === 1 ? v < l.threshold : v > l.threshold);
        const flashingOnLook = on && l.blink;   // a flashing lamp may be showing either look at this instant
        if (!flashingOnLook && d.bg !== rgb(on ? l.on_color : l.off_color)) bad(`front page lamp ${i + 1} (${l.on_text}): should be ${on ? 'ON' : 'OFF'} (${rgb(on ? l.on_color : l.off_color)}) but is ${d.bg}`);
        if (!flashingOnLook && d.text !== (on ? l.on_text : l.off_text)) bad(`front page lamp ${i + 1}: text should be "${on ? l.on_text : l.off_text}" but shows "${d.text}"`);
        if (!d.inside) bad(`front page lamp ${i + 1} sticks out of the page column`);
        if (d.w < 60 || Math.abs(d.w / d.h - 1.6) > 0.12) bad(`front page lamp ${i + 1} is the wrong size or shape (${d.w.toFixed(0)} x ${d.h.toFixed(0)})`);
      });
    });
    if ((route === '/gauges' || route === '/web-config') && m.lampCards !== mockLamps.length) bad(`the editor should show ${mockLamps.length} lamp cards, shows ${m.lampCards}`);
    if ((route === '/gauges' || route === '/web-config') && m.chips !== 2 * mockLamps.length) bad(`each lamp card should have an ON and an OFF preview chip (${2 * mockLamps.length} expected, ${m.chips} found)`);
    if (!m.nav) bad('missing the shared header / menu');
    if (errors.length) bad('script error: ' + errors.join('; '));
  }
  await browser.close();
}
server.close();
console.log(`${checked} page views checked (${PAGES.length} pages x ${PROFILES.length} devices); screenshots in ${outDir}`);
if (problems.length) { console.log(`${problems.length} PROBLEM(S):`); problems.forEach(p => console.log('  - ' + p)); process.exit(1); }
console.log('all layout rules hold'); process.exit(0);
