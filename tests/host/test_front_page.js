const fs = require('fs');
// the front page's <script>, exactly as the board serves it
const script = require('./extract_page.js').pageScript('INDEX_HTML');

// ---- controllable clock and timers ----
let now = 1000000; const timers = [];
global.Date.now = () => now;
global.setTimeout = (fn, ms) => { const t = {fn, at: now + ms, rep: 0}; timers.push(t); return t; };
global.setInterval = (fn, ms) => { const t = {fn, at: now + ms, rep: ms}; timers.push(t); return t; };
function advance(ms) {
  const end = now + ms;
  while (true) {
    const due = timers.filter(t => t.at <= end).sort((a, b) => a.at - b.at)[0];
    if (!due) break;
    now = due.at;
    if (due.rep) due.at += due.rep; else timers.splice(timers.indexOf(due), 1);
    due.fn();
  }
  now = end;
}

// ---- fake browser ----
const els = { build: { id: 'build', textContent: 'build ...', innerHTML: '', className: '' } }; const docListeners = {};
const getEl = id => els[id] || (els[id] = { id, innerHTML: "", textContent: "", className: "", classList: { toggle() {} } });
global.document = { getElementById: getEl, hidden: false, addEventListener: (ev, fn) => { docListeners[ev] = fn; } };
global.location = { host: "192.168.55.102" };
class FakeWS {
  constructor(url) { this.url = url; this.readyState = 0; FakeWS.all.push(this); }
  close() { if (this.readyState === 3) return; this.readyState = 3; if (this.onclose) this.onclose(); }
}
FakeWS.all = []; global.WebSocket = FakeWS;
const cells = [{enabled:true,signal:1,caption:"RPM",value_divisor:1,has_warning:false},
               {enabled:true,signal:3,caption:"MAP",value_divisor:1,has_warning:false,units:"kPa"},
               {enabled:true,signal:2,caption:"CLT",value_divisor:1,has_warning:true,warning_threshold:220,warning_above:true},
               {enabled:true,signal:4,caption:"",value_divisor:1,has_warning:false}];
const lamp = (sig, op, thr, hyst, on, off) => ({ signal: sig, op, threshold: thr, hysteresis: hyst, on_text: on, off_text: off });
const webPages = [{enabled:true,layout:3,cells},
                  {enabled:true,layout:0,cells:[{enabled:true,signal:1,caption:"RPM BIG",value_divisor:1000,decimals:2}, cells[1]]},
                  {enabled:true,kind:1,name:"Lamps <1>",indicators:[lamp(5,1,40,3,"OIL","oil"), lamp(17,0,0.5,0,"FAN","fan"), lamp(4,0,15,0,"A<b>","x")]},
                  {enabled:false,layout:0,cells:[{enabled:true,signal:9,caption:"HIDDEN",value_divisor:1}]}];
let statusFail = true; const urls = [];
global.fetch = async (url) => {
  urls.push(url);
  if (url.includes("status.json")) { if (statusFail) throw new Error("x"); return { json: async () => ({ build: 102, compiled: "Oct  6 2026 14:32:10" }) }; }
  return { json: async () => webPages };
};

eval(script + "\n;global.__ind=indicatorNext; global.__render=render;");
const open = (ws) => { ws.readyState = 1; ws.onopen(); };
const send = (ws, o) => ws.onmessage({ data: JSON.stringify(o) });
const status = () => `[${els.dot.className}] ${els.stext.textContent}`;
const html = () => els.cellgrid.innerHTML;
const rows = () => [...html().matchAll(/<div class="srow"><span class="nm">(.*?)<\/span><span class="vv ?([^"]*)">([^<]*)<\/span><\/div>/g)].map(m => ({ name: m[1], cls: m[2].trim(), val: m[3] }));
const heads = () => [...html().matchAll(/<div class="spage">([^<]*)<\/div>/g)].map(m => m[1]);
let pass = 0, fail = 0;
const check = (c, m) => { if (c) pass++; else { fail++; console.log("  FAIL:", m); } };

(async () => {
  await new Promise(r => setImmediate(r)); await new Promise(r => setImmediate(r));

  console.log("1. loads the round screen's own pages, build number retries");
  check(urls[0] === "/gauges.json", "reads /gauges.json (the screen's pages), not a separate web config: " + urls[0]);
  check(els.build.textContent === "build ...", "while /status.json fails the page still loads");
  statusFail = false; advance(5100); await new Promise(r => setImmediate(r)); await new Promise(r => setImmediate(r));
  check(els.build.textContent === "build 102 - Oct  6 2026 14:32:10", "then shows the build: " + els.build.textContent);
  check(FakeWS.all.length === 1 && FakeWS.all[0].url === "ws://192.168.55.102/ws", "opens ws://<board>/ws");
  check(/Connecting/.test(status()), "says Connecting: " + status());

  console.log("2. one line per sensor, grouped by page");
  check(heads().join(",") === "Page 1,Page 2,Page 3: Lamps &lt;1&gt;", "enabled pages only, numbered as on the screen, with the name when set (escaped): " + heads().join(","));
  check(!html().includes("HIDDEN"), "a disabled page is left out");
  check(rows().length === 4 + 1 + 3, "4 + 1 (1-cell layout shows only its first cell) + 3 lamps = 8 lines: " + rows().length);
  check(rows().slice(0, 4).every(r => r.val === "--"), "no data yet: --");
  check(rows()[3].name === "AFR", "a cell with no caption uses the signal name");

  console.log("3. live values");
  let ws = FakeWS.all[0]; open(ws);
  check(/waiting for the first update/.test(status()), status());
  send(ws, { v: { "1": 3000, "2": 185.5, "3": 120.1, "4": 14.66 }, fps: 412, ids: 38 });
  check(rows().slice(0, 4).map(r => r.val).join("|") === "3000.0|120.1 kPa|185.5|14.7", "values, units, 1 decimal by default: " + rows().slice(0, 4).map(r => r.val).join("|"));
  check(rows()[4].val === "3.00", "divisor and decimals honoured (3000 / 1000, 2 decimals): " + rows()[4].val);
  check(els.dot.className === "st-ok" && /Live - 4 signals - CAN 412/.test(status()), status());
  send(ws, { v: { "1": 3000, "2": 230 }, fps: 412, ids: 38 });
  check(rows()[2].cls === "warn" && rows()[0].cls === "", "coolant over its threshold is red, the others are not");
  check(rows()[1].val === "--", "a signal that stops coming shows --");

  console.log("4. lamps");
  send(ws, { v: { "5": 30, "17": 1, "4": 10 }, fps: 400, ids: 30 });
  const L = rows().slice(5);
  check(L.map(r => r.val).join(",") === "ON,ON,off", "low oil ON, fan ON, AFR lamp off: " + L.map(r => r.val).join(","));
  check(L[0].cls === "on" && L[2].cls === "off", "ON green, off grey");
  check(L[2].name.startsWith("A&lt;b&gt;"), "lamp text is escaped: " + L[2].name);
  send(ws, { v: { "5": 42, "17": 0 }, fps: 400, ids: 30 });
  check(rows()[5].val === "ON", "oil 42 is inside the 3 band: still ON (hysteresis)");
  send(ws, { v: { "5": 44 }, fps: 400, ids: 30 });
  check(rows()[5].val === "off" && rows()[6].val === "off", "oil 44: off; fan flag gone: off");

  console.log("5. silence, disconnect, reconnect");
  send(ws, { v: {}, fps: 0, ids: 0 });
  check(/no CAN frames are arriving/.test(status()), status());
  send(ws, { v: {}, fps: 350, ids: 25 });
  check(/arriving \(350\/s\) but none decoded/.test(status()), status());
  send(ws, { v: { "1": 3000 }, fps: 400, ids: 38 });
  advance(4000); check(/No updates from the board for [34]s/.test(status()), status());
  advance(5000); check(ws.readyState === 3 && /Disconnected/.test(status()), "watchdog closes a dead connection: " + status());
  check(rows().slice(0, 5).every(r => r.val === "--") && rows().slice(5).every(r => r.val === "off"), "disconnected: every value --, every lamp off");
  advance(1100); check(FakeWS.all.length === 2, "reconnects after ~1 s");
  FakeWS.all[1].onerror(); advance(2100); check(FakeWS.all.length === 3, "then ~2 s");
  FakeWS.all[2].onerror(); advance(4100); check(FakeWS.all.length === 4, "then ~4 s");
  FakeWS.all[3].onerror(); advance(5100); check(FakeWS.all.length === 5, "capped at 5 s");
  ws = FakeWS.all[4]; open(ws); send(ws, { v: { "1": 2500 }, fps: 380, ids: 36, sim: 1 });
  check(rows()[0].val === "2500.0" && els.dot.className === "st-ok", "live again");
  check(els.simbar.className === "simbar on", "SIMULATED bar follows the board");
  ws.close(); check(els.simbar.className === "simbar", "bar hidden on disconnect");
  { const before = FakeWS.all.length; docListeners.visibilitychange(); check(FakeWS.all.length >= before, "waking the phone never duplicates a pending connection"); }

  console.log("6. no pages");
  webPages.length = 0; __render();
  check(/No pages on this screen yet/.test(html()), "explains an empty screen");

  console.log("13. indicator lamps: the same on/off rule as the round display");
  { // the SAME cases, with the SAME expected answers, as tests/host/test_indicators.c
    const N = (was, v, op, thr, hyst) => __ind(was, true, v, op, thr, hyst);
    check(!N(false, 99.9, 0, 100, 0) && !N(false, 100, 0, 100, 0) && N(false, 100.1, 0, 100, 0), "above: off below, off exactly at, on above the threshold");
    check(N(true, 100.1, 0, 100, 0) && !N(true, 100, 0, 100, 0), "above, no hysteresis: off the moment it is no longer above");
    check(!N(false, 98, 0, 100, 5) && N(false, 100.5, 0, 100, 5) && N(true, 98, 0, 100, 5) && N(true, 95.1, 0, 100, 5) && !N(true, 95, 0, 100, 5) && !N(true, 90, 0, 100, 5), "above 100 with a band of 5: stays on down to just above 95, off at 95");
    check(!N(false, 40, 1, 40, 3) && N(false, 39.9, 1, 40, 3) && N(true, 42.9, 1, 40, 3) && !N(true, 43, 1, 40, 3) && !N(false, 41, 1, 40, 3), "below 40 with a band of 3: on below 40, stays on until 43");
    { let on = false, flips = 0; for (const v of [99, 101, 100.2, 99.8, 100.3, 99.5, 100.1, 94, 99, 101]) { const n = N(on, v, 0, 100, 5); if (n !== on) flips++; on = n; } check(flips === 3, "a value wobbling around the threshold flips the lamp 3 times, not on every wobble"); }
    check(!__ind(true, false, 5, 0, 1, 0) && !__ind(true, false, 5, 1, 100, 0), "no data: off, even for a lamp that was on");
    check(!N(true, NaN, 0, 1, 0) && !N(true, Infinity, 0, 1, 0) && !N(true, -Infinity, 1, 1, 0), "NaN and infinity: off");
    check(N(false, 101, 0, 100, -4) && !N(true, 100, 0, 100, -4) && N(false, 101, 0, 100, NaN) && !N(true, 100, 0, 100, NaN) && N(false, 101, 0, 100, undefined), "a negative, NaN or missing hysteresis acts like none");
    check(!N(false, 5, 0, NaN, 0) && !N(true, 5, 0, NaN, 0), "a NaN threshold never lights a lamp");
    check(N(false, 1, 0, 0.5, 0) && !N(false, 0, 0, 0.5, 0) && N(false, 0, 1, 0.5, 0) && !N(false, 1, 1, 0.5, 0), "ECU flags (1 or 0) with threshold 0.5: 'flag on' and 'flag off' lamps");
  }

  console.log(`\n${pass} passed, ${fail} failed`);
  if (fail) process.exitCode = 1;
})();
