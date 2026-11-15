// Gauge editor step 4 (web UI): the layout picture to pick a cell, the folding setting groups, the preview beside the
// settings, and copying a cell or a page to this screen or another screen.
const vm = require('vm');
const { pageScript } = require('./extract_page.js');
let pass = 0, fail = 0; const check = (c, m) => { if (c) pass++; else { fail++; console.log("  FAIL:", m); } };
const settle = async () => { for (let i = 0; i < 20; i++) await new Promise(r => setImmediate(r)); };
const cell = (sig, cap) => ({ enabled: true, signal: sig, gauge_type: 0, min: 0, max: 100, rest: 0, caption: cap, needle_color: 0xff0000, tick_color: 0xffffff, bg_color: 0,
  value_divisor: 1, tick_count: 26, major_nth: 5, refresh_hz: 10, show_value: true, value_font_size: 28, has_warning: false, warning_threshold: 0, warning_above: false });
const local = [{ enabled: true, layout: 3, cells: [cell(1, "RPM"), cell(2, "CLT"), cell(5, "OILP"), cell(10, "VOLTS")] },
               { enabled: true, layout: 1, cells: [cell(3, "BOOST"), cell(4, "AFR")] }];
let remote = [{ enabled: true, layout: 1, cells: [cell(1, "R1"), cell(2, "R2")] }, { enabled: true, kind: 1, indicators: [] }];
const els = {}; const posts = []; const gets = [];
const box = {
  document: { getElementById: id => els[id] || (els[id] = { id, innerHTML: "", textContent: "", value: "" }) },
  confirm: () => true, console, setTimeout, Date, JSON, Math, Number, String, parseInt, parseFloat, isNaN, Object, Array,
  fetch: async (url, opts) => {
    gets.push([url, opts && opts.method, opts && opts.headers]);
    if (opts && opts.method === "POST") { posts.push({ url, body: opts.body }); if (url === "http://192.168.55.203/gauges") remote = JSON.parse(opts.body); return { ok: true, status: 200, text: async () => "OK" }; }
    if (url === "/gauges.json") return { json: async () => JSON.parse(JSON.stringify(local)) };
    if (url === "/signals.json") return { json: async () => [] };
    if (url === "/status.json") return { json: async () => ({ ip: "192.168.55.202", link: { boards: [
      { name: "Right", ip: "192.168.55.203", age_ms: 500 }, { name: "Left", ip: "192.168.55.201", age_ms: 600 }, { name: "Old", ip: "192.168.55.209", age_ms: 99000 }, { name: "Center", ip: "192.168.55.202", age_ms: 10 }] } }) };
    if (url === "http://192.168.55.203/gauges.json") return { ok: true, json: async () => JSON.parse(JSON.stringify(remote)) };
    throw new Error("unexpected fetch " + url);
  } };
box.window = box;
const ctx = vm.createContext(box);
vm.runInContext(pageScript('GAUGES_HTML'), ctx);
const R = code => vm.runInContext(code, ctx);
const el = id => box.document.getElementById(id);
(async () => {
  R('load()'); await settle();
  let h = els.pages.innerHTML;
  console.log("1. layout picture and one cell at a time");
  check((h.match(/onclick="selectCell\(0,\d\)"/g) || []).length === 4 && (h.match(/onclick="selectCell\(1,\d\)"/g) || []).length === 2, "each page's picture has a clickable spot per cell (4 and 2)");
  check(/Editing cell 1 of 4/.test(h) && (h.match(/Cell 1 enabled/g) || []).length === 2 && !/Cell 2 enabled/.test(h), "only the chosen cell's settings show (cell 1 on each page)");
  R('selectCell(0, 2)'); h = els.pages.innerHTML;
  check(/Editing cell 3 of 4/.test(h) && /Cell 3 enabled/.test(h) && /value="OILP"/.test(h), "clicking cell 3 opens cell 3 (OILP)");
  check(/stroke="#4dd0e1" stroke-width="8"/.test(h), "the chosen cell is outlined in the picture");
  console.log("2. folding groups and the pinned preview");
  check(/data-g="signal" open/.test(h) && /data-g="text"\s*ontoggle/.test(h) && /data-g="look"\s*ontoggle/.test(h) && /data-g="warn"\s*ontoggle/.test(h), "four groups; Signal open, the rest folded");
  R("noteGroup('warn', true); renderPages()"); h = els.pages.innerHTML;
  check(/data-g="warn" open/.test(h), "a group you open stays open when the page redraws");
  const iSig = h.indexOf('data-g="signal"'), iText = h.indexOf('data-g="text"'), iLook = h.indexOf('data-g="look"'), iWarn = h.indexOf('data-g="warn"');
  const at = s => h.indexOf(s);
  check(at("<label>Signal <select") > iSig && at("<label>Min") > iSig && at("<label>Min") < iText, "Signal group: signal, scale");
  check(at("<label>Caption <input") > iText && at("Value Text Size") > iText && at("Value Text Size") < iLook, "Text group: caption, value size");
  check(at("<label>Color preset") > iLook && at("<label>Needle Color") < iWarn, "Look group: colours");
  check(at("Warning enabled") > iWarn, "Warning group: the warning");
  check(/<div class="cellside"><div class="screenwrap" id="prev_0_2">/.test(h), "the screen preview sits beside the settings");
  console.log("3. copying a cell on this screen");
  { const row = (h.match(/Copy this cell to <select id="cs_0_2">([\s\S]*?)<\/select>/) || ["", ""])[1];
    check(/<option value="192.168.55.203">Right<\/option>/.test(row) && /<option value="192.168.55.201">Left<\/option>/.test(row) && !/Old/.test(row) && !/Center/.test(row), "copy targets: the other screens heard recently (not this one, not stale ones)"); }
  el("cs_0_2").value = ""; el("cp_0_2").value = "1"; el("cc_0_2").value = "1";
  await R('copyCell(0, 2)');
  check(R('pages[1].cells[1].caption') === "OILP" && /Copied to page 2, cell 2. Save to keep it/.test(els.status.innerHTML), "copy cell 3 of page 1 onto page 2 cell 2, then Save");
  R('pages[1].cells[1].caption = "X"');
  check(R('pages[0].cells[2].caption') === "OILP", "it is a copy, not the same object");
  el("cc_0_2").value = "3"; await R('copyCell(0, 2)');
  check(/no gauge page 2 with a cell 4/.test(els.status.innerHTML), "a cell the target layout does not have: refused");
  console.log("4. copying to another screen");
  el("cs_0_2").value = "192.168.55.203"; el("cp_0_2").value = "0"; el("cc_0_2").value = "1";
  await R('copyCell(0, 2)');
  check(remote[0].cells[1].caption === "OILP" && remote[0].cells[0].caption === "R1" && remote[1].kind === 1, "Right's page 1 cell 2 replaced, everything else on Right kept");
  check(/Copied to Right, page 1, cell 2/.test(els.status.innerHTML) && /restartPeer\('192.168.55.203','Right'\)/.test(els.status.innerHTML), "says where it went, offers to restart Right");
  el("cp_0_2").value = "1"; el("cc_0_2").value = "0"; await R('copyCell(0, 2)');
  check(/Right has no gauge page 2 with a cell 1/.test(els.status.innerHTML), "Right's page 2 is a lamps page: refused");
  els.pc_1 = { value: "192.168.55.203" };
  await R('copyPage(1)');
  check(remote.length === 3 && remote[2].cells[0].caption === "BOOST" && /added to Right as its page 3/.test(els.status.innerHTML), "copy a whole page: added as Right's last page");
  await R("restartPeer('192.168.55.203','Right')");
  const rb = gets.find(g => g[0] === "http://192.168.55.203/reboot");
  check(rb && rb[1] === "POST" && rb[2]["X-MSP"] === "1", "restart button: POST /reboot with the X-MSP header");
  console.log(`\n${pass} passed, ${fail} failed`); process.exit(fail ? 1 : 0);
})();
