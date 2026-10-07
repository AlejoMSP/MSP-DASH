// A stand-in for the board's web server, for looking at the pages in a real browser on a PC.
//   node mock_board.js [port]      then open http://localhost:8080/
// Serves the real pages (assembled by extract_page.js) plus believable data. Needs the 'ws' package.
const http = require('http');
const { pageHtml } = require('./extract_page.js');
let WebSocketServer; try { ({ WebSocketServer } = require('ws')); } catch (e) { console.error("npm install ws  (needed for the live connection)"); process.exit(1); }

const PAGES = { '/': 'INDEX_HTML', '/can': 'CAN_HTML', '/gauges': 'GAUGES_HTML', '/web-config': 'WEBCFG_HTML', '/sd': 'SD_HTML', '/update': 'OTA_HTML', '/log-channels': 'LOGCHANS_HTML', '/simulator': 'SIM_HTML' };
const SIGNALS = ['None', 'RPM', 'Coolant Temp', 'MAP/Boost', 'AFR', 'Oil Pressure', 'Oil Temp', 'Fuel Pressure', 'Ethanol %', 'Throttle Position', 'Battery Voltage',
  'Rev Limiter Active', 'Main Relay', 'Fuel Pump', 'Check Engine Light', 'O2 Heater', 'Lambda Protection', 'Fan 1', 'Fan 2', 'Brake Pedal'];   // 11..19 are the ECU on/off flags
const cell = (o) => Object.assign({ enabled: true, signal: 1, gauge_type: 0, min: 0, max: 100, rest: 0, caption: 'RPM', needle_color: 0xFFFFFF, tick_color: 0xFFFFFF, bg_color: 0, value_divisor: 1, tick_count: 26, major_nth: 5, refresh_hz: 10, show_value: false, value_font_size: 28, tick_font_size: 28, start_deg: 225, end_deg: 135, low_crit_on: false, low_crit: 0, low_warn_on: false, low_warn: 0, high_warn_on: false, high_warn: 0, high_crit_on: false, high_crit: 0, zone_style: 1, needle_hub_pct: 0, needle_tip_pct: 0, has_warning: false, warning_threshold: 0, warning_above: false, alert_fullscreen: false, history_sec: 30, ref_enabled: false, ref_value: 0 }, o);
const webPages = [
  { enabled: true, layout: 3, cells: [cell({ signal: 1, caption: 'RPM', value_font_size: 96 }), cell({ signal: 3, caption: 'MAP', value_font_size: 28 }), cell({ signal: 2, caption: 'CLT', value_font_size: 14, has_warning: true, warning_threshold: 220, warning_above: true }), cell({ signal: 4, caption: 'AFR', value_font_size: 48 })] },
  { enabled: true, layout: 2, cells: [cell({ signal: 5, caption: 'OIL PRESS', value_font_size: 120 }), cell({ signal: 6, caption: 'OIL TEMP', value_font_size: 36 }), cell({ signal: 7, caption: 'FUEL PRESS', value_font_size: 64 })] },
  { enabled: true, layout: 0, cells: [cell({ signal: 1, caption: 'RPM', value_font_size: 120 })] }];
const gaugePages = ['CLT', 'MAP', 'AFR', 'OIL PRESS', 'OIL TEMP', 'FUEL PRESS', 'ETH %'].map((c, i) => ({ enabled: true, layout: 0, cells: [cell(Object.assign({ signal: [2, 3, 4, 5, 6, 7, 8][i], caption: c, max: 250, has_warning: i === 0 || i === 3, warning_threshold: 40 },
  i === 0 ? { low_warn_on: true, low_warn: 80, high_warn_on: true, high_warn: 200, high_crit_on: true, high_crit: 230, zone_style: 1, needle_hub_pct: 15, needle_tip_pct: 5 } : {}))] }));
gaugePages.push({ enabled: true, layout: 3, cells: [cell({ signal: 1, caption: 'RPM', gauge_type: 3, history_sec: 30, ref_enabled: true, ref_value: 3000 }), cell({ caption: 'MAP', signal: 3 }), cell({ caption: 'AFR', signal: 4, gauge_type: 1 }), cell({ caption: 'CLT', signal: 2 })] });

const lamp = (signal, op, threshold, on, onColor, extra) => Object.assign({ signal, op, threshold, hysteresis: 0, on_text: on, off_text: on, on_color: onColor, off_color: 0x2A2A2A, on_text_color: 0xFFFFFF, off_text_color: 0x808080, blink: false }, extra || {});
const indicatorPage = { enabled: true, kind: 1, cols: 3, indicators: [
  lamp(5, 1, 100, 'OIL', 0xE53935),                 // oil pressure below 100: ON with the live value 62
  lamp(2, 0, 220, 'TEMP', 0xE53935),                // coolant above 220: off (188)
  lamp(17, 0, 0.5, 'FAN 1', 0x1E88E5),              // fan 1 flag: ON (1)
  lamp(14, 0, 0.5, 'CHECK ENG', 0xFFB300, { on_text_color: 0x000000 }),   // check engine flag: off (0)
  lamp(11, 0, 0.5, 'REV LIM', 0xE53935, { blink: true }),                 // rev limiter flag: off (0)
  lamp(19, 1, 0.5, 'NO BRAKE', 0x8E24AA),           // lit while the brake flag is OFF: ON (brake 0)
  lamp(10, 1, 12, 'BATT', 0xFFB300),                // battery: the mock sends no value: off (no data)
  lamp(4, 0, 15, '<LEAN>', 0x43A047) ] };           // AFR 14.7 is not above 15: off - and the text has '<' in it
webPages.push(indicatorPage);
gaugePages.push(JSON.parse(JSON.stringify(indicatorPage)));

const JSON_ROUTES = {
  '/web-pages.json': () => webPages,
  '/gauges.json': () => gaugePages,
  '/signals.json': () => SIGNALS.map((n, i) => ({ id: i, name: n, age_ms: i === 0 ? 4294967295 : i * 40, flag: i >= 11 })),
  '/status.json': () => ({ build: 104, compiled: 'Oct  7 2026 06:43:44', uptime_s: 83, can: { frames_per_s: 412, ids_seen: 38, state: 'running' }, storage: { mounted: true } }),
  '/can.json': () => [[0x201, 8, '5A 0B 00 00 00 00 00 1E', 20, 12], [0x202, 8, '00 00 E8 03 00 00 00 00', 20, 31], [0x203, 8, 'F4 0A 4B 00 52 00 00 00', 20, 8], [0x204, 8, '00 00 B4 00 00 00 36 35', 20, 44], [0x207, 8, 'FE 5C 00 00 3B 06 00 00', 20, 17], [0x190, 8, '12 34 56 78 9A BC DE F0', 99, 3], [0xEF50000, 8, '00 00 00 00 00 00 00 00', 10, 91]]
    .map(([id, dlc, data, hz, ago]) => ({ id, dlc, data, hz, ago })),
  '/sd/list': () => [{ name: 'log_16.csv', size: 48213 }, { name: 'log_17.csv', size: 1530922 }, { name: 'a_rather_long_file_name_for_testing_overflow.csv', size: 12 }],
  '/log-channels.json': () => SIGNALS.slice(1).map((n, i) => ({ id: i + 1, name: n, enabled: i % 3 !== 2 })),
  '/sim.json': () => ({ active: true, manual: true, seconds_left: 543, stop_reason: '', signals: SIGNALS.slice(1).map((n, i) => ({ id: i + 1, name: n, min: 0, max: 9000, step: i === 3 ? 0.1 : 1, value: 100 * (i + 1) })) }),
};

// The live values the mock sends (no battery voltage, so the BATT lamp shows 'no data'); the ECU flags are 1 or 0.
const WS_VALUES = { 1: 3000, 2: 188.2, 3: 112.4, 4: 14.7, 5: 62, 6: 201.5, 7: 58, 8: 31, 11: 0, 12: 1, 13: 1, 14: 0, 15: 0, 16: 0, 17: 1, 18: 0, 19: 0 };

function makeServer() {
  const server = http.createServer((req, res) => {
    const url = req.url.split('?')[0];
    if (req.method === 'POST') { req.resume(); req.on('end', () => { res.writeHead(200, { 'Content-Type': 'text/plain' }); res.end('OK'); }); return; }
    if (PAGES[url]) { res.writeHead(200, { 'Content-Type': 'text/html' }); res.end(pageHtml(PAGES[url])); return; }
    if (JSON_ROUTES[url]) { res.writeHead(200, { 'Content-Type': 'application/json' }); res.end(JSON.stringify(JSON_ROUTES[url]())); return; }
    res.writeHead(404); res.end('Not found');
  });
  const wss = new WebSocketServer({ server, path: '/ws' });
  setInterval(() => {
    const t = Date.now() / 1000;
    const msg = JSON.stringify({ v: Object.assign({}, WS_VALUES, { 1: Math.round(3000 + 2000 * Math.sin(t / 3)) }), fps: 412, ids: 38, sim: 0 });
    wss.clients.forEach(c => c.readyState === 1 && c.send(msg));
  }, 500);
  return server;
}
module.exports = { makeServer, WEB_PAGES: webPages, WS_VALUES };
if (require.main === module) { const port = Number(process.argv[2]) || 8080; makeServer().listen(port, () => console.log('mock board on http://localhost:' + port + '/')); }
