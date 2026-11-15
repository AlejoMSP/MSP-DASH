// hub_pages.h - the hub's web pages.
// They live in this header, NOT in the .ino: the Arduino builder scans the .ino to add function prototypes and
// can mangle long raw strings full of JavaScript ("'asyncfunction' does not name a type"). Headers are left alone.
#pragma once
#include <Arduino.h>
#include "oled_icons.h"   // OLED_ICONS_JS for the /oled page

// The side menu on every hub page (HUB_NAV, styled by HUB_NAV_STYLE inside each page's <style>): grouped by task, a
// drawer behind the menu button on a phone. Also the shared look (chips, toast). The current page is highlighted by the
// onerror of a 1-pixel image, so the menu adds no <script> to the page.
#define HUB_NAV_STYLE "*{box-sizing:border-box}\n.nav{position:fixed;left:0;top:0;bottom:0;width:200px;background:#0b1a1d;padding:12px 8px;overflow-y:auto;z-index:50}\n.nav .brand{color:#4dd0e1;font-weight:bold;font-size:18px;margin:2px 10px 8px}\n.nav .grp{color:#6b8a90;font-size:11px;text-transform:uppercase;letter-spacing:.08em;margin:14px 10px 4px}\n.nav a{display:block;color:#cfe8ec;text-decoration:none;padding:7px 10px;border-radius:6px;font-size:14px}\n.nav a:hover{background:#13292d}.nav a.on{background:#1d3b40;color:#4dd0e1;font-weight:bold}\n.navbtn{display:none;position:fixed;top:8px;left:8px;z-index:60;background:#0b1a1d;color:#4dd0e1;border:1px solid #1d3b40;border-radius:6px;font-size:20px;padding:4px 10px}\n@media(min-width:800px){body{margin-left:216px!important}}\n@media(max-width:799px){.navbtn{display:block}.nav{transform:translateX(-100%);transition:transform .2s}.nav.open{transform:none;box-shadow:4px 0 20px #000}body{padding-top:48px!important}}\n.chip{display:inline-block;font-size:12px;padding:2px 8px;border-radius:10px;background:#222;color:#bbb}.chip.ok{background:#16351c;color:#7bd88f}.chip.warn{background:#3d3000;color:#ffd54f}.chip.bad{background:#3d0b0b;color:#ff6b6b}\n.toast{position:fixed;right:16px;bottom:16px;background:#1d3b40;color:#e0f7fa;padding:10px 14px;border-radius:8px;z-index:70;box-shadow:0 4px 16px #000}"
#define HUB_NAV "<button class=\"navbtn\" onclick=\"document.querySelector('.nav').classList.toggle('open')\">&#9776;</button><div class=\"nav\"><div class=\"brand\">MSP-DASH</div><div class=\"grp\">Overview</div><a href=\"/\">Dashboard</a><div class=\"grp\">Displays</div><a href=\"/screen\">Screens</a><a href=\"/oled\">OLED</a><div class=\"grp\">Sensors</div><a href=\"/sensors\">Live sensors</a><a href=\"/custom\">Custom sensors</a><div class=\"grp\">Controls</div><a href=\"/buttons\">Buttons</a><div class=\"grp\">Logging</div><a href=\"/screen?s=Center&p=log-channels\">Logging</a><a href=\"/screen?s=Center&p=sd\">SD card</a><div class=\"grp\">Setup</div><a href=\"/setup\">Setup</a><a href=\"/firmware\">Firmware</a><div class=\"grp\">Tools</div><a href=\"/simulator\">Simulator</a><a href=\"/can\">Raw CAN</a><a href=\"/rf\">433 MHz</a></div><img src=\"data:,\" alt=\"\" style=\"display:none\" onerror=\"var h=location.pathname+location.search;document.querySelectorAll('.nav a').forEach(function(a){var t=a.getAttribute('href');if(t===h||(t===location.pathname&&t.indexOf('?')<0&&h.indexOf('?')<0))a.classList.add('on')})\">"

static const char HOME_HTML[] PROGMEM = R"HTML(<html><head><meta name="viewport" content="width=device-width, initial-scale=1"><title>MSP-DASH</title>
<style>body{background:#000;color:#ddd;font-family:sans-serif;max-width:900px;margin:auto;padding:12px}h2{color:#4dd0e1;margin:6px 0 10px}
h3{color:#4dd0e1;margin:0 0 8px;font-size:16px}.card{background:#111;border-radius:8px;padding:12px;margin:0 0 12px}.note{color:#888;font-size:13px}
.tiles{display:grid;grid-template-columns:repeat(auto-fill,minmax(150px,1fr));gap:10px;margin:0 0 12px}
.tile{background:#111;border-radius:8px;padding:10px 12px;border-left:5px solid #444;text-decoration:none;color:#ddd;display:block}
.tile b{display:block;color:#888;font-weight:normal;font-size:12px;margin-bottom:4px}.tile span{font-size:18px}
.tile.ok{border-color:#2e7d32}.tile.warn{border-color:#ffb300}.tile.bad{border-color:#e53935}
.two{display:grid;grid-template-columns:1fr 1fr;gap:12px}@media(max-width:700px){.two{grid-template-columns:1fr}}
canvas{image-rendering:pixelated;width:100%;max-width:384px;border:1px solid #333;border-radius:4px;background:#000}
.read{display:grid;grid-template-columns:repeat(auto-fill,minmax(120px,1fr));gap:8px}.read div{background:#000;border-radius:6px;padding:8px}
.read b{display:block;color:#888;font-weight:normal;font-size:12px}.read span{font-size:22px}
.scr{display:flex;flex-wrap:wrap;align-items:center;gap:6px 10px;padding:6px 0;border-top:1px solid #222}.scr:first-child{border-top:0}.scr .who{flex:1 1 180px}
.scr a{background:#1d2b2e;color:#4dd0e1;text-decoration:none;padding:5px 10px;border-radius:5px;font-size:13px}
.dot{display:inline-block;width:10px;height:10px;border-radius:50%;margin-right:6px}.dot.on{background:#2e7d32}.dot.off{background:#e53935}
)HTML" HUB_NAV_STYLE R"HTML(</style></head><body>)HTML" HUB_NAV R"HTML(
<h2>Dashboard</h2>
<div class="tiles" id="tiles"></div>
<div class="two">
<div class="card"><h3>Warning panel</h3><canvas id="cv" width="128" height="64"></canvas><div class="note" id="lit"></div></div>
<div class="card"><h3>Readings</h3><div class="read" id="read"></div></div>
</div>
<div class="card"><h3>Screens</h3><div id="scr">Waiting for screens...</div></div>
<script>
const NAME={1:"RPM",2:"Coolant Temp",3:"MAP/Boost",4:"AFR",5:"Oil Pressure",6:"Oil Temp",7:"Fuel Pressure",8:"Ethanol",9:"Throttle Position",10:"Battery Voltage",
11:"Rev Limiter",12:"Main Relay",13:"Fuel Pump",14:"Check Engine",15:"O2 Heater",16:"Lambda Protection",17:"Fan 1",18:"Fan 2",19:"Brake Pedal",
20:"EGT",21:"Intake Air Temp",22:"Ambient Temp",23:"Humidity",24:"Fuel Level",25:"Speed",26:"GPS Speed",27:"GPS Satellites",45:"Lateral G",46:"Accel/Brake G",47:"Vertical G",48:"Yaw Rate"};
const $=id=>document.getElementById(id),j=async u=>(await fetch(u)).json(),order=n=>({left:0,center:1,centre:1,middle:1,right:2})[(n||"").toLowerCase()]??3;
const tile=(k,v,cls,href)=>'<a class="tile '+cls+'" href="'+href+'"><b>'+k+'</b><span>'+v+'</span></a>';
// The center screen's logging status (screens from build 203 allow this cross-origin read). Asked every 5 s.
let logText="on Center",logCls="",logAt=0;
async function logStatus(scr){
 if(Date.now()-logAt<5000)return;logAt=Date.now();
 const c=(scr||[]).find(s=>/^cent(er|re)$/i.test(s.name||""))||(scr||[])[0];if(!c||!c.online){logText="Center offline";logCls="warn";return;}
 try{const ctl=new AbortController();setTimeout(()=>ctl.abort(),2500);const st=await (await fetch("http://"+c.ip+"/status.json",{signal:ctl.signal})).json();
  const t=st.logging||"";logText=t.length>28?t.slice(0,27)+"...":(t||"no status");logCls=/no sd/i.test(t)?"bad":/logging to|started/i.test(t)?"ok":"";}
 catch(e){logText="on Center";logCls="";}}
const KEY=[[1,"RPM",0],[3,"Boost psi",1],[2,"Coolant F",0],[5,"Oil psi",0],[6,"Oil F",0],[10,"Battery V",1],[25,"Speed mph",0],[45,"Lateral G",2]];
async function tick(){
 let st={},can={},spd={},sim={},scr=[],ol={lamps:[]},tm={},sens=[];
 try{[st,can,spd,sim,scr,ol,tm,sens]=await Promise.all(["/state","/can.json","/speed.json","/sim.json","/screens.json","/oled.json","/time.json","/sensors.json"].map(u=>j(u).catch(()=>({}))));}catch(e){}
 if(!Array.isArray(scr))scr=[];if(!Array.isArray(sens))sens=[];   // a failed request gives {}: treat as an empty list
 logStatus(scr);
 const on=(scr||[]).filter(s=>s.online).length,tot=(scr||[]).length;
 const w=(ol.lamps||[]).filter(l=>l.state===1).length,d=(ol.lamps||[]).filter(l=>l.state===2).length;
 const canOk=can.state===1&&can.total>0,ecu=st.ecu||"--";
 $("tiles").innerHTML=
  tile("ECU / CAN",(ecu==="none"||!canOk?"no data":ecu),canOk&&ecu!=="none"?"ok":"bad","/can")+
  tile("GPS",!spd.gpsok?"not connected":spd.fix?"fix, "+spd.sats+" sats":"no fix ("+(spd.sats||0)+")",!spd.gpsok?"bad":spd.fix?"ok":"warn","/setup")+
  tile("Screens",on+" of "+tot+" online",tot&&on===tot?"ok":on?"warn":"bad","/screen")+
  tile("Warnings",d?d+" danger"+(w?", "+w+" warning":""):w?w+" warning":"none",d?"bad":w?"warn":"ok","/oled")+
  tile("Simulator",sim.active?"ON, "+sim.seconds_left+" s left":"off",sim.active?"warn":"ok","/simulator")+
  tile("Clock",tm.known?tm.local.slice(11,16):"no GPS time",tm.known?"ok":"warn","/setup")+
  tile("Brightness",(st.bri!=null?st.bri:"--")+" %",st.bri>0?"ok":"warn","/setup")+
  tile("Logging",logText,logCls,"/screen?s=Center&p=log-channels");
 $("lit").textContent=(ol.lamps||[]).map((l,i)=>l.state?"spot "+(i+1)+" "+(l.state===2?"DANGER":"warning")+(l.op===3?" (input)":" ("+(NAME[l.sig]||"signal "+l.sig)+")"):"").filter(x=>x).join(" | ")||"Nothing lit";
 const by={};(sens||[]).forEach(x=>by[x.sig]=x);
 $("read").innerHTML=KEY.map(k=>{const x=by[k[0]],fresh=x&&x.age<3000;return '<div><b>'+k[1]+'</b><span>'+(fresh?x.v.toFixed(k[2]):"--")+'</span></div>';}).join("");
 (scr||[]).sort((a,b)=>order(a.name)-order(b.name));
 $("scr").innerHTML=tot?scr.map(s=>{const q="/screen?s="+encodeURIComponent(s.name||s.ip)+"&p=";return '<div class="scr"><span class="who"><span class="dot '+(s.online?"on":"off")+'"></span><b>'+(s.name||"(no name)")+'</b> <span class="note">'+s.ip+' | build '+s.build+'</span></span>'+
  '<a href="'+q+'">Live</a><a href="'+q+'gauges">Gauges</a><a href="'+q+'settings">Settings</a></div>';}).join(""):"No screens heard yet.";
}
async function pic(){try{const b=new Uint8Array(await (await fetch("/oled.bin")).arrayBuffer());const cx=$("cv").getContext("2d"),img=cx.createImageData(128,64);
 for(let y=0;y<64;y++)for(let x=0;x<128;x++){const v=b[(y>>3)*128+x]>>(y&7)&1,p=(y*128+x)*4;img.data[p]=img.data[p+1]=img.data[p+2]=v?255:0;img.data[p+3]=255;}cx.putImageData(img,0,0);}catch(e){}}
tick();pic();setInterval(tick,1000);setInterval(pic,700);
</script></body></html>)HTML";

// /setup: brightness, clock, accelerometer, I2C devices, hub sensors and the speed check, hub details.
static const char SETUP_HTML[] PROGMEM = R"HTML(<html><head><meta name="viewport" content="width=device-width, initial-scale=1"><title>MSP-DASH Setup</title>
<style>body{background:#000;color:#ddd;font-family:sans-serif;max-width:720px;margin:auto;padding:12px}h2{color:#4dd0e1;margin:6px 0 10px}
h3{color:#4dd0e1;margin:0 0 8px;font-size:16px}.card{background:#111;border-radius:8px;padding:12px;margin:0 0 12px}
label{display:block;margin:10px 0 4px}input,select{width:100%;padding:6px;box-sizing:border-box;background:#000;color:#ddd;border:1px solid #333;border-radius:4px}
button{margin-top:10px;padding:8px 12px;background:#4dd0e1;border:0;color:#000;border-radius:6px}.row{display:flex;gap:8px}.row>*{flex:1}.note{color:#999;font-size:13px}
.dot{display:inline-block;width:12px;height:12px;border-radius:50%;margin-right:8px;vertical-align:middle}.on{background:#2e7d32}.off{background:#e53935}
.scr{display:flex;flex-wrap:wrap;align-items:center;gap:6px 10px;padding:8px 0;border-top:1px solid #222}.scr:first-child{border-top:0}
.scr .who{flex:1 1 200px}.scr a.btn{background:#1d2b2e;color:#4dd0e1;text-decoration:none;padding:5px 10px;border-radius:5px;font-size:13px}
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(150px,1fr));gap:8px}.kv{background:#000;border-radius:6px;padding:8px}
.kv b{display:block;color:#888;font-weight:normal;font-size:12px;margin-bottom:2px}details summary{cursor:pointer;color:#4dd0e1}
.jump{display:flex;flex-wrap:wrap;gap:6px;margin:0 0 12px}.jump a{background:#1d2b2e;color:#4dd0e1;text-decoration:none;padding:5px 10px;border-radius:5px;font-size:13px}
.card{scroll-margin-top:56px}
)HTML" HUB_NAV_STYLE R"HTML(</style></head><body>)HTML" HUB_NAV R"HTML(
<h2>Setup</h2>
<div class="jump"><a href="#brightness">Brightness</a><a href="#clock">Clock</a><a href="#accel">Accelerometer</a><a href="#i2c">I2C bus</a><a href="#speed">Speed check</a><a href="#hub">Hub</a></div>
<div class="card" id="brightness"><details open><summary><b>Brightness</b> <span class="note" id="brisum"></span></summary>
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
<div class="card" id="clock"><details><summary><b>Clock</b> <span class="note" id="clk"></span></summary>
<p class="note">From the GPS. The screens name their log files by it and stamp each row with it.</p>
<label>Time zone<select id="tzstd"><option value="-300">Eastern</option><option value="-360">Central</option><option value="-420">Mountain</option><option value="-480">Pacific</option><option value="-540">Alaska</option><option value="-600">Hawaii</option><option value="0">UTC</option></select></label>
<label><input type="checkbox" id="tzdst" style="width:auto"> US daylight saving time</label>
<button type="button" onclick="saveTz()">Save</button> <span id="tzmsg" class="note"></span></details></div>
<div class="card" id="accel"><h3>Accelerometer</h3><div class="grid" id="imu"></div>
<details><summary class="note">Mounting and zero</summary>
<p class="note">MPU6050 on the OLED wires: VCC 3.3 V (L6), GND, SDA GPIO 5 (R15), SCL GPIO 15 (R21). Mount it solid to the chassis. Say which arrow printed on the module points to the front and which to the left, then park on level ground, engine running, and press Zero.</p>
<div class="row"><label>Points to the front<select id="ifwd"></select></label><label>Points to the left<select id="ileft"></select></label></div>
<div class="row"><button type="button" onclick="imuSet()">Save mounting</button><button type="button" onclick="imuZero()">Zero (car still and level)</button></div>
<span id="imsg" class="note"></span></details></div>
<div class="card" id="i2c"><h3>I2C bus</h3><div class="grid" id="i2c"></div>
<p class="note">Found at power-up on SDA GPIO 5 (R15) / SCL GPIO 15 (R21). Plug a device in with the hub powered off, then restart it. ADS1115 inputs read 0 to 3.3 V; an input with nothing connected drifts.</p></div>
<div class="card" id="speed"><h3>Hub sensors and speed check</h3><div class="grid" id="sens"></div>
<p class="note">Speed comes from rusEFI (hall sensor on the ECU). If it disagrees with the GPS at a steady speed, multiply rusEFI's speed calibration by the factor shown.</p></div>
<div class="card" id="hub"><h3>Hub</h3><div class="grid" id="stat"></div></div>
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
async function spd(){try{const g=await (await fetch("/speed.json")).json();
const gps=!g.gpsok?"not connected":!g.fix?"no fix ("+g.sats+" sats)":g.gpsmph.toFixed(1)+" mph, "+g.sats+" sats";
const ecu=g.ecuok?g.ecumph.toFixed(1)+" mph":"no data";
const cmp=g.ecuok&&g.fix&&g.gpsmph>10&&g.ecumph>1?((g.ecumph-g.gpsmph)/g.gpsmph*100).toFixed(1)+" % (factor "+(g.gpsmph/g.ecumph).toFixed(3)+")":"--";
$("sens").innerHTML=kv("Ambient",st.dht||"--")+kv("Fuel",(st.fuel!=null?st.fuel+" %":"--"))+kv("EGT",(st.egt||"--")+"<br><span class=note>"+(st.egtsent||0)+" frames to CAN</span>")+
kv("Speed (rusEFI)",ecu)+kv("GPS",gps)+kv("Speed difference",cmp);}catch(e){}}
async function clk(){try{const t=await (await fetch("/time.json")).json();$("clk").textContent="| "+(t.known?t.local:"no time from the GPS yet");
if(!clk.done){$("tzstd").value=String(t.std);$("tzdst").checked=t.dst;clk.done=1;}}catch(e){}}
async function saveTz(){$("tzmsg").textContent=await (await fetch("/time/set?std="+$("tzstd").value+"&dst="+($("tzdst").checked?1:0))).text();}
const AX=[[1,"+X"],[-1,"-X"],[2,"+Y"],[-2,"-Y"],[3,"+Z"],[-3,"-Z"]];
["ifwd","ileft"].forEach(id=>{const e=$(id);if(e)e.innerHTML=AX.map(a=>'<option value="'+a[0]+'">'+a[1]+'</option>').join("");});
async function imu(){try{const m=await (await fetch("/imu.json")).json();
if(!m.found){$("imu").innerHTML=kv("Status","not found");return;}
$("imu").innerHTML=kv("Lateral",m.lat.toFixed(2)+" G")+kv("Accel / brake",m.lon.toFixed(2)+" G")+kv("Vertical",m.vert.toFixed(2)+" G")+kv("Yaw rate",m.yaw.toFixed(1)+" deg/s")+
 kv("Zero",m.zeroing?"zeroing...":m.zeroed?"done":"<span style=color:#ffb300>not yet</span>");
if(!imu.done){$("ifwd").value=String(m.fwd);$("ileft").value=String(m.left);imu.done=1;}}catch(e){}}
async function imuSet(){$("imsg").textContent=await (await fetch("/imu/set?fwd="+$("ifwd").value+"&left="+$("ileft").value)).text();}
async function imuZero(){$("imsg").textContent=await (await fetch("/imu/zero")).text();}
const I2CNAME={0x3C:"OLED",0x3D:"OLED",0x68:"MPU6050",0x69:"MPU6050",0x20:"MCP23017",0x21:"MCP23017",0x22:"MCP23017",0x23:"MCP23017",0x24:"MCP23017",0x25:"MCP23017",0x26:"MCP23017",0x27:"MCP23017",0x48:"ADS1115",0x49:"ADS1115",0x4A:"ADS1115",0x4B:"ADS1115"};
async function i2c(){try{const b=await (await fetch("/i2c.json")).json();
let h=b.found.length?b.found.map(a=>kv("0x"+a.toString(16).toUpperCase().padStart(2,"0"),I2CNAME[a]||"unknown device")).join(""):kv("Bus","nothing found");
if(b.ads)h+=b.adsv.map((v,i)=>kv("ADS1115 A"+i,v.toFixed(3)+" V")).join("");
$("i2c").innerHTML=h;}catch(e){}}
poll();setInterval(poll,1000);i2c();setInterval(i2c,1000);imu();setInterval(imu,500);spd();setInterval(spd,1000);clk();setInterval(clk,1000);
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
20:"EGT",21:"Intake Air Temp",22:"Ambient Temp",23:"Humidity",24:"Fuel Level",25:"Speed",26:"GPS Speed",27:"GPS Satellites",45:"Lateral G",46:"Accel/Brake G",47:"Vertical G",48:"Yaw Rate"};
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
20:"EGT (F)",21:"Intake Air Temp (F)",22:"Ambient Temp (F)",23:"Humidity %",24:"Fuel Level %",25:"Speed (mph)",26:"GPS Speed (mph)",27:"GPS Satellites",45:"Lateral G",46:"Accel/Brake G",47:"Vertical G",48:"Yaw Rate"};
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
// /oled: the warning-light panel on the 0.96" OLED - a live copy of the display and the 8 lamps.
static const char OLED_HTML[] PROGMEM = R"HTML(<!doctype html><html><head><meta name="viewport" content="width=device-width, initial-scale=1"><title>MSP-DASH OLED</title>
<style>body{background:#000;color:#ddd;font-family:sans-serif;max-width:760px;margin:auto;padding:12px}h2{color:#4dd0e1}a{color:#4dd0e1}
.card{background:#111;border-radius:8px;padding:12px;margin:0 0 12px}.note{color:#888;font-size:13px}canvas{image-rendering:pixelated;width:100%;max-width:512px;border:1px solid #333;border-radius:4px;background:#000}
select,input{background:#000;color:#ddd;border:1px solid #333;border-radius:4px;padding:5px;max-width:100%}button{padding:7px 12px;background:#4dd0e1;color:#000;border:0;border-radius:6px}
.lamp{border-top:1px solid #222;padding:10px 0}.lamp:first-child{border-top:0}.l1,.l2{display:flex;flex-wrap:wrap;gap:8px;align-items:center;margin:4px 0}
.n{width:26px;color:#888}.st{font-size:12px;padding:2px 6px;border-radius:4px}.WARNING{background:#5a4300;color:#ffd54f}.DANGER{background:#5a0000;color:#ff6b6b}.off{color:#666}
#msg{color:#4dd0e1;min-height:1.2em}input[type=number]{width:80px}
)HTML" HUB_NAV_STYLE R"HTML(</style></head><body>)HTML" HUB_NAV R"HTML(
<h2>OLED warning panel</h2>
<div class="card"><canvas id="cv" width="128" height="64"></canvas><div class="note" id="st"></div></div>
<div class="card"><h3 style="color:#4dd0e1;margin:0 0 6px">Pages</h3><div id="pages"></div><div id="pmsg" style="color:#4dd0e1;min-height:1.2em"></div>
<p><button onclick="addPage()">Add a page</button> &nbsp; Change page every <input type="number" id="cyc" min="0" max="60" style="width:60px"> s (0 = only the button)
 &nbsp; <label><input type="checkbox" id="tko"> A lit lamp takes over the screen</label> <button onclick="pageOpts()">Save</button></p>
<p><button onclick="pagesDefaults()">Pages back to the defaults</button> <button onclick="peakReset()">Clear peak G</button></p>
<p class="note">Change pages with the "OLED next page" button action (Buttons page) or the timer. With the takeover on, any lit lamp switches to the lamps page until it clears.</p></div>
<h3 style="color:#4dd0e1">Warning lamps</h3>
<div class="card"><div id="rows"></div><div id="msg"></div>
<p>Layout <select id="layout" onchange="opts()"><option value="0">Each lamp in its own spot</option><option value="1">Lit lamps packed together</option></select>
 <button onclick="defaults()">Back to the defaults</button></p>
<p class="note">The OLED shows one colour, so the two levels look different instead: WARNING = the icon (it flashes if Blink is ticked); DANGER = a white box with the icon cut out, flashing. Spots go left to right, top row first.</p>
<p class="note">Inputs: GPIO 2 (R20), GPIO 12 (L18, must not be held HIGH at power-up), GPIO 0 (R19, the BOOT button, must not be held LOW at power-up), and up to two MCP23017 expanders (16 inputs each: board 1 at its default 0x27, board 2 set to 0x26) on the OLED's two wires (SDA GPIO 5 / R15, SCL GPIO 15 / R21, 3.3 V). Every input has a pull-up: wire a switch to ground and pick "lit when LOW".</p></div>
<script>
)HTML" OLED_ICONS_JS R"HTML(
const NAME={1:"RPM",2:"Coolant Temp",3:"MAP/Boost",4:"AFR",5:"Oil Pressure",6:"Oil Temp",7:"Fuel Pressure",8:"Ethanol",9:"Throttle Position",10:"Battery Voltage",11:"Rev Limiter",12:"Main Relay",13:"Fuel Pump",14:"Check Engine",15:"O2 Heater",16:"Lambda Protection",17:"Fan 1",18:"Fan 2",19:"Brake Pedal",20:"EGT",21:"Intake Air Temp",22:"Ambient Temp",23:"Humidity",24:"Fuel Level",25:"Speed",26:"GPS Speed",27:"GPS Satellites",45:"Lateral G",46:"Accel/Brake G",47:"Vertical G",48:"Yaw Rate"};
const $=id=>document.getElementById(id);let cfg=null;
const OPS=["Is on","Above","Below","Input wire"],STATE=["off","WARNING","DANGER"];
const INPUTS=[[1,"GPIO 2 (R20)"],[2,"GPIO 12 (L18)"],[3,"GPIO 0 (R19, BOOT)"]];
for(let e=0;e<2;e++)for(let b=0;b<16;b++)INPUTS.push([16+e*16+b,"MCP23017 #"+(e+1)+" "+(b<8?"PA":"PB")+(b%8)]);
const opt=(v,t,sel)=>'<option value="'+v+'"'+(sel?" selected":"")+'>'+t+'</option>';
function svg(i){const ic=ICONS[i];return ic&&ic[1]?'<svg viewBox="0 0 24 24" width="24" height="24" style="vertical-align:middle;fill:#4dd0e1"><path d="'+ic[1]+'"/></svg>':"";}
function rowHtml(l,i){
 const st=STATE[l.state]||"off",lvl=l.op===1||l.op===2;
 let h='<div class="lamp" id="lamp'+i+'"><div class="l1"><span class="n">'+(i+1)+'</span>'+svg(l.icon)+
  ' <select id="ic'+i+'">'+ICONS.map((c,k)=>opt(k,k?c[0]:"(unused)",k===l.icon)).join("")+'</select>'+
  ' <select id="op'+i+'" onchange="chOp('+i+',+this.value)">'+OPS.map((o,k)=>opt(k,o,k===l.op)).join("")+'</select>';
 if(l.op===3){h+=' <select id="in'+i+'">'+INPUTS.map(x=>opt(x[0],x[1]+(x[0]>=16&&!(cfg.mcp||[])[x[0]>=32?1:0]?" (not found)":""),x[0]===l.input)).join("")+'</select>'+
   ' <select id="lo'+i+'">'+opt(1,"lit when LOW",l.input_low)+opt(0,"lit when HIGH",!l.input_low)+'</select>';}
 else h+=' <select id="sg'+i+'">'+Object.keys(NAME).map(k=>opt(k,NAME[k],+k===l.sig)).join("")+'</select>';
 h+=' <span class="st '+st+'">'+st+(l.op===3?" | input "+(l.in_low?"LOW":"HIGH"):"")+'</span></div><div class="l2"><span class="n"></span>';
 if(lvl){h+='Warning '+(l.op===1?"above":"below")+' <input id="lv'+i+'" type="number" step="any" value="'+l.level+'">'+
   ' <label><input type="checkbox" id="hd'+i+'"'+(l.has_danger?" checked":"")+'> Danger '+(l.op===1?"above":"below")+'</label> <input id="dg'+i+'" type="number" step="any" value="'+l.danger+'">';}
 else h+='<label><input type="checkbox" id="hd'+i+'"'+(l.has_danger?" checked":"")+'> Lit means DANGER (not just a warning)</label>';
 h+=' <label><input type="checkbox" id="bl'+i+'"'+(l.blink?" checked":"")+'> Blink the warning</label> <button onclick="save('+i+')">Save</button></div></div>';
 return h;}
function render(){$("rows").innerHTML=cfg.lamps.map(rowHtml).join("");$("layout").value=String(cfg.layout);}
function chOp(i,op){const l=cfg.lamps[i];l.op=op;if(op===3&&!l.input){l.input=1;l.input_low=1;}const d=document.createElement("div");$("lamp"+i).outerHTML=rowHtml(l,i);}
function val(id,def){const e=$(id);return e?e.value:def;}
async function save(i){const l=cfg.lamps[i],op=+$("op"+i).value;
 const q=new URLSearchParams({i:i,icon:$("ic"+i).value,op:op,sig:op===3?0:val("sg"+i,l.sig),input:op===3?val("in"+i,1):0,input_low:op===3?val("lo"+i,1):1,
  level:val("lv"+i,l.level)||0,danger:val("dg"+i,l.danger)||0,has_danger:$("hd"+i).checked?1:0,blink:$("bl"+i).checked?1:0});
 const r=await fetch("/oled/set?"+q);$("msg").textContent=r.ok?"Spot "+(i+1)+" saved":await r.text();if(r.ok)load();}
async function opts(){$("msg").textContent=await (await fetch("/oled/opts?layout="+$("layout").value)).text();}
async function defaults(){$("msg").textContent=await (await fetch("/oled/defaults")).text();load();}
async function load(){try{cfg=await (await fetch("/oled.json")).json();render();}catch(e){}}
async function live(){try{const b=new Uint8Array(await (await fetch("/oled.bin")).arrayBuffer());const cx=$("cv").getContext("2d"),img=cx.createImageData(128,64);
 for(let y=0;y<64;y++)for(let x=0;x<128;x++){const on=b[(y>>3)*128+x]>>(y&7)&1,p=(y*128+x)*4;img.data[p]=img.data[p+1]=img.data[p+2]=on?255:0;img.data[p+3]=255;}
 cx.putImageData(img,0,0);const j=await (await fetch("/oled.json")).json();
 $("st").textContent=(j.found?"Display found":"Display NOT found: check VCC 3.3 V, GND, SDA GPIO 5 (R15), SCL GPIO 15 (R21)")+" | expanders: "+((j.mcp||[]).filter(a=>a).map((a,e)=>"#"+(e+1)+" at 0x"+a.toString(16).toUpperCase()).join(", ")||"none")+
  " | lit: "+(j.lamps.map((l,i)=>l.state?(i+1)+(l.state===2?" DANGER":" warning"):0).filter(x=>x).join(", ")||"none");}catch(e){}}
const PTYPES=["Warning lamps","Big number","Values (1-4)","G-meter","Clock"];let pg=null;
function pageHtml(p,i){
 let h='<div class="lamp"><div class="l1"><span class="n">'+(i+1)+'</span><select id="pt'+i+'" onchange="pg.pages['+i+'].type=+this.value;renderPages()">'+PTYPES.map((t,k)=>opt(k,t,k===p.type)).join("")+'</select>';
 if(p.type===2)h+=' <select id="pc'+i+'" onchange="pg.pages['+i+'].count=+this.value;renderPages()">'+[1,2,3,4].map(k=>opt(k,k+" value"+(k>1?"s":""),k===p.count)).join("")+'</select>';
 h+=' <button onclick="showPage('+i+')">Show</button> <button onclick="movePage('+i+',-1)"'+(i?"":" disabled")+'>&uarr;</button> <button onclick="movePage('+i+',1)"'+(i<pg.count-1?"":" disabled")+'>&darr;</button>'+
  ' <button onclick="delPage('+i+')">Remove</button> <button onclick="savePage('+i+')">Save</button>'+(pg.shown===i?' <span class="st WARNING">ON SCREEN</span>':'')+'</div>';
 const n=p.type===1?1:p.type===2?p.count:0;
 for(let k=0;k<n;k++)h+='<div class="l2"><span class="n"></span><select id="ps'+i+'_'+k+'" onchange="autoLabel('+i+','+k+')">'+Object.keys(NAME).map(s=>opt(s,NAME[s],+s===p.sig[k])).join("")+'</select>'+
  ' label <input id="pl'+i+'_'+k+'" maxlength="9" style="width:100px" value="'+(p.label[k]||"").replace(/"/g,"")+'">'+
  ' <select id="pd'+i+'_'+k+'">'+[0,1,2].map(d=>opt(d,d+" decimals",d===p.dec[k])).join("")+'</select></div>';
 return h+'</div>';}
function renderPages(){$("pages").innerHTML=pg.pages.map(pageHtml).join("");$("cyc").value=pg.cycle;$("tko").checked=pg.takeover;}
function autoLabel(i,k){const s=+$("ps"+i+"_"+k).value;$("pl"+i+"_"+k).value=(NAME[s]||"").toUpperCase().slice(0,9);}
async function loadPages(){try{pg=await (await fetch("/oled/pages.json")).json();renderPages();}catch(e){}}
async function pcall(u){const r=await fetch(u);$("pmsg").textContent=await r.text();loadPages();return r.ok;}
function savePage(i){const p=pg.pages[i],q={i:i,type:$("pt"+i).value,count:$("pc"+i)?$("pc"+i).value:p.count||1};
 for(let k=0;k<4;k++){const s=$("ps"+i+"_"+k);q["s"+k]=s?s.value:p.sig[k];q["l"+k]=$("pl"+i+"_"+k)?$("pl"+i+"_"+k).value:p.label[k];q["d"+k]=$("pd"+i+"_"+k)?$("pd"+i+"_"+k).value:p.dec[k];}
 return pcall("/oled/page/set?"+new URLSearchParams(q));}
const addPage=()=>pcall("/oled/page/add"),delPage=i=>pcall("/oled/page/del?i="+i),movePage=(i,d)=>pcall("/oled/page/move?i="+i+"&d="+d),showPage=i=>pcall("/oled/page/show?i="+i);
const pageOpts=()=>pcall("/oled/pageopts?cycle="+$("cyc").value+"&takeover="+($("tko").checked?1:0)),pagesDefaults=()=>pcall("/oled/pages/defaults"),peakReset=()=>pcall("/oled/peak/reset");
load();loadPages();live();setInterval(live,700);
</script></body></html>)HTML";
// /screen: one screen's own pages (live view, gauge editor, logging, SD card, settings) inside the hub's site, with a
// Left / Center / Right picker, so no screen address has to be typed. ?s=<screen name>&p=<page>.
static const char SCREEN_HTML[] PROGMEM = R"HTML(<!doctype html><html><head><meta name="viewport" content="width=device-width, initial-scale=1"><title>MSP-DASH Screens</title>
<style>body{background:#000;color:#ddd;font-family:sans-serif;margin:0;padding:12px}h2{color:#4dd0e1;margin:4px 0 10px}
.bar{display:flex;flex-wrap:wrap;gap:6px;align-items:center;margin:0 0 8px}.bar button{background:#1d2b2e;color:#4dd0e1;border:1px solid #1d3b40;border-radius:6px;padding:7px 12px;font-size:14px}
.bar button.on{background:#4dd0e1;color:#000;font-weight:bold}.bar button:disabled{opacity:.4}.sep{width:12px}
iframe{width:100%;height:calc(100vh - 130px);border:1px solid #222;border-radius:8px;background:#000}.note{color:#888;font-size:13px}
)HTML" HUB_NAV_STYLE R"HTML(</style></head><body>)HTML" HUB_NAV R"HTML(
<h2>Screens</h2>
<div class="bar" id="who"></div>
<div class="bar" id="tabs"></div>
<div class="note" id="where"></div>
<iframe id="fr" src="about:blank"></iframe>
<script>
const TABS=[["","Live"],["gauges","Gauges"],["log-channels","Logging"],["sd","SD card"],["settings","Settings"]];
const q=new URLSearchParams(location.search);let cur=q.get("s")||"",page=q.get("p")||"gauges",list=[];
const $=id=>document.getElementById(id),order=n=>({left:0,center:1,centre:1,middle:1,right:2})[(n||"").toLowerCase()]??3;
function show(){const s=list.find(x=>(x.name||x.ip)===cur)||list[0];if(!s){$("where").textContent="No screens heard yet.";return;}
 cur=s.name||s.ip;history.replaceState(null,"","/screen?s="+encodeURIComponent(cur)+"&p="+page);
 $("who").innerHTML=list.map(x=>'<button class="'+((x.name||x.ip)===cur?"on":"")+'" onclick="pick(\''+(x.name||x.ip).replace(/'/g,"")+'\')">'+(x.name||x.ip)+(x.online?"":" (offline)")+'</button>').join("");
 $("tabs").innerHTML=TABS.map(t=>'<button class="'+(t[0]===page?"on":"")+'" onclick="tab(\''+t[0]+'\')">'+t[1]+'</button>').join("");
 const u="http://"+s.ip+"/"+page;$("where").innerHTML=(s.online?"":"<b style=color:#ff6b6b>Offline</b> - ")+s.ip+" | build "+s.build+' | <a style="color:#4dd0e1" href="'+u+'" target="_blank">open on its own</a>';
 if($("fr").src!==u)$("fr").src=u;}
function pick(n){cur=n;show();}function tab(p){page=p;show();}
async function load(){try{list=await (await fetch("/screens.json")).json();list.sort((a,b)=>order(a.name)-order(b.name)||(a.ip<b.ip?-1:1));show();}catch(e){$("where").textContent="Could not reach the hub";}}
load();
</script></body></html>)HTML";

// /can: every CAN frame the hub hears, live.
static const char CAN_HTML[] PROGMEM = R"HTML(<!doctype html><html><head><meta name="viewport" content="width=device-width, initial-scale=1"><title>MSP-DASH Raw CAN</title>
<style>body{background:#000;color:#ddd;font-family:sans-serif;max-width:900px;padding:12px}h2{color:#4dd0e1}td,th{padding:3px 10px;font-family:monospace}th{text-align:left;color:#4dd0e1}.old{color:#666}
)HTML" HUB_NAV_STYLE R"HTML(</style></head><body>)HTML" HUB_NAV R"HTML(
<h2>Raw CAN</h2><div id="s"></div><table><thead><tr><th>ID</th><th>Bytes</th><th>Count</th><th>Age ms</th></tr></thead><tbody id="t"></tbody></table>
<script>
const ST=['stopped','running','bus-off','recovering'];
async function u(){try{const r=await fetch('/can.json');const j=await r.json();
document.getElementById('s').textContent='Frames: '+j.total+' | bus: '+(ST[j.state]||'?')+' | rx errors: '+j.rxerr+' | missed: '+j.missed;
j.ids.sort((a,b)=>a.id-b.id);document.getElementById('t').innerHTML=j.ids.map(e=>'<tr class='+(e.age>2000?'old':'')+'><td>0x'+e.id.toString(16).toUpperCase().padStart(3,'0')+'</td><td>'+e.d+'</td><td>'+e.n+'</td><td>'+e.age+'</td></tr>').join('');}catch(x){}}
setInterval(u,500);u();
</script></body></html>)HTML";

// /firmware: every board's build in one table; the hub's own update form; each screen's update page.
static const char FIRMWARE_HTML[] PROGMEM = R"HTML(<!doctype html><html><head><meta name="viewport" content="width=device-width, initial-scale=1"><title>MSP-DASH Firmware</title>
<style>body{background:#000;color:#ddd;font-family:sans-serif;max-width:760px;padding:12px}h2{color:#4dd0e1}h3{color:#4dd0e1;margin:0 0 8px;font-size:16px}.card{background:#111;border-radius:8px;padding:12px;margin:0 0 12px}
table{width:100%;border-collapse:collapse}td,th{padding:6px;border-bottom:1px solid #222;text-align:left}th{color:#4dd0e1;font-weight:normal}a{color:#4dd0e1}.note{color:#888;font-size:13px}
input,button{background:#000;color:#ddd;border:1px solid #333;border-radius:4px;padding:6px}button{background:#4dd0e1;color:#000;border:0}button:disabled{opacity:.4}
progress{width:100%;height:14px}
)HTML" HUB_NAV_STYLE R"HTML(</style></head><body>)HTML" HUB_NAV R"HTML(
<h2>Firmware</h2>
<div class="card"><table><thead><tr><th></th><th>Board</th><th>Address</th><th>Build</th><th>Status</th><th>Update</th></tr></thead><tbody id="b"></tbody></table></div>
<div class="card"><h3>Update the screens</h3>
<p class="note">Pick the screens' .bin once (C:\esp32build\esp32s3-amoled-bsp-base.ino.bin); it goes to every ticked screen, one after another. Each restarts and comes back with its new build.</p>
<input type="file" id="sbin" accept=".bin"> <button id="sgo" onclick="updScreens()">Update ticked screens</button></div>
<div class="card"><h3>Update the hub</h3>
<p class="note">C:\esp32build-input\esp32-input.ino.bin. The hub restarts at the end; this page comes back by itself.</p>
<input type="file" id="hbin" accept=".bin"> <button id="hgo" onclick="updHub()">Upload</button><progress id="hp" max="100" value="0" style="display:none"></progress></div>
<p class="note">The PowerShell flash commands still work and also build the firmware. Keep the boards powered during an update.</p>
<script>
const $=id=>document.getElementById(id),order=n=>({left:0,center:1,centre:1,middle:1,right:2})[(n||"").toLowerCase()]??3;
let st={},scr=[],stat={},busy=false;
function render(){
 let h='<tr><td></td><td><b>Hub</b></td><td>'+(st.ip||"")+'</td><td>'+(st.build||"")+'</td><td><span class="chip ok">online</span></td><td>'+(stat.hub||"")+'</td></tr>';
 h+=scr.map(s=>'<tr><td><input type="checkbox" id="t_'+s.ip+'"'+(s.online?" checked":" disabled")+'></td><td><b>'+(s.name||"(no name)")+'</b></td><td>'+s.ip+'</td><td>'+s.build+'</td><td><span class="chip '+(s.online?"ok":"bad")+'">'+(s.online?"online":"offline")+'</span></td><td>'+(stat[s.ip]||'<a href="http://'+s.ip+'/update" target="_blank">update page</a>')+'</td></tr>').join("");
 const keep={};scr.forEach(s=>{const e=$("t_"+s.ip);if(e)keep[s.ip]=e.checked;});
 $("b").innerHTML=h;scr.forEach(s=>{const e=$("t_"+s.ip);if(e&&s.ip in keep&&s.online)e.checked=keep[s.ip];});}
async function load(){try{st=await (await fetch("/state")).json();scr=await (await fetch("/screens.json")).json();scr.sort((a,b)=>order(a.name)-order(b.name));render();}catch(e){}}
const sleep=ms=>new Promise(r=>setTimeout(r,ms));
async function updScreens(){
 const f=$("sbin").files[0];if(!f){alert("Pick the screens' .bin first");return;}
 const targets=scr.filter(s=>s.online&&$("t_"+s.ip)&&$("t_"+s.ip).checked);if(!targets.length){alert("Tick at least one online screen");return;}
 if(!confirm("Update "+targets.map(s=>s.name||s.ip).join(", ")+" with "+f.name+"?"))return;
 busy=true;$("sgo").disabled=true;
 for(const s of targets){
  stat[s.ip]="uploading...";render();
  const before=s.build,upBefore=s.up||0,t0=Date.now();const fd=new FormData();fd.append("firmware",f,f.name);
  // The screen restarts as soon as it has the file, often before it answers, so a dropped connection is normal here.
  // Success is seen afterwards: its uptime starts again from 0 (screens from build 195 send it), or for older screens
  // it goes quiet and comes back.
  try{await fetch("http://"+s.ip+"/update",{method:"POST",mode:"no-cors",body:fd});}catch(e){}
  stat[s.ip]="sent, waiting for the restart...";render();
  let back=false,wentQuiet=false;
  for(let k=0;k<45&&!back;k++){await sleep(2000);await load();const n=scr.find(x=>x.ip===s.ip);if(!n)continue;
   if(!n.online)wentQuiet=true;
   const restarted=(upBefore&&n.up&&n.up<upBefore+(Date.now()-t0)/1000-5)||(wentQuiet&&n.online)||n.build!==before;
   if(n.online&&restarted){back=true;stat[s.ip]='<span class="chip ok">done, build '+n.build+(n.build===before?' (same build)':'')+'</span>';}}
  if(!back)stat[s.ip]='<span class="chip warn">no restart seen: try the curl command</span>';render();
 }
 busy=false;$("sgo").disabled=false;}
function updHub(){
 const f=$("hbin").files[0];if(!f){alert("Pick the hub's .bin first");return;}
 if(!confirm("Update the hub with "+f.name+"?"))return;
 const fd=new FormData();fd.append("firmware",f,f.name);const x=new XMLHttpRequest();$("hp").style.display="block";$("hgo").disabled=true;
 x.upload.onprogress=e=>{if(e.lengthComputable){$("hp").value=Math.round(e.loaded*100/e.total);stat.hub="uploading "+$("hp").value+" %";render();}};
 x.onload=async()=>{stat.hub=x.responseText==="OK"?"restarting...":'<span class="chip bad">'+x.responseText+'</span>';render();
  if(x.responseText!=="OK"){$("hgo").disabled=false;return;}
  const before=st.build;for(let k=0;k<40;k++){await sleep(2000);try{const n=await (await fetch("/state")).json();st=n;stat.hub='<span class="chip ok">done, build '+n.build+'</span>';break;}catch(e){}}
  $("hgo").disabled=false;$("hp").style.display="none";load();};
 x.onerror=()=>{stat.hub='<span class="chip bad">upload failed</span>';render();$("hgo").disabled=false;};
 x.open("POST","/update");x.send(fd);}
load();setInterval(()=>{if(!busy)load();},3000);
</script></body></html>)HTML";
