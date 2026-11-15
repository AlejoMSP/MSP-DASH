// Drives the HUB's /sensors page script (from esp32-input/hub_pages.h) with a fake hub: sensors listed
// alphabetically, and CAN frames that no sensor reads listed under "On the bus, not decoded".
const fs = require('fs'), path = require('path'), vm = require('vm');
const HUB = fs.readFileSync(path.join(__dirname, '..', '..', 'esp32-input', 'esp32-input.ino'), 'utf8');
const PAGES = require('./hub_page.js').SRC;
const html = require('./hub_page.js').hubPage('SENSORS_HTML');
const script = html.match(/<script>([\s\S]*?)<\/script>/)[1];
let pass = 0, fail = 0; const check = (c, m) => { if (c) pass++; else { fail++; console.log("  FAIL:", m); } };
const settle = async () => { for (let i = 0; i < 10; i++) await new Promise(r => setImmediate(r)); };
const els = {}; const el = id => els[id] || (els[id] = { innerHTML: "", textContent: "" });
let can = { total: 9, ids: [] };
const sensors = [
  { sig: 1, v: 3000, age: 20, src: 0x201 }, { sig: 2, v: 190, age: 30, src: 0x203 }, { sig: 24, v: 50, age: 50, src: 4294901763 },
  { sig: 22, v: 72, age: 900, src: 4294901762 }, { sig: 9, v: 12, age: 40, src: 0x202 } ];
const ctx = { document: { getElementById: el }, setInterval: () => 0, console,
  fetch: async url => ({ json: async () => url === "/sensors.json" ? JSON.parse(JSON.stringify(sensors)) : url === "/can.json" ? can : null }) };
vm.createContext(ctx);

(async () => {
  can = { total: 9, ids: [
    { id: 0x201, n: 5, age: 20, d: "00 " }, { id: 0x203, n: 5, age: 30, d: "00 " }, { id: 0x202, n: 5, age: 40, d: "00 " },
    { id: 0x208, n: 5, age: 25, d: "11 22 33 " }, { id: 0x5F2, n: 3, age: 3000, d: "AA " }, { id: 0x123, n: 1, age: 60, d: "01 " }, { id: 0x7E8, n: 1, age: 20000, d: "02 " } ] };
  vm.runInContext(script, ctx); await settle();
  console.log("1. sensors in alphabetical order");
  const rows = [...el("t").innerHTML.matchAll(/<tr[^>]*><td>([^<]+)<\/td>/g)].map(m => m[1]);
  check(rows.join(",") === "Ambient Temp,Coolant Temp,Fuel Level,RPM,Throttle Position", "order: " + rows.join(", "));
  console.log("2. frames no sensor reads");
  const u = el("u").innerHTML;
  check(/0x208/.test(u) && /rusEFI/.test(u), "a rusEFI frame nothing decodes (0x208) shows, tagged rusEFI");
  check(/0x5F2/.test(u) && /MegaSquirt\/Link/.test(u) && /0x5F2[\s\S]*?class="stale"|class="stale"[\s\S]*?0x5F2/.test(u), "a MegaSquirt/Link frame shows, grey after 2 s");
  check(/0x123/.test(u) && /unknown/.test(u), "an unknown frame shows as unknown");
  check(!/0x201|0x202|0x203/.test(u), "frames a sensor already reads are not listed");
  check(!/0x7E8/.test(u), "a frame gone for more than 10 s drops off");
  check(u.indexOf("0x123") < u.indexOf("0x208") && u.indexOf("0x208") < u.indexOf("0x5F2"), "sorted by CAN ID");
  check(/^3 frames not read/.test(el("usum").textContent), "summary: " + el("usum").textContent);
  can = { total: 0, ids: [] }; vm.runInContext("und()", ctx); await settle();
  check(/Every frame/.test(el("usum").textContent) && el("u").innerHTML === "", "nothing undecoded: says so");
  console.log("3. the .ino has no raw strings (the Arduino builder mangles them: 'asyncfunction' does not name a type)");
  check(!/R"[A-Za-z]*\(/.test(HUB), "esp32-input.ino has no R\"...( raw string literals - pages go in hub_pages.h");
  check(!/\)\s*\{\s*static\s/.test(HUB), "no one-line function with 'static' inside its body (the Arduino prototype scan reads it as a static function)");
  for (const n of ["HOME_HTML", "SETUP_HTML", "SENSORS_HTML", "SIM_HTML", "CUSTOM_HTML", "RF_HTML", "BUTTONS_HTML", "OLED_HTML", "SCREEN_HTML", "CAN_HTML", "FIRMWARE_HTML"])
    check(HUB.includes('server.send_P(200, "text/html", ' + n + ', strlen_P(' + n + '))') && PAGES.includes(n + "[] PROGMEM"), n + " is in hub_pages.h and served straight from flash");
  console.log(`\n${pass} passed, ${fail} failed`); process.exit(fail ? 1 : 0);
})();
