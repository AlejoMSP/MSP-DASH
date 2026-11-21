// The hub's Backup page (BACKUP_HTML) with a fake hub and screens: export collects the hub and every online screen
// into one file; restore sends each ticked part to its board (screens first, the hub last), a screen part onto a
// different screen without its name / network lines, and shows each board's answer.
const vm = require('vm'), { hubScript, hubPage } = require('./hub_page.js');
let pass = 0, fail = 0; const check = (c, m) => { if (c) pass++; else { fail++; console.log("  FAIL:", m); } };
const settle = async () => { for (let i = 0; i < 30; i++) await new Promise(r => setImmediate(r)); };
const boards = { screens: [{ name: "Left", ip: "192.168.55.201", online: true, build: 224 }, { name: "Center", ip: "192.168.55.202", online: true, build: 224 }, { name: "Right", ip: "192.168.55.203", online: false, build: 220 }] };
const els = {}; const el = id => els[id] || (els[id] = { id, innerHTML: "", textContent: "", value: "", className: "", checked: true, files: [] });
const calls = []; let downloaded = null, failIp = null;
const ctx = { document: { getElementById: el, createElement: () => ({ click() { downloaded = this; }, remove() {} }), body: { appendChild() {} } },
  URL: { createObjectURL: b => b }, Blob: function (parts) { this.text = parts.join(""); }, JSON, Date, console, confirm: () => true, Promise,
  fetch: async (u, o) => { calls.push({ u, o });
    if (u === "/boards.json") return { ok: true, json: async () => boards };
    if (failIp && u.includes(failIp)) throw new Error("down");
    if (u.startsWith("/backup.txt")) return { ok: true, text: async () => "# msp-dash hub build 62\nn shift cfg blob 01" };
    if (u.includes("/backup.txt")) return { ok: true, text: async () => "# screen\nn link name str 4c656674\nn net ip str 31\nn disp diam i32 520\nf cfg_display.json 7b7d" };
    if (u.endsWith("/restore")) return { ok: true, text: async () => "Restored" };
    return { ok: false, text: async () => "?" }; } };
vm.createContext(ctx); vm.runInContext(hubScript('BACKUP_HTML'), ctx);
const R = c => vm.runInContext(c, ctx);
(async () => {
  console.log("1. export");
  check(/<a href="\/backup">Backup<\/a>/.test(hubPage('HOME_HTML')), "Backup is in the menu");
  el("pw").checked = false; failIp = "192.168.55.202";
  await R("exportAll()"); await settle();
  const file = JSON.parse(downloaded.href.text);
  check(file.msp_dash_backup === 1 && file.parts.length === 2 && file.parts[0].board === "Hub" && file.parts[1].board === "Left", "hub + the online screens that answered (Right is offline)");
  check(calls.some(c => c.u === "/backup.txt?pw=0") && calls.some(c => c.u === "http://192.168.55.201/backup.txt?pw=0"), "passwords left out unless ticked");
  check(/not reached: Center/.test(el("exp").textContent) && el("msg").className === "bad", "a screen that did not answer is named");
  check(/^msp-dash-backup-\d{4}-\d\d-\d\d\.json$/.test(downloaded.download), "file named by date");
  console.log("2. restore");
  failIp = null; calls.length = 0;
  file.parts.push({ board: "Center", kind: "screen", text: "n link name str 43\nn disp diam i32 480" });
  el("file").files = [{ text: async () => JSON.stringify(file) }];
  await R("readFile()"); await settle();
  check(/Hub/.test(el("parts").innerHTML) && /this hub/.test(el("parts").innerHTML) && /<select id="t1">/.test(el("parts").innerHTML), "lists each part, with where it goes");
  el("t1").value = "192.168.55.202";   // Left's part onto Center: layout only
  el("t2").value = "192.168.55.202";   // Center's part onto Center: everything
  await R("restore()"); await settle();
  const posts = calls.filter(c => c.o && c.o.method === "POST");
  check(posts.length === 3 && posts[2].u === "/restore", "three restores, the hub last");
  const toCenter = posts.filter(c => c.u === "http://192.168.55.202/restore");
  check(toCenter.length === 2 && toCenter.every(c => c.o.headers["X-MSP"] === "1"), "screens get the X-MSP header");
  check(!/n link |n net /.test(toCenter[0].o.body) && /n disp diam/.test(toCenter[0].o.body) && /f cfg_display/.test(toCenter[0].o.body), "Left's part onto Center: no name or network lines");
  check(/n link name/.test(toCenter[1].o.body), "Center's own part: everything");
  check(/Hub: Restored/.test(el("msg").textContent) && el("msg").className === "", "each board's answer shown");
  el("file").files = [{ text: async () => "nope" }]; await R("readFile()"); await settle();
  check(/Not an MSP-DASH backup/.test(el("msg").textContent), "a wrong file is refused");
  console.log(`\n${pass} passed, ${fail} failed`); process.exit(fail ? 1 : 0);
})();
