// The display editor's text-size, dial-angle and Save & Reboot features, driven with a fake browser and a fake board.
const { pageScript, pageHtml } = require('./extract_page.js');
let pass = 0, fail = 0; const check = (c, m) => { if (c) pass++; else { fail++; console.log("  FAIL:", m); } };

// ---- fake browser: controllable clock, instant timers ----
let now = 5000000;
global.Date.now = () => now;
global.setTimeout = (fn, ms) => { now += ms || 0; fn(); return 0; };
const els = {};
const getEl = id => els[id] || (els[id] = { id, innerHTML: "", textContent: "", value: "" });
global.document = { getElementById: getEl, activeElement: null };
global.confirm = () => true;
global.window = global;

// ---- fake board ----
const board = { posts: [], status: "up", statusCalls: 0, saveStatus: 200, rebootFails: false, comesBackAfter: 2, build: 107 };
const cellJson = (o) => Object.assign({ enabled: true, signal: 4, gauge_type: 0, min: 0, max: 250, rest: 0, caption: "AFR", needle_color: 0xFFFFFF, tick_color: 0xFFFFFF, bg_color: 0,
  value_divisor: 1, tick_count: 26, major_nth: 5, refresh_hz: 10, show_value: true, value_font_size: 28, tick_font_size: 28, start_deg: 225, end_deg: 135,
  has_warning: false, warning_threshold: 0, warning_above: false, alert_fullscreen: false, history_sec: 30, ref_enabled: false, ref_value: 0 }, o);
global.fetch = async (url, opts) => {
  if (opts && opts.method === "POST") {
    board.posts.push({ url, headers: opts.headers || {}, body: opts.body });
    if (url === "/gauges") return { ok: board.saveStatus === 200, status: board.saveStatus, text: async () => board.saveStatus === 200 ? "OK" : "NOT saved to flash: test" };
    if (url === "/reboot") { if (board.rebootFails) throw new Error("network down"); board.rebootedAt = now; return { ok: true, status: 200, text: async () => "Rebooting" }; }
  }
  if (url === "/gauges.json") return { json: async () => [{ enabled: true, layout: 0, cells: [cellJson({})] }, { enabled: true, layout: 3, cells: [cellJson({ gauge_type: 2, start_deg: 270, end_deg: 90 }), cellJson({ gauge_type: 1 }), cellJson({ gauge_type: 3 }), cellJson({})] }] };
  if (url === "/signals.json") return { json: async () => [{ id: 0, name: "None", age_ms: 1 }, { id: 4, name: "AFR", age_ms: 10 }] };
  if (url === "/status.json") {
    board.statusCalls++;
    if (board.status === "never-restarts") return { ok: true, json: async () => ({ build: board.build }) };
    if (board.status === "never-returns" && board.rebootedAt) throw new Error("down");
    if (board.status === "up" && board.rebootedAt) {                // restarts: down for a while, then answers again
      const sinceReboot = now - board.rebootedAt;
      if (sinceReboot < 4000) throw new Error("down");
    }
    return { ok: true, json: async () => ({ build: board.build }) };
  }
  throw new Error("unexpected fetch " + url);
};

const script = pageScript('GAUGES_HTML');
eval(script + "\n;global.__t = { renderCell, dialGeometry, dialSvg, setAngle, changeGaugeType, save, saveAndReboot, emptyCell, VALUE_SIZES, TICK_SIZES, getPages: () => pages, setPages: p => { pages = p; loaded = true; } };");
const T = global.__t;
const settle = async () => { for (let i = 0; i < 8; i++) await new Promise(r => global.process.nextTick(r)); };

(async () => {
  await settle();

  console.log("1. dial geometry (what the preview draws) matches the display's rules, from a hand-written table");
  //            start  end  half  -> [start, sweep, standard?]
  const GEO = [[225,135,0,225,270,false],[270,90,1,270,180,false],[0,0,0,225,270,true],[0,0,1,270,180,true],[10,15,0,225,270,true],[0,90,0,0,90,false],[90,180,0,90,90,false],
    [180,0,0,180,180,false],[300,60,0,300,120,false],[45,315,0,45,270,false],[0,359,0,0,359,false],[359,0,0,225,270,true],[350,10,0,350,20,false],[-135,135,0,225,270,false],
    [500,140,0,225,270,true],[135,45,0,135,270,false],[90,270,0,90,180,false],[5,5,1,270,180,true],[0,10,0,0,10,false],[0,9,0,225,270,true]];
  for (const [s, e, half, es, ew, estd] of GEO) {
    const g = T.dialGeometry({ gauge_type: half ? 2 : 0, start_deg: s, end_deg: e });
    check(g.start === es && g.sweep === ew && g.standard === estd, `start ${s}, end ${e}${half ? " (half moon)" : ""}: starts ${es}, sweeps ${ew}${estd ? ", standard dial" : ""} - got ${g.start}/${g.sweep}/${g.standard}`);
  }

  console.log("2. the controls shown for each gauge type");
  const html = (c) => T.renderCell(0, 0, c);
  const needle = html(cellJson({})), half = html(cellJson({ gauge_type: 2 })), number = html(cellJson({ gauge_type: 1 })), trend = html(cellJson({ gauge_type: 3 }));
  for (const [name, h, want] of [["needle", needle, true], ["half moon", half, true], ["number", number, false], ["trend graph", trend, false]]) {
    check(h.includes("setAngle(0,0,'start_deg'") === want && h.includes("setAngle(0,0,'end_deg'") === want, `${name}: angle boxes ${want ? "shown" : "hidden"}`);
    check(h.includes("Dial numbers") === want, `${name}: dial-number size ${want ? "shown" : "hidden"}`);
    check(h.includes("Value Text Size"), `${name}: text size always shown`);
  }
  const sizeSelect = needle.slice(needle.indexOf("Value Text Size"), needle.indexOf("</select>", needle.indexOf("Value Text Size")));
  check((sizeSelect.match(/<option value="(14|28|36|48|64|80|96|120)"/g) || []).length === 8, "the Value Text Size list has all eight sizes");
  check(T.VALUE_SIZES.map(v => v[0]).join(",") === "14,28,36,48,64,80,96,120", "sizes are 14, 28, 36, 48, 64, 80, 96, 120 (the same list the display firmware has)");
  check(html(cellJson({ value_font_size: 96 })).includes('<option value="96" selected>'), "the saved size is the one selected");
  check(needle.includes('<option value="28" selected>28 Medium') , "dial numbers: the saved size (28) is selected");
  check(html(cellJson({ tick_font_size: -1 })).includes('<option value="-1" selected>Hidden'), "dial numbers: Hidden can be selected");
  check(T.TICK_SIZES.map(v => v[0]).join(",") === "-1,14,28,36", "dial number choices: hidden, 14, 28, 36");
  check(needle.includes('id="dial_0_0"') && needle.includes("225&deg; sweep".replace("225", "270")) , "a preview is drawn and says 270 degrees for the standard gauge");

  console.log("3. typing an angle redraws the preview and keeps the model in step");
  T.setPages([{ enabled: true, layout: 0, cells: [cellJson({})] }]);
  const c0 = T.getPages()[0].cells[0];
  els.dial_0_0 = { innerHTML: "" };
  T.setAngle(0, 0, "start_deg", "200"); T.setAngle(0, 0, "end_deg", "340");
  check(c0.start_deg === 200 && c0.end_deg === 340, "the values are stored as numbers");
  check(els.dial_0_0.innerHTML.includes("140&deg; sweep"), "the preview shows the new 140 degree sweep: " + els.dial_0_0.innerHTML.slice(-40));
  const before = els.dial_0_0.innerHTML;
  T.setAngle(0, 0, "start_deg", ""); T.setAngle(0, 0, "start_deg", "abc");
  check(c0.start_deg === 200 && els.dial_0_0.innerHTML === before, "an empty or non-numeric box changes nothing");
  T.setAngle(0, 0, "start_deg", "-135"); T.setAngle(0, 0, "end_deg", "135");
  check(c0.start_deg === -135 && els.dial_0_0.innerHTML.includes("270&deg; sweep"), "-135 is accepted and means the same as 225");
  T.setAngle(0, 0, "end_deg", "-135");
  check(els.dial_0_0.innerHTML.includes("(standard dial)"), "start = end is not a dial: the preview says the standard dial is used");

  console.log("4. the preview picture");
  const svg = (c) => T.dialSvg(Object.assign({ tick_count: 26, major_nth: 5 }, c));
  check((svg({ gauge_type: 0, start_deg: 225, end_deg: 135 }).match(/<line /g) || []).length === 26, "26 ticks drawn for tick count 26");
  check((svg({ gauge_type: 0, start_deg: 225, end_deg: 135, tick_count: 11 }).match(/<line /g) || []).length === 11, "and 11 for 11");
  check(/ A40 40 0 1 1 /.test(svg({ gauge_type: 0, start_deg: 225, end_deg: 135 })), "a 270 degree arc uses the long way round (large-arc flag 1)");
  check(/ A40 40 0 0 1 /.test(svg({ gauge_type: 0, start_deg: 0, end_deg: 90 })), "a 90 degree arc uses the short way (flag 0)");
  check(svg({ gauge_type: 0, start_deg: 225, end_deg: 135 }).includes('fill="#2ecc71"') && svg({ gauge_type: 0, start_deg: 225, end_deg: 135 }).includes('fill="#e74c3c"'), "a green start dot and a red end dot");
  check(!svg({ gauge_type: 0, start_deg: 225, end_deg: 135, tick_count: 0, major_nth: 0 }).includes("NaN"), "tick count 0 / major 0 cannot produce NaN in the picture");

  console.log("5. switching between Needle and Half-Moon");
  T.setPages([{ enabled: true, layout: 0, cells: [cellJson({})] }]);
  const c = T.getPages()[0].cells[0]; els.pages = { innerHTML: "" };
  T.changeGaugeType(0, 0, 2); check(c.gauge_type === 2 && c.start_deg === 270 && c.end_deg === 90, "needle -> half moon takes the half moon's standard dial (270 -> 90)");
  T.changeGaugeType(0, 0, 0); check(c.gauge_type === 0 && c.start_deg === 225 && c.end_deg === 135, "half moon -> needle goes back to 225 -> 135");
  c.start_deg = 200; c.end_deg = 100; T.changeGaugeType(0, 0, 2);
  check(c.start_deg === 200 && c.end_deg === 100, "a dial you customised is left alone");
  T.changeGaugeType(0, 0, 1); check(c.gauge_type === 1 && c.start_deg === 200, "other types keep the stored angles too");

  console.log("6. new cells start with the standard settings");
  const e = T.emptyCell();
  check(e.value_font_size === 28 && e.tick_font_size === 28 && e.start_deg === 225 && e.end_deg === 135, "new cell: text 28, dial numbers 28, dial 225 -> 135");

  console.log("7. Save & Reboot");
  const reset = (o) => { Object.assign(board, { posts: [], status: "up", statusCalls: 0, saveStatus: 200, rebootFails: false, rebootedAt: 0 }, o); els.status = { textContent: "" }; };
  T.setPages([{ enabled: true, layout: 0, cells: [cellJson({})] }]);
  reset({});
  await T.saveAndReboot();
  check(board.posts.map(p => p.url).join(",") === "/gauges,/reboot", "saves first, THEN reboots: " + board.posts.map(p => p.url).join(","));
  check(board.posts[1] && board.posts[1].headers["X-MSP"] === "1", "the reboot request carries the X-MSP header the board requires");
  check(/Board restarted \(build 107\)/.test(els.status.textContent), "it waits for the board to go down and come back, then says so: " + els.status.textContent);
  check(board.statusCalls >= 3, "it polled until the board answered again (" + board.statusCalls + " checks)");
  check(JSON.parse(board.posts[0].body)[0].cells[0].start_deg === 225, "what was saved includes the new angle fields");

  reset({ saveStatus: 500 });
  await T.saveAndReboot();
  check(board.posts.map(p => p.url).join(",") === "/gauges", "if the save fails the board is NOT rebooted");
  check(/Save FAILED: NOT saved to flash: test/.test(els.status.textContent), "and the board's reason is shown: " + els.status.textContent);

  reset({ rebootFails: true });
  await T.saveAndReboot();
  check(/Saved, but the reboot request failed: network down/.test(els.status.textContent), "a failed reboot request is reported: " + els.status.textContent);

  reset({ status: "never-restarts" });
  await T.saveAndReboot();
  check(/never restarted/.test(els.status.textContent), "a board that never went down is not reported as restarted: " + els.status.textContent);

  reset({ status: "never-returns" });
  await T.saveAndReboot();
  check(/has not come back after 45 seconds/.test(els.status.textContent), "a board that does not come back is reported: " + els.status.textContent);

  reset({}); T.setPages([]); await T.save();
  check(board.posts.length === 1, "an empty page list still goes through the existing confirm-and-save path");

  console.log("8. the web dashboard editor");
  const web = pageHtml('WEBCFG_HTML');
  check(web.includes("VALUE_SIZES") && /\[120,"120 Max"\]/.test(web), "has the same eight text sizes (the front page now uses them)");
  check(!web.includes("setAngle") && !web.includes("dialSvg") && !web.includes("saveAndReboot") && !web.includes("Dial numbers"), "no dial angles, dial numbers or reboot button (a web page has no dial and needs no reboot)");

  console.log(`\n${pass} passed, ${fail} failed`);
  process.exit(fail ? 1 : 0);
})();
