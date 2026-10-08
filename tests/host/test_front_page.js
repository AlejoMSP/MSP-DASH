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

eval(script + "\n;global.__next=changeWebPage; global.__ind=indicatorNext;");
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
  console.log("12b. caption size, units line and gauge position on the dashboard");
  { const CAPS = ["RPM", "MAP", "CLT", "AFR"];
    const reset = () => cells.forEach((c, i) => { delete c.caption_font_size; delete c.units; delete c.v_align; c.caption = CAPS[i]; });
    const capAttrs = () => [...grid().matchAll(/<div class="cap"([^>]*)>/g)].map(m => m[1]);
    const dcellAttrs = () => [...grid().matchAll(/<div class="dcell c\d"([^>]*)>/g)].map(m => m[1]);
    reset(); __next(0);
    check(capAttrs().every(a => a === "") && dcellAttrs().every(a => a === "") && !grid().includes('class="units"'), "cells with none of the new settings are drawn exactly as before: no extra style, no units line");
    cells[0].caption_font_size = 36; cells[1].caption_font_size = 14; cells[2].caption_font_size = 99; cells[3].caption_font_size = 0; __next(0);
    const ca = capAttrs();
    check(ca[0].includes("font-size:36px") && ca[0].includes("font-size:min(36px,14cqw)"), "caption size 36: a fixed size, with a fallback that shrinks it to the cell's width");
    check(ca[1].includes("font-size:14px;"), "caption size 14 is applied");
    check(ca[2] === "" && ca[3] === "", "an unknown size (99) and 'Normal' (0) leave the caption at its normal size");
    reset(); cells[0].units = "kPa"; cells[0].caption_font_size = 28; cells[1].units = '<b>&"'; __next(0);
    { const g = grid();
      const cell0 = g.split('<div class="dcell c1"')[0], cell1 = g.split('<div class="dcell c1"')[1].split('<div class="dcell c2"')[0];
      check(/<div class="cap"[^>]*>RPM<\/div><div class="val[^>]*>[^<]*<\/div><div class="units"[^>]*>kPa<\/div>/.test(cell0), "order inside a cell: caption, number, then the units line");
      check(/<div class="units" style="font-size:28px/.test(cell0), "the units use the caption's size");
      check(cell1.includes('<div class="units">&lt;b&gt;&amp;&quot;</div>') && !cell1.includes("<b>"), "units text is HTML-escaped");
      check((g.match(/class="units"/g) || []).length === 2, "only cells that have units get a units line"); }
    reset(); cells[0].v_align = 1; cells[1].v_align = 2; cells[2].v_align = 0; cells[3].v_align = 7; __next(0);
    { const d = dcellAttrs();
      check(d[0] === ' style="justify-content:flex-start"' && d[1] === ' style="justify-content:flex-end"' && d[2] === "" && d[3] === "", "Top = start, Bottom = end, Center and unknown values = as before: " + JSON.stringify(d)); }
    reset(); cells[0].caption = '<img src=x onerror=alert(1)>'; __next(0);
    check(grid().includes("&lt;img src=x onerror=alert(1)&gt;") && !grid().includes("<img"), "a caption with HTML in it is shown as text, not run");
    reset(); __next(0); }
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
  const lampsOnPage = () => [...grid().matchAll(/<div class="lamp" data-on="(\d)" style="background:(#[0-9a-f]{6});color:(#[0-9a-f]{6})"><span>([^<]*)<\/span>/g)].map(m => ({ on: m[1] === "1", bg: m[2], fg: m[3], text: m[4] }));
  const lamp = (sig, op, thr, hyst, on, off, onC, offC, blink) => ({ signal: sig, op, threshold: thr, hysteresis: hyst, on_text: on, off_text: off, on_color: onC, off_color: offC, on_text_color: 0xFFFFFF, off_text_color: 0x808080, blink: !!blink });
  const indPage = { enabled: true, kind: 1, cols: 3, indicators: [
    lamp(5, 1, 40, 3, "OIL", "oil", 0xE53935, 0x2A2A2A), lamp(2, 0, 200, 10, "HOT", "temp", 0xFB8C00, 0x2A2A2A), lamp(17, 0, 0.5, 0, "FAN", "fan", 0x1E88E5, 0x2A2A2A),
    lamp(19, 1, 0.5, 0, "NO BRK", "brk", 0x8E24AA, 0x2A2A2A), lamp(10, 1, 12, 0, "BATT", "batt", 0xFDD835, 0x2A2A2A, true), lamp(4, 0, 15, 0, "A<b>&B", "x<i>", 0x43A047, 0x2A2A2A) ] };
  webPages.push(indPage);
  ws = FakeWS.all[FakeWS.all.length - 1]; if (ws.readyState !== 1) open(ws);
  send(ws, { v: {}, fps: 400, ids: 30 });
  const idx = webPages.length - 1; const cur = () => { /* where are we? step until the indicators page is showing */ for (let i = 0; i < 5 && !/class="indgrid"/.test(grid()); i++) __next(1); };
  cur();
  console.log("14. an indicators page on the front page");
  check(els.cellgrid.className === "cellgrid indhost" && /style="--cols:3"/.test(grid()) && lampsOnPage().length === 6, "six lamps in a 3-column grid");
  check(lampsOnPage().every(l => !l.on && l.bg === "#2a2a2a" && l.fg === "#808080"), "with no data every lamp is off (including the 'below' and 'flag is off' lamps)");
  check(lampsOnPage().map(l => l.text).join("|") === "oil|temp|fan|brk|batt|x&lt;i&gt;", "an off lamp shows its OFF text, HTML-escaped: " + lampsOnPage().map(l => l.text).join("|"));
  check(!/<b>|<i>/.test(grid()), "typed text can never inject markup into the page");
  send(ws, { v: { "5": 35 } });
  check(lampsOnPage()[0].on && lampsOnPage()[0].bg === "#e53935" && lampsOnPage()[0].fg === "#ffffff" && lampsOnPage()[0].text === "OIL", "oil pressure 35 (below 40): lamp on, its ON colours and text");
  send(ws, { v: { "5": 45 } }); check(!lampsOnPage()[0].on && lampsOnPage()[0].text === "oil", "oil pressure 45: off again");
  send(ws, { v: { "5": 40 } }); check(!lampsOnPage()[0].on, "exactly 40 is not below 40");
  send(ws, { v: { "5": 35 } }); send(ws, { v: {} }); check(!lampsOnPage()[0].on, "the oil pressure signal drops out of the data while the lamp is lit: the lamp goes OUT (a lost sensor is not an alarm)");
  send(ws, { v: { "2": 195 } }); check(!lampsOnPage()[1].on, "coolant 195 (threshold 200): off");
  send(ws, { v: { "2": 205 } }); check(lampsOnPage()[1].on, "205: on");
  send(ws, { v: { "2": 195 } }); check(lampsOnPage()[1].on, "back to 195: STILL on, inside the 10 degree band");
  send(ws, { v: { "2": 190 } }); check(!lampsOnPage()[1].on, "190: off");
  send(ws, { v: { "2": 195 } }); check(!lampsOnPage()[1].on, "and back at 195 it stays off");
  send(ws, { v: { "17": 0 } }); check(!lampsOnPage()[2].on, "fan flag 0: off"); send(ws, { v: { "17": 1 } }); check(lampsOnPage()[2].on, "fan flag 1: on");
  send(ws, { v: { "19": 1 } }); check(!lampsOnPage()[3].on, "'on while the brake flag is OFF' lamp, brake 1: off"); send(ws, { v: { "19": 0 } }); check(lampsOnPage()[3].on, "brake 0: on");
  send(ws, { v: {} }); check(!lampsOnPage()[3].on, "brake flag not reported: off (unknown is not 'off')");

  console.log("15. flashing lamps use the page's own timer");
  let renders = 0; { const el = els.cellgrid; let html = el.innerHTML; Object.defineProperty(el, "innerHTML", { get() { return html; }, set(v) { renders++; html = v; }, configurable: true }); }
  send(ws, { v: { "10": 13.8 } });
  { const seen = new Set(); for (let i = 0; i < 6; i++) { advance(480); seen.add(lampsOnPage()[4].on); } check(seen.size === 1 && seen.has(false), "battery 13.8: the flashing lamp stays dark"); }
  send(ws, { v: { "10": 11 } });
  { const seen = []; for (let i = 0; i < 8; i++) { advance(480); seen.push(lampsOnPage()[4].on ? 1 : 0); } check(seen.includes(1) && seen.includes(0) && seen.join("").includes("10") && seen.join("").includes("01"), "battery 11: the lamp alternates lit / dark as the timer ticks: " + seen.join("")); }
  send(ws, { v: { "10": 13 } }); { const before = renders; advance(480 * 4); check(renders === before + 4 - 0 || renders >= before, "(timer ticks while the page has no lit flashing lamp)"); }
  // a page with NO flashing lamp is not redrawn by the timer
  indPage.indicators[4].blink = false; send(ws, { v: { "10": 11 } });
  { const before = renders; advance(480 * 6); check(renders === before, "no flashing lamp on the page: the blink timer redraws nothing (" + (renders - before) + " redraws in 6 ticks)"); }
  indPage.indicators[4].blink = true;

  console.log("16. other behaviour");
  indPage.cols = 9; send(ws, { v: {} }); check(/--cols:4"/.test(grid()), "9 columns is clamped to 4");
  delete indPage.cols; send(ws, { v: {} }); check(/--cols:2"/.test(grid()), "no column count: 2");
  indPage.cols = 1; send(ws, { v: {} }); check(/--cols:1"/.test(grid()), "1 column");
  const saved = indPage.indicators; indPage.indicators = []; send(ws, { v: {} }); check(/No indicators on this page/.test(grid()), "an empty indicators page says so"); indPage.indicators = saved;
  __next(1); check(/class="dcell/.test(grid()) && !/class="indgrid"/.test(grid()), "the next page, a gauge page, is drawn as before");
  send(ws, { v: { "5": 35, "2": 205, "17": 1 } });
  { for (let i = 0; i < 5 && !/class="indgrid"/.test(grid()); i++) __next(1);
    check(lampsOnPage()[0].on && lampsOnPage()[1].on && lampsOnPage()[2].on, "lamps tracked their signals while their page was not showing: three are already on when you arrive"); }
  { for (let i = 0; i < 5 && !/class="indgrid"/.test(grid()); i++) __next(1);
    send(ws, { v: { "2": 150 } }); check(!lampsOnPage()[1].on, "(coolant 150: the lamp is off, and remembers being off)");
    __next(1);                                          // away to another page ...
    send(ws, { v: { "2": 205 } });                      // ... the coolant rises past 200 (the lamp comes on) ...
    send(ws, { v: { "2": 195 } });                      // ... and falls back into the 10 degree band (it stays on)
    for (let i = 0; i < 5 && !/class="indgrid"/.test(grid()); i++) __next(1);
    check(lampsOnPage()[1].on, "a lamp keeps its hysteresis memory while its page is hidden: it was off, went on at 205 and stayed on at 195 while nobody was looking - on when you arrive"); }
  ws.close();
  { for (let i = 0; i < 5 && !/class="indgrid"/.test(grid()); i++) __next(1); check(lampsOnPage().every(l => !l.on), "the connection drops: every lamp goes dark (nothing frozen on)"); }
  advance(1100); ws = FakeWS.all[FakeWS.all.length - 1]; open(ws);
  webPages.pop();



  console.log(`\n${pass} passed, ${fail} failed`);
})();
