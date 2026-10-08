// Settings every gauge cell has, shared by the display editor and the web dashboard editor: default scale per signal,
// caption size, units text, gauge position (centre / top / bottom) and, in the display editor only, the caption on the dial face.
const fs = require('fs');
const path = require('path');
const vm = require('vm');
const { pageScript } = require('./extract_page.js');
let pass = 0, fail = 0; const check = (c, m) => { if (c) pass++; else { fail++; console.log("  FAIL:", m); } };
const SK = path.join(__dirname, '..', '..', 'esp32s3-amoled-bsp-base');

const ENUM = (() => {
  const h = fs.readFileSync(path.join(SK, 'gauge_config.h'), 'utf8');
  const body = h.match(/typedef enum \{\s*SIG_NONE = 0,([\s\S]*?)SIG_COUNT\s*\} SignalId;/)[1];
  return ['SIG_NONE', ...[...body.matchAll(/\b(SIG_[A-Z0-9]+)\b/g)].map(m => m[1])];
})();
const STYLE = fs.readFileSync(path.join(SK, 'gauge_style.h'), 'utf8');
const NAMES = ["None", "RPM", "Coolant Temp", "MAP/Boost", "AFR", "Oil Pressure", "Oil Temp", "Fuel Pressure", "Ethanol %", "Throttle Position", "Battery Voltage",
  "Rev Limiter Active", "Main Relay", "Fuel Pump", "Check Engine Light", "O2 Heater", "Lambda Protection", "Fan 1", "Fan 2", "Brake Pedal"];

function makeEditor(pageName, getUrl) {
  const els = {}; const posts = [];
  const box = {
    document: { getElementById: id => els[id] || (els[id] = { id, innerHTML: "", textContent: "", value: "" }) },
    confirm: () => true, console, setTimeout, Date, JSON, Math, Number, String, parseInt, parseFloat, isNaN, Object, Array,
    fetch: async (url, opts) => {
      if (opts && opts.method === "POST") { posts.push({ url, body: opts.body }); return { ok: true, status: 200, text: async () => "OK" }; }
      if (url === getUrl) return { json: async () => [{ enabled: true, layout: 0, cells: [{ enabled: true, signal: 2, gauge_type: 0, min: 0, max: 250, rest: 0, caption: "CLT", needle_color: 16777215, tick_color: 16777215, bg_color: 0, value_divisor: 1, tick_count: 26, major_nth: 5, refresh_hz: 10, show_value: false, value_font_size: 28, has_warning: false, warning_threshold: 0, warning_above: false }] }] };
      if (url === "/signals.json") return { json: async () => NAMES.map((n, i) => ({ id: i, name: n, age_ms: i === 0 ? 4294967295 : 100 * i, flag: i >= 11 })) };
      throw new Error("unexpected fetch " + url);
    },
  };
  box.window = box;
  const ctx = vm.createContext(box);
  vm.runInContext(pageScript(pageName), ctx);
  return { ctx, run: code => vm.runInContext(code, ctx), els, posts };
}
const settle = async () => { for (let i = 0; i < 10; i++) await new Promise(r => setImmediate(r)); };

(async () => {
  console.log("0. CONTRACT with gauge_style.h (the firmware's own lists)");
  const probe = makeEditor('GAUGES_HTML', '/gauges.json'); await settle();
  { const block = STYLE.match(/#define GAUGE_DEFAULT_RANGES\(X\)([\s\S]*?)\n\n/)[1];
    const fw = {}; for (const m of block.matchAll(/X\((SIG_[A-Z0-9]+),\s*(-?\d+),\s*(-?\d+)\)/g)) fw[ENUM.indexOf(m[1])] = [+m[2], +m[3]];
    const js = probe.run('JSON.stringify(SIGNAL_DEFAULT_RANGES)');
    check(Object.keys(fw).length === 5 && JSON.stringify(Object.fromEntries(Object.entries(fw).sort((a, b) => a[0] - b[0]))) === JSON.stringify(JSON.parse(js)), "the editor's default scales are EXACTLY the firmware's list: " + js);
    check(fw[4] && fw[4].join() === "10,20" && fw[2].join() === "60,250" && fw[6].join() === "60,250" && fw[5].join() === "0,100" && fw[7].join() === "0,100", "AFR 10-20, coolant and oil temperature 60-250, oil and fuel pressure 0-100");
    check(!(3 in fw), "MAP/boost has no default scale (it reads above 100 under boost)"); }
  { const fwSizes = STYLE.match(/GAUGE_CAPTION_SIZES\[GAUGE_CAPTION_SIZE_COUNT\] = \{([^}]*)\}/)[1].split(",").map(s => +s.trim());
    const js = probe.run('CAPTION_SIZES.map(s => s[0])');
    check(JSON.stringify(js) === JSON.stringify([0, ...fwSizes]) && fwSizes.join() === "14,20,28,36,48", "the editor offers 'Normal' (0) and exactly the firmware's caption sizes: " + fwSizes.join(", ")); }
  { const d = n => +STYLE.match(new RegExp("#define GAUGE_VALIGN_" + n + "\\s+(\\d)"))[1];
    const js = probe.run('VALIGNS.map(v => v[0] + ":" + v[1]).join(",")');
    check(js === `${d("CENTER")}:Center,${d("TOP")}:Top,${d("BOTTOM")}:Bottom`, "Center / Top / Bottom use the firmware's numbers: " + js); }

  for (const [label, pageName, getUrl, display] of [["display editor", 'GAUGES_HTML', '/gauges.json', true], ["web dashboard editor", 'WEBCFG_HTML', '/web-pages.json', false]]) {
    console.log(`\n=== ${label} ===`);
    const E = makeEditor(pageName, getUrl); await settle(); const R = E.run;
    R('pages[0].cells[0].min = 5; pages[0].cells[0].max = 50; pages[0].cells[0].rest = 7;');

    console.log("1. choosing a signal sets its usual scale");
    const scale = () => R('[pages[0].cells[0].signal, pages[0].cells[0].min, pages[0].cells[0].max, pages[0].cells[0].rest].join()');
    R('setCellSignal(0, 0, 4)'); check(scale() === "4,10,20,10", "AFR: 10 to 20, rest at 10 (got " + scale() + ")");
    R('setCellSignal(0, 0, 2)'); check(scale() === "2,60,250,60", "coolant: 60 to 250 (got " + scale() + ")");
    R('setCellSignal(0, 0, 6)'); check(scale() === "6,60,250,60", "oil temperature: 60 to 250");
    R('setCellSignal(0, 0, 5)'); check(scale() === "5,0,100,0", "oil pressure: 0 to 100");
    R('setCellSignal(0, 0, 7)'); check(scale() === "7,0,100,0", "fuel pressure: 0 to 100");
    for (const [sig, nm] of [[3, "MAP/boost"], [1, "RPM"], [8, "ethanol"], [9, "throttle"], [10, "battery"]]) {
      R('pages[0].cells[0].min = 5; pages[0].cells[0].max = 50; pages[0].cells[0].rest = 7;'); R(`setCellSignal(0, 0, ${sig})`);
      check(scale() === `${sig},5,50,7`, `${nm}: the signal changes but the scale you set is left alone (got ${scale()})`);
    }
    R('pages[0].cells[0].min = 5; pages[0].cells[0].max = 50; setCellSignal(0, 0, 4)'); check(scale() === "4,10,20,10", "a custom scale IS replaced when you switch to AFR (the quantity changed)");
    R('setCellSignal(0, 0, 4)'); check(E.els.pages && E.els.pages.innerHTML.includes('value="10"') && E.els.pages.innerHTML.includes('value="20"'), "the boxes on screen show the new Min and Max");

    console.log("2. the controls");
    const cell = o => R(`Object.assign({ enabled: true, signal: 2, gauge_type: 0, min: 0, max: 250, rest: 0, caption: "CLT", needle_color: 0xFFFFFF, tick_color: 0xFFFFFF, bg_color: 0, value_divisor: 1, tick_count: 26, major_nth: 5, refresh_hz: 10, show_value: false, value_font_size: 28, caption_font_size: 0, caption_on_face: false, units: "", v_align: 0, has_warning: false, warning_threshold: 0, warning_above: false }, ${JSON.stringify(o)})`);
    const html = o => { R(`globalThis.__c = ${JSON.stringify(cell(o))}`); return R('renderCell(0, 0, __c)'); };
    check(html({}).includes('<select onchange="setCellSignal(0,0,parseInt(this.value))">'), "the Signal dropdown on screen is wired to setCellSignal (so choosing AFR really sets 10-20)");
    for (const [gt, nm, hasPos, hasFace] of [[0, "needle", true, display], [1, "number", true, false], [2, "half moon", true, display], [3, "trend graph", false, false]]) {
      const h = html({ gauge_type: gt });
      check(h.includes("Caption size") && h.includes('maxlength="7"') && h.includes("Units"), `${nm}: caption size and units controls are shown`);
      check(h.includes("Gauge position in the cell") === hasPos, `${nm}: gauge position ${hasPos ? "shown" : "hidden (a trend graph fills its cell)"}`);
      check(h.includes("Show the caption on the dial face") === hasFace, `${nm}: 'caption on the dial face' ${hasFace ? "shown" : "not shown"}`);
    }
    { const h = html({ caption_font_size: 36 });
      const capSel = h.match(/Caption size <select[^>]*>([\s\S]*?)<\/select>/)[1];
      check((capSel.match(/<option /g) || []).length === 6 && capSel.includes('<option value="36" selected>36 Large</option>') && capSel.includes('>Normal</option>') && capSel.includes('>14 Small</option>') && capSel.includes('>48 XL</option>'), "caption size: Normal, 14, 20, 28, 36, 48 with the saved 36 selected");
      check(html({}).includes('<option value="0" selected>Normal</option>'), "an unset caption size shows as Normal");
      check(html({ v_align: 2 }).includes('<option value="2" selected>Bottom</option>') && html({ v_align: 1 }).includes('<option value="1" selected>Top</option>') && html({}).includes('<option value="0" selected>Center</option>'), "gauge position: Center / Top / Bottom with the saved one selected");
      { const ds = h => (h.match(/'decimals',parseInt\(this\.value\)\)">([\s\S]*?)<\/select>/) || [])[1] || "";
        check((ds(html({})).match(/<option /g) || []).length === 3 && ds(html({})).includes('<option value="1" selected>'), "decimals: 0 / 1 / 2, an unset one shows 1");
        check(ds(html({ decimals: 0 })).includes('<option value="0" selected>') && ds(html({ decimals: 2 })).includes('<option value="2" selected>') && ds(html({ decimals: 9 })).includes('<option value="1" selected>'), "decimals: the saved 0 / 2 is selected, an unknown value shows 1"); }
      const faceOn = /<input type="checkbox" checked onchange="setCellField\(0,0,'caption_on_face',this\.checked\)">/.test(html({ gauge_type: 0, caption_on_face: true }));
      const faceOff = /<input type="checkbox"  onchange="setCellField\(0,0,'caption_on_face',this\.checked\)">/.test(html({ gauge_type: 0, caption_on_face: false }));
      check(display ? (faceOn && faceOff) : (!faceOn && !faceOff), display ? "the face checkbox is ticked when saved on, and not ticked when off" : "the web dashboard editor has no face checkbox (there is no dial there)"); }
    { const h = html({ caption: 'a"><img src=x onerror=alert(1)>', units: '"><b>' });
      check(!h.includes('"><img') && !h.includes('"><b>') && h.includes("&quot;&gt;&lt;img") && h.includes("&quot;&gt;&lt;b&gt;"), "caption and units text are HTML-escaped in the editor"); }

    console.log("3. new cells and saving");
    const e = R('emptyCell()');
    check(e.caption_font_size === 0 && e.caption_on_face === false && e.units === "" && e.v_align === 0 && e.decimals === 1, "a new cell: Normal caption, not on the face, no units, centred, 1 decimal");
    R('setCellField(0, 0, "caption_font_size", 36); setCellField(0, 0, "units", "kPa"); setCellField(0, 0, "v_align", 2); setCellField(0, 0, "caption_on_face", true);');
    check(R('pages[0].cells[0].caption_font_size') === 36 && R('pages[0].cells[0].units') === "kPa" && R('pages[0].cells[0].v_align') === 2 && R('pages[0].cells[0].caption_on_face') === true, "the four settings are stored on the cell");
    R('save()'); await settle();
    const sent = E.posts.length ? JSON.parse(E.posts[E.posts.length - 1].body)[0].cells[0] : {};
    check(sent.caption_font_size === 36 && sent.units === "kPa" && sent.v_align === 2 && sent.caption_on_face === true, "all four are sent to the board when saving");
  }

  console.log(`\n${pass} passed, ${fail} failed`);
  process.exit(fail ? 1 : 0);
})();
