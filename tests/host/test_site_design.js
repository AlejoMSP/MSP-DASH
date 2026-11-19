// One design for the whole site (hub 55 / build 217): every hub page and every screen page has the side menu (a menu
// button on narrow screens), the shared base look, the iPhone-safe viewport, and no iPhone bezel. Plus the Dashboard's
// quick controls, the page made for the phone.
const vm = require('vm'), fs = require('fs');
const { pageHtml, pageNames } = require('./extract_page.js'), { hubPage, hubScript, SRC } = require('./hub_page.js');
let pass = 0, fail = 0; const check = (c, m) => { if (c) pass++; else { fail++; console.log("  FAIL:", m); } };
const settle = async () => { for (let i = 0; i < 20; i++) await new Promise(r => setImmediate(r)); };
(async () => {
  console.log("1. hub pages");
  const hub = [...SRC.matchAll(/static const char (\w+_HTML)\[\] PROGMEM/g)].map(m => m[1]);
  check(hub.length >= 12, "found the hub pages (" + hub.length + ")");
  for (const p of hub) {
    const h = hubPage(p);
    check(/^<!doctype html>/i.test(h) && /viewport-fit=cover/.test(h), p + ": doctype and an iPhone-safe viewport");
    check(/class="nav"/.test(h) && /class="navbtn"/.test(h) && /max-width:1000px/.test(h) && /input,select,textarea\{font-size:16px\}/.test(h), p + ": side menu, menu button, shared base look");
    check(!/phone-outer|border-radius:48px/.test(h), p + ": no iPhone bezel");
  }
  check(/<body class="wide">/.test(hubPage('SCREEN_HTML')), "the Screens page uses the full width (it holds a screen's page)");
  console.log("2. screen pages");
  const scr = pageNames();
  check(scr.length >= 6, "found the screen pages (" + scr.length + ")");
  for (const p of scr) {
    const h = pageHtml(p);
    check(/viewport-fit=cover/.test(h) && /class='nav'/.test(h) && /class='navbtn'/.test(h) && /max-width:1000px/.test(h), p + ": side menu, menu button, the same base look as the hub");
    check(!/phone-outer|phone-screen|border-radius:48px|max-width:430px/.test(h), p + ": no iPhone column or bezel");
    check(/self!==window\.top[^<]*framed/.test(h) && /body\.framed \.nav/.test(h), p + ": inside the hub's Screens page, its own menu hides");
    check(/background:#000;color:#ddd/.test(h) && /input:not\(\[type=checkbox\]\)[^{]*\{background:#000/.test(h), p + ": dark page and dark form fields, like the hub");
  }
  const theme = fs.readFileSync('../../esp32s3-amoled-bsp-base/web_theme.h', 'utf8'), hp = fs.readFileSync('../../esp32-input/hub_pages.h', 'utf8');
  const navCss = s => (s.match(/\.nav a\{[^}]*\}/) || [""])[0].replace(/\\"/g, '"');
  check(navCss(theme) && navCss(theme) === navCss(hp), "the menu links look the same on the hub and the screens");
  console.log("3. the phone page (Dashboard)");
  const home = hubPage('HOME_HTML');
  check(/apple-mobile-web-app-capable" content="yes"/.test(home) && /apple-mobile-web-app-title" content="MSP-DASH"/.test(home), "Add to Home Screen opens it like an app");
  check(/Quick controls/.test(home) && /Previous page/.test(home) && /Next page/.test(home) && /Race mode/.test(home) && /Log marker/.test(home), "quick controls: big page buttons and the common actions");
  const els = {}; const el = id => els[id] || (els[id] = { innerHTML: "", textContent: "", value: "0", style: {}, getContext: () => ({ createImageData: (w, h) => ({ data: new Uint8ClampedArray(w * h * 4) }), putImageData: () => {} }) });
  const gets = [];
  const api = { "/buttons.json": { ecu: { used: true, cur: 1, names: ["Street", "Boost"] } } };
  const ctx = { document: { getElementById: el }, setInterval: () => 0, setTimeout: () => 0, AbortController, Date, console, Uint8Array, Uint8ClampedArray, encodeURIComponent, Promise,
    fetch: async u => { gets.push(u); return { ok: true, text: async () => "Map 1: Street", json: async () => { if (!(u in api)) throw new Error("no " + u); return api[u]; }, arrayBuffer: async () => new ArrayBuffer(1024) }; } };
  vm.createContext(ctx); vm.runInContext(hubScript('HOME_HTML'), ctx); await settle();
  check(el("qecu").style.display === "" && el("qecu").textContent === "ECU map: Boost", "ECU maps in use: the button shows, named after the map in use");
  el("qt").value = "2"; await vm.runInContext("act(1)", ctx); await settle();
  check(gets.includes("/action?a=1&p=0&t=2"), "Next page on the Center screen");
  await vm.runInContext("act(14)", ctx); await settle();
  check(el("qecu").textContent === "ECU map: Map 1: Street" && el("qmsg").textContent === "Map 1: Street", "ECU map: the hub's answer shows");
  console.log(`\n${pass} passed, ${fail} failed`); process.exit(fail ? 1 : 0);
})();
