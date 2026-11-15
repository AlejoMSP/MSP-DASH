// The hub's /oled page (hub_pages.h OLED_HTML) with a fake hub: lamp rows, warning / danger, inputs, saving, live status.
const vm = require('vm'), { hubScript } = require('./hub_page.js');
let pass = 0, fail = 0; const check = (c, m) => { if (c) pass++; else { fail++; console.log("  FAIL:", m); } };
const settle = async () => { for (let i = 0; i < 20; i++) await new Promise(r => setImmediate(r)); };
const els = {};
const el = id => els[id] || (els[id] = { id, innerHTML: "", textContent: "", value: "", checked: false, set outerHTML(v) { this._outer = v; },
  getContext: () => ({ createImageData: (w, h) => ({ data: new Uint8ClampedArray(w * h * 4) }), putImageData: () => {} }) });
const lamp = (o) => Object.assign({ icon: 0, sig: 0, op: 0, blink: 0, level: 0, danger: 0, has_danger: 0, input: 0, input_low: 1, state: 0, in_low: false }, o);
const lamps = [lamp({ icon: 1, sig: 14, op: 0, state: 1 }), lamp({ icon: 4, sig: 2, op: 1, level: 220, danger: 235, has_danger: 1, state: 2 }),
               lamp({ icon: 56, op: 3, input: 1, input_low: 1, state: 1, in_low: true }), lamp({}), lamp({}), lamp({}), lamp({}), lamp({})];
const gets = [];
const ctx = { document: { getElementById: el, createElement: () => ({}) }, URLSearchParams, setInterval: () => 0, console, Uint8Array, Uint8ClampedArray,
  fetch: async u => { gets.push(u); return { ok: true, json: async () => u === "/oled/pages.json" ? pagesJ : ({ found: true, mcp: [0x27, 0], layout: 0, lamps }), text: async () => "Saved", arrayBuffer: async () => new ArrayBuffer(1024) }; } };
const pagesJ = { count: 3, cycle: 0, takeover: true, chosen: 1, shown: 1, pages: [
  { type: 0, count: 0, sig: [0, 0, 0, 0], dec: [0, 0, 0, 0], label: ["", "", "", ""] },
  { type: 1, count: 1, sig: [3, 0, 0, 0], dec: [1, 0, 0, 0], label: ["BOOST PSI", "", "", ""] },
  { type: 2, count: 2, sig: [5, 2, 0, 0], dec: [0, 0, 0, 0], label: ["OIL PSI", "WATER F", "", ""] }] };
vm.createContext(ctx); vm.runInContext(hubScript('OLED_HTML'), ctx);
(async () => {
  await settle();
  const h = el("rows").innerHTML;
  check((h.match(/class="lamp"/g) || []).length === 8, "8 spots");
  check(/class="st WARNING">WARNING/.test(h) && /class="st DANGER">DANGER/.test(h), "each lit spot shows WARNING or DANGER");
  check(/Warning above <input id="lv1" type="number" step="any" value="220">/.test(h) && /id="hd1" checked> Danger above<\/label> <input id="dg1" type="number" step="any" value="235">/.test(h), "coolant: warning 220, danger 235");
  check(/Lit means DANGER/.test(h), "flag / input lamps: a 'lit means danger' box instead of levels");
  check(/<option value="1" selected>GPIO 2 \(R20\)<\/option>/.test(h) && /<option value="1" selected>lit when LOW<\/option>/.test(h) && /input LOW/.test(h), "seatbelt: input GPIO 2, lit when LOW, reading LOW now");
  check(/MCP23017 #1 PA7<\/option>/.test(h) && /MCP23017 #1 PB7<\/option>/.test(h) && /MCP23017 #2 PA0 \(not found\)<\/option>/.test(h) && !/PCF/.test(h), "MCP23017 board 1: PA0-PB7; a missing board 2 is marked");
  check(/GPIO 12 \(L18\)/.test(h) && /GPIO 0 \(R19, BOOT\)/.test(h), "the other hub pins with their terminals");
  Object.assign(el("ic1"), { value: "4" }); Object.assign(el("op1"), { value: "1" }); Object.assign(el("sg1"), { value: "2" });
  Object.assign(el("lv1"), { value: "215" }); Object.assign(el("dg1"), { value: "240" }); Object.assign(el("hd1"), { checked: true }); Object.assign(el("bl1"), { checked: false });
  await vm.runInContext('save(1)', ctx); await settle();
  let q = new URLSearchParams(gets.filter(u => u.startsWith("/oled/set?")).pop().split("?")[1]);
  check(q.get("op") === "1" && q.get("sig") === "2" && q.get("level") === "215" && q.get("danger") === "240" && q.get("has_danger") === "1" && q.get("input") === "0", "saving coolant: levels 215 / 240, danger on, no input");
  Object.assign(el("ic2"), { value: "56" }); Object.assign(el("op2"), { value: "3" }); Object.assign(el("in2"), { value: "27" }); Object.assign(el("lo2"), { value: "0" }); Object.assign(el("hd2"), { checked: false }); Object.assign(el("bl2"), { checked: true });
  await vm.runInContext('save(2)', ctx); await settle();
  q = new URLSearchParams(gets.filter(u => u.startsWith("/oled/set?")).pop().split("?")[1]);
  check(q.get("op") === "3" && q.get("input") === "27" && q.get("input_low") === "0" && q.get("blink") === "1", "saving an input lamp: MCP23017 #1 PB3, lit when HIGH, blink");
  vm.runInContext('chOp(3, 3)', ctx);
  check(/GPIO 2 \(R20\)/.test(el("lamp3")._outer || "") && vm.runInContext('cfg.lamps[3].input', ctx) === 1, "switching a spot to Input wire shows the input boxes (GPIO 2 first)");
  check(/expanders: #1 at 0x27/.test(el("st").textContent) && /2 DANGER/.test(el("st").textContent), "status line: the expander found and what is lit");
  { const p = el("pages").innerHTML;
    check((p.match(/class="lamp"/g) || []).length === 3, "pages listed");
    check(/<option value="1" selected>Big number<\/option>/.test(p) && /id="pl1_0" maxlength="9" style="width:100px" value="BOOST PSI"/.test(p), "big number page: its signal label");
    check(/<option value="2" selected>2 values<\/option>/.test(p) && /id="pl2_1"[^>]*value="WATER F"/.test(p), "values page: two readouts with labels");
    check(/ON SCREEN/.test(p), "the page on screen is marked");
    Object.assign(el("pt2"), { value: "2" }); Object.assign(el("pc2"), { value: "2" });
    Object.assign(el("ps2_0"), { value: "6" }); Object.assign(el("pl2_0"), { value: "OIL F" }); Object.assign(el("pd2_0"), { value: "0" });
    Object.assign(el("ps2_1"), { value: "10" }); Object.assign(el("pl2_1"), { value: "VOLTS" }); Object.assign(el("pd2_1"), { value: "1" });
    await vm.runInContext('savePage(2)', ctx); await settle();
    const q = new URLSearchParams(gets.filter(u => u.startsWith("/oled/page/set?")).pop().split("?")[1]);
    check(q.get("i") === "2" && q.get("type") === "2" && q.get("count") === "2" && q.get("s0") === "6" && q.get("l1") === "VOLTS" && q.get("d1") === "1", "saving a page sends type, count, signals, labels, decimals");
    await vm.runInContext('showPage(1)', ctx); await settle();
    check(gets.includes("/oled/page/show?i=1"), "Show puts that page on the OLED");
    Object.assign(el("cyc"), { value: "5" }); Object.assign(el("tko"), { checked: false });
    await vm.runInContext('pageOpts()', ctx); await settle();
    check(gets.includes("/oled/pageopts?cycle=5&takeover=0"), "timer and takeover saved"); }
  console.log(`\n${pass} passed, ${fail} failed`); process.exit(fail ? 1 : 0);
})();
