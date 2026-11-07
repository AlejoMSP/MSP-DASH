// The hub's /oled page (hub_pages.h OLED_HTML) with a fake hub: 8 lamp rows, icons, saving a spot, the live picture text.
const vm = require('vm'), { hubScript } = require('./hub_page.js');
let pass = 0, fail = 0; const check = (c, m) => { if (c) pass++; else { fail++; console.log("  FAIL:", m); } };
const settle = async () => { for (let i = 0; i < 20; i++) await new Promise(r => setImmediate(r)); };
const els = {}; const el = id => els[id] || (els[id] = { innerHTML: "", textContent: "", value: "", checked: false, getContext: () => ({ createImageData: (w, h) => ({ data: new Uint8ClampedArray(w * h * 4) }), putImageData: () => {} }) });
const lamps = [{ icon: 1, sig: 14, op: 0, blink: 1, level: 0, lit: true }, { icon: 9, sig: 10, op: 2, blink: 0, level: 12, lit: false },
               { icon: 56, sig: 0, op: 3, blink: 0, level: 0, lit: false }].concat(Array(5).fill({ icon: 0, sig: 0, op: 0, blink: 0, level: 0, lit: false }));
const gets = [];
const ctx = { document: { getElementById: el }, URLSearchParams, setInterval: () => 0, console, Uint8Array, Uint8ClampedArray,
  fetch: async u => { gets.push(u); return { ok: true, json: async () => ({ found: true, layout: 0, belt_lit_low: 1, belt_low: false, lamps }), text: async () => "Saved", arrayBuffer: async () => new ArrayBuffer(1024) }; } };
vm.createContext(ctx); vm.runInContext(hubScript('OLED_HTML'), ctx);
(async () => {
  await settle();
  const h = el("rows").innerHTML;
  check((h.match(/<tr>/g) || []).length === 8, "8 spots");
  check(/>1 LIT</.test(h), "a lit spot is marked");
  check(/<option value="1" selected>Check engine<\/option>/.test(h) && /<option value="56" selected>Seatbelt<\/option>/.test(h), "icons selected by number (same list as the screens)");
  check(/<option value="14" selected>Check Engine<\/option>/.test(h) || /value="14" selected/.test(h), "check engine lamp watches the CEL flag");
  check(/id="lv1" type="number" step="any" style="width:80px" value="12"\s*>/.test(h), "battery: level 12 editable (Below)");
  check(/id="sg2" disabled/.test(h), "seatbelt: no signal to pick (it is the hub input)");
  Object.assign(el("ic1"), { value: "9" }); Object.assign(el("sg1"), { value: "10" }); Object.assign(el("op1"), { value: "2" }); Object.assign(el("lv1"), { value: "11.5" }); Object.assign(el("bl1"), { checked: true });
  await vm.runInContext('save(1)', ctx); await settle();
  const q = new URLSearchParams(gets.find(u => u.startsWith("/oled/set?")).split("?")[1]);
  check(q.get("i") === "1" && q.get("icon") === "9" && q.get("sig") === "10" && q.get("op") === "2" && q.get("level") === "11.5" && q.get("blink") === "1", "saving a spot sends icon, signal, condition, level and blink");
  check(/Display found/.test(el("st").textContent) && /lit: 1/.test(el("st").textContent), "the status line under the live picture");
  console.log(`\n${pass} passed, ${fail} failed`); process.exit(fail ? 1 : 0);
})();
