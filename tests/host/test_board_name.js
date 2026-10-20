// The board's name on every page: the shared script in PH_CLOSE (web_theme.h) puts it next to MSP-DASH in the header
// and at the front of the browser tab title, so with three screens open you can tell which one you are on.
const vm = require('vm');
const { pageHtml, pageNames } = require('./extract_page.js');
let pass = 0, fail = 0; const check = (c, m) => { if (c) pass++; else { fail++; console.log("  FAIL:", m); } };
const settle = async () => { for (let i = 0; i < 10; i++) await new Promise(r => setImmediate(r)); };

async function run(page, status, fetchFails) {
  const html = pageHtml(page);
  const code = html.match(/<script data-shared>([\s\S]*?)<\/script>/)[1];
  const title = html.match(/<title>([^<]*)<\/title>/)[1];
  const brand = { textContent: "MSP-DASH" };
  const ctx = { document: { title, querySelector: s => s === '.brand' ? brand : null },
    fetch: async url => { if (fetchFails) throw new Error("offline"); if (url !== '/status.json') throw new Error(url); return { json: async () => status }; } };
  vm.runInNewContext(code, ctx); await settle();
  return { brand: brand.textContent, title: ctx.document.title };
}

(async () => {
  const pages = pageNames();
  console.log("1. every page has the shared script, once");
  for (const p of pages) check((pageHtml(p).match(/<script data-shared>/g) || []).length === 1, p + " has it exactly once");
  console.log("2. a named board");
  for (const p of pages) {
    const r = await run(p, { link: { name: "Center" } });
    check(r.brand === "MSP-DASH Center", p + ": header says MSP-DASH Center (got " + r.brand + ")");
    check(/^Center \| MSP-DASH/.test(r.title), p + ": tab title starts with the name (got " + r.title + ")");
  }
  console.log("3. no name, no answer");
  let r = await run('GAUGES_HTML', { link: {} });
  check(r.brand === "MSP-DASH" && r.title === "MSP-DASH Gauge Config", "no name: header and title unchanged");
  r = await run('GAUGES_HTML', null, true);
  check(r.brand === "MSP-DASH" && r.title === "MSP-DASH Gauge Config", "board not answering: nothing breaks, nothing changes");
  console.log(`\n${pass} passed, ${fail} failed`);
  process.exit(fail ? 1 : 0);
})();
