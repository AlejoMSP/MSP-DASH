// The hub's Board status page (BOARDS_HTML) with a fake hub: the hub's card, screens left to right, warning colours,
// an older screen without stats, an offline screen.
const vm = require('vm'), { hubScript, hubPage } = require('./hub_page.js');
let pass = 0, fail = 0; const check = (c, m) => { if (c) pass++; else { fail++; console.log("  FAIL:", m); } };
const settle = async () => { for (let i = 0; i < 20; i++) await new Promise(r => setImmediate(r)); };
const base = { cpu0: 20, cpu1: 35, heap: 120, heapMin: 90, heapBig: 64, temp: 45, rssi: -60, mhz: 240, uptime: 3725 };
const D = { hub: Object.assign({ name: "Hub", build: 58, ip: "192.168.55.204", loops: 900, canFps: 320, canState: "running", canRxMissed: 0, canBusErr: 0, canTxFail: 0, sentPs: 25, apClients: 0 }, base),
  screens: [
    Object.assign({ name: "Right", build: 220, ip: "192.168.55.203", age: 800, online: true, stats: true, fps: 30, draw: 40, rxPs: 25, rxAge: 1, psram: 7000, logging: false }, base, { heap: 12 }),
    { name: "Center", build: 215, ip: "192.168.55.202", age: 1200, online: true, stats: false },
    Object.assign({ name: "Left", build: 220, ip: "192.168.55.201", age: 30000, online: false, stats: true, fps: 5, draw: 97, rxPs: 0, rxAge: -1, psram: 7000, logging: true }, base, { temp: 82 }) ] };
const els = {}; const el = id => els[id] || (els[id] = { innerHTML: "" });
const ctx = { document: { getElementById: el }, setInterval: () => 0, console, Math, String, fetch: async u => ({ json: async () => JSON.parse(JSON.stringify(D)) }) };
vm.createContext(ctx); vm.runInContext(hubScript('BOARDS_HTML'), ctx);
(async () => {
  await settle();
  const h = el("boards").innerHTML, cards = h.split('<div class="card').slice(1);
  console.log("1. layout");
  check(/<a href="\/boards">Board status<\/a>/.test(hubPage('HOME_HTML')), "Board status is in the menu");
  check(cards.length === 4 && /<h3>Hub<\/h3>/.test(cards[0]) && /<h3>Left<\/h3>/.test(cards[1]) && /<h3>Center<\/h3>/.test(cards[2]) && /<h3>Right<\/h3>/.test(cards[3]), "hub first, then the screens left to right");
  console.log("2. the hub");
  check(/Main loop<\/b><span>900 \/s/.test(cards[0]) && /CAN frames in<\/b><span>320 \/s/.test(cards[0]) && /CAN bus<\/b><span>running/.test(cards[0]) && /Sent to screens<\/b><span>25 \/s/.test(cards[0]), "loop, CAN, sent to screens");
  check(/Up for<\/b><span>1h 2m/.test(cards[0]) && /CPU busy, core 1<\/b><span>35 %/.test(cards[0]) && /build 58/.test(cards[0]), "uptime, CPU, build");
  console.log("3. screens");
  check(/Frames per second<\/b><span>30/.test(cards[3]) && /class="kv bad"><b>Free RAM<\/b><span>12 KB/.test(cards[3]), "right: 30 fps; 12 KB free RAM in red");
  check(/older build that does not send its numbers/.test(cards[2]), "center on an older build: says to update it");
  check(/dot off/.test(cards[1]) && /offline, last heard 30 s ago/.test(cards[1]) && /opacity:.6/.test(cards[1]), "left offline: red dot, faded, how long ago");
  check(/class="kv bad"><b>Frames per second<\/b><span>5/.test(cards[1]) && /class="kv bad"><b>Chip temperature<\/b><span>82/.test(cards[1]) && /class="kv bad"><b>Last data<\/b><span>never/.test(cards[1]), "low fps, hot chip, no data: red");
  check(/SD logging<\/b><span>running/.test(cards[1]) && /class="kv na"><b>SD logging<\/b><span>not running/.test(cards[3]), "SD logging shown");
  console.log(`\n${pass} passed, ${fail} failed`); process.exit(fail ? 1 : 0);
})();
