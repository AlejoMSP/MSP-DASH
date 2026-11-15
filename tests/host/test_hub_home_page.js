// The hub's home page (hub_pages.h HOME_HTML) with a fake hub: screens with their links, the status grid, and the
// navigation bar on every hub page.
const vm = require('vm'), { hubPage, hubScript } = require('./hub_page.js');
let pass = 0, fail = 0; const check = (c, m) => { if (c) pass++; else { fail++; console.log("  FAIL:", m); } };
const settle = async () => { for (let i = 0; i < 20; i++) await new Promise(r => setImmediate(r)); };
const els = {}; const el = id => els[id] || (els[id] = { innerHTML: "", textContent: "", value: "" });
const state = { lightsV: 4.5, lightsOn: false, dimV: 4.5, bri: 100, build: 30, ip: "192.168.55.204", ch: 10, last: "brightness 100", ecu: "rusEFI",
  live: 25, sent: 7, sim: "off", dht: "77 F, 40% humidity", fuel: 38, egt: "26 C / 78 F", egtsent: 5, cfg: { mode: 1, day: 100, nmin: 20, nmax: 60, dlo: 2, dhi: 12, lon: 6 } };
const screens = [{ name: "Right", online: true, ip: "192.168.55.233", build: 185, mac: "80:45:6B:33:94:68", age: 100 },
                 { name: "Center", online: true, ip: "192.168.55.231", build: 185, mac: "80:45:6B:33:96:CC", age: 100 },
                 { name: "Left", online: false, ip: "192.168.55.232", build: 184, mac: "80:45:6B:33:94:40", age: 90000 }];
const ctx = { document: { getElementById: el }, setInterval: () => 0, console,
  fetch: async u => ({ json: async () => u === "/state" ? state : u === "/screens.json" ? screens : u === "/speed.json" ? { ecuok: true, ecumph: 0, gpsok: true, fix: false, sats: 3 } : u === "/i2c.json" ? { found: [0x3C, 0x48, 0x68, 0x55], ads: 0x48, adsv: [0.0123, 1.65, 3.3, 0] } : u === "/imu.json" ? { found: true, fwd: -1, left: 3, zeroed: false, zeroing: false, lat: 0.82, lon: -0.31, vert: 0.02, yaw: 24.5, raw: [0, 0, 1, 0] } : null, text: async () => "" }) };
vm.createContext(ctx); vm.runInContext(hubScript('HOME_HTML'), ctx);
(async () => {
  await settle();
  const s = el("scr").innerHTML;
  check(s.indexOf("Left") < s.indexOf("Center") && s.indexOf("Center") < s.indexOf("Right"), "screens listed Left, Center, Right");
  for (const [ip, p] of [["192.168.55.231", "/gauges"], ["192.168.55.231", "/log-channels"], ["192.168.55.232", "/settings"], ["192.168.55.233", "/sd"], ["192.168.55.233", "/"]])
    check(s.includes('href="http://' + ip + p + '"'), "link to " + ip + p);
  check(/OFFLINE, last heard 2 min ago/.test(s), "an offline screen says when it was last heard");
  const st = el("stat").innerHTML;
  check(/ECU<\/b>rusEFI/.test(st) && /Signals live<\/b>25/.test(st) && /build 30/.test(st), "status grid: ECU, signals, hub build");
  check(/77 F/.test(el("sens").innerHTML) && /38 %/.test(el("sens").innerHTML), "hub sensors: ambient and fuel");
  check(/100 % now/.test(el("brisum").textContent), "brightness folded away, with a one-line summary");
  { const m = el("imu").innerHTML;
    check(/Lateral<\/b>0.82 G/.test(m) && /Accel \/ brake<\/b>-0.31 G/.test(m) && /Yaw rate<\/b>24.5 deg\/s/.test(m) && /not yet/.test(m), "accelerometer card: G values, yaw, zero not taken yet");
    check(el("ifwd").value === "-1" && el("ileft").value === "3" && /\+Z<\/option>/.test(el("ifwd").innerHTML), "mounting boxes show the saved axes (-X front, +Z left)"); }
  { const b = el("i2c").innerHTML;
    check(/0x3C<\/b>OLED/.test(b) && /0x48<\/b>ADS1115/.test(b) && /0x68<\/b>MPU6050/.test(b) && /0x55<\/b>unknown device/.test(b), "I2C card: each address found, named");
    check(/ADS1115 A1<\/b>1.650 V/.test(b) && /ADS1115 A2<\/b>3.300 V/.test(b), "ADS1115 inputs in volts"); }
  for (const p of ["HOME_HTML", "SENSORS_HTML", "SIM_HTML", "CUSTOM_HTML", "RF_HTML", "BUTTONS_HTML"]) {
    const h = hubPage(p);
    check(/<div class="nav">/.test(h) && /href="\/buttons"/.test(h) && /href="\/oled"/.test(h) && /\.nav\{/.test(h), p + " has the navigation bar (with OLED)");
  }
  console.log(`\n${pass} passed, ${fail} failed`); process.exit(fail ? 1 : 0);
})();
