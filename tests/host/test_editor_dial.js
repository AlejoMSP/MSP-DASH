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
  low_crit_on: false, low_crit: 0, low_warn_on: false, low_warn: 0, high_warn_on: false, high_warn: 0, high_crit_on: false, high_crit: 0, zone_style: 1, needle_hub_pct: 0, needle_tip_pct: 0,
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
eval(script + "\n;global.__t = { renderCell, dialGeometry, dialSvg, setAngle, changeGaugeType, save, saveAndReboot, emptyCell, VALUE_SIZES, TICK_SIZES, zoneOf, zoneRanges, tickValue, resolveTrim, toggleZone, setZoneStyle, setZone, setTrim, getPages: () => pages, setPages: p => { pages = p; loaded = true; } };");
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
  check((svg({ gauge_type: 0, start_deg: 225, end_deg: 135 }).match(/<line class="tick/g) || []).length === 26, "26 ticks drawn for tick count 26 (the needle line is separate)");
  check((svg({ gauge_type: 0, start_deg: 225, end_deg: 135, tick_count: 11 }).match(/<line class="tick/g) || []).length === 11, "and 11 for 11");
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

  console.log("8. the web dashboard editor is gone (removed in build 160)");
  check(!/WEBCFG_HTML/.test(require('fs').readFileSync(__dirname + '/../../esp32s3-amoled-bsp-base/web_monitor.cpp', 'utf8')), "web_monitor.cpp no longer has the web dashboard editor page");

  console.log("9. warning zones: which ticks the picture colours (the SAME hand-written cases as the display's pixel test)");
  const OFF = null;
  //  min 0, max 100, 11 ticks: tick i has value 10*i.   [name, [lowCrit, lowWarn, highWarn, highCrit], expectedYellowTicks, expectedRedTicks]
  const ZC = [["no zones", [OFF, OFF, OFF, OFF], [], []], ["high warning 70, high critical 90", [OFF, OFF, 70, 90], [70, 80], [90, 100]],
    ["low critical 20, low warning 40", [20, 40, OFF, OFF], [30, 40], [0, 10, 20]], ["only high critical 100", [OFF, OFF, OFF, 100], [], [100]],
    ["high warning 55 (between two ticks)", [OFF, OFF, 55, OFF], [60, 70, 80, 90, 100], []], ["all four: 10, 30, 70, 90", [10, 30, 70, 90], [20, 30, 70, 80], [0, 10, 90, 100]]];
  const zcell = (th, style) => ({ gauge_type: 0, start_deg: 225, end_deg: 135, tick_count: 11, major_nth: 2, min: 0, max: 100, needle_color: 0xFFFFFF, zone_style: style,
    low_crit_on: th[0] !== OFF, low_crit: th[0] || 0, low_warn_on: th[1] !== OFF, low_warn: th[1] || 0, high_warn_on: th[2] !== OFF, high_warn: th[2] || 0, high_crit_on: th[3] !== OFF, high_crit: th[3] || 0 });
  for (const [name, th, wantY, wantR] of ZC) {
    const ticks = [...T.dialSvg(zcell(th, 2)).matchAll(/<line class="tick([^"]*)"/g)].map(m => m[1].trim());     // style 2 = coloured tick marks
    const gotY = ticks.map((z, i) => z === "yellow" ? 10 * i : -1).filter(v => v >= 0), gotR = ticks.map((z, i) => z === "red" ? 10 * i : -1).filter(v => v >= 0);
    check(ticks.length === 11, `[${name}] 11 ticks drawn`);
    check(JSON.stringify(gotY) === JSON.stringify(wantY), `[${name}] yellow ticks ${JSON.stringify(wantY)} - got ${JSON.stringify(gotY)}`);
    check(JSON.stringify(gotR) === JSON.stringify(wantR), `[${name}] red ticks ${JSON.stringify(wantR)} - got ${JSON.stringify(gotR)}`);
  }
  const bandsOf = svg => [...svg.matchAll(/<path class="band (\w+)" d="M([\d.-]+) ([\d.-]+) A/g)].map(m => ({ color: m[1], x: +m[2], y: +m[3] }));
  const tickColoured = svg => /<line class="tick (yellow|red)"/.test(svg);
  { const all = zcell([10, 30, 70, 90], 1);
    check(bandsOf(T.dialSvg(all)).map(b => b.color).join(",") === "yellow,yellow,red,red", "style 1 with all four on: 4 bands, yellow ones first and red ones last (so red is drawn over yellow)");
    check(!tickColoured(T.dialSvg(all)), "style 1 (bands): the tick marks keep their normal colour");
    check(bandsOf(T.dialSvg(zcell([10, 30, 70, 90], 2))).length === 0 && tickColoured(T.dialSvg(zcell([10, 30, 70, 90], 2))), "style 2 (tick marks): no bands, coloured ticks");
    check(bandsOf(T.dialSvg(zcell([10, 30, 70, 90], 3))).length === 4 && tickColoured(T.dialSvg(zcell([10, 30, 70, 90], 3))), "style 3 (both): bands and coloured ticks");
    check(bandsOf(T.dialSvg(Object.assign(zcell([10, 30, 70, 90], 1), { zone_style: undefined }))).length === 4 && bandsOf(T.dialSvg(Object.assign(zcell([10, 30, 70, 90], 1), { zone_style: 0 }))).length === 4 && bandsOf(T.dialSvg(Object.assign(zcell([10, 30, 70, 90], 1), { zone_style: 9 }))).length === 4, "an unset or invalid style is bands, as on the display");
    check(bandsOf(T.dialSvg(zcell([OFF, OFF, OFF, OFF], 1))).length === 0, "nothing switched on: no bands");
    check(bandsOf(T.dialSvg(zcell([OFF, -5, OFF, OFF], 1))).length === 0 && bandsOf(T.dialSvg(zcell([OFF, -5, OFF, OFF], 3))).length === 0, "a threshold with no width (low warning -5 on a 0..100 scale) draws no band");
    check(/A36 36 0 1 1 /.test(T.dialSvg(zcell([OFF, 300, OFF, OFF], 1)).match(/<path class="band[^>]*>/)[0]) && /A36 36 0 0 1 /.test(T.dialSvg(zcell([OFF, 20, OFF, OFF], 1)).match(/<path class="band[^>]*>/)[0]), "a band over the whole 270 degree scale takes the long way round (large-arc flag 1); a short one (the first 20%) does not (flag 0)");
    // where a band starts: high warning 70 on the 225..135 dial is 225 + 270 * 0.7 = 414 = 54 degrees; the picture's radius for bands is 36
    const b = bandsOf(T.dialSvg(zcell([OFF, OFF, 70, OFF], 1)))[0], ang = 54 * Math.PI / 180;
    check(Math.abs(b.x - (48 + 36 * Math.sin(ang))) < 0.15 && Math.abs(b.y - (48 - 36 * Math.cos(ang))) < 0.15, `a band starts at its threshold's angle: (${b.x}, ${b.y}) vs (${(48 + 36 * Math.sin(ang)).toFixed(1)}, ${(48 - 36 * Math.cos(ang)).toFixed(1)})`);
  }
  check(T.dialSvg({ gauge_type: 0, start_deg: 225, end_deg: 135, tick_count: 11, major_nth: 2, min: 0, max: 100, high_warn_on: true, high_warn: 50, zone_style: 2 }).includes('stroke="#ffd600"'), "yellow uses the display's yellow (#ffd600)");
  check(T.dialSvg({ gauge_type: 0, start_deg: 225, end_deg: 135, tick_count: 11, major_nth: 2, min: 0, max: 100, high_crit_on: true, high_crit: 50, zone_style: 1 }).includes('stroke="#ff2a2a"'), "red uses the display's red (#ff2a2a)");
  { const r = T.zoneRanges(zcell([20, 40, 60, 80], 1)).map(z => `${z.color}:${z.start}-${z.end}`).join(" ");
    check(r === "yellow:0-40 yellow:60-100 red:0-20 red:80-100", "the ranges, in draw order, are the same as the display's: " + r);
    const edge = T.zoneRanges({ min: 0, max: 100, low_warn_on: true, low_warn: 300, high_warn_on: true, high_warn: -50, high_crit_on: true, high_crit: 100 }).map(z => `${z.color}:${z.start}-${z.end}`).join(" ");
    check(edge === "yellow:0-100 yellow:0-100 red:100-100", "thresholds outside the scale are clamped to it: " + edge);
    check(T.zoneRanges({ min: 5, max: 5, low_crit_on: true, low_crit: 5 }).length === 1, "a scale with min = max does not break it"); }
  { const c = zcell([20, 40, 70, 90], 2);
    check(T.zoneOf(c, 20) === "red" && T.zoneOf(c, 21) === "yellow" && T.zoneOf(c, 40) === "yellow" && T.zoneOf(c, 41) === null && T.zoneOf(c, 70) === "yellow" && T.zoneOf(c, 89) === "yellow" && T.zoneOf(c, 90) === "red", "a tick exactly AT a threshold is coloured (both ends included); red wins over yellow"); }
  const lvmap = (i, n, mn, mx) => mn + Math.trunc(i * (mx - mn) / (n - 1));
  check(T.tickValue({ min: 0, max: 250 }, 3, 26) === 30 && T.tickValue({ min: -40, max: 50 }, 4, 11) === lvmap(4, 11, -40, 50) && T.tickValue({ min: 0, max: 100 }, 5, 7) === 83, "tick values come out as LVGL's integer maths gives them (e.g. 5 of 7 ticks on 0..100 is 83)");

  console.log("10. the controls for warning zones and needle length");
  const needle2 = html(cellJson({})), half2 = html(cellJson({ gauge_type: 2 })), num2 = html(cellJson({ gauge_type: 1 })), trend2 = html(cellJson({ gauge_type: 3 }));
  for (const [name, h, want] of [["needle", needle2, true], ["half moon", half2, true], ["number", num2, false], ["trend graph", trend2, false]]) {
    check(h.includes("Warning zones") === want && h.includes("Needle gap at center") === want && h.includes("Needle short of tip") === want, `${name}: warning zones + needle controls ${want ? "shown" : "hidden"}`);
  }
  check(["Low critical (red) up to", "Low warning (yellow) up to", "High warning (yellow) from", "High critical (red) from"].every(t => needle2.includes(t)), "four thresholds, each named, in the order low critical, low warning, high warning, high critical");
  check((needle2.match(/<input type="number" step="1" value="0" disabled/g) || []).length === 4, "all four value boxes are greyed out while their threshold is off");
  const on2 = html(cellJson({ low_crit_on: true, low_crit: 20, high_crit_on: true, high_crit: 230 }));
  check(on2.includes('value="20"') && on2.includes('value="230"') && !/value="20" disabled/.test(on2) && !/value="230" disabled/.test(on2) && (on2.match(/value="0" disabled/g) || []).length === 2, "a threshold that is on has a usable box with its value; the two that are off stay greyed");
  check(["low_crit", "low_warn", "high_warn", "high_crit"].every(k => on2.includes(`toggleZone(0,0,'${k}',this.checked)`) && on2.includes(`setZone(0,0,'${k}',this.value)`)), "each of the four rows has its OWN tick box and value box wired to its own threshold");
  check(!needle2.includes("Tick colors") && !needle2.includes("High end (e.g.") && !needle2.includes("Yellow from"), "the old one-sided 'Tick colors' controls are gone");
  check(needle2.includes('<option value="1" selected>Colored bands behind the ticks') && html(cellJson({ zone_style: 2 })).includes('<option value="2" selected>Colored tick marks') && html(cellJson({ zone_style: 3 })).includes('<option value="3" selected>Both'), "'Show as': bands, tick marks or both, with the saved one selected");
  check(html(cellJson({ zone_style: undefined })).includes('<option value="1" selected>'), "an unset style shows as bands");

  console.log("11. switching a threshold on, and typing values");
  const zc = cellJson({ min: 0, max: 250 }); T.setPages([{ enabled: true, layout: 0, cells: [zc] }]); els.pages = { innerHTML: "" };
  for (const k of ["low_crit", "low_warn", "high_warn", "high_crit"]) T.toggleZone(0, 0, k, true);
  check(zc.low_crit_on && zc.low_warn_on && zc.high_warn_on && zc.high_crit_on && zc.low_crit === 25 && zc.low_warn === 63 && zc.high_warn === 188 && zc.high_crit === 225, "first switch-on offers 10%, 25%, 75% and 90% of the scale (25, 63, 188, 225 on 0..250)");
  zc.high_warn = 150; T.toggleZone(0, 0, "high_warn", false); check(zc.high_warn_on === false && zc.high_warn === 150, "switching off keeps the value you typed ...");
  T.toggleZone(0, 0, "high_warn", true); check(zc.high_warn === 150, "... and switching it back on keeps it too");
  const sz = cellJson({ min: -40, max: 140 }); T.setPages([{ enabled: true, layout: 0, cells: [sz] }]); T.toggleZone(0, 0, "low_crit", true); T.toggleZone(0, 0, "high_crit", true);
  check(sz.low_crit === -22 && sz.high_crit === 122, "on a -40..140 scale the offered values are -22 and 122");
  els.dial_0_0 = { innerHTML: "" };
  T.setZone(0, 0, "high_crit", "130"); check(sz.high_crit === 130 && els.dial_0_0.innerHTML.includes("<svg"), "typing a value stores it and redraws the picture");
  T.setZone(0, 0, "high_crit", ""); T.setZone(0, 0, "high_crit", "abc"); check(sz.high_crit === 130, "an empty or non-numeric box changes nothing");
  T.setZone(0, 0, "low_crit", "-40"); check(sz.low_crit === -40, "negative values are fine");
  T.setZoneStyle(0, 0, 3); check(sz.zone_style === 3 && els.dial_0_0.innerHTML.includes("<svg"), "'Show as' changes the style and redraws the picture");

  console.log("12. needle length and the picture of the needle");
  const needleLine = (c) => { const m = T.dialSvg(Object.assign({ tick_count: 11, major_nth: 2, min: 0, max: 100 }, c)).match(/<line class="needle" x1="([\d.-]+)" y1="([\d.-]+)" x2="([\d.-]+)" y2="([\d.-]+)"/);
    const d = (x, y) => Math.hypot(x - 48, y - 48); return m ? { from: d(+m[1], +m[2]), to: d(+m[3], +m[4]) } : null; };
  let nl = needleLine({ gauge_type: 0, start_deg: 225, end_deg: 135 });
  check(nl && Math.abs(nl.from - 0) < 0.3 && Math.abs(nl.to - 37.6) < 0.3, `no trim: the needle runs from the centre to 94% of the radius (${nl && nl.from.toFixed(1)} .. ${nl && nl.to.toFixed(1)}, expected 0 .. 37.6)`);
  nl = needleLine({ gauge_type: 0, start_deg: 225, end_deg: 135, needle_hub_pct: 20, needle_tip_pct: 10 });
  check(nl && Math.abs(nl.from - 8) < 0.3 && Math.abs(nl.to - 33.6) < 0.3, `hub 20%, tip 10%: starts 8 and ends 33.6 (got ${nl && nl.from.toFixed(1)} .. ${nl && nl.to.toFixed(1)})`);
  nl = needleLine({ gauge_type: 0, start_deg: 225, end_deg: 135, needle_hub_pct: 60, needle_tip_pct: 60 });
  check(nl && Math.abs(nl.from - 24) < 0.3 && Math.abs(nl.to - (0.94 - 0.25) * 40) < 0.3, `60% + 60% is over the limit: the hub gap stays 60% (24) and the tip gives way to 25% (got ${nl && nl.from.toFixed(1)} .. ${nl && nl.to.toFixed(1)})`);
  check(T.dialSvg({ gauge_type: 0, start_deg: 225, end_deg: 135 }).includes('class="hub"'), "no gap: the grey hub dot is drawn");
  check(!T.dialSvg({ gauge_type: 0, start_deg: 225, end_deg: 135, needle_hub_pct: 5 }).includes('class="hub"'), "with a gap the hub dot is not drawn (the display hides it too)");
  check(T.dialSvg({ gauge_type: 0, start_deg: 225, end_deg: 135, needle_color: 0x00FF00 }).includes('class="needle" x1') && /class="needle"[^>]*stroke="#00ff00"/.test(T.dialSvg({ gauge_type: 0, start_deg: 225, end_deg: 135, needle_color: 0x00FF00 })), "the needle in the picture has the cell's needle colour");
  for (const [h, t, eh, et] of [[-5, 999, 0, 60], [60, 60, 60, 25], [40, 45, 40, 45], [41, 45, 41, 44], [0, 0, 0, 0], [12.6, 3.2, 13, 3]]) {
    const r = T.resolveTrim({ needle_hub_pct: h, needle_tip_pct: t });
    check(r.hub === eh && r.tip === et, `trim ${h}/${t} -> ${eh}/${et} (the display's rule) - got ${r.hub}/${r.tip}`);
  }
  const tc = cellJson({}); T.setPages([{ enabled: true, layout: 0, cells: [tc] }]);
  T.setTrim(0, 0, "needle_hub_pct", "75"); check(tc.needle_hub_pct === 60, "the box stops at 60");
  T.setTrim(0, 0, "needle_tip_pct", "-4"); check(tc.needle_tip_pct === 0, "and at 0");
  T.setTrim(0, 0, "needle_tip_pct", "x"); check(tc.needle_tip_pct === 0, "text changes nothing");

  console.log("13. new cells and saving");
  const e2 = T.emptyCell();
  check(!e2.low_crit_on && !e2.low_warn_on && !e2.high_warn_on && !e2.high_crit_on && e2.zone_style === 1 && e2.needle_hub_pct === 0 && e2.needle_tip_pct === 0, "a new cell has no warning zones (bands if you switch them on) and the normal needle");
  const sc = cellJson({ low_crit_on: true, low_crit: 20, low_warn_on: true, low_warn: 40, high_warn_on: true, high_warn: 200, high_crit_on: true, high_crit: 230, zone_style: 3, needle_hub_pct: 15, needle_tip_pct: 5 });
  T.setPages([{ enabled: true, layout: 0, cells: [sc] }]); board.posts = []; els.status = { textContent: "" };
  await T.save();
  const sent = JSON.parse(board.posts[0].body)[0].cells[0];
  check(sent.low_crit_on === true && sent.low_crit === 20 && sent.low_warn_on === true && sent.low_warn === 40 && sent.high_warn_on === true && sent.high_warn === 200 && sent.high_crit_on === true && sent.high_crit === 230 && sent.zone_style === 3 && sent.needle_hub_pct === 15 && sent.needle_tip_pct === 5, "the four thresholds, the style and the needle trim are all sent to the board when saving");

  console.log(`\n${pass} passed, ${fail} failed`);
  process.exit(fail ? 1 : 0);
})();
