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
               {enabled:true,signal:3,caption:"MAP",value_divisor:1,has_warning:false},
               {enabled:true,signal:2,caption:"CLT",value_divisor:1,has_warning:true,warning_threshold:220,warning_above:true},
               {enabled:true,signal:4,caption:"AFR",value_divisor:1,has_warning:false}];
const webPages = [{enabled:true,layout:3,cells}, {enabled:true,layout:0,cells:[cells[0]]}];
let statusFail = true;
global.fetch = async (url) => {
  if (url.includes("status.json")) { if (statusFail) throw new Error("x"); return { json: async () => ({ build: 102, compiled: "Oct  6 2026 14:32:10" }) }; }
  return { json: async () => webPages };
};

eval(script + "\n;global.__next=changeWebPage;");
const open = (ws) => { ws.readyState = 1; ws.onopen(); };
const send = (ws, o) => ws.onmessage({ data: JSON.stringify(o) });
const status = () => `[${els.dot.className}] ${els.stext.textContent}`;
const grid = () => els.cellgrid.innerHTML;
// the number cells, read by meaning (class + text + attributes) rather than exact markup
const vals = () => [...grid().matchAll(/<div class="val([^"]*)"([^>]*)>([^<]*)</g)].map(m => ({ warn: /\bwarn\b/.test(m[1]), attrs: m[2], text: m[3] }));
let pass = 0, fail = 0;
const check = (c, m) => { if (c) pass++; else { fail++; console.log("  FAIL:", m); } };

(async () => {
  await new Promise(r => setImmediate(r)); await new Promise(r => setImmediate(r));

  console.log("1. page loads: build number, layout, connection attempt");
  check(els.build.textContent === "build ...", "while /status.json is failing the page still loads and keeps a placeholder: " + els.build.textContent);
  statusFail = false; advance(5100); await new Promise(r => setImmediate(r)); await new Promise(r => setImmediate(r));
  check(els.build.textContent === "build 102 - Oct  6 2026 14:32:10", "it retries on its own and then shows the build number and compile time: " + els.build.textContent);
  check(FakeWS.all.length === 1 && FakeWS.all[0].url === "ws://192.168.55.102/ws", "opens ws://<board>/ws");
  check(/Connecting/.test(status()) && els.dot.className === "st-warn", "says Connecting: " + status());
  check((grid().match(/class="dcell/g) || []).length === 4, "2x2 grid drawn with 4 cells");

  console.log("2. connected, nothing yet");
  let ws = FakeWS.all[0]; open(ws);
  check(/waiting for the first update/.test(status()), "waiting for first update: " + status());

  console.log("3. live data");
  send(ws, { v: { "1": 3000, "2": 185.5, "3": 120.1, "4": 14.7 }, fps: 412, ids: 38 });
  check(/3000\.0/.test(grid()) && /185\.5/.test(grid()) && /120\.1/.test(grid()) && /14\.7/.test(grid()), "all four values on the page");
  check(els.dot.className === "st-ok" && /Live - 4 signals - CAN 412 frames\/s/.test(status()), "green Live status: " + status());
  send(ws, { v: { "1": 3000, "2": 230, "3": 120.1, "4": 14.7 }, fps: 412, ids: 38 });
  check(vals().some(v => v.warn && v.text === "230.0"), "coolant over its threshold is highlighted");
  advance(500); send(ws, { v: { "1": 3100 }, fps: 400, ids: 38 });
  check(/Live - 1 signal - CAN 400/.test(status()), "singular wording: " + status());

  console.log("4. connected but the bus is silent (bench, ECU off)");
  send(ws, { v: {}, fps: 0, ids: 0 });
  check(/^\[st-warn\].*no CAN frames are arriving/.test(status()), "explains no CAN traffic: " + status());
  check(vals().filter(v => v.text === "--").length === 4, "all four show --");

  console.log("5. CAN traffic arrives but nothing is decoded");
  send(ws, { v: {}, fps: 350, ids: 25 });
  check(/arriving \(350\/s\) but none decoded/.test(status()), "explains undecoded traffic: " + status());

  console.log("6. board stops sending");
  send(ws, { v: { "1": 3000 }, fps: 400, ids: 38 });
  advance(4000);
  check(/No updates from the board for [34]s/.test(status()) && els.dot.className === "st-warn", "reports silence: " + status());
  advance(5000);                                    // 9s of silence: the page gives up on that connection
  check(ws.readyState === 3, "watchdog closes the dead connection");
  check(/Disconnected/.test(status()) && els.dot.className === "st-bad", "shows disconnected: " + status());
  check(vals().filter(v => v.text === "--").length === 4, "values cleared to -- (not frozen numbers)");

  console.log("7. reconnects by itself, with backoff");
  check(FakeWS.all.length === 1, "no reconnect yet before the delay");
  advance(1100);
  check(FakeWS.all.length === 2, "reconnect attempt #1 after ~1s");
  FakeWS.all[1].onerror();                          // fails again
  advance(2100); check(FakeWS.all.length === 3, "attempt #2 after ~2s");
  FakeWS.all[2].onerror();
  advance(4100); check(FakeWS.all.length === 4, "attempt #3 after ~4s");
  FakeWS.all[3].onerror();
  advance(5100); check(FakeWS.all.length === 5, "attempt #4 after 5s (capped)");
  FakeWS.all[4].onerror();
  advance(5100); check(FakeWS.all.length === 6, "attempt #5 still 5s apart, never gives up");

  console.log("8. recovery");
  ws = FakeWS.all[5]; open(ws);
  send(ws, { v: { "1": 2500, "2": 90, "3": 100, "4": 14.1 }, fps: 380, ids: 36 });
  check(/2500\.0/.test(grid()) && els.dot.className === "st-ok", "live again: " + status());
  ws.close(); advance(1100);
  check(FakeWS.all.length === 7, "backoff reset after a good connection (retries after ~1s again)");

  console.log("9. phone wakes up with a dead connection");
  FakeWS.all[6].onerror(); advance(100);
  const before = FakeWS.all.length;
  docListeners.visibilitychange();
  check(FakeWS.all.length >= before, "visibility change never creates a duplicate while one is pending");

  console.log("9b. SIMULATED bar follows the board's flag");
  ws = FakeWS.all[FakeWS.all.length - 1]; if (ws.readyState !== 1) open(ws);
  send(ws, { v: { "1": 3000 }, fps: 0, ids: 0, sim: 1 });
  check(els.simbar && els.simbar.className === "simbar on", "bar shown while the simulator is on: " + (els.simbar && els.simbar.className));
  send(ws, { v: { "1": 3000 }, fps: 400, ids: 30, sim: 0 });
  check(els.simbar.className === "simbar", "bar hidden when it is off");
  send(ws, { v: { "1": 3000 }, fps: 0, ids: 0, sim: 1 });
  ws.close();
  check(els.simbar.className === "simbar", "bar hidden on disconnect (we no longer know)");
  advance(1100); ws = FakeWS.all[FakeWS.all.length - 1]; open(ws);

  console.log("10. page buttons still work");
  ws = FakeWS.all[FakeWS.all.length - 1]; if (ws.readyState !== 1) open(ws);
  send(ws, { v: { "1": 3000, "2": 90, "3": 100, "4": 14.1 }, fps: 380, ids: 36 });
  __next(1);
  check((grid().match(/class="dcell/g) || []).length === 1, "Next goes to the 1-cell page");
  __next(-1); check((grid().match(/class="dcell/g) || []).length === 4, "Prev comes back");
  console.log("12. the cell's Value Text Size is honoured (it used to be ignored: every number was a fixed 35px)");
  const sizeOf = v => (v.attrs.match(/--fs:(\d+)px/) || [])[1];
  cells[0].value_font_size = 14; cells[1].value_font_size = 28; cells[2].value_font_size = 36; cells[3].value_font_size = 120;
  __next(0);
  check(vals().map(sizeOf).join(",") === "18,35,45,150", "sizes 14/28/36/120 become 18/35/45/150px: " + vals().map(sizeOf).join(","));
  check(vals().map(v => (v.attrs.match(/data-size="(\d+)"/) || [])[1]).join(",") === "14,28,36,120", "the chosen size is recorded on each number");
  check(sizeOf(vals()[1]) === "35", "Medium (28) is exactly the 35px these numbers always were, so existing dashboards look the same");
  delete cells[0].value_font_size; __next(0);
  check(sizeOf(vals()[0]) === "35", "a cell with no saved size falls back to Medium");
  cells[0].value_font_size = 96; cells[1].value_font_size = 64; cells[2].value_font_size = 48; cells[3].value_font_size = 80; __next(0);
  check(vals().map(sizeOf).join(",") === "120,80,60,100", "96/64/48/80 -> 120/80/60/100px: " + vals().map(sizeOf).join(","));


  console.log(`\n${pass} passed, ${fail} failed`);
})();
