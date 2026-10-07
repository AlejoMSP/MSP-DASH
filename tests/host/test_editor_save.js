const fs = require('fs');
async function load(file, postStatus, postText) {
  // pull the editor's <script> straight out of the sketch
  const cpp = fs.readFileSync(__dirname + '/../../esp32s3-amoled-bsp-base/web_monitor.cpp', 'utf8');
  const name = file.includes('GAUGES') ? 'GAUGES_HTML' : 'WEBCFG_HTML';
  const page = cpp.match(new RegExp('static const char ' + name + '\\[\\] PROGMEM = R"rawliteral\\(([\\s\\S]*?)\\)rawliteral";'))[1];
  const script = page.match(/<script>([\s\S]*?)<\/script>/)[1];
  const els = {}; const posts = [];
  const getEl = id => els[id] || (els[id] = {id, innerHTML:"", textContent:"", value:"#ffffff"});
  global.document = { getElementById: getEl }; global.window = global; global.confirm = () => true;
  const cell = { enabled:true,signal:3,gauge_type:0,min:0,max:250,rest:0,caption:"MAP",needle_color:16777215,tick_color:16777215,bg_color:0,value_divisor:1,tick_count:26,major_nth:5,refresh_hz:50,show_value:false,value_font_size:28,has_warning:false,warning_threshold:0,warning_above:false,alert_fullscreen:false,history_sec:30,ref_enabled:false,ref_value:0 };
  const sigs = [{id:0,name:"None",age_ms:1},{id:3,name:"MAP/Boost",age_ms:1}];
  global.fetch = async (url, opts) => {
    if (opts && opts.method === "POST") { posts.push(url); return { ok: postStatus === 200, status: postStatus, text: async () => postText }; }
    return { json: async () => url.includes("signals") ? sigs : [{enabled:true,layout:0,cells:[cell]}] };
  };
  eval(script + "\n;global.__save=save;");
  await new Promise(r => setTimeout(r, 30));
  await __save();
  return { status: els.status.textContent, posts };
}
let pass = 0, fail = 0; const check = (c, m) => { if (c) pass++; else { fail++; console.log("  FAIL:", m); } };
(async () => {
  let r = await load('/tmp/GAUGES_HTML_0.js', 200, "OK");
  check(r.posts[0] === "/gauges" && /^Saved to the board's flash\. Reboot the board/.test(r.status), "display editor success: " + r.status);
  r = await load('/tmp/WEBCFG_HTML_0.js', 200, "OK");
  check(r.posts[0] === "/web-pages" && /no reboot needed/.test(r.status) && !/Reboot the board/.test(r.status), "web editor success (no reboot wording): " + r.status);
  const boardMsg = "NOT saved to flash: flash filesystem (ffat) failed to mount. The change is active until the next reboot.";
  r = await load('/tmp/GAUGES_HTML_0.js', 500, boardMsg);
  check(r.status === "Save FAILED: " + boardMsg, "display editor shows the board's own explanation: " + r.status);
  r = await load('/tmp/WEBCFG_HTML_0.js', 500, boardMsg);
  check(r.status === "Save FAILED: " + boardMsg, "web editor shows the board's own explanation: " + r.status);
  r = await load('/tmp/GAUGES_HTML_0.js', 500, "");
  check(/Save FAILED: board returned 500/.test(r.status), "empty error body falls back to the status code: " + r.status);
  console.log(`\n${pass} passed, ${fail} failed`);
})();
