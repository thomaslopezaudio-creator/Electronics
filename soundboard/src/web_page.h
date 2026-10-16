#pragma once
#include <Arduino.h>
const char WEB_PAGE[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Soundboard</title><style>
/* Late-1970s audio equipment: flat colours, push keys, LED lamps, silkscreen labels. */
:root{color-scheme:light;--case:#e8e2d3;--strip:#2b2622;--ink:#2b2622;--muted:#6e665a;--key:#f5f1e7;--edge:#b5ac9c;
--orange:#e2602b;--mustard:#d9a332;--brown:#8a5a34;--lamp:#9fd04a;font-family:"Helvetica Neue",Helvetica,Arial,sans-serif;background:var(--case);color:var(--ink)}
*{box-sizing:border-box}html{overscroll-behavior-x:none}body{margin:0}main{max-width:850px;margin:auto;padding:28px 18px 48px}
header{display:flex;flex-wrap:wrap;align-items:flex-end;justify-content:space-between;gap:10px 16px;padding-bottom:14px;position:relative}
header::after{content:'';position:absolute;left:0;right:0;bottom:-12px;height:9px;
background:linear-gradient(var(--orange) 0 33%,var(--mustard) 33% 66%,var(--brown) 66%)}
h1{font-family:Futura,"Century Gothic","Avenir Next",sans-serif;font-size:30px;font-weight:700;letter-spacing:.08em;text-transform:uppercase;margin:0;line-height:1}
.model,.label,.hint,#storage,#connection{font-size:11px;letter-spacing:.16em;text-transform:uppercase}
.model{color:var(--muted);margin-top:6px}p{color:var(--muted);line-height:1.5}
button,input{font:inherit}
button,label.upload{cursor:pointer;border:1px solid var(--edge);border-bottom-width:4px;border-radius:6px;padding:11px 16px;
background:var(--key);color:var(--ink);font-size:13px;font-weight:700;letter-spacing:.1em;text-transform:uppercase}
button:active,label.upload:active{transform:translateY(2px);border-bottom-width:2px}
button:focus-visible,input:focus-visible,label.upload:focus-within{outline:3px solid var(--orange);outline-offset:3px}
button:disabled{opacity:.45;cursor:wait}
#connection{display:flex;align-items:center;gap:8px;color:var(--muted)}
.lamp{width:10px;height:10px;border-radius:50%;background:var(--lamp);box-shadow:0 0 6px var(--lamp);flex:none}
#connection.off .lamp{background:#8d8579;box-shadow:none}
.panel{background:var(--strip);color:var(--case);border-radius:8px;padding:18px 20px;margin:36px 0 22px}
.panel .label{color:#b8ae9d}
/* Master trim fader: 0 dB at the top, -inf at the bottom, marks every 10 dB. */
.master{display:flex;gap:22px;align-items:stretch}
.mside{display:flex;flex-direction:column;align-items:flex-start;justify-content:space-between;gap:12px;flex:1;min-width:0}
.trimfader{position:relative;width:86px;height:220px;flex:none}
.trimfader .marks{position:absolute;inset:0;pointer-events:none}
.trimfader .mark{position:absolute;left:0;right:0;transform:translateY(-50%);display:grid;grid-template-columns:28px 1fr 12px 1fr;align-items:center;gap:0;font:9px "SF Mono",Menlo,monospace;color:#b8ae9d}
.trimfader .mark span{text-align:right;padding-right:5px}.trimfader .mark i{height:1px;background:#8a8172}
.trimfader input{position:absolute;left:58px;top:50%;width:220px;height:34px;margin:0;transform:translate(-50%,-50%) rotate(-90deg);
-webkit-appearance:none;appearance:none;background:transparent;touch-action:none}
.trimfader input::-webkit-slider-runnable-track{height:4px;background:#171310;border-radius:2px}
.trimfader input::-moz-range-track{height:4px;background:#171310;border-radius:2px}
.trimfader input::-webkit-slider-thumb{-webkit-appearance:none;width:20px;height:32px;margin-top:-14px;border:1px solid #171310;border-radius:3px;
background:linear-gradient(90deg,#d8d2c2 9px,var(--orange) 9px 11px,#d8d2c2 11px)}
.trimfader input::-moz-range-thumb{width:20px;height:32px;border:1px solid #171310;border-radius:3px;background:linear-gradient(90deg,#d8d2c2 9px,var(--orange) 9px 11px,#d8d2c2 11px)}
#level{min-width:86px;text-align:right;background:#171310;color:var(--orange);font-family:"SF Mono",Menlo,monospace;font-size:18px;
padding:4px 8px;border-radius:4px;font-variant-numeric:tabular-nums}
.stop{background:var(--orange);border-color:#a8441c;color:#fff}
#now{margin:14px 0 0;color:var(--case);font-family:"SF Mono",Menlo,monospace;font-size:14px}
#message{min-height:24px}#message.error{color:#b33a12;font-weight:700}
.toolbar{display:flex;gap:12px;align-items:center;flex-wrap:wrap}#storage{color:var(--muted)}
#file,#record{position:absolute;width:1px;height:1px;opacity:0}.rec{color:#d3301c}
.hint{margin:12px 0 0;line-height:1.6;letter-spacing:.08em}
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(160px,1fr));gap:12px}
.clip{display:flex;flex-direction:column;gap:6px}
.play{width:100%;min-height:96px;text-align:left;overflow-wrap:anywhere;display:flex;flex-direction:column;justify-content:space-between;gap:12px}
.play::before{content:'';width:9px;height:9px;border-radius:50%;background:#a59d8f}
.clip.active .play{border-color:var(--orange)}.clip.active .play::before{background:var(--orange);box-shadow:0 0 6px var(--orange)}
.tools{display:flex;justify-content:space-between;gap:2px}
.tool{background:none;border:none;padding:6px 1px;font-size:10px;letter-spacing:.06em;color:var(--muted)}
.tool:active{transform:none}.tool.on{color:var(--orange)}.tool.on::before{content:'● '}
.small{padding:7px 11px;font-size:11px;border-bottom-width:3px}
.toggle{display:inline-flex;align-items:center;gap:8px}.toggle .lamp{background:#a59d8f;box-shadow:none}
.toggle.on .lamp{background:var(--orange);box-shadow:0 0 6px var(--orange)}
.module{background:var(--key);border:1px solid var(--edge);border-radius:8px;padding:14px;margin:0 0 14px}
.modhead{display:flex;justify-content:space-between;align-items:center;gap:8px;margin-bottom:12px}
/* Edit page: a console channel strip in the spirit of a well-used SSL 4000 G. */
.strip{max-width:360px;margin:0 auto 14px;background:#3a3d43;border:1px solid #1f2125;border-radius:6px;
box-shadow:inset 0 0 0 3px #2f3237;color:#e7e4dc;padding:12px 14px 14px}
.sec{border-top:1px solid #565a61;padding:12px 0}.sec:first-child{border-top:0;padding-top:2px}
.silk{font:700 9px/1.25 "Helvetica Neue",Helvetica,Arial,sans-serif;letter-spacing:.12em;text-transform:uppercase;color:rgba(231,228,220,.74)}
.sechead{display:flex;justify-content:space-between;align-items:center;gap:8px;min-height:24px;margin-bottom:10px}
#gainState{color:#ffb547}
.scribble{flex:1;min-width:0;background:#e9dfc2;color:#1f2b47;font:21px/1.2 "Marker Felt","Chalkboard SE","Comic Sans MS",cursive;
padding:5px 10px;border-radius:2px;transform:rotate(-.6deg);overflow-wrap:anywhere;margin:0}
.toprow{display:flex;gap:10px;align-items:center}.transport2{display:flex;gap:8px;align-items:center;margin-top:12px}
#editor .sslbtn{background:#cfccc3;color:#25272b;border:1px solid #17181b;border-bottom-width:3px;border-radius:3px;padding:6px 9px;min-width:60px;
display:inline-flex;flex-direction:column;align-items:center;gap:5px;font:700 10px/1 "Helvetica Neue",Helvetica,Arial,sans-serif;letter-spacing:.08em;text-transform:uppercase}
#editor .sslbtn:active{transform:translateY(1px);border-bottom-width:2px}
.led{width:9px;height:9px;border-radius:50%;background:#4b4036}
.sslbtn.on .led{background:var(--ledc);box-shadow:0 0 6px var(--ledc)}
.vslot{position:relative;width:44px;height:120px}
.vslot input{position:absolute;left:50%;top:50%;width:120px;height:34px;margin:0;transform:translate(-50%,-50%) rotate(-90deg);
-webkit-appearance:none;appearance:none;background:transparent;touch-action:none}
.vslot input::-webkit-slider-runnable-track{height:4px;background:#141619;border-radius:2px}
.vslot input::-moz-range-track{height:4px;background:#141619;border-radius:2px}
.vslot input::-webkit-slider-thumb{-webkit-appearance:none;width:18px;height:30px;margin-top:-13px;border:1px solid #17181b;border-radius:3px;
background:linear-gradient(90deg,#d8d2c2 8px,#25272b 8px 10px,#d8d2c2 10px)}
.vslot input::-moz-range-thumb{width:18px;height:30px;border:1px solid #17181b;border-radius:3px;background:linear-gradient(90deg,#d8d2c2 8px,#25272b 8px 10px,#d8d2c2 10px)}
.val{font:12px "SF Mono",Menlo,monospace;color:#f1eee5}
.dyn{display:grid;grid-template-columns:auto auto auto 1fr;gap:14px;align-items:center}
.switches{display:flex;flex-direction:column;gap:10px;align-items:flex-end;justify-self:end}#editor .switches .sslbtn{min-width:66px;line-height:1.15}
.harm{margin-top:12px;padding-top:10px;border-top:1px solid #565a61}
.hrow{display:grid;grid-template-columns:76px 1fr 78px;gap:8px;align-items:center;height:26px}
.hrow input{width:100%;margin:0;accent-color:#d8d2c2;touch-action:pan-y}.hrow .val{text-align:right}
.ctl{display:flex;flex-direction:column;align-items:center;gap:6px}
.leds{display:grid;grid-template-columns:auto 16px;gap:3px 6px;align-items:center}
.leds .silk{font-size:8px;text-align:right}
.seg{height:7px;border-radius:1px;background:var(--off)}.seg.lit{background:var(--on);box-shadow:0 0 5px var(--on)}
.seg.g{--on:#3fd25a;--off:#1f3324}.seg.y{--on:#f4d23c;--off:#3a3520}.seg.r{--on:#ff4a3a;--off:#3d2220}
.api{background:#121315;border:1px solid #050506;border-radius:4px;box-shadow:inset 0 0 0 1px #26272a;padding:10px 10px 12px}
.api .silk{color:rgba(240,238,232,.86)}
.arow{display:grid;grid-template-columns:30px 1fr 30px;gap:8px;align-items:center;height:36px}
/* The whole faceplate is the touch surface: horizontal drags adjust a band, vertical drags scroll. */
#eq .arow{touch-action:pan-y;cursor:ew-resize}#eq .aslot input{pointer-events:none}
.arow .silk{text-align:right}.arow .val{text-align:right}
.aslot{position:relative;height:30px}
/* Major marks only (-12, -6, 0, +6, +12), above and below the slot. */
.aslot::after{content:'';position:absolute;left:6px;right:6px;top:3px;bottom:3px;pointer-events:none;
background:repeating-linear-gradient(90deg,#8d9096 0 1px,transparent 1px calc((100% - 1px) / 4))}
.aslot input{position:relative;z-index:1;-webkit-appearance:none;appearance:none;width:100%;height:30px;margin:0;background:transparent}
/* A recessed slot with a small actuator that travels inside it, as on a 560. */
.aslot input::-webkit-slider-runnable-track{height:12px;background:#000;border-radius:2px;box-shadow:inset 0 0 0 1px #3a3c41}
.aslot input::-moz-range-track{height:12px;background:#000;border-radius:2px;box-shadow:inset 0 0 0 1px #3a3c41}
.aslot input::-webkit-slider-thumb{-webkit-appearance:none;width:12px;height:8px;margin-top:2px;border:0;border-radius:1px;background:#d9d6cd}
.aslot input::-moz-range-thumb{width:12px;height:8px;border:0;border-radius:1px;background:#d9d6cd}
.ascale{position:relative;height:12px;margin:0 6px}.ascale span{position:absolute;transform:translateX(-50%);font-size:8px}
.lcd{background:#141619;border:1px solid #0d0e10;border-radius:3px;color:#ffb547;font:12px/1.6 "SF Mono",Menlo,monospace;padding:8px 10px;white-space:pre-line}
.trim2{display:grid;grid-template-columns:auto 1fr auto;gap:10px;align-items:center}
.trim2 .side{display:flex;flex-direction:column;gap:5px;min-width:44px}.trim2 .side.right{align-items:flex-end}
.dual{position:relative;height:56px;background:#141619;border-radius:3px}
.dual canvas{position:absolute;left:0;top:0;width:100%;height:100%;pointer-events:none}
.fader{position:absolute;left:0;top:0;-webkit-appearance:none;appearance:none;width:calc(100% - 16px);height:56px;margin:0;background:transparent;pointer-events:none;touch-action:none}
#trimEnd{left:16px}
.fader::-webkit-slider-runnable-track{height:4px;background:transparent}
.fader::-moz-range-track{height:4px;background:transparent}
.fader::-webkit-slider-thumb{-webkit-appearance:none;pointer-events:auto;width:16px;height:44px;margin-top:-20px;border:1px solid #17181b;border-radius:3px;background:#cfccc3}
.fader::-moz-range-thumb{pointer-events:auto;width:16px;height:44px;border:1px solid #17181b;border-radius:3px;background:#cfccc3}
.bpm{display:grid;grid-template-columns:repeat(4,1fr);gap:6px;margin-top:10px}
.bpm div{background:#141619;border:1px solid #0d0e10;border-radius:3px;padding:5px 6px;display:flex;justify-content:space-between;align-items:baseline}
.bpm b{font:12px "SF Mono",Menlo,monospace;color:#ffb547;font-weight:400}
.namerow{display:flex;gap:8px;margin-top:8px}#nameInput{flex:1;min-width:0;font:inherit;font-size:16px;padding:9px 10px;border:1px solid var(--edge);border-radius:6px;background:#fff;color:var(--ink)}#nameHint{margin-top:8px}
</style></head><body><main>
<header><div><h1>Soundboard</h1><div class="model">Model SB-1 &middot; PCM5102 output</div></div><span id="connection"><span class="lamp"></span><span id="connText">Connecting…</span></span></header>
<section class="panel" aria-label="Playback controls">
<div class="master"><div class="trimfader"><div class="marks" id="marks" aria-hidden="true"></div>
<input id="volume" type="range" min="-70" max="0" step="1" value="-23" aria-label="Volume, dB"></div>
<div class="mside"><label class="label" for="volume">Volume trim</label><output id="level" for="volume">-23 dB</output>
<button class="stop" id="stop">Stop</button><p id="now" aria-live="polite">Ready</p></div></div>
</section>
<p id="message" role="status" aria-live="polite"></p>
<div id="namer" class="module" hidden><label class="label" for="nameInput">Name this sound</label><div class="namerow"><input id="nameInput" maxlength="40" autocomplete="off" autocapitalize="off" enterkeyhint="done"><button id="nameSave" class="small">Save</button><button id="nameDiscard" class="small">Discard</button></div><p id="nameHint" class="hint"></p></div>
<div id="home">
<div class="toolbar"><label class="upload">+ Add sound<input id="file" type="file" accept="audio/*,.wav,.mp3,.m4a,.ogg"></label><label class="upload"><span class="rec">●</span> Record<input id="record" type="file" accept="video/*" capture></label><button id="nr" class="toggle small"><span class="lamp"></span>Noise reduction</button><span id="storage"></span><button id="restart" class="small">Restart board</button></div>
<p class="hint">Clips up to ~6 s stereo or ~12 s mono, converted in your browser. Record films a short video and keeps only its sound (mono). Noise reduction (RNNoise) cleans steady background noise before upload. Uploading stops playback.</p>
<section aria-label="Your sounds"><div id="clips" class="grid"></div><p id="empty">No clips yet. Add a sound.</p></section>
</div>
<section id="editor" hidden aria-label="Edit clip"><div class="strip">
<div class="sec"><div class="toprow"><button id="back" class="sslbtn">&lsaquo; Sounds</button><h2 id="edName" class="scribble"></h2></div>
<div class="transport2"><button id="edPlay" class="sslbtn" style="--ledc:#3fd25a"><span class="led"></span>Play</button>
<button id="edLoop" class="sslbtn" style="--ledc:#f4d23c"><span class="led"></span>Loop</button><span id="edLen" class="silk"></span></div></div>
<div class="sec"><div class="sechead"><span></span><span class="silk">Start &amp; end buffer</span></div><div class="trim2">
<div class="side"><span class="silk">Start</span><output id="startVal" class="val"></output></div>
<div class="dual"><canvas id="wave" aria-hidden="true"></canvas><input id="trimStart" class="fader" type="range" min="0" step="1" aria-label="Start buffer"><input id="trimEnd" class="fader" type="range" min="0" step="1" aria-label="End buffer"></div>
<div class="side right"><span class="silk">End</span><output id="endVal" class="val"></output></div></div>
<div class="bpm" id="bpm"></div>
<p id="trimInfo" class="silk" style="margin:10px 0 0"></p></div>
<div class="sec"><div class="sechead"><span id="gainState" class="silk" aria-live="polite"></span><span class="silk">Leveling amplifier</span></div>
<div class="dyn"><div class="ctl"><div class="vslot"><input id="drive" type="range" min="0" max="48" step="1" value="0" aria-label="Drive"></div><span class="silk">Drive</span><output id="driveVal" class="val">0 dB</output></div>
<div class="ctl"><div class="vslot"><input id="comp" type="range" min="0" max="60" step="1" value="0" aria-label="Input gain"></div><span class="silk">Input gain</span><output id="compVal" class="val">0 dB</output></div>
<div class="ctl"><div class="leds" id="grLeds" aria-label="Gain reduction meter"></div><span class="silk">Reduction</span><output id="grVal" class="val">&ndash;</output></div>
<div class="switches"><button id="edPre" class="sslbtn" style="--ledc:#f4d23c" aria-pressed="false"><span class="led"></span>Pre EQ</button>
<button id="edFast" class="sslbtn" style="--ledc:#ff4a3a" aria-pressed="false"><span class="led"></span>Fast<br>attack</button></div></div>
<div class="harm"><div class="sechead" style="margin-bottom:4px"><span class="silk">Drive shape &middot; which harmonics, how hard</span><button id="edOs" class="sslbtn" style="--ledc:#3fd25a" aria-pressed="false"><span class="led"></span>Oversample<br><span id="osVal">off</span></button></div><div id="harm"></div></div></div>
<div class="sec"><div class="sechead"><button id="flat" class="sslbtn">Flat</button><span class="silk">Equaliser &middot; octaves</span></div>
<div class="api"><div class="arow" style="height:14px"><span></span><div class="ascale silk" aria-hidden="true"><span style="left:0">-12</span><span style="left:25%">-6</span><span style="left:50%">0</span><span style="left:75%">+6</span><span style="left:100%">+12</span></div><span></span></div><div id="eq"></div></div></div>
<div class="sec"><div class="sechead"><span id="matchState" class="silk"></span><span class="silk">Loudness match</span></div><div class="lcd" id="lufs"></div></div>
<p class="silk" style="margin:0">Loop + Play to hear changes live. <span id="loadInfo"></span></p>
</div></section>
</main><script>
const $ = id => document.getElementById(id);
let busy = false, polling = false, audioReady = false, touchingVolume = false, clipNames = new Set();
let editing = null, lastPoll = 0, wasMatching = false, sendTimer;
const display = name => name.replace(/\.wav$/i,'').replace(/_/g,' ');
const signed = v => (v > 0 ? '+' : '') + v;
function message(text, error = false) { $('message').textContent = text; $('message').classList.toggle('error', error); }
async function api(path, args) {
  const options = args === undefined ? {} : {method:'POST', body:new URLSearchParams(args)};
  const response = await fetch('/api/' + path, options);
  const data = await response.json();
  if (!response.ok) throw new Error(data.error || 'The board could not complete that request.');
  return data;
}
async function act(path, args) {
  try { await api(path, args); message(''); await status(); }
  catch (e) { message(e.message, true); }
}
$('stop').onclick = () => act('stop', {});
$('restart').onclick = async () => {
  if (!confirm('Restart the board? Playback stops and the page reloads when it is back (about 10 s).')) return;
  try { await api('restart', {}); } catch (e) { message(e.message, true); return; }
  busy = true;
  message('Restarting…');
  $('connText').textContent = 'Restarting';
  $('connection').classList.add('off');
  const back = async () => {
    try { await fetch('/api/status', {cache:'no-store'}); location.reload(); }
    catch (e) { setTimeout(back, 1000); }
  };
  setTimeout(back, 3000);
};
// The fader's bottom stop (-70) is -inf: the board outputs silence there.
const VOLUME_OFF = -70;
const dbText = v => v <= VOLUME_OFF ? '-\u221e dB' : v + ' dB';
for (let db = 0; db >= VOLUME_OFF; db -= 10) {
  // Thumb centre travels 10 px in from each end of the 220 px track.
  const mark = document.createElement('div'); mark.className = 'mark'; mark.style.top = (10 + 200 * db / VOLUME_OFF) + 'px';
  const label = document.createElement('span'); label.textContent = db <= VOLUME_OFF ? '-\u221e' : db;
  mark.append(label, document.createElement('i'), document.createElement('b'), document.createElement('i'));
  $('marks').append(mark);
}
$('volume').oninput = () => { touchingVolume = true; $('level').textContent = dbText(+$('volume').value); };
$('volume').onchange = async () => {
  await act('volume', {value:$('volume').value});
  touchingVolume = false;
};
async function status() {
  if (busy || polling) return;
  polling = true;
  try {
    const s = await api('status');
    audioReady = s.audioReady;
    $('connText').textContent = s.audioReady ? 'Connected' : 'Audio unavailable';
    $('connection').classList.toggle('off', !s.audioReady);
    lastPoll = Date.now();
    $('now').textContent = s.playing ? 'Playing: ' + display(s.playing) : 'Ready';
    if (editing) showMeters(s);
    if (!touchingVolume) { $('volume').value = s.volume; $('level').textContent = dbText(s.volume); }
    document.querySelectorAll('.clip').forEach(c => {
      c.classList.toggle('active', c.dataset.name === s.playing);
      c.querySelector('.play').disabled = !s.audioReady;
    });
    if (s.error) message(s.error, true);
  } catch (e) {
    audioReady = false;
    $('connText').textContent = 'Disconnected';
    $('connection').classList.add('off');
  } finally { polling = false; }
}
async function refresh() {
  if (editing) return;
  const data = await api('clips');
  clipNames = new Set(data.clips.map(c => c.name));
  $('clips').replaceChildren();
  $('empty').hidden = data.clips.length > 0;
  $('storage').textContent = (data.free / 1048576).toFixed(1) + ' MB available';
  data.clips.sort((a,b) => a.name.localeCompare(b.name)).forEach(clip => {
    const card = document.createElement('div'); card.className = 'clip'; card.dataset.name = clip.name;
    const play = document.createElement('button'); play.className = 'play';
    play.append(display(clip.name));
    play.disabled = !audioReady;
    play.onclick = () => act('play', {name:clip.name});
    const tool = (label, run) => {
      const b = document.createElement('button'); b.className = 'tool'; b.textContent = label;
      b.setAttribute('aria-label', label + ' ' + display(clip.name));
      b.onclick = async () => { try { await run(); } catch (e) { message(e.message, true); } };
      return b;
    };
    const loop = tool('Loop', async () => { await api('clip', {name:clip.name, loop:clip.loop ? 0 : 1}); await refresh(); });
    loop.classList.toggle('on', clip.loop); loop.setAttribute('aria-pressed', clip.loop);
    const edit = tool('Edit', () => { location.hash = '#edit=' + encodeURIComponent(clip.name); });
    const rename = tool('Rename', async () => {
      const text = prompt('Rename ' + display(clip.name) + ':', display(clip.name));
      if (text === null) return;
      const name = clipName(text);
      if (!name) throw new Error('Use letters, numbers, spaces or dashes.');
      if (name + '.wav' === clip.name) return;
      if (clipNames.has(name + '.wav')) throw new Error('That name is taken.');
      await api('rename', {name:clip.name, to:name + '.wav'}); await refresh(); message('');
    });
    const remove = tool('Remove', async () => {
      if (!confirm('Remove ' + display(clip.name) + ' from the board?')) return;
      await api('delete', {name:clip.name}); await refresh();
    });
    const tools = document.createElement('div'); tools.className = 'tools';
    tools.append(loop, edit, rename, remove);
    card.append(play, tools); $('clips').append(card);
  });
}
function encodeWav(buffer) {
  const channels = buffer.numberOfChannels, frames = buffer.length;
  const bytes = frames * channels * 2;
  const result = new ArrayBuffer(44 + bytes), view = new DataView(result);
  const text = (offset, value) => { for(let i=0;i<value.length;i++) view.setUint8(offset+i,value.charCodeAt(i)); };
  text(0,'RIFF'); view.setUint32(4,36+bytes,true); text(8,'WAVE'); text(12,'fmt ');
  view.setUint32(16,16,true); view.setUint16(20,1,true); view.setUint16(22,channels,true);
  view.setUint32(24,44100,true); view.setUint32(28,44100*channels*2,true);
  view.setUint16(32,channels*2,true); view.setUint16(34,16,true); text(36,'data'); view.setUint32(40,bytes,true);
  const data = Array.from({length:channels},(_,c)=>buffer.getChannelData(c));
  for (let i=0,offset=44;i<frames;i++) for(let c=0;c<channels;c++,offset+=2) {
    // A short fade avoids an abrupt cut at either end of an uploaded clip.
    const fade = Math.min(1, i/220, (frames-1-i)/220);
    const x = Math.max(-1,Math.min(1,data[c][i]*fade));
    view.setInt16(offset, Math.round(x < 0 ? x*32768 : x*32767), true);
  }
  return new Blob([result],{type:'audio/wav'});
}
// Safari will not decode the audio inside a .MOV directly. This copies the
// track's AAC frames out of the QuickTime boxes and gives each one a 7-byte
// ADTS header, making a plain .aac stream that decodeAudioData accepts.
function movToAdts(buffer) {
  const u8 = new Uint8Array(buffer), dv = new DataView(buffer);
  const tag = (p, n=4) => String.fromCharCode(...u8.subarray(p, p+n));
  const boxes = (start, end) => {
    const out = [];
    for (let p = start; p + 8 <= end;) {
      let size = dv.getUint32(p), header = 8;
      if (size === 1) { size = Number(dv.getBigUint64(p+8)); header = 16; }
      else if (size === 0) size = end - p;
      if (size < header || p + size > end) break;
      out.push({type:tag(p+4), start:p+header, end:p+size}); p += size;
    }
    return out;
  };
  const child = (box, type) => box && boxes(box.start, box.end).find(b => b.type === type);
  const fail = () => { throw new Error('The recording\'s audio could not be read.'); };
  const moov = boxes(0, u8.length).find(b => b.type === 'moov') || fail();
  for (const trak of boxes(moov.start, moov.end).filter(b => b.type === 'trak')) {
    const mdia = child(trak, 'mdia'), hdlr = child(mdia, 'hdlr');
    if (!hdlr || tag(hdlr.start+8) !== 'soun') continue;
    const stbl = child(child(mdia, 'minf'), 'stbl'), stsd = child(stbl, 'stsd') || fail();
    // esds may sit in mp4a directly or inside a QuickTime 'wave' box.
    let p = -1;
    for (let i = stsd.start; i < stsd.end - 4; i++) if (tag(i) === 'esds') { p = i + 8; break; }
    if (p < 0) fail();
    let config;
    while (p < stsd.end && !config) {
      const id = u8[p]; let len = 0, q = p + 1, b;
      do { b = u8[q++]; len = len << 7 | b & 127; } while (b & 128 && q < p + 5);
      if (id === 3) { const f = u8[q+2]; p = q + 3 + (f & 128 ? 2 : 0) + (f & 32 ? 1 + u8[q+3] : 0) + (f & 64 ? 2 : 0); }
      else if (id === 4) p = q + 13;
      else if (id === 5) config = u8.subarray(q, q + len);
      else p = q + len;
    }
    if (!config) fail();
    const type = config[0] >> 3, rate = (config[0] & 7) << 1 | config[1] >> 7, ch = config[1] >> 3 & 15;
    if (type < 1 || type > 4) throw new Error('Unsupported recording format (AAC type ' + type + ').');
    const stsz = child(stbl, 'stsz'), stsc = child(stbl, 'stsc'), stco = child(stbl, 'stco'), co64 = child(stbl, 'co64');
    if (!stsz || !stsc || !(stco || co64)) fail();
    const fixed = dv.getUint32(stsz.start+4), count = dv.getUint32(stsz.start+8);
    const sizes = Array.from({length:count}, (_, i) => fixed || dv.getUint32(stsz.start+12+i*4));
    const runs = Array.from({length:dv.getUint32(stsc.start+4)}, (_, i) =>
      [dv.getUint32(stsc.start+8+i*12), dv.getUint32(stsc.start+12+i*12)]);
    const chunks = dv.getUint32((stco || co64).start+4);
    const out = new Uint8Array(sizes.reduce((a, n) => a + n + 7, 0));
    for (let c = 1, s = 0, w = 0; c <= chunks && s < count; c++) {
      let offset = stco ? dv.getUint32(stco.start+4+c*4) : Number(dv.getBigUint64(co64.start+c*8));
      let perChunk = 0; for (const [first, n] of runs) if (first <= c) perChunk = n;
      for (let k = 0; k < perChunk && s < count; k++, s++) {
        const n = sizes[s], len = n + 7;
        out.set([0xFF, 0xF1, (type-1) << 6 | rate << 2 | ch >> 2, (ch & 3) << 6 | len >> 11, len >> 3 & 255, (len & 7) << 5 | 31, 0xFC], w);
        out.set(u8.subarray(offset, offset + n), w + 7); w += len; offset += n;
      }
    }
    return out.buffer;
  }
  throw new Error('That video has no sound track.');
}
const clipName = text => text.trim().replace(/[^a-zA-Z0-9_-]/g,'_').slice(0,40);
// An in-page name field. On the iPhone, prompt() here returned null every time
// ("Recording discarded"), so the page does not depend on it.
// Resolves to a usable, unused name, or null for Discard.
function askName() {
  return new Promise(resolve => {
    const box = $('namer'), input = $('nameInput'), hint = $('nameHint');
    input.value = 'recording ' + new Date().toTimeString().slice(0,5).replace(':','');
    hint.textContent = 'Letters, numbers, spaces and dashes.';
    for (const el of [input, $('nameSave'), $('nameDiscard')]) el.disabled = false; // addSound disables controls
    box.hidden = false; box.scrollIntoView({block:'center'}); input.focus();
    const done = value => { box.hidden = true; resolve(value); };
    $('nameSave').onclick = () => {
      const name = clipName(input.value);
      if (!name) hint.textContent = 'Use letters, numbers, spaces or dashes.';
      else if (clipNames.has(name + '.wav')) hint.textContent = 'That name is taken. Choose another.';
      else done(name);
    };
    $('nameDiscard').onclick = () => done(null);
    input.onkeydown = e => { if (e.key === 'Enter') $('nameSave').onclick(); };
  });
}
// ---- Noise reduction: RNNoise compiled to WebAssembly, served by the board.
// It works on 480-sample frames at 48 kHz and expects int16-scaled floats.
let noiseReduction = localStorage.getItem('nr') !== '0', rnnoise;
function showNr() { $('nr').classList.toggle('on', noiseReduction); $('nr').setAttribute('aria-pressed', noiseReduction); }
$('nr').onclick = () => { noiseReduction = !noiseReduction; localStorage.setItem('nr', noiseReduction ? '1' : '0'); showNr(); };
showNr();
function loadRnnoise() {
  // The module imports only two helpers (memcpy and heap growth), so no
  // Emscripten loader script is needed.
  return rnnoise ||= fetch('/rnnoise.wasm').then(r => {
    if (!r.ok) throw new Error('Could not load noise reduction from the board.');
    return r.arrayBuffer();
  }).then(bytes => {
    let ex;
    const imports = {a: {
      a: size => { try { ex.c.grow(Math.ceil((size - ex.c.buffer.byteLength) / 65536)); return 1; } catch (e) { return 0; } },
      b: (dest, src, n) => { new Uint8Array(ex.c.buffer).copyWithin(dest, src, src + n); },
    }};
    return WebAssembly.instantiate(bytes, imports).then(({instance}) => {
      ex = instance.exports; ex.d(); // d: static constructors
      return ex;
    });
  }).catch(e => { rnnoise = null; throw e; });
}
// In place on each channel of a 48 kHz AudioBuffer.
async function reduceNoise(buffer) {
  const ex = await loadRnnoise(); // c: memory, f: create, g: malloc, j: process, i: free, h: destroy
  for (let c = 0; c < buffer.numberOfChannels; c++) {
    const data = buffer.getChannelData(c), st = ex.f(0), ptr = ex.g(480 * 4);
    const frame = i => {
      const heap = new Float32Array(ex.c.buffer, ptr, 480);
      for (let k = 0; k < 480; k++) heap[k] = (data[i + k] || 0) * 32768;
      ex.j(st, ptr, ptr);
      return new Float32Array(ex.c.buffer, ptr, 480);
    };
    // Prime on the first second so the start is cleaned as well as the rest
    // (measured: -40 dBFS hiss -> -62 primed, -41 unprimed).
    for (let i = 0; i + 480 <= Math.min(data.length, 48000); i += 480) frame(i);
    // Output lags input by one frame, so frame i is written to i - 480.
    const out = new Float32Array(data.length);
    for (let i = 0; i < data.length + 480; i += 480) {
      const y = frame(i);
      for (let k = 0; k < 480; k++) { const j = i - 480 + k; if (j >= 0 && j < out.length) out[j] = y[k] / 32768; }
    }
    data.set(out);
    ex.i(ptr); ex.h(st);
  }
}
// Recording: browsers block microphone access on plain-HTTP pages, but a phone
// camera capture is just a file pick. Its audio track is unpacked above.
async function addSound(input, recording) {
  const file = input.files[0]; if(!file) return;
  busy = true;
  document.querySelectorAll('button,input').forEach(e => e.disabled = true);
  message('Preparing ' + file.name + '…');
  let decoder;
  try {
    if(file.size > (recording ? 150 : 20)*1048576) throw new Error(recording ?
      'That video is too large. Record a shorter one.' : 'Choose a smaller source file (under 20 MB).');
    const OfflineContext = window.OfflineAudioContext || window.webkitOfflineAudioContext;
    if(!OfflineContext) throw new Error('This browser cannot convert audio. Try Safari or Chrome.');
    // RNNoise needs 48 kHz; the renderer below converts to the board's 44.1 kHz.
    const denoise = noiseReduction;
    decoder = new OfflineContext(2,1,denoise ? 48000 : 44100);
    const bytes = await file.arrayBuffer();
    const decoded = await decoder.decodeAudioData(recording ? movToAdts(bytes) : bytes);
    if (denoise) { message('Reducing noise…'); await reduceNoise(decoded); }
    // A phone mic gains little from stereo; mono doubles the recording time.
    const channels = recording ? 1 : Math.min(decoded.numberOfChannels,2);
    const maxFrames = Math.floor((1048576-44)/(channels*2));
    let frames = Math.ceil(decoded.duration*44100);
    const trimmed = recording && frames > maxFrames;
    if(trimmed) frames = maxFrames; // Keep the start rather than lose the take.
    else if(frames > maxFrames) throw new Error('That clip is too long. Choose about 6 seconds stereo or 12 seconds mono.');
    const renderer = new OfflineContext(channels,frames,44100);
    const source = renderer.createBufferSource(); source.buffer=decoded; source.connect(renderer.destination); source.start();
    const wav = encodeWav(await renderer.startRendering());
    let name;
    if (recording) {
      message('Name the recording to save it.');
      name = await askName();
      if (name === null) { message('Recording discarded.'); return; }
    } else name = clipName(file.name.replace(/\.[^.]*$/,'')) || 'sound';
    const body = new FormData(); body.append('clip',wav,name+'.wav');
    message('Saving ' + name + ' to the board…');
    const response = await fetch('/api/upload',{method:'POST',body}); const result = await response.json();
    if(!response.ok) throw new Error(result.error || 'Upload failed.');
    await refresh(); message('Added ' + name + (trimmed ? ', trimmed to 12 seconds' : '') + '. Tap it to play.');
  } catch(e) { message(e.message || (recording ? 'The recording\'s audio could not be read.' : 'This audio file could not be converted.'),true); }
  finally {
    busy=false; input.value='';
    document.querySelectorAll('button,input').forEach(e => e.disabled = false);
    await status();
  }
}
$('file').onchange = () => addSound($('file'), false);
$('record').onchange = () => addSound($('record'), true);
// ---- Edit view. Settings live on the board, which applies them while playing.
const BANDS = ['31','63','125','250','500','1k','2k','4k','8k','16k'];
$('comp').oninput = () => { editing.comp = +$('comp').value; $('compVal').textContent = editing.comp + ' dB'; schedule(); };
$('drive').oninput = () => { editing.drive = +$('drive').value; showDrive(); schedule(); };
const OS_NAMES = ['off', '2×', '4×'];
function showOs() { $('osVal').textContent = OS_NAMES[editing.os]; $('edOs').classList.toggle('on', editing.os > 0); $('edOs').setAttribute('aria-pressed', editing.os > 0); }
$('edOs').onclick = () => { editing.os = (editing.os + 1) % 3; showOs(); schedule(); };
function showDrive() { $('driveVal').textContent = editing.drive + ' dB'; }
const SHAPES = [
  ['tilt', 'Low–High', v => v == 50 ? 'even' : (v < 50 ? 'low ' : 'high ') + Math.abs(Math.round((v - 50) / 50 * 12)) + ' dB'],
  ['balance', 'Odd–Even', v => { const d = Math.abs(v - 50); if (v == 0) return 'odd only'; if (v == 100) return 'even only'; if (d <= 5) return 'both'; const e = (50 + (v > 50 ? 1 : -1) * (3 + (d - 5) * 47 / 45)) / 100; return (v < 50 ? 'odd +' : 'even +') + Math.round(Math.abs(20 * Math.log10(e / (1 - e)))) + ' dB'; }],
  ['hardness', 'Soft–Hard', v => v + '%']];
for (const [key, name, text] of SHAPES) {
  const row = document.createElement('div'); row.className = 'hrow'; row.dataset.key = key;
  row.innerHTML = '<span class="silk"></span><input type="range" min="0" max="100" step="1" value="50"><output class="val"></output>';
  row.firstChild.textContent = name;
  row.querySelector('input').oninput = e => { editing[key] = +e.target.value; showShape(); schedule(); };
  $('harm').append(row);
}
function showShape() {
  $('harm').querySelectorAll('.hrow').forEach((row, k) => {
    const [key, , text] = SHAPES[k];
    row.querySelector('input').value = editing[key];
    row.querySelector('output').textContent = text(editing[key]);
  });
}
// Gain-reduction LEDs, top (most) to bottom (least).
const SEGMENTS = [[20,'r'],[16,'r'],[12,'y'],[10,'y'],[8,'y'],[6,'g'],[4,'g'],[3,'g'],[2,'g'],[1,'g']];
for (const [db, colour] of SEGMENTS) {
  const label = document.createElement('span'); label.className = 'silk'; label.textContent = db;
  const seg = document.createElement('div'); seg.className = 'seg ' + colour; seg.dataset.db = db;
  $('grLeds').append(label, seg);
}
function route() {
  const m = location.hash.match(/^#edit=(.+)$/);
  if (m) openEditor(decodeURIComponent(m[1])); else closeEditor();
}
async function openEditor(name) {
  try { editing = await api('clip?name=' + encodeURIComponent(name)); }
  catch (e) { message(e.message, true); location.hash = ''; return; }
  $('home').hidden = true; $('editor').hidden = false; window.scrollTo(0, 0); message('');
  $('edName').textContent = display(name);
  $('edLen').textContent = (editing.duration / 1000).toFixed(2) + ' s ' + (editing.channels === 2 ? 'stereo' : 'mono');
  editing.a = Math.round(editing.start * editing.rate / 1000); editing.b = Math.round(editing.end * editing.rate / 1000);
  buildEq(); $('comp').value = editing.comp; $('compVal').textContent = editing.comp + ' dB';
  $('drive').value = editing.drive; showDrive(); showShape(); showOs();
  showTrim(); showLoudness(); showLoop(); showPre();
  const opened = editing;
  api('peaks?name=' + encodeURIComponent(name) + '&n=160').then(r => { if (editing === opened) { opened.peaks = r.peaks; drawWave(); } }).catch(() => {});
}
function closeEditor() {
  const wasOpen = editing || !$('editor').hidden;
  editing = null; $('editor').hidden = true; $('home').hidden = false;
  if (wasOpen) refresh().catch(e => message(e.message, true));
}
$('back').onclick = () => { location.hash = ''; };
$('edPlay').onclick = () => act('play', {name:editing.name});
$('edLoop').onclick = async () => {
  editing.loop = !editing.loop; showLoop();
  try { await api('clip', {name:editing.name, loop:editing.loop ? 1 : 0}); } catch (e) { message(e.message, true); }
};
function showLoop() { $('edLoop').classList.toggle('on', editing.loop); $('edLoop').setAttribute('aria-pressed', editing.loop); }
function showPre() {
  $('edPre').classList.toggle('on', editing.pre); $('edPre').setAttribute('aria-pressed', editing.pre);
  $('edFast').classList.toggle('on', editing.fast); $('edFast').setAttribute('aria-pressed', editing.fast);
}
$('edPre').onclick = () => { editing.pre = !editing.pre; showPre(); schedule(); };
$('edFast').onclick = () => { editing.fast = !editing.fast; showPre(); schedule(); };
// EQ as horizontal sliders on a 560-style faceplate: -12 dB left, +12 right; 31 Hz at the top.
function buildEq() {
  $('eq').replaceChildren();
  BANDS.forEach((label, b) => {
    const row = document.createElement('div'); row.className = 'arow';
    const name = document.createElement('span'); name.className = 'silk'; name.textContent = label;
    const slot = document.createElement('div'); slot.className = 'aslot';
    const input = document.createElement('input');
    Object.assign(input, {type:'range', min:-12, max:12, step:1, value:editing.eq[b]});
    input.setAttribute('aria-label', label + ' Hz');
    const out = document.createElement('output'); out.className = 'val'; out.textContent = signed(editing.eq[b]);
    input.oninput = () => { editing.eq[b] = +input.value; out.textContent = signed(editing.eq[b]); schedule(); };
    slot.append(input); row.append(name, slot, out); $('eq').append(row);
    dragRow(row, input);
  });
}
// Drag anywhere in a row (label, slot or number). Relative, so a tap alone
// changes nothing: one dB per 1/24 of the slot's width.
function dragRow(row, input) {
  let startX = 0, startValue = 0;
  row.onpointerdown = e => { row.setPointerCapture(e.pointerId); startX = e.clientX; startValue = +input.value; };
  row.onpointermove = e => {
    if (!row.hasPointerCapture(e.pointerId)) return;
    const perDb = (input.getBoundingClientRect().width - 12) / 24;
    const v = Math.max(-12, Math.min(12, Math.round(startValue + (e.clientX - startX) / perDb)));
    if (v !== +input.value) { input.value = v; input.oninput(); }
  };
}
$('flat').onclick = () => { editing.eq = editing.eq.map(() => 0); buildEq(); schedule(); };
const seconds = ms => (ms / 1000).toFixed(2) + ' s';
// Waveform behind the trim thumbs, spanning the thumbs' travel; trimmed-off parts are dimmed.
function drawWave() {
  const cv = $('wave'), w = cv.clientWidth, h = cv.clientHeight, dpr = window.devicePixelRatio || 1;
  if (!w || !editing) return;
  cv.width = w * dpr; cv.height = h * dpr;
  const g = cv.getContext('2d'); g.scale(dpr, dpr);
  const peaks = editing.peaks; if (!peaks) return;
  const x0 = 16, span = w - 32, bar = span / peaks.length, F = editing.frames;
  const keepFrom = editing.a / F, keepTo = (F - editing.b) / F;
  peaks.forEach((p, i) => {
    const mid = (i + 0.5) / peaks.length, kept = mid >= keepFrom && mid <= keepTo;
    g.fillStyle = kept ? '#ffb547' : 'rgba(255,181,71,.22)';
    const bh = Math.max(2, p / 255 * (h - 8));
    g.fillRect(x0 + i * bar, (h - bh) / 2, Math.max(1, bar - 1), bh);
  });
}
// One slider, two thumbs: Start moves from the left, End from the right.
// Both work in frames; the End input holds a position (frames - end buffer).
// The played length is always a whole BPM at /2 beats, which makes /4, /6 and
// /12 whole too (2x, 3x, 6x). Round the BPM first, then place the End from
// it: length = rate * 120 / BPM, rounded to the nearest frame (within 11 us).
// Start stays where it is dropped.
const BEATS = [2, 4, 6, 12];
function snapLength(want, room) {
  const k0 = editing.rate * 120, shortest = Math.round(editing.rate * 0.05); // at least 50 ms plays
  const most = Math.floor(k0 / shortest), fewest = Math.ceil(k0 / room);
  if (fewest > most) return null;
  const bpm = Math.min(most, Math.max(fewest, Math.round(k0 / Math.max(1, want))));
  return Math.round(k0 / bpm);
}
const toMs = f => f * 1000 / editing.rate;
function showTrim() {
  const e = editing, F = e.frames, len = F - e.a - e.b, bpm2 = e.rate * 120 / len;
  for (const id of ['trimStart', 'trimEnd']) $(id).max = F;
  $('trimStart').value = e.a; $('trimEnd').value = F - e.b;
  $('startVal').textContent = seconds(toMs(e.a)); $('endVal').textContent = seconds(toMs(e.b));
  // Untouched trims may not sit on a whole BPM yet; say so rather than round the BPM.
  const whole = Math.round(e.rate * 120 / Math.round(bpm2)) === len;
  $('bpm').replaceChildren(...BEATS.map(n => {
    const cell = document.createElement('div'), label = document.createElement('span'), value = document.createElement('b');
    label.className = 'silk'; label.textContent = '/' + n;
    value.textContent = (whole ? '' : '≈') + Math.round(bpm2 * n / 2);
    cell.append(label, value); return cell;
  }));
  $('trimInfo').textContent = 'Plays ' + seconds(toMs(e.a)) + ' to ' + seconds(toMs(F - e.b)) +
    ' (' + (toMs(len) / 1000).toFixed(4) + ' s). BPM' + (whole ? '' : ' ≈ until a buffer moves') + '.';
  drawWave();
}
$('trimStart').oninput = () => {
  const e = editing, a = +$('trimStart').value, len = snapLength(e.frames - e.b - a, e.frames - a);
  if (len !== null) { e.a = a; e.b = e.frames - a - len; e.start = toMs(e.a); e.end = toMs(e.b); schedule(); }
  showTrim();
};
$('trimEnd').oninput = () => {
  const e = editing, len = snapLength(+$('trimEnd').value - e.a, e.frames - e.a);
  if (len !== null) { e.b = e.frames - e.a - len; e.end = toMs(e.b); schedule(); }
  showTrim();
};
// OUT is the processed clip before makeup; makeup brings it back to IN.
const lufs = v => v === null ? 'silent' : v.toFixed(1) + ' LUFS';
function showLoudness() {
  const e = editing;
  $('matchState').textContent = e.matching ? 'Matching…' : '';
  $('gainState').textContent = e.matching ? 'Adjusting output gain' : '';
  $('lufs').textContent = e.in === null ? (e.matching ? 'Measuring…' : 'No processing: output = input.') :
    'IN      ' + lufs(e.in) + '\nOUT     ' + lufs(e.out) + '\nMAKEUP  ' + signed(+e.makeup.toFixed(1)) + ' dB';
}
function showMeters(s) {
  const here = s.playing === editing.name, gr = here ? s.gr : 0;
  document.querySelectorAll('#grLeds .seg').forEach(seg => seg.classList.toggle('lit', gr >= +seg.dataset.db));
  $('grVal').textContent = here ? gr.toFixed(1) + ' dB' : '–';
  $('edPlay').classList.toggle('on', here);
  $('loadInfo').textContent = 'DSP load ' + s.load + '%.';
  if (s.matching) { $('matchState').textContent = 'Matching…'; $('gainState').textContent = 'Adjusting output gain'; }
  if (wasMatching && !s.matching) api('clip?name=' + encodeURIComponent(editing.name)).then(c => {
    if (editing && editing.name === c.name) Object.assign(editing, {in:c.in, out:c.out, makeup:c.makeup, matching:c.matching}), showLoudness();
  }).catch(() => {});
  wasMatching = s.matching;
}
// Coalesce rapid slider moves; send them in order.
// One request in flight at a time, always carrying the latest settings: while the board is
// busy, further slider moves collapse into a single follow-up instead of queueing up.
let sendBusy = false, sendDirty = false;
function schedule() {
  $('matchState').textContent = 'Matching…'; $('gainState').textContent = 'Adjusting output gain';
  sendDirty = true;
  clearTimeout(sendTimer);
  sendTimer = setTimeout(flush, 40);
}
async function flush() {
  const e = editing;
  if (sendBusy || !sendDirty || !e) return;
  sendBusy = true; sendDirty = false;
  const args = {name:e.name, eq:e.eq.join(','), comp:e.comp, drive:e.drive, tilt:e.tilt, balance:e.balance, hardness:e.hardness, os:e.os, pre:e.pre ? 1 : 0, fast:e.fast ? 1 : 0, start:e.start.toFixed(4), end:e.end.toFixed(4)};
  try {
    const c = await api('clip', args);
    if (editing && editing.name === c.name) Object.assign(editing, {in:c.in, out:c.out, makeup:c.makeup, matching:c.matching}), showLoudness();
  } catch (err) { message(err.message, true); }
  sendBusy = false;
  if (sendDirty) flush();
}
window.onhashchange = route;
async function start() { await status(); try { await refresh(); } catch(e) { message(e.message,true); } route(); }
start();
setInterval(() => { if (editing || Date.now() - lastPoll >= 1000) status(); }, 200); // faster for the LED meter
</script></body></html>)HTML";
