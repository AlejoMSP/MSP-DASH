// hub_pages.h - the hub's web pages.
// They live in this header, NOT in the .ino: the Arduino builder scans the .ino to add function prototypes and
// can mangle long raw strings full of JavaScript ("'asyncfunction' does not name a type"). Headers are left alone.
#pragma once
#include <Arduino.h>

// The navigation bar on every hub page (HUB_NAV, styled by HUB_NAV_STYLE inside each page's <style>).
#define HUB_NAV_STYLE ".nav{display:flex;flex-wrap:wrap;gap:4px;margin:0 0 12px;padding:6px;background:#0b1a1d;border-radius:8px;position:sticky;top:0;z-index:5}\n.nav a{color:#4dd0e1;text-decoration:none;padding:6px 10px;border-radius:5px;font-size:14px}.nav a:hover{background:#1d2b2e}"
#define HUB_NAV "<div class=\"nav\"><a href=\"/\">Hub</a><a href=\"/sensors\">Sensors</a><a href=\"/custom\">Custom sensors</a><a href=\"/buttons\">Buttons</a><a href=\"/simulator\">Simulator</a><a href=\"/rf\">433 MHz</a><a href=\"/can\">Raw CAN</a><a href=\"/update\">Firmware</a></div>"

static const char HOME_HTML[] PROGMEM = R"HTML(<html><head><meta name="viewport" content="width=device-width, initial-scale=1"><title>MSP-DASH Hub</title>
<style>body{background:#000;color:#ddd;font-family:sans-serif;max-width:720px;margin:auto;padding:12px}h2{color:#4dd0e1;margin:6px 0 10px}
h3{color:#4dd0e1;margin:0 0 8px;font-size:16px}.card{background:#111;border-radius:8px;padding:12px;margin:0 0 12px}
label{display:block;margin:10px 0 4px}input,select{width:100%;padding:6px;box-sizing:border-box;background:#000;color:#ddd;border:1px solid #333;border-radius:4px}
button{margin-top:10px;padding:8px 12px;background:#4dd0e1;border:0;color:#000;border-radius:6px}.row{display:flex;gap:8px}.row>*{flex:1}.note{color:#999;font-size:13px}
.dot{display:inline-block;width:12px;height:12px;border-radius:50%;margin-right:8px;vertical-align:middle}.on{background:#2e7d32}.off{background:#e53935}
.scr{display:flex;flex-wrap:wrap;align-items:center;gap:6px 10px;padding:8px 0;border-top:1px solid #222}.scr:first-child{border-top:0}
.scr .who{flex:1 1 200px}.scr a.btn{background:#1d2b2e;color:#4dd0e1;text-decoration:none;padding:5px 10px;border-radius:5px;font-size:13px}
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(150px,1fr));gap:8px}.kv{background:#000;border-radius:6px;padding:8px}
.kv b{display:block;color:#888;font-weight:normal;font-size:12px;margin-bottom:2px}details summary{cursor:pointer;color:#4dd0e1}
)HTML" HUB_NAV_STYLE R"HTML(</style></head><body>)HTML" HUB_NAV R"HTML(
<h2>MSP-DASH Hub</h2>
<div class="card"><h3>Screens</h3><div id="scr">Waiting for screens...</div></div>
<div class="card"><h3>Status</h3><div class="grid" id="stat"></div></div>
<div class="card"><h3>Sensors on the hub</h3><div class="grid" id="sens"></div>
<p class="note">Speed comes from rusEFI (hall sensor on the ECU). If it disagrees with the GPS at a steady speed, multiply rusEFI's speed calibration by the factor shown.</p></div>
<div class="card"><details><summary><b>Clock</b> <span class="note" id="clk"></span></summary>
<p class="note">From the GPS. The screens name their log files by it and stamp each row with it.</p>
<label>Time zone<select id="tzstd"><option value="-300">Eastern</option><option value="-360">Central</option><option value="-420">Mountain</option><option value="-480">Pacific</option><option value="-540">Alaska</option><option value="-600">Hawaii</option><option value="0">UTC</option></select></label>
<label><input type="checkbox" id="tzdst" style="width:auto"> US daylight saving time</label>
<button type="button" onclick="saveTz()">Save</button> <span id="tzmsg" class="note"></span></details></div>
<div class="card"><details><summary><b>Brightness</b> <span class="note" id="brisum"></span></summary>
<label>Brightness mode<select id="mode"><option value="0">Auto: car lights and dimmer</option><option value="1">Knob</option></select></label>
<label>Day brightness, lights off (%)<input type="number" id="day" min="5" max="100"></label>
<div class="row"><label>Night, dimmer lowest (%)<input type="number" id="nmin" min="5" max="100"></label>
<label>Night, dimmer highest (%)<input type="number" id="nmax" min="5" max="100"></label></div>
<div class="row"><label>Dimmer lowest (V)<input type="number" id="dlo" step="0.1"></label>
<label>Dimmer highest (V)<input type="number" id="dhi" step="0.1"></label></div>
<div class="row"><button type="button" onclick="grab('dlo')">Use now as lowest</button><button type="button" onclick="grab('dhi')">Use now as highest</button></div>
<label>Lights count as on above (V)<input type="number" id="lon" step="0.1"></label>
<button type="button" onclick="save()">Save</button> <span id="msg" class="note"></span>
<p class="note">Turn the lights on, roll the dimmer all the way down and press "Use now as lowest", all the way up and press "Use now as highest", then Save.</p></details></div>
<script>
let st={};const ids=["mode","day","nmin","nmax","dlo","dhi","lon"];const $=id=>document.getElementById(id);
const kv=(k,v)=>'<div class="kv"><b>'+k+'</b>'+v+'</div>';
function grab(id){$(id).value=st.dimV.toFixed(1);}
async function poll(){try{st=await (await fetch("/state")).json();
$("stat").innerHTML=kv("ECU",st.ecu)+kv("Signals live",st.live)+kv("Simulator",st.sim)+kv("Last button / command",st.last)+kv("Data packets sent",st.sent)+kv("Hub","build "+st.build+"<br>"+st.ip+", channel "+st.ch);
$("brisum").textContent="| "+st.bri+" % now | lights "+st.lightsV.toFixed(1)+" V ("+(st.lightsOn?"on":"off")+") | dimmer "+st.dimV.toFixed(1)+" V";
if(!poll.done){ids.forEach(i=>$(i).value=st.cfg[i]);poll.done=1;}}catch(e){}}
async function save(){const q=ids.map(i=>i+"="+encodeURIComponent($(i).value)).join("&");$("msg").textContent=await (await fetch("/save?"+q)).text();}
function ago(ms){return ms<60000?Math.round(ms/1000)+" s ago":Math.round(ms/60000)+" min ago";}
async function scr(){try{const l=await (await fetch("/screens.json")).json();
const order=n=>({left:0,center:1,centre:1,middle:1,right:2})[(n||"").toLowerCase()]??3;
l.sort((a,b)=>order(a.name)-order(b.name)||(a.ip<b.ip?-1:1));
$("scr").innerHTML=l.length?l.map(s=>{const u="http://"+s.ip;return '<div class="scr"><span class="who"><span class="dot '+(s.online?'on':'off')+'"></span><b>'+(s.name||'(no name)')+'</b> '+
(s.online?'online':'OFFLINE, last heard '+ago(s.age))+'<br><span class="note">'+s.ip+' | build '+s.build+' | '+s.mac+'</span></span>'+
'<a class="btn" href="'+u+'/">Live</a><a class="btn" href="'+u+'/gauges">Gauges</a><a class="btn" href="'+u+'/log-channels">Logging</a><a class="btn" href="'+u+'/sd">SD card</a><a class="btn" href="'+u+'/settings">Settings</a></div>';}).join(''):
'No screens heard yet. Screens set to main do not say hello.';}catch(e){}}
async function spd(){try{const g=await (await fetch("/speed.json")).json();
const gps=!g.gpsok?"not connected":!g.fix?"no fix ("+g.sats+" sats)":g.gpsmph.toFixed(1)+" mph, "+g.sats+" sats";
const ecu=g.ecuok?g.ecumph.toFixed(1)+" mph":"no data";
const cmp=g.ecuok&&g.fix&&g.gpsmph>10&&g.ecumph>1?((g.ecumph-g.gpsmph)/g.gpsmph*100).toFixed(1)+" % (factor "+(g.gpsmph/g.ecumph).toFixed(3)+")":"--";
$("sens").innerHTML=kv("Ambient",st.dht||"--")+kv("Fuel",(st.fuel!=null?st.fuel+" %":"--"))+kv("EGT",(st.egt||"--")+"<br><span class=note>"+(st.egtsent||0)+" frames to CAN</span>")+
kv("Speed (rusEFI)",ecu)+kv("GPS",gps)+kv("Speed difference",cmp);}catch(e){}}
async function clk(){try{const t=await (await fetch("/time.json")).json();$("clk").textContent="| "+(t.known?t.local:"no time from the GPS yet");
if(!clk.done){$("tzstd").value=String(t.std);$("tzdst").checked=t.dst;clk.done=1;}}catch(e){}}
async function saveTz(){$("tzmsg").textContent=await (await fetch("/time/set?std="+$("tzstd").value+"&dst="+($("tzdst").checked?1:0))).text();}
poll();setInterval(poll,1000);scr();setInterval(scr,1000);spd();setInterval(spd,1000);clk();setInterval(clk,1000);
</script></body></html>)HTML";

static const char SENSORS_HTML[] PROGMEM = R"HTML(<!doctype html><html><head><meta name="viewport" content="width=device-width, initial-scale=1"><title>MSP-DASH Sensors</title>
<style>body{background:#000;color:#ddd;font-family:sans-serif;max-width:640px;margin:auto;padding:12px}h2{color:#4dd0e1}a{color:#4dd0e1}
table{width:100%;border-collapse:collapse}td,th{padding:5px 8px;text-align:left;border-bottom:1px solid #222}th{color:#999;font-weight:normal}
.v{font-family:monospace;text-align:right}.tag{padding:1px 6px;border-radius:4px;font-size:12px}.hub{background:#1b5e20}.sim{background:#e65100}.can{background:#0d47a1}
.stale td{color:#666}.note{color:#999;font-size:13px})HTML" HUB_NAV_STYLE R"HTML(</style></head><body>)HTML" HUB_NAV R"HTML(
<h2>Sensors</h2><div class="note" id="sum"></div>
<table><thead><tr><th>Signal</th><th class="v">Value</th><th>Source</th><th class="v">Age</th></tr></thead><tbody id="t"></tbody></table>
<p class="note">Hub = a sensor wired to this board. ECU = decoded from a CAN frame. Grey = not heard for 2 s.</p>
<h3 style="color:#4dd0e1;margin:16px 0 4px">On the bus, not decoded</h3>
<div class="note" id="usum"></div>
<table><thead><tr><th>CAN ID</th><th>From</th><th>Bytes</th><th class="v">Age</th><th></th></tr></thead><tbody id="u"></tbody></table>
<p class="note">Frames heard on the CAN bus that no sensor reads yet. They show up here by themselves as soon as they appear. "Make a sensor" opens a custom slot for that frame.</p>
<p><a href="/">Back</a> | <a href="/can">Raw CAN</a></p>
<script>
const NAME={1:"RPM",2:"Coolant Temp",3:"MAP/Boost",4:"AFR",5:"Oil Pressure",6:"Oil Temp",7:"Fuel Pressure",8:"Ethanol",9:"Throttle Position",10:"Battery Voltage",
11:"Rev Limiter",12:"Main Relay",13:"Fuel Pump",14:"Check Engine",15:"O2 Heater",16:"Lambda Protection",17:"Fan 1",18:"Fan 2",19:"Brake Pedal",
20:"EGT",21:"Intake Air Temp",22:"Ambient Temp",23:"Humidity",24:"Fuel Level",25:"Speed",26:"GPS Speed",27:"GPS Satellites"};
const UNIT={1:"rpm",2:"F",3:"psi",4:"",5:"psi",6:"F",7:"psi",8:"%",9:"%",10:"V",20:"F",21:"F",22:"F",23:"%",24:"%",25:"mph",26:"mph",27:"sats"};
const DEC={1:0,2:0,3:0,4:1,5:0,6:0,7:0,8:0,9:0,10:1,20:0,21:0,22:1,23:0,24:0,25:0,26:0,27:0};
const HUB={4294901761:"MAX6675 thermocouple (GPIO 18/19/23)",4294901762:"DHT22 (GPIO 4)",4294901763:"Fuel sender pot (GPIO 36)",4294901764:"Bench simulator (FAKE)",4294901766:"GT-U7 GPS (GPIO 16/17, R17/R16)"};
function ecu(id){return id>=0x200&&id<=0x20F?"rusEFI":id>=0x5F0&&id<=0x63F?"MegaSquirt/Link":"";}
async function u(){try{const l=(await (await fetch("/sensors.json")).json()).sort((a,b)=>(NAME[a.sig]||"").localeCompare(NAME[b.sig]||""));let h=0,c=0,e=new Set();
document.getElementById("t").innerHTML=l.map(s=>{const isHub=HUB[s.src]!==undefined;isHub?h++:c++;const k=ecu(s.src);if(!isHub&&k)e.add(k);
const flag=s.sig>=11&&s.sig<=19;const val=flag?(s.v>0.5?"ON":"off"):s.v.toFixed(DEC[s.sig]??1)+" "+(UNIT[s.sig]||"");
const src=s.src===4294901764?'<span class="tag sim">Sim</span> '+HUB[s.src]:isHub?'<span class="tag hub">Hub</span> '+HUB[s.src]:'<span class="tag can">ECU</span> CAN 0x'+s.src.toString(16).toUpperCase()+(k?" ("+k+")":"");
return '<tr class="'+(s.age>2000?'stale':'')+'"><td>'+(NAME[s.sig]||("Signal "+s.sig))+'</td><td class="v">'+val+'</td><td>'+src+'</td><td class="v">'+(s.age<1000?s.age+" ms":(s.age/1000).toFixed(0)+" s")+'</td></tr>';}).join("");
document.getElementById("sum").textContent=h+" from the hub, "+c+" from the ECU"+(e.size?" ("+[...e].join(", ")+")":"");}catch(x){}}
async function und(){try{const used=new Set((await (await fetch("/sensors.json")).json()).map(s=>s.src));
const j=await (await fetch("/can.json")).json();
const l=j.ids.filter(x=>!used.has(x.id)&&x.age<10000).sort((a,b)=>a.id-b.id);
document.getElementById("usum").textContent=l.length?l.length+" frame"+(l.length===1?"":"s")+" not read by any sensor":"Every frame on the bus is read by a sensor (or the bus is quiet).";
document.getElementById("u").innerHTML=l.map(x=>'<tr class="'+(x.age>2000?'stale':'')+'"><td class="v" style="text-align:left">0x'+x.id.toString(16).toUpperCase().padStart(3,"0")+'</td><td>'+(ecu(x.id)||"unknown")+'</td><td class="v" style="text-align:left">'+x.d+'</td><td class="v">'+(x.age<1000?x.age+" ms":(x.age/1000).toFixed(0)+" s")+'</td><td><a href="/custom?id=0x'+x.id.toString(16).toUpperCase()+'">Make a sensor</a></td></tr>').join("");}catch(x){}}
(async()=>{try{for(const c of await (await fetch("/custom.json")).json())if(c.name)NAME[28+c.i]=c.name;}catch(x){}u();})();setInterval(u,500);und();setInterval(und,1000);
</script></body></html>)HTML";

static const char SIM_HTML[] PROGMEM = R"HTML(<!doctype html><html><head><meta name="viewport" content="width=device-width, initial-scale=1"><title>MSP-DASH Simulator</title>
<style>body{background:#000;color:#ddd;font-family:sans-serif;max-width:480px;margin:auto;padding:12px}h2{color:#4dd0e1}a{color:#4dd0e1}
.warnbox{background:#2a1f00;border:1px solid #f5a623;border-radius:8px;padding:10px;font-size:.85em;color:#f5c26b}
.statebox{margin:12px 0;padding:10px;border-radius:8px;background:#111;text-align:center}.statebox.on{background:#f5a623;color:#000;font-weight:bold}
.btnrow{display:flex;gap:8px;justify-content:center;flex-wrap:wrap}button{padding:10px 14px;background:#4dd0e1;color:#000;border:0;border-radius:6px}
#msg{text-align:center;margin:10px 0;min-height:1.2em;color:#4dd0e1}#msg.bad{color:#ff6b6b}
.sl{margin:10px 0}.sl label{display:flex;justify-content:space-between;font-size:.85em;color:#4dd0e1}.sl input{width:100%})HTML" HUB_NAV_STYLE R"HTML(</style></head><body>)HTML" HUB_NAV R"HTML(
<h2>Bench Simulator</h2>
<div class="warnbox"><b>This feeds FAKE values</b> to every screen, so you can test with no ECU connected. It switches itself off after 10 minutes without a change, at every reboot of the hub, and the moment a real CAN frame arrives. The screens say SIMULATED and the main screen does not log while it runs.</div>
<div id="state" class="statebox">Loading...</div>
<div class="btnrow"><button onclick="cmd('start','demo')">Start demo</button><button onclick="cmd('start','manual')">Start manual</button><button onclick="cmd('stop')">Stop</button></div>
<div id="msg"></div><div id="sliders"></div>
<p><a href="/">Back</a> | <a href="/sensors">Sensors</a></p>
<script>
const NAME={1:"RPM",2:"Coolant Temp (F)",3:"Boost (psi)",4:"AFR",5:"Oil Pressure (psi)",6:"Oil Temp (F)",7:"Fuel Pressure (psi)",8:"Ethanol %",9:"Throttle %",10:"Battery (V)",
11:"Rev Limiter",12:"Main Relay",13:"Fuel Pump",14:"Check Engine",15:"O2 Heater",16:"Lambda Protection",17:"Fan 1",18:"Fan 2",19:"Brake Pedal",
20:"EGT (F)",21:"Intake Air Temp (F)",22:"Ambient Temp (F)",23:"Humidity %",24:"Fuel Level %",25:"Speed (mph)",26:"GPS Speed (mph)",27:"GPS Satellites"};
let info=null,building="",sending=false,pending=null;
function setMsg(t,bad){const m=document.getElementById("msg");m.textContent=t;m.className=bad?"bad":"";}
async function post(p){const r=await fetch("/sim",{method:"POST",headers:{"Content-Type":"application/x-www-form-urlencoded"},body:new URLSearchParams(p).toString()});
const t=await r.text();if(!r.ok)throw new Error(t||("board returned "+r.status));return t;}
async function cmd(c,mode){try{await post(mode?{cmd:c,mode:mode}:{cmd:c});setMsg("");}catch(e){setMsg(e.message,true);}refresh();}
function fmtTime(s){return Math.floor(s/60)+":"+String(s%60).padStart(2,"0");}
function shown(s){return Number(s.value).toFixed(s.step<1?1:0);}
function render(){const st=document.getElementById("state");
if(info.active){st.className="statebox on";st.textContent="SIMULATING ("+(info.manual?"manual":"demo")+") - switches off in "+fmtTime(info.seconds_left);}
else{st.className="statebox";st.textContent="Off"+(info.stop_reason?" - last stopped: "+info.stop_reason:"");}
const box=document.getElementById("sliders"),key=info.active?(info.manual?"manual":"demo"):"off";
if(key!==building){building=key;if(key==="off"){box.innerHTML="";return;}
box.innerHTML=info.signals.map(s=>'<div class="sl"><label>'+(NAME[s.id]||("Signal "+s.id))+' <span id="v'+s.id+'"></span></label><input id="r'+s.id+'" type="range" min="'+s.min+'" max="'+s.max+'" step="'+s.step+'" '+(info.manual?"":"disabled")+' oninput="slide('+s.id+',this.value)"></div>').join("");}
for(const s of info.signals){const lab=document.getElementById("v"+s.id),rng=document.getElementById("r"+s.id);if(lab)lab.textContent=shown(s);if(rng&&document.activeElement!==rng)rng.value=s.value;}}
function slide(id,val){document.getElementById("v"+id).textContent=val;pending={id:id,val:val};pump();}
async function pump(){if(sending)return;sending=true;while(pending){const p=pending;pending=null;try{await post({cmd:"set",sig:p.id,val:p.val});}catch(e){setMsg(e.message,true);}}sending=false;}
async function refresh(){try{info=await (await fetch("/sim.json")).json();render();}catch(e){document.getElementById("state").textContent="Could not reach the hub - retrying...";}}
setInterval(refresh,1000);refresh();
</script></body></html>)HTML";

// /custom: the 16 custom sensor slots (custom_slots.h). Stage 1: each reads a field of a CAN frame.
static const char CUSTOM_HTML[] PROGMEM = R"HTML(<!doctype html><html><head><meta name="viewport" content="width=device-width, initial-scale=1"><title>MSP-DASH Custom Sensors</title>
<style>body{background:#000;color:#ddd;font-family:sans-serif;max-width:640px;margin:auto;padding:12px}h2,h3{color:#4dd0e1}a{color:#4dd0e1}
table{width:100%;border-collapse:collapse}td,th{padding:5px 8px;text-align:left;border-bottom:1px solid #222}th{color:#999;font-weight:normal}
.v{font-family:monospace;text-align:right}.note{color:#888;font-size:13px}.stale{color:#666}
.box{background:#111;border-radius:8px;padding:12px;margin:12px 0}label{display:block;margin:6px 0 2px;color:#4dd0e1;font-size:13px}
input,select{width:100%;box-sizing:border-box;padding:6px;background:#000;color:#ddd;border:1px solid #333;border-radius:4px}
.row{display:flex;gap:8px}.row>div{flex:1}button{padding:8px 14px;background:#4dd0e1;color:#000;border:0;border-radius:6px;margin:8px 8px 0 0}
button.del{background:#c62828;color:#fff}#preview{font-family:monospace;font-size:18px;color:#4dd0e1;margin-top:8px}#msg{min-height:1.2em;color:#4dd0e1}#msg.bad{color:#ff6b6b})HTML" HUB_NAV_STYLE R"HTML(</style></head><body>)HTML" HUB_NAV R"HTML(
<h2>Custom Sensors</h2>
<p class="note">Read any value from a CAN frame and send it to the screens. Each slot shows up in the screens' gauge, lamp and logging lists under its name within a few seconds, no reflash.</p>
<table><thead><tr><th>Slot</th><th>Name</th><th>From</th><th class="v">Value</th><th></th></tr></thead><tbody id="list"></tbody></table>
<div class="box" id="ed" style="display:none">
<h3 id="edtitle">Slot</h3>
<div class="row"><div><label>Name</label><input id="name" maxlength="15"></div><div><label>Unit</label><input id="unit" maxlength="7"></div></div>
<div class="row"><div><label>Decimals</label><select id="dec"><option>0</option><option>1</option><option>2</option><option>3</option></select></div>
<div><label>Gauge min</label><input id="min" type="number" step="any"></div><div><label>Gauge max</label><input id="max" type="number" step="any"></div></div>
<label>CAN ID (frames seen on the bus, or type one like 0x208)</label><input id="cid" list="ids" oninput="pv()"><datalist id="ids"></datalist>
<div class="row"><div><label>Start byte (0-7)</label><input id="start" type="number" min="0" max="7" oninput="pv()"></div>
<div><label>Size</label><select id="bits" onchange="shape();pv()"><option value="8">8 bit</option><option value="16">16 bit</option><option value="32">32 bit</option><option value="1">1 bit (on/off)</option></select></div></div>
<div class="row" id="multi"><div><label>Byte order</label><select id="be" onchange="pv()"><option value="0">Little endian (Intel, rusEFI)</option><option value="1">Big endian (Motorola, MegaSquirt/Link)</option></select></div>
<div><label>Signed</label><select id="sgn" onchange="pv()"><option value="0">No</option><option value="1">Yes</option></select></div></div>
<div class="row" id="onebit" style="display:none"><div><label>Bit (0-7)</label><input id="bit" type="number" min="0" max="7" oninput="pv()"></div></div>
<div class="row" id="lin"><div><label>Scale (value = raw x scale + offset)</label><input id="scale" type="number" step="any" oninput="pv()"></div><div><label>Offset</label><input id="offset" type="number" step="any" oninput="pv()"></div></div>
<div id="preview">-</div><div class="note" id="raw"></div>
<button onclick="save()">Save</button><button onclick="closeEd()">Cancel</button><button class="del" onclick="clr()">Clear slot</button>
<div id="msg"></div></div>
<p><a href="/">Back</a> | <a href="/sensors">Sensors</a></p>
<script>
let slots=[],can={ids:[]},cur=-1;
const $=id=>document.getElementById(id);
const esc=t=>String(t).replace(/&/g,"&amp;").replace(/</g,"&lt;").replace(/>/g,"&gt;").replace(/"/g,"&quot;");
const hex=n=>"0x"+Number(n).toString(16).toUpperCase().padStart(3,"0");
// Same maths as custom_can_decode() in custom_slots.h. null = the frame is too short for this field.
function decode(c,bytes){
  if(c.bits==1){if(c.start>=bytes.length)return null;return (bytes[c.start]>>c.bit)&1;}
  const n=c.bits/8;if(c.start+n>bytes.length)return null;
  let raw=0;for(let i=0;i<n;i++){const b=c.be?bytes[c.start+i]:bytes[c.start+n-1-i];raw=raw*256+b;}
  if(c.sgn){const top=Math.pow(2,c.bits);if(raw>=top/2)raw-=top;}
  return raw*c.scale+c.offset;}
function bytesOf(id){const f=can.ids.find(x=>x.id===id);return f?f.d.trim().split(/\s+/).filter(Boolean).map(h=>parseInt(h,16)):null;}
function form(){return{i:cur,name:$("name").value.trim(),unit:$("unit").value.trim(),dec:+$("dec").value,min:parseFloat($("min").value),max:parseFloat($("max").value),
  id:parseInt($("cid").value),start:parseInt($("start").value),bits:+$("bits").value,bit:parseInt($("bit").value)||0,be:+$("be").value,sgn:+$("sgn").value,
  scale:$("scale").value===""?1:parseFloat($("scale").value),offset:parseFloat($("offset").value)||0};}
function shape(){const one=$("bits").value==="1";$("onebit").style.display=one?"":"none";$("multi").style.display=one?"none":"";$("lin").style.display=one?"none":"";}
function pv(){const c=form();const b=bytesOf(c.id);
  if(isNaN(c.id)){$("preview").textContent="-";$("raw").textContent="Pick or type a CAN ID";return;}
  if(!b){$("preview").textContent="-";$("raw").textContent=hex(c.id)+" not heard on the bus right now";return;}
  $("raw").textContent=hex(c.id)+": "+b.map(x=>x.toString(16).toUpperCase().padStart(2,"0")).join(" ");
  const v=isNaN(c.start)?null:decode(c,b);
  $("preview").textContent=v===null?"does not fit in this frame":(c.bits==1?(v?"ON":"off"):v.toFixed(c.dec)+(c.unit?" "+c.unit:""));}
function srcText(s){return s.src==1?(hex(s.id)+" byte "+s.start+(s.bits==1?" bit "+s.bit:", "+s.bits+" bit")):"";}
function render(){$("list").innerHTML=slots.map(s=>'<tr class="'+(s.name&&!s.has?'stale':'')+'"><td>'+(s.i+1)+'</td><td>'+(s.name?esc(s.name):'<span class="note">empty</span>')+'</td><td class="note">'+srcText(s)+'</td><td class="v">'+
  (s.name&&s.has?(s.bits==1?(s.value?"ON":"off"):s.value.toFixed(s.dec)+(s.unit?" "+esc(s.unit):"")):"--")+'</td><td><button onclick="edit('+s.i+')">'+(s.name?"Edit":"Set up")+'</button></td></tr>').join("");
  $("ids").innerHTML=can.ids.map(x=>'<option value="'+hex(x.id)+'">').join("");}
function edit(i,id){cur=i;const s=slots[i];$("ed").style.display="";$("edtitle").textContent="Slot "+(i+1);$("msg").textContent="";
  $("name").value=s.name;$("unit").value=s.unit;$("dec").value=String(s.dec);$("min").value=s.name?s.min:0;$("max").value=s.name?s.max:100;
  $("cid").value=s.src==1?hex(s.id):(id||"");$("start").value=s.src==1?s.start:0;$("bits").value=String(s.src==1?s.bits:8);$("bit").value=s.bit;
  $("be").value=String(s.be);$("sgn").value=String(s.sgn);$("scale").value=s.src==1?s.scale:1;$("offset").value=s.src==1?s.offset:0;shape();pv();}
function closeEd(){cur=-1;$("ed").style.display="none";}
function setMsg(t,bad){$("msg").textContent=t;$("msg").className=bad?"bad":"";}
async function save(){const c=form();if(!c.name){setMsg("Give the sensor a name",true);return;}
  const q=new URLSearchParams({i:c.i,name:c.name,unit:c.unit,dec:c.dec,min:c.min,max:c.max,id:hex(c.id),start:c.start,bits:c.bits,bit:c.bit,be:c.be,sgn:c.sgn,scale:c.scale,offset:c.offset});
  const r=await fetch("/custom/set?"+q.toString());const t=await r.text();setMsg(t,!r.ok);if(r.ok)load();}
async function clr(){if(cur<0)return;const r=await fetch("/custom/clear?i="+cur);setMsg(await r.text(),!r.ok);if(r.ok){closeEd();load();}}
async function load(){try{slots=await (await fetch("/custom.json")).json();render();}catch(e){}}
async function tick(){try{can=await (await fetch("/can.json")).json();if(cur>=0)pv();}catch(e){}load();}
(async()=>{await load();await tick();const want=new URLSearchParams(location.search).get("id");
  if(want){const free=slots.findIndex(s=>!s.name);if(free>=0)edit(free,want);}})();
setInterval(tick,1000);
</script></body></html>)HTML";
// /rf: CC1101 433 MHz listen mode - every burst it hears, as pulses, bits and Manchester bytes.
static const char RF_HTML[] PROGMEM = R"HTML(<!doctype html><html><head><meta name="viewport" content="width=device-width, initial-scale=1"><title>MSP-DASH 433 MHz</title>
<style>body{background:#000;color:#ddd;font-family:sans-serif;max-width:720px;margin:auto;padding:12px}h2{color:#4dd0e1}a{color:#4dd0e1}
.box{background:#111;border-radius:8px;padding:10px;margin:10px 0}.note{color:#888;font-size:13px}.bad{color:#ff6b6b}
.mono{font-family:monospace;font-size:12px;word-break:break-all}.b{border-top:1px solid #222;padding:8px 0}.hex{color:#4dd0e1;font-size:14px}
select,input{background:#000;color:#ddd;border:1px solid #333;border-radius:4px;padding:5px}button{padding:6px 12px;background:#4dd0e1;color:#000;border:0;border-radius:6px}
svg{width:100%;height:140px;background:#000;border:1px solid #222;border-radius:4px})HTML" HUB_NAV_STYLE R"HTML(</style></head><body>)HTML" HUB_NAV R"HTML(
<h2>433 MHz listen</h2>
<div class="box" id="st">Loading...</div>
<div class="box"><svg id="ch" viewBox="0 0 240 100" preserveAspectRatio="none"></svg>
<div class="note">Last 2 minutes, one point per second: <span style="color:#4dd0e1">strongest signal</span>, <span style="color:#888">background</span>, <span style="color:#f5a623">threshold</span> (scale -110 to -30 dBm). A sensor sending shows as a spike.</div></div>
<div class="box">Mode <select id="mod"><option value="fsk">2-FSK (most tire sensors)</option><option value="ook">OOK / ASK (remotes, some sensors)</option></select>
<label><input type="checkbox" id="auto" onchange="shape()"> Automatic threshold</label>
<span id="mbox">margin <input id="margin" type="number" min="3" max="40" style="width:55px"> dB above the background</span>
<span id="tbox">threshold <input id="thr" type="number" min="-110" max="-30" style="width:70px"> dBm</span>
<button onclick="save()">Save</button> <span id="msg" class="note"></span>
<div><label><input type="checkbox" id="hide" checked onchange="draw()"> Hide noise (show only bursts with 24+ Manchester bits)</label></div>
<div class="note">Tire sensors send when the wheel turns, when the pressure changes, or every few minutes when parked. Watch for a spike when the kit's display changes.</div></div>
<h3 style="color:#4dd0e1">Strongest bursts</h3><div class="note">The 5 strongest since the last clear. Noise cannot push these out. <button onclick="clr()">Clear</button></div>
<div id="top"></div><pre id="full" class="mono box" style="display:none;white-space:pre-wrap"></pre>
<h3 style="color:#4dd0e1">Latest bursts</h3>
<div id="list"></div>

<script>
const $=id=>document.getElementById(id);let loaded=false,last=null;
function shape(){$("mbox").style.display=$("auto").checked?"":"none";$("tbox").style.display=$("auto").checked?"none":"";}
async function save(){const q=new URLSearchParams({mod:$("mod").value,auto:$("auto").checked?1:0,margin:$("margin").value,thr:$("thr").value});const r=await fetch("/rf/set?"+q);$("msg").textContent=await r.text();}
function age(ms){return ms<60000?(ms/1000).toFixed(0)+" s ago":(ms/60000).toFixed(1)+" min ago";}
function y(d){return Math.max(0,Math.min(100,(-30-d)*100/80));}
function chart(j){const h=j.hist,n=h.length;if(!n){$("ch").innerHTML="";return;}const x=i=>(240-(n-1-i)*2);
 const line=(k,c)=>'<polyline fill="none" stroke="'+c+'" stroke-width="1" points="'+h.map((p,i)=>x(i)+","+y(p[k]).toFixed(1)).join(" ")+'"/>';
 const thr=j.auto?h.map((p,i)=>x(i)+","+y(p[1]+j.margin).toFixed(1)).join(" "):(x(0)+","+y(j.thr)+" 240,"+y(j.thr));
 $("ch").innerHTML=line(1,"#888")+'<polyline fill="none" stroke="#f5a623" stroke-width="0.7" stroke-dasharray="2,2" points="'+thr+'"/>'+line(0,"#4dd0e1");}
function card(b){return '<div class="b"><b>'+age(b.age)+'</b> | '+b.rssi+' dBm | '+b.pulses+' pulses | half-bit '+b.unit+' us (about '+(b.unit?Math.round(500000/b.unit):0)+' bit/s) | '+b.nbits+' Manchester bits'+(b.inv?' (inverted)':'')+
 ' <button onclick="full('+b.ms+')">All pulses</button><div class="hex mono">'+(b.hex||'(no Manchester bytes)')+'</div><div class="mono note">bits: '+b.bits+'</div><div class="mono note">first pulses (us, first level '+b.first+'): '+b.w.join(" ")+'</div></div>';}
async function full(ms){const r=await fetch("/rf/burst?ms="+ms);$("full").style.display="";$("full").textContent=await r.text();}
async function clr(){await fetch("/rf/clear");$("full").style.display="none";}
function draw(){const j=last;if(!j)return;const hide=$("hide").checked;const l=j.bursts.filter(b=>!hide||b.nbits>=24);
 const t=(j.top||[]).slice().sort((a,b)=>b.rssi-a.rssi);$("top").innerHTML=t.length?t.map(card).join(""):'<p class="note">None yet.</p>';
 $("list").innerHTML=l.length?l.map(b=>'<div class="b"><b>'+age(b.age)+'</b> | '+b.rssi+' dBm | '+b.pulses+' pulses | half-bit '+b.unit+' us (about '+(b.unit?Math.round(500000/b.unit):0)+' bit/s) | '+b.nbits+' Manchester bits'+(b.inv?' (inverted)':'')+
 '<div class="hex mono">'+(b.hex||'(no Manchester bytes)')+'</div><div class="mono note">bits: '+b.bits+'</div><div class="mono note">pulses (us, first level '+b.first+'): '+b.w.join(" ")+'</div></div>').join("")
 :'<p class="note">'+(j.bursts.length&&hide?j.bursts.length+" bursts hidden as noise (untick Hide noise to see them).":"Nothing heard yet above the threshold.")+'</p>';}
async function poll(){try{const j=await (await fetch("/rf.json")).json();last=j;
if(!j.ok){$("st").innerHTML='<span class="bad">CC1101 not found</span> (read part 0x'+j.part.toString(16)+', version 0x'+j.version.toString(16)+'; expected 00 / 14). Check VCC on 3.3 V, GND, and SCK/MOSI/MISO/CSN.';return;}
$("st").innerHTML="CC1101 found (version 0x"+j.version.toString(16)+") | 433.92 MHz, "+(j.ook?"OOK":"2-FSK")+" | signal now "+j.rssi+" dBm | background "+j.floor+" dBm | threshold "+j.active+" dBm"+(j.auto?" (auto)":"")+" | bursts heard: "+j.total;
if(!loaded){$("mod").value=j.ook?"ook":"fsk";$("thr").value=j.thr;$("auto").checked=j.auto;$("margin").value=j.margin;shape();loaded=true;}
chart(j);draw();
}catch(e){$("st").textContent="Could not reach the hub - retrying...";}}
poll();setInterval(poll,1000);
</script></body></html>)HTML";
// /buttons: what each button does, for a press and for holds of 1, 2, 3, 5 and 10 s (button_actions.h).
static const char BUTTONS_HTML[] PROGMEM = R"HTML(<!doctype html><html><head><meta name="viewport" content="width=device-width, initial-scale=1"><title>MSP-DASH Buttons</title>
<style>body{background:#000;color:#ddd;font-family:sans-serif;max-width:760px;margin:auto;padding:12px}h2{color:#4dd0e1}a{color:#4dd0e1}
table{width:100%;border-collapse:collapse}td,th{padding:6px;border-bottom:1px solid #222;vertical-align:top}th{color:#4dd0e1;text-align:left}
select{width:100%;box-sizing:border-box;background:#000;color:#ddd;border:1px solid #333;border-radius:4px;padding:5px;margin:2px 0}
.note{color:#888;font-size:13px}#msg{color:#4dd0e1;min-height:1.2em}#msg.bad{color:#ff6b6b}button{padding:8px 14px;background:#4dd0e1;color:#000;border:0;border-radius:6px})HTML" HUB_NAV_STYLE R"HTML(</style></head><body>)HTML" HUB_NAV R"HTML(
<h2>Buttons</h2>
<p class="note">Left = GPIO 26 (L15), right = GPIO 25 (L14). Hold a button and let go: the action for the longest time you reached runs
(if nothing is set at that time, the last one you passed). While you hold, the screens show what letting go will do.
Page actions go to the screen chosen with "Next gauge" (all screens until you choose one; back to all after a minute).</p>
<div id="state" class="note"></div>
<table><thead><tr><th>Hold</th><th>Left</th><th>Right</th><th>Both</th></tr></thead><tbody id="grid"></tbody></table>
<div id="msg"></div>
<p><button onclick="defaults()">Back to the defaults</button></p>

<script>
const LEVELS=["Press","1 s","2 s","3 s","5 s","10 s"];const GOTO=4,COLOR=7,RACE=8,CYCLE=255;
let cfg=null;const $=id=>document.getElementById(id);
function paramHtml(i,l,a,p){
  if(a==GOTO||a==RACE){let o="";for(let n=1;n<=8;n++)o+='<option value="'+n+'"'+(n==p?" selected":"")+'>'+(a==RACE?"Race page ":"Page ")+n+'</option>';return '<select onchange="setB('+i+','+l+','+a+',+this.value)">'+o+'</select>';}
  if(a==COLOR){let o='<option value="255"'+(p==CYCLE?" selected":"")+'>Next scheme each time</option>';cfg.colors.forEach((n,k)=>o+='<option value="'+k+'"'+(k==p?" selected":"")+'>'+(k+1)+'. '+n+'</option>');return '<select onchange="setB('+i+','+l+','+a+',+this.value)">'+o+'</select>';}
  return "";}
function render(){let h="";for(let l=0;l<6;l++){h+="<tr><th>"+LEVELS[l]+"</th>";for(let i=0;i<3;i++){const b=cfg.bind[i][l];
  const opts=cfg.actions.map((n,k)=>'<option value="'+k+'"'+(k==b[0]?" selected":"")+'>'+n+'</option>').join("");
  h+='<td><select onchange="pickA('+i+','+l+',+this.value)">'+opts+'</select>'+paramHtml(i,l,b[0],b[1])+'</td>';}h+="</tr>";}
  $("grid").innerHTML=h;$("state").textContent="Race mode: "+(cfg.race?"ON":"off")+" | buttons control: "+(cfg.target||"all screens");}
function pickA(i,l,a){const p=(a==GOTO||a==RACE)?1:(a==COLOR?CYCLE:0);setB(i,l,a,p);}
async function setB(i,l,a,p){const r=await fetch("/buttons/set?"+new URLSearchParams({in:i,lv:l,a:a,p:p}));const t=await r.text();
  $("msg").textContent=r.ok?(LEVELS[l]+" "+["left","right","both"][i]+": "+cfg.actions[a]+" - saved"):t;$("msg").className=r.ok?"":"bad";if(r.ok){cfg.bind[i][l]=[a,p];render();}}
async function defaults(){const r=await fetch("/buttons/defaults");$("msg").textContent=await r.text();load();}
async function load(){try{cfg=await (await fetch("/buttons.json")).json();render();}catch(e){$("msg").textContent="Could not reach the hub";}}
load();
</script></body></html>)HTML";
