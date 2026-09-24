// The whole web app lives in this one string, baked into flash.
// It talks to the board through the JSON API in main.cpp:
//   GET  /api/state                            full state
//   POST /api/set?target=…&on=&color=&effect=&speed=&brightness=&flip=&fade=
//   POST /api/scene?i=                         apply a scene now
//   POST /api/auto?on=                         follow the schedule or not
//   POST /api/schedule?i=&h=&m=&scene=&on=     edit one schedule slot
//   POST /api/bounds?i=&led=                   move a segment boundary
//   POST /api/locate?seg=  |  ?bound=          flash something on the strip
// target is "all", "sec:<Section>" or "seg:<id>".
//
// UI shape: the things you touch daily are on top and always visible (auto
// toggle, brightness, scene tiles). Schedule, per-area tuning and the physical
// layout are collapsed below, because you set them once.
#pragma once
#include <pgmspace.h>

const char INDEX_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<meta name="theme-color" content="#0d0d0f">
<title>Desk Lights</title>
<style>
  :root{
    color-scheme:dark;
    --bg:#0d0d0f; --card:#17171a; --card2:#1f1f24; --line:#2c2c33;
    --fg:#f0f0f2; --mute:#8b8b94; --acc:#4b8cf7; --warn:#f7a84b;
    font-family:-apple-system,BlinkMacSystemFont,system-ui,sans-serif;
  }
  *{box-sizing:border-box;-webkit-tap-highlight-color:transparent}
  body{margin:0;background:var(--bg);color:var(--fg);display:flex;justify-content:center;
       padding-bottom:env(safe-area-inset-bottom)}
  main{width:min(520px,100%);padding:20px 16px 48px}

  header{display:flex;align-items:baseline;justify-content:space-between;margin-bottom:18px}
  h1{font-size:1.35rem;margin:0;letter-spacing:-.01em}
  #clock{font-variant-numeric:tabular-nums;color:var(--mute);font-size:.95rem}

  .card{background:var(--card);border:1px solid var(--line);border-radius:16px;padding:16px;margin-bottom:14px}
  .lbl{font-size:.7rem;color:var(--mute);text-transform:uppercase;letter-spacing:.08em;margin-bottom:10px}

  /* auto / schedule banner */
  #autoCard{display:flex;align-items:center;gap:14px}
  #autoTxt{flex:1;min-width:0}
  #autoTitle{font-size:.95rem;font-weight:600}
  #autoSub{font-size:.78rem;color:var(--mute);margin-top:3px}
  .switch{width:52px;height:31px;border-radius:999px;background:#3a3a42;position:relative;
          flex:none;cursor:pointer;transition:background .2s;border:none;padding:0}
  .switch.on{background:var(--acc)}
  .switch::after{content:"";position:absolute;top:3px;left:3px;width:25px;height:25px;border-radius:50%;
                 background:#fff;transition:transform .2s}
  .switch.on::after{transform:translateX(21px)}

  /* brightness */
  input[type=range]{width:100%;accent-color:var(--acc);height:28px}
  .sliderRow{display:flex;align-items:center;gap:12px}
  #briVal{font-variant-numeric:tabular-nums;color:var(--mute);font-size:.8rem;width:38px;text-align:right}

  /* scenes */
  #scenes{display:grid;grid-template-columns:1fr 1fr;gap:10px}
  .scene{display:flex;align-items:center;gap:11px;padding:12px 12px;border-radius:13px;
         background:var(--card2);border:1.5px solid transparent;cursor:pointer;text-align:left;
         color:var(--fg);font-family:inherit;transition:border-color .15s,background .15s}
  .scene:active{background:#26262c}
  .scene.on{border-color:var(--acc);background:#1b2540}
  .swatch{width:17px;height:17px;border-radius:50%;flex:none;box-shadow:0 0 12px -1px currentColor}
  .scene b{font-size:.92rem;font-weight:600;display:block;line-height:1.25}
  .scene span{font-size:.68rem;color:var(--mute);display:block;line-height:1.3;margin-top:2px}

  /* collapsibles */
  details{background:var(--card);border:1px solid var(--line);border-radius:16px;margin-bottom:14px;overflow:hidden}
  summary{padding:15px 16px;cursor:pointer;font-size:.9rem;font-weight:500;list-style:none;
          display:flex;align-items:center;justify-content:space-between}
  summary::-webkit-details-marker{display:none}
  summary::after{content:"›";color:var(--mute);font-size:1.3rem;transform:rotate(90deg);transition:transform .2s}
  details[open] summary::after{transform:rotate(270deg)}
  .body{padding:0 16px 16px}
  .note{font-size:.73rem;color:var(--mute);line-height:1.5;margin:10px 0 0}

  /* schedule */
  .srow{display:grid;grid-template-columns:auto 1fr auto;gap:9px;align-items:center;
        padding:9px 0;border-top:1px solid var(--line)}
  .srow:first-child{border-top:none}
  .srow.dim{opacity:.4}
  input[type=time],select,input[type=number]{background:var(--card2);color:var(--fg);border:1px solid var(--line);
        border-radius:9px;padding:9px;font-size:.88rem;font-family:inherit}
  input[type=time]{font-variant-numeric:tabular-nums}
  select{width:100%}
  .tog{width:40px;height:24px;border-radius:999px;background:#3a3a42;border:none;position:relative;cursor:pointer;flex:none;padding:0}
  .tog.on{background:var(--acc)}
  .tog::after{content:"";position:absolute;top:2px;left:2px;width:20px;height:20px;border-radius:50%;background:#fff;transition:transform .2s}
  .tog.on::after{transform:translateX(16px)}

  /* fine tune */
  .row{display:flex;gap:7px;flex-wrap:wrap}
  .chip{display:flex;align-items:center;gap:7px;padding:9px 13px;border:1px solid var(--line);
        border-radius:999px;background:var(--card2);color:var(--fg);font-size:.85rem;
        font-family:inherit;cursor:pointer}
  .chip.on{background:var(--acc);border-color:var(--acc)}
  .dot{width:9px;height:9px;border-radius:50%;background:#555;flex:none}
  .dot.off{opacity:.25}
  #map{display:flex;height:16px;border-radius:8px;overflow:hidden;gap:2px;margin:4px 0 12px}
  #map div{cursor:pointer;min-width:2px;opacity:.7}
  #map div.sel{opacity:1;outline:2px solid #fff;outline-offset:-2px}
  .rainbow{background:linear-gradient(90deg,red,yellow,lime,cyan,blue,magenta,red)}
  input[type=color]{width:100%;height:46px;border:0;background:none;padding:0;cursor:pointer}
  .wide{flex:1;border-radius:11px;justify-content:center}
  h4{font-size:.7rem;color:var(--mute);text-transform:uppercase;letter-spacing:.08em;
     margin:18px 0 9px;font-weight:600}
  h4:first-child{margin-top:0}

  /* layout */
  .bound{display:grid;grid-template-columns:1fr auto auto auto auto;gap:6px;align-items:center;
         padding:9px 0;border-top:1px solid var(--line);font-size:.85rem}
  .bound input{width:58px;text-align:center}
  .bound button{padding:8px 11px;border:1px solid var(--line);border-radius:9px;background:var(--card2);
                color:var(--fg);font-size:.85rem;cursor:pointer;font-family:inherit}
  #status{font-size:.68rem;color:#5a5a62;text-align:center;margin-top:20px;line-height:1.6}
</style></head><body><main>

<header><h1>Desk Lights</h1><span id="clock">--:--</span></header>

<div class="card" id="autoCard">
  <div id="autoTxt">
    <div id="autoTitle">Auto</div>
    <div id="autoSub">…</div>
  </div>
  <button class="switch" id="autoSw" aria-label="Toggle automatic schedule"></button>
</div>

<div class="card">
  <div class="lbl">Brightness</div>
  <div class="sliderRow"><input id="bri" type="range" min="0" max="255"><span id="briVal"></span></div>
</div>

<div class="card">
  <div class="lbl">Scenes</div>
  <div id="scenes"></div>
</div>

<details id="dSched"><summary>Daily schedule</summary><div class="body">
  <div id="sched"></div>
  <h4>Crossfade</h4>
  <div class="sliderRow"><input id="fade" type="range" min="0" max="60"><span id="fadeVal" class="note"></span></div>
  <p class="note">Scheduled changes ease in over this long, so the room shifts without you noticing a switch flip. Tapping a scene by hand is always immediate.</p>
</div></details>

<details id="dTune"><summary>Fine tune an area</summary><div class="body">
  <h4>Strip map — tap a run</h4>
  <div id="map"></div>
  <div class="row" id="sections"></div>
  <div class="row" id="sides" style="margin-top:7px"></div>
  <h4>Controlling: <span id="target"></span></h4>
  <div class="row"><button class="chip wide" id="power"></button></div>
  <h4>Color</h4>
  <input id="col" type="color">
  <h4>Effect</h4>
  <div class="row" id="effects"></div>
  <h4>Speed</h4>
  <input id="spd" type="range" min="0" max="255">
</div></details>

<details id="dLayout"><summary>Physical layout</summary><div class="body">
  <div class="row"><button class="chip wide" id="flip"></button></div>
  <p class="note">If the first run (Pegboard bottom) lights up at the wrong end of the strip, flip it.</p>
  <h4>Where each run starts</h4>
  <div id="bounds"></div>
  <p class="note">Tap <b>show</b> and the strip lights the two runs meeting at that boundary: blue before, orange after. Nudge until the split lands on the corner.</p>
</div></details>

<div id="status"></div>
</main>
<script>
const $=s=>document.querySelector(s);
const EFFECTS=["solid","rainbow","breathe","chase"];
let st=null, target={type:"all"}, busy=false;

const targetStr=()=>target.type==="all"?"all":target.type==="sec"?"sec:"+target.name:"seg:"+target.id;
const targetSegs=()=>st.segments.filter(s=>target.type==="all"
  ||(target.type==="sec"&&s.section===target.name)||(target.type==="seg"&&s.id===target.id));
const sections=()=>[...new Set(st.segments.map(s=>s.section))];
const hhmm=e=>String(e.h).padStart(2,"0")+":"+String(e.m).padStart(2,"0");

async function api(path,params){
  busy=true;
  try{
    const q=new URLSearchParams(params||{}).toString();
    const r=await fetch(path+(q?"?"+q:""),{method:path==="/api/state"?"GET":"POST"});
    st=await r.json(); render();
    $("#status").textContent="192.168.1.50 · up "+st.uptime_s+"s · "+st.frames+" frames · last reset: "+st.last_reset;
  }catch(e){ $("#status").textContent="lost contact with the board — "+e.message; }
  busy=false;
}
const set=p=>api("/api/set",{target:targetStr(),...p});

function el(tag,cls,txt){const e=document.createElement(tag);if(cls)e.className=cls;if(txt!=null)e.textContent=txt;return e;}

function renderAuto(){
  $("#autoSw").className="switch"+(st.autoMode?" on":"");
  const nx=st.schedule[st.nextIdx];
  if(!st.timeValid){
    $("#autoTitle").textContent="Auto";
    $("#autoSub").textContent="waiting for the clock to sync…";
  }else if(st.autoMode&&nx){
    const cur=st.activeScene>=0?st.scenes[st.activeScene].name:"a custom look";
    $("#autoTitle").textContent="Following the schedule";
    $("#autoSub").textContent="Now "+cur+" · "+st.scenes[nx.scene].name+" at "+hhmm(nx);
  }else{
    $("#autoTitle").textContent="Manual";
    $("#autoSub").textContent="the schedule won't change anything";
  }
}

function renderScenes(){
  const g=$("#scenes"); g.innerHTML="";
  st.scenes.forEach(s=>{
    const b=el("button","scene"+(st.activeScene===s.i?" on":""));
    const sw=el("span","swatch"); sw.style.background="#"+s.sw; sw.style.color="#"+s.sw;
    const t=el("div"); t.append(el("b",null,s.name),el("span",null,s.hint));
    b.append(sw,t); b.onclick=()=>api("/api/scene",{i:s.i});
    g.append(b);
  });
}

function renderSched(){
  const w=$("#sched"); w.innerHTML="";
  st.schedule.forEach(e=>{
    const row=el("div","srow"+(e.on?"":" dim"));
    const t=el("input"); t.type="time"; t.value=hhmm(e);
    t.onchange=()=>{const [h,m]=t.value.split(":");api("/api/schedule",{i:e.i,h,m});};
    const sel=el("select");
    st.scenes.forEach(s=>{const o=el("option",null,s.name);o.value=s.i;if(s.i===e.scene)o.selected=true;sel.append(o);});
    sel.onchange=()=>api("/api/schedule",{i:e.i,scene:sel.value});
    const tg=el("button","tog"+(e.on?" on":""));
    tg.onclick=()=>api("/api/schedule",{i:e.i,on:e.on?0:1});
    row.append(t,sel,tg); w.append(row);
  });
  $("#fade").value=st.fadeMin;
  $("#fadeVal").textContent=st.fadeMin?st.fadeMin+" min":"instant";
}

function renderTune(){
  const map=$("#map"); map.innerHTML="";
  const sel=targetSegs().map(s=>s.id);
  st.segments.forEach((s,i)=>{
    const d=el("div"); d.style.flex=(st.bounds[i+1]-st.bounds[i])||0.01; d.title=s.section+" "+s.side;
    if(s.effect==="rainbow"&&s.on)d.className="rainbow"; else d.style.background=s.on?"#"+s.color:"#333";
    if(sel.includes(s.id))d.classList.add("sel");
    d.onclick=()=>{target={type:"seg",id:s.id};render();api("/api/locate",{seg:s.id});};
    map.append(d);
  });
  $("#target").textContent=target.type==="all"?"everything":target.type==="sec"?target.name+" · all sides"
    :st.segments[target.id].section+" · "+st.segments[target.id].side;
  const chip=(txt,on,fn,seg)=>{
    const b=el("button","chip"+(on?" on":""));
    if(seg){const d=el("span","dot"+(seg.on?"":" off"));
      d.style.background=seg.effect==="rainbow"?"linear-gradient(90deg,red,lime,blue)":"#"+seg.color;b.append(d);}
    b.append(txt); b.onclick=fn; return b;
  };
  const secs=$("#sections"); secs.innerHTML="";
  secs.append(chip("All",target.type==="all",()=>{target={type:"all"};render();}));
  sections().forEach(n=>secs.append(chip(n,target.type==="sec"&&target.name===n,()=>{target={type:"sec",name:n};render();})));
  const sides=$("#sides"); sides.innerHTML="";
  const secName=target.type==="sec"?target.name:target.type==="seg"?st.segments[target.id].section:null;
  if(secName) st.segments.filter(s=>s.section===secName).forEach(s=>
    sides.append(chip(s.side,target.type==="seg"&&target.id===s.id,
      ()=>{target={type:"seg",id:s.id};render();api("/api/locate",{seg:s.id});},s)));
  const rep=targetSegs()[0];
  $("#power").textContent=rep.on?"Turn off":"Turn on";
  $("#power").className="chip wide"+(rep.on?" on":"");
  $("#col").value="#"+rep.color; $("#spd").value=rep.speed;
  const ef=$("#effects"); ef.innerHTML="";
  EFFECTS.forEach(e=>ef.append(chip(e,rep.effect===e,()=>set({effect:e}))));
}

function renderLayout(){
  $("#flip").textContent=st.flipped?"Direction: flipped":"Direction: normal";
  $("#flip").className="chip wide"+(st.flipped?" on":"");
  const b=$("#bounds"); b.innerHTML="";
  for(let i=1;i<st.segments.length;i++){
    const row=el("div","bound");
    row.append(el("span",null,st.segments[i-1].side+" → "+st.segments[i].side));
    const minus=el("button",null,"−"); minus.onclick=()=>api("/api/bounds",{i,led:st.bounds[i]-1});
    const inp=el("input"); inp.type="number"; inp.value=st.bounds[i];
    inp.onchange=()=>api("/api/bounds",{i,led:inp.value});
    const plus=el("button",null,"+"); plus.onclick=()=>api("/api/bounds",{i,led:st.bounds[i]+1});
    const show=el("button",null,"show"); show.onclick=()=>api("/api/locate",{bound:i});
    row.append(minus,inp,plus,show); b.append(row);
  }
}

function render(){
  if(!st)return;
  $("#clock").textContent=st.clock;
  $("#bri").value=st.brightness;
  $("#briVal").textContent=Math.round(st.brightness/255*100)+"%";
  renderAuto(); renderScenes();
  if($("#dSched").open) renderSched();
  if($("#dTune").open)  renderTune();
  if($("#dLayout").open)renderLayout();
}

$("#autoSw").onclick=()=>api("/api/auto",{on:st.autoMode?0:1});
$("#bri").oninput=e=>{$("#briVal").textContent=Math.round(e.target.value/255*100)+"%";};
$("#bri").onchange=e=>api("/api/set",{brightness:e.target.value});
$("#fade").onchange=e=>api("/api/set",{fade:e.target.value});
$("#spd").onchange=e=>set({speed:$("#spd").value});
$("#col").onchange=e=>set({color:e.target.value.slice(1)});
$("#power").onclick=()=>set({on:targetSegs()[0].on?0:1});
$("#dSched").ontoggle=render; $("#dTune").ontoggle=render; $("#dLayout").ontoggle=render;

// The board changes things on its own (schedule, crossfades), so keep in sync —
// but never while a request of ours is in flight or a control has focus.
setInterval(()=>{ if(!busy && !/INPUT|SELECT/.test(document.activeElement.tagName)) api("/api/state"); },10000);
api("/api/state");
</script></body></html>)HTML";
