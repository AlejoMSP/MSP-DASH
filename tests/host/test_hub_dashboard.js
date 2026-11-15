// The hub's Dashboard (hub_pages.h HOME_HTML) with a fake hub: status tiles and their colours, the lit lamps, key
// readings (fresh only), the screens list.
const vm = require('vm'), { hubScript } = require('./hub_page.js');
let pass = 0, fail = 0; const check = (c, m) => { if (c) pass++; else { fail++; console.log("  FAIL:", m); } };
const settle = async () => { for (let i = 0; i < 30; i++) await new Promise(r => setImmediate(r)); };
function run(api) {
  const els = {}; const el = id => els[id] || (els[id] = { innerHTML: "", textContent: "", getContext: () => ({ createImageData: (w, h) => ({ data: new Uint8ClampedArray(w * h * 4) }), putImageData: () => {} }) });
  const ctx = { document: { getElementById: el }, setInterval: () => 0, setTimeout: () => 0, AbortController, Date, console, Uint8Array, Uint8ClampedArray, encodeURIComponent, Promise,
    fetch: async u => ({ json: async () => { if (!(u in api)) throw new Error("no " + u); return api[u]; }, arrayBuffer: async () => new ArrayBuffer(1024) }) };
  vm.createContext(ctx); vm.runInContext(hubScript('HOME_HTML'), ctx); el.again = () => vm.runInContext('tick()', ctx); return el;
}
const tileOf = (h, name) => { const m = h.match(new RegExp('<a class="tile ([a-z]*)" href="([^"]*)"><b>' + name + '</b><span>([^<]*)</span>')); return m ? { cls: m[1], href: m[2], text: m[3] } : null; };
(async () => {
  const good = {
    "/state": { ecu: "rusEFI", bri: 80 }, "/can.json": { state: 1, total: 5000 }, "/speed.json": { gpsok: true, fix: true, sats: 9 },
    "/sim.json": { active: false }, "/time.json": { known: true, local: "2026-11-15 14:32:05" },
    "/screens.json": [{ name: "Right", online: true, ip: "192.168.55.203", build: 194 }, { name: "Left", online: true, ip: "192.168.55.201", build: 194 }, { name: "Center", online: true, ip: "192.168.55.202", build: 194 }],
    "/oled.json": { lamps: [{ state: 0, sig: 14, op: 0 }, { state: 0, sig: 10, op: 2 }] },
    "http://192.168.55.202/status.json": { logging: "Logging to 2026-11-15_14-32-05.csv" },
    "/sensors.json": [{ sig: 1, v: 3250.4, age: 50 }, { sig: 3, v: 7.26, age: 50 }, { sig: 2, v: 188, age: 9000 }, { sig: 45, v: -0.42, age: 20 }] };
  let el = run(good); await settle();
  let h = el("tiles").innerHTML;
  console.log("1. all good");
  check(tileOf(h, "ECU / CAN").cls === "ok" && tileOf(h, "ECU / CAN").text === "rusEFI", "ECU tile green: rusEFI");
  check(tileOf(h, "GPS").cls === "ok" && /fix, 9 sats/.test(tileOf(h, "GPS").text), "GPS green with satellites");
  check(tileOf(h, "Screens").cls === "ok" && tileOf(h, "Screens").text === "3 of 3 online" && tileOf(h, "Screens").href === "/screen", "screens 3 of 3, links to Screens");
  check(tileOf(h, "Warnings").cls === "ok" && tileOf(h, "Warnings").text === "none", "no warnings: green");
  check(tileOf(h, "Clock").text === "14:32", "clock from the GPS");
  check(el("lit").textContent === "Nothing lit", "nothing lit");
  el = run(good); await settle(); el.again(); await settle();
  const lt = tileOf(el("tiles").innerHTML, "Logging");
  check(lt && lt.cls === "ok" && /^Logging to 2026-11-15_14-32/.test(lt.text) && lt.text.endsWith("..."), "Logging tile: the center screen's status, green while logging (shortened)");
  const r = el("read").innerHTML;
  check(/<b>RPM<\/b><span>3250<\/span>/.test(r) && /<b>Boost psi<\/b><span>7.3<\/span>/.test(r) && /<b>Lateral G<\/b><span>-0.42<\/span>/.test(r), "readings with their decimals");
  check(/<b>Coolant F<\/b><span>--<\/span>/.test(r) && /<b>Oil psi<\/b><span>--<\/span>/.test(r), "stale or missing readings show --");
  const s = el("scr").innerHTML;
  check(s.indexOf("Left") < s.indexOf("Center") && s.indexOf("Center") < s.indexOf("Right") && s.includes('href="/screen?s=Center&p=gauges"'), "screens in order, linked through the Screens page");
  console.log("2. trouble");
  const bad = Object.assign({}, good, { "/can.json": { state: 2, total: 0 }, "/speed.json": { gpsok: true, fix: false, sats: 2 }, "/sim.json": { active: true, seconds_left: 240 },
    "/screens.json": [{ name: "Left", online: false, ip: "1", build: 1 }, { name: "Center", online: true, ip: "2", build: 1 }],
    "/oled.json": { lamps: [{ state: 2, sig: 2, op: 1 }, { state: 1, sig: 0, op: 3 }, { state: 0 }] } });
  el = run(bad); await settle(); h = el("tiles").innerHTML;
  check(tileOf(h, "ECU / CAN").cls === "bad" && tileOf(h, "ECU / CAN").text === "no data", "CAN bus-off: red, no data");
  check(tileOf(h, "GPS").cls === "warn", "GPS without a fix: amber");
  check(tileOf(h, "Screens").cls === "warn" && tileOf(h, "Screens").text === "1 of 2 online", "one screen offline: amber");
  check(tileOf(h, "Warnings").cls === "bad" && tileOf(h, "Warnings").text === "1 danger, 1 warning", "a danger lamp: red, counted");
  check(tileOf(h, "Simulator").cls === "warn" && /ON, 240 s left/.test(tileOf(h, "Simulator").text), "simulator running: amber");
  check(/spot 1 DANGER \(Coolant Temp\)/.test(el("lit").textContent) && /spot 2 warning \(input\)/.test(el("lit").textContent), "lit lamps named");
  console.log("3. a failing request does not blank the page");
  const part = Object.assign({}, good); delete part["/speed.json"];
  el = run(part); await settle(); h = el("tiles").innerHTML;
  check(tileOf(h, "GPS").text === "not connected" && tileOf(h, "ECU / CAN").cls === "ok", "GPS unknown, the rest still shown");
  console.log(`\n${pass} passed, ${fail} failed`); process.exit(fail ? 1 : 0);
})();
