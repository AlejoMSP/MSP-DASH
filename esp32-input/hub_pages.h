// hub_pages.h - the hub's web pages.
// They live in this header, NOT in the .ino: the Arduino builder scans the .ino to add function prototypes and
// can mangle long raw strings full of JavaScript ("'asyncfunction' does not name a type"). Headers are left alone.
#pragma once
#include <Arduino.h>

static const char HOME_HTML[] PROGMEM = R"HTML(<html><head><meta name="viewport" content="width=device-width, initial-scale=1"><title>MSP-DASH Hub</title>
<style>body{background:#000;color:#ddd;font-family:sans-serif;max-width:480px;margin:auto;padding:12px}h2{color:#4dd0e1}
label{display:block;margin:10px 0 4px}input,select{width:100%;padding:6px;box-sizing:border-box;background:#111;color:#ddd;border:1px solid #333}
button{margin-top:10px;padding:8px 12px;background:#4dd0e1;border:0;color:#000}.live{background:#111;padding:10px;border-radius:6px}
.row{display:flex;gap:8px}.row>*{flex:1}.note{color:#999;font-size:13px}
.dot{display:inline-block;width:12px;height:12px;border-radius:50%;margin-right:8px;vertical-align:middle}.on{background:#2e7d32}.off{background:#e53935}
.scr{padding:4px 0}.scr a{color:#4dd0e1}</style></head><body>
<h2>MSP-DASH Hub</h2>
<h3 style="color:#4dd0e1;margin:8px 0">Screens</h3><div class="live" id="scr">Waiting for screens...</div>
<h3 style="color:#4dd0e1;margin:8px 0">Hub</h3><div class="live" id="live">Loading...</div>
<h3 style="color:#4dd0e1;margin:8px 0">Speed</h3><div class="live" id="spd">Loading...</div>
<p class="note">Speed comes from rusEFI (the hall sensor is wired to the ECU). If it disagrees with the GPS at a steady speed, multiply rusEFI's speed calibration by the factor shown.</p>
<label>Brightness mode<select id="mode"><option value="0">Auto: car lights and dimmer</option><option value="1">Knob</option></select></label>
<label>Day brightness, lights off (%)<input type="number" id="day" min="5" max="100"></label>
<div class="row"><label>Night, dimmer lowest (%)<input type="number" id="nmin" min="5" max="100"></label>
<label>Night, dimmer highest (%)<input type="number" id="nmax" min="5" max="100"></label></div>
<div class="row"><label>Dimmer lowest (V)<input type="number" id="dlo" step="0.1"></label>
<label>Dimmer highest (V)<input type="number" id="dhi" step="0.1"></label></div>
<div class="row"><button type="button" onclick="grab('dlo')">Use now as lowest</button><button type="button" onclick="grab('dhi')">Use now as highest</button></div>
<label>Lights count as on above (V)<input type="number" id="lon" step="0.1"></label>
<button type="button" onclick="save()">Save</button> <span id="msg" class="note"></span>
<p class="note">Turn the lights on, roll the dimmer all the way down and press "Use now as lowest", all the way up and press "Use now as highest", then Save.</p>
<p><a href="/update" style="color:#4dd0e1">Firmware update</a></p>
<script>
let st={};const ids=["mode","day","nmin","nmax","dlo","dhi","lon"];
function grab(id){document.getElementById(id).value=st.dimV.toFixed(1);}
async function poll(){try{st=await (await fetch("/state")).json();
document.getElementById("live").innerHTML="Lights: "+st.lightsV.toFixed(1)+" V ("+(st.lightsOn?"ON":"off")+")<br>Dimmer: "+st.dimV.toFixed(1)+" V<br>Brightness sent: "+st.bri+" %<br>Build "+st.build+" | "+st.ip+" | channel "+st.ch+"<br>Last command: "+st.last+"<br>ECU: "+st.ecu+" (detected from CAN)<br>Signals live: "+st.live+" | data packets sent: "+st.sent+" | <a href=/sensors>Sensors</a> | <a href=/can>Raw CAN</a> | <a href=/simulator>Simulator</a> | <a href=/custom>Custom Sensors</a> | <a href=/rf>433 MHz</a><br>Simulator: "+st.sim+"<br>Ambient: "+st.dht+"<br>Fuel: "+st.fuel+" %<br>EGT: "+st.egt+" | sent to CAN: "+st.egtsent;
if(!poll.done){ids.forEach(i=>document.getElementById(i).value=st.cfg[i]);poll.done=1;}}catch(e){}}
async function save(){const q=ids.map(i=>i+"="+encodeURIComponent(document.getElementById(i).value)).join("&");
document.getElementById("msg").textContent=await (await fetch("/save?"+q)).text();}
function ago(ms){return ms<60000?Math.round(ms/1000)+" s ago":Math.round(ms/60000)+" min ago";}
async function scr(){try{const l=await (await fetch("/screens.json")).json();
l.sort((a,b)=>a.ip<b.ip?-1:1);
document.getElementById("scr").innerHTML=l.length?l.map(s=>'<div class="scr"><span class="dot '+(s.online?'on':'off')+'"></span><b>'+(s.name||'(no name)')+'</b> '+
(s.online?'online':'OFFLINE, last heard '+ago(s.age))+'<br><span class="note">'+'<a href="http://'+s.ip+'/">'+s.ip+'</a> | build '+s.build+' | '+s.mac+'</span></div>').join(''):
'No screens heard yet. Screens set to main do not say hello.';}catch(e){}}
async function spd(){try{const g=await (await fetch("/speed.json")).json();
const gps=!g.gpsok?"not connected (no data on R17 / GPIO 16)":!g.fix?"no fix yet ("+g.sats+" satellites)":g.gpsmph.toFixed(1)+" mph, "+g.sats+" satellites";
const ecu=g.ecuok?g.ecumph.toFixed(1)+" mph":"no data from rusEFI";
const cmp=g.ecuok&&g.fix&&g.gpsmph>10&&g.ecumph>1?" | difference "+((g.ecumph-g.gpsmph)/g.gpsmph*100).toFixed(1)+" % | factor "+(g.gpsmph/g.ecumph).toFixed(3):"";
document.getElementById("spd").innerHTML="rusEFI: "+ecu+"<br>GPS: "+gps+cmp;}catch(e){}}
poll();setInterval(poll,1000);scr();setInterval(scr,1000);spd();setInterval(spd,500);
</script></body></html>)HTML";

static const char SENSORS_HTML[] PROGMEM = R"HTML(<!doctype html><html><head><meta name="viewport" content="width=device-width, initial-scale=1"><title>MSP-DASH Sensors</title>
<style>body{background:#000;color:#ddd;font-family:sans-serif;max-width:640px;margin:auto;padding:12px}h2{color:#4dd0e1}a{color:#4dd0e1}
table{width:100%;border-collapse:collapse}td,th{padding:5px 8px;text-align:left;border-bottom:1px solid #222}th{color:#999;font-weight:normal}
.v{font-family:monospace;text-align:right}.tag{padding:1px 6px;border-radius:4px;font-size:12px}.hub{background:#1b5e20}.sim{background:#e65100}.can{background:#0d47a1}
.stale td{color:#666}.note{color:#999;font-size:13px}</style></head><body>
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
.sl{margin:10px 0}.sl label{display:flex;justify-content:space-between;font-size:.85em;color:#4dd0e1}.sl input{width:100%}</style></head><body>
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
button.del{background:#c62828;color:#fff}#preview{font-family:monospace;font-size:18px;color:#4dd0e1;margin-top:8px}#msg{min-height:1.2em;color:#4dd0e1}#msg.bad{color:#ff6b6b}</style></head><body>
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
#bar{height:8px;background:#222;border-radius:4px;overflow:hidden;margin-top:6px}#barin{height:100%;background:#4dd0e1;width:0}</style></head><body>
<h2>433 MHz listen</h2>
<div class="box" id="st">Loading...</div>
<div class="box">Mode <select id="mod"><option value="fsk">2-FSK (most tire sensors)</option><option value="ook">OOK / ASK (remotes, some sensors)</option></select>
 Threshold <input id="thr" type="number" min="-110" max="-30" style="width:70px"> dBm <button onclick="save()">Save</button> <span id="msg" class="note"></span>
<div class="note">Bursts stronger than the threshold are captured. Set it a few dB above the noise shown below. Tire sensors send when the wheel turns, when the pressure changes, or every few minutes when parked; a TPMS tool or letting a little air out makes them send now.</div></div>
<div id="list"></div>
<p><a href="/">Back</a></p>
<script>
const $=id=>document.getElementById(id);let loaded=false;
async function save(){const q=new URLSearchParams({mod:$("mod").value,thr:$("thr").value});const r=await fetch("/rf/set?"+q);$("msg").textContent=await r.text();}
function age(ms){return ms<60000?(ms/1000).toFixed(0)+" s ago":(ms/60000).toFixed(1)+" min ago";}
async function poll(){try{const j=await (await fetch("/rf.json")).json();
if(!j.ok){$("st").innerHTML='<span class="bad">CC1101 not found</span> (read part 0x'+j.part.toString(16)+', version 0x'+j.version.toString(16)+'; expected 00 / 14). Check VCC on 3.3 V, GND, and SCK/MOSI/MISO/CSN.';return;}
const pct=Math.max(0,Math.min(100,(j.rssi+110)*100/80));
$("st").innerHTML="CC1101 found (version 0x"+j.version.toString(16)+") | listening on 433.92 MHz, "+(j.ook?"OOK":"2-FSK")+" | signal now "+j.rssi+" dBm | bursts heard: "+j.total+'<div id="bar"><div id="barin" style="width:'+pct+'%"></div></div>';
if(!loaded){$("mod").value=j.ook?"ook":"fsk";$("thr").value=j.thr;loaded=true;}
$("list").innerHTML=j.bursts.length?j.bursts.map(b=>'<div class="b"><b>'+age(b.age)+'</b> | '+b.rssi+' dBm | '+b.pulses+' pulses | half-bit '+b.unit+' us (about '+(b.unit?Math.round(500000/b.unit):0)+' bit/s) | '+b.nbits+' Manchester bits'+(b.inv?' (inverted)':'')+
 '<div class="hex mono">'+(b.hex||'(no Manchester bytes)')+'</div><div class="mono note">bits: '+b.bits+'</div><div class="mono note">pulses (us, first level '+b.first+'): '+b.w.join(" ")+'</div></div>').join(""):'<p class="note">Nothing heard yet above '+j.thr+' dBm.</p>';
}catch(e){$("st").textContent="Could not reach the hub - retrying...";}}
poll();setInterval(poll,1000);
</script></body></html>)HTML";
