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
const { makeServer } = require(path.join(here, 'mock_board.js'));
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
      return { has: !!po, border: cs && cs.borderTopWidth, radius: cs && cs.borderTopLeftRadius, w: po && Math.round(po.getBoundingClientRect().width),
               scrollW: document.documentElement.scrollWidth, vw, wide: [...new Set(wide)].slice(0, 4), nav: !!document.getElementById('navmenu') && !!document.querySelector('.brand'), smallInputs };
    });
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
    if (!m.nav) bad('missing the shared header / menu');
    if (errors.length) bad('script error: ' + errors.join('; '));
  }
  await browser.close();
}
server.close();
console.log(`${checked} page views checked (${PAGES.length} pages x ${PROFILES.length} devices); screenshots in ${outDir}`);
if (problems.length) { console.log(`${problems.length} PROBLEM(S):`); problems.forEach(p => console.log('  - ' + p)); process.exit(1); }
console.log('all layout rules hold'); process.exit(0);
