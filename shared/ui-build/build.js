// Assembles an app's index.html: the new markup and rendering, plus the
// proven plumbing carried over from the inherited UI (bridge, sheets,
// dropdowns, knobs, Learn storage/import/stems/transport, click, test gate).
//   node build.js [top|low]      (default: top)
const fs = require('fs'), path = require('path');
const KEY = process.argv[2] || 'top';
const P = require('./products')[KEY];
if (!P) throw new Error('unknown product ' + KEY);
const UI = P.ui;
const HERE = __dirname;
const old = fs.readFileSync(path.join(HERE, 'old-index.html'), 'utf8');
const part = n => fs.readFileSync(path.join(HERE, n), 'utf8');

function slice(from, to) {
  const a = old.indexOf(from); if (a < 0) throw new Error('marker missing: ' + from);
  const b = old.indexOf(to, a); if (b < 0) throw new Error('end marker missing: ' + to);
  return old.slice(a, b);
}
function edit(src, pairs, label) {
  for (const [a, b] of pairs) {
    if (a instanceof RegExp) { if (!a.test(src)) throw new Error(label + ': pattern not found ' + a); src = src.replace(a, b); continue; }
    const i = src.indexOf(a); if (i < 0) throw new Error(label + ': not found: ' + a.slice(0, 90));
    src = src.slice(0, i) + b + src.slice(i + a.length);
  }
  return src;
}
function cut(src, from, to, label) {
  const a = src.indexOf(from); if (a < 0) throw new Error(label + ': cut start missing ' + from.slice(0, 60));
  const b = src.indexOf(to, a); if (b < 0) throw new Error(label + ': cut end missing ' + to.slice(0, 60));
  return src.slice(0, a) + src.slice(b);
}

//---------------------------------------------------------------- K1 core
let k1 = slice('/* ═════════════════════════════ BRIDGE', '/* ═════════════════════════════ PEDALS');
k1 = edit(k1, [
  ["let S = null, view = 'stage';", "let S = null, view = 'rig';"],
  ["let lib = {tab:'presets', id:null};", "let lib = {tab:'presets', id:null, page:0, q:''};"],
  ["d.innerHTML='<svg viewBox=\"0 0 64 64\">'+knobSVG(32,32,22,norm(p),opts.col||'#7FB0FF')+'</svg>';",
   "d.innerHTML='<svg viewBox=\"0 0 64 64\">'+knobSVG(32,32,22,norm(p),opts.col||'#e3a447')+'</svg>';"],
  ['const ch=p.choices||[];\n    if(ch.length && ch.length<=4){', 'const ch=p.choices||[];\n    if(ch.length && ch.length<=4){'],
], 'K1');

//---------------------------------------------------------------- K2 preset menus
const k2 = slice('/* ═════════════════════════════ PRESET MENU', '/* ═════════════════════════════ STATE / TICK');

//---------------------------------------------------------------- K3 learn core
let k3 = slice('const NN_SHARP=', '/*──────────────── analysis lookups ────────────────*/');
k3 = edit(k3, [
  ["  source:'song', inst:'bass', strings:4,", "  source:'song', inst:'" + P.inst + "', strings:" + P.strings + ", show:'me', detail:'bars', pitch:0, countIn:false, click:false, side:'stems',"],
  ["localStorage.getItem('le.learn')", "localStorage.getItem('" + P.prefix + ".learn.v2')"],
  ["localStorage.setItem('le.learn', JSON.stringify({source:L.source,inst:L.inst,strings:L.strings,key:L.key,zoom:L.zoom,lessonId:L.lesson&&L.lesson.id}))",
   "localStorage.setItem('" + P.prefix + ".learn.v2', JSON.stringify({source:L.source,strings:L.strings,key:L.key,show:L.show,detail:L.detail,countIn:L.countIn,click:L.click,lessonId:L.lesson&&L.lesson.id}))"],
  [`    if(SP.lesson!==x.id) SP.unload();
    v.muted=false;
    cmd('stem',{action:'has',id:x.id}).then(has=>{
      if(has&&L.lessonId===x.id) SP.load(x.id).then(()=>refreshStemBar());
      else refreshStemBar();
    });`,
   `    if(SP.lesson!==x.id) SP.unload();
    v.muted=false;
    L.pitch=0;
    // Separated: the stems play. Not separated: the song itself plays through
    // the same worklet, so speed, pitch, the click and the count-in still work.
    cmd('stem',{action:'has',id:x.id}).then(has=>{
      if(L.lessonId!==x.id) return;
      if(has) SP.load(x.id).then(()=>refreshStemBar());
      else if(x.blob) SP.loadMix(x.id,x.blob).then(()=>refreshStemBar());
      else refreshStemBar();
    });`],
  ["{k:'other',n:'Other'}", "{k:'other',n:'Guitar & keys'}"],
  ["  ctx:null, node:null, ready:false, loading:false, lesson:null,", "  ctx:null, node:null, ready:false, loading:false, lesson:null, mode:'',"],
  ["  gains(){\n", "  gains(){\n    if(this.mode==='mix') return [1];\n"],
  ["    if(this.lesson===lessonId&&this.ready) return true;\n    this.loading=true; this.ready=false; this.lesson=lessonId;",
   "    if(this.lesson===lessonId&&this.ready&&this.mode==='stems') return true;\n    this.loading=true; this.ready=false; this.lesson=lessonId; this.mode='stems';"],
  ["      this.node.port.postMessage({type:'rate',rate:L.rate});\n      progress(null);",
   "      this.node.port.postMessage({type:'rate',rate:L.rate});\n      this.sync();\n      progress(null);"],
  ["  unload(){\n    if(this.node){ this.node.port.postMessage({type:'clear'}); }\n    this.ready=false;",
   `  /** The whole song as ONE stem, for a song that has not been separated. */
  async loadMix(lessonId,blob){
    this.loading=true; this.ready=false; this.lesson=lessonId; this.mode='mix';
    try{
      await this.attach();
      progress('Loading the song',0.2);
      const ab=await this.ctx.decodeAudioData(await blob.arrayBuffer());
      if(L.lessonId!==lessonId){ progress(null); return false; }
      const st=[[ab.getChannelData(0), ab.numberOfChannels>1?ab.getChannelData(1):ab.getChannelData(0)]];
      const move=[st[0][0].buffer]; if(st[0][1].buffer!==st[0][0].buffer) move.push(st[0][1].buffer);
      this.node.port.postMessage({type:'load',stems:st},move);
      this.dur=ab.duration; this.ready=true;
      this.push(); this.node.port.postMessage({type:'rate',rate:L.rate}); this.sync();
      progress(null);
      return true;
    }catch(err){ this.ready=false; this.lesson=null; this.mode=''; progress(null); return false; }
    finally{ this.loading=false; }
  },
  /** The song's beat, the click and the pitch, after any load. */
  sync(){
    if(!this.node) return;
    const a=grid()||{};
    this.node.port.postMessage({type:'beats',beats:a.beats||[],downbeat:a.downbeat||0,tempo:a.tempo||0,accents:(a.inBar||[]).map(x=>x===0)});
    this.node.port.postMessage({type:'click',on:!!L.click,level:0.45});
    this.node.port.postMessage({type:'pitch',semitones:L.pitch||0});
  },
  setPitch(st){ if(this.node) this.node.port.postMessage({type:'pitch',semitones:st}); },
  setClick(on){ if(this.node) this.node.port.postMessage({type:'click',on:!!on,level:0.45}); },
  unload(){
    if(this.node){ this.node.port.postMessage({type:'clear'}); }
    this.mode='';
    this.ready=false;`],
  ["  play(at){ if(!this.ready) return; this.ctx.resume(); this.node.port.postMessage({type:'play',at:at==null?this.pos:at}); this.playing=true; },",
   "  play(at,countIn){ if(!this.ready) return; this.ctx.resume(); this.node.port.postMessage({type:'play',at:at==null?this.pos:at,countIn:countIn||0}); this.playing=true; },"],
  ["            if(T.sp()){ SP.playing?SP.pause():SP.play(); syncVideoToStems(); return; }",
   "            if(T.sp()){ if(SP.playing) SP.pause(); else SP.play(null,L.countIn&&grid()&&grid().tempo?4:0); syncVideoToStems(); return; }"],
  ["SP.load(L.lessonId).then(ok=>{ if(ok){ toast('Stems ready'); analyseBassStem(); } refreshStemBar(); });",
   "SP.load(L.lessonId).then(ok=>{ if(ok){ toast('Stems ready — Mute guitar to play your part'); renderLearnLayout(); } refreshStemBar(); });"],
  ["$('#tZoomIn').onclick=()=>{ L.zoom=clamp(L.zoom/1.4,3,40); saveLearnPrefs(); };\n", ""],
  ["$('#tZoomOut').onclick=()=>{ L.zoom=clamp(L.zoom*1.4,3,40); saveLearnPrefs(); };\n", ""],
  [/\$\('#lInst'\)\.addEventListener\([^\n]*\n/, ''],
  // A song opened while another's stems are still loading: the older load
  // must not finish over the newer one (it left the old song's length behind).
  ["this.loading=true; this.ready=false; this.lesson=lessonId; this.mode='stems';\n    try{\n      await this.attach();\n      const bufs=[];\n      for(let i=0;i<STEMS.length;i++){\n",
   "this.loading=true; this.ready=false; this.lesson=lessonId; this.mode='stems';\n    const gen=this.gen=(this.gen||0)+1, stale=()=>gen!==this.gen;\n    try{\n      await this.attach();\n      const bufs=[];\n      for(let i=0;i<STEMS.length;i++){\n        if(stale()) return false;              // another song was opened meanwhile\n"],
  ["      const stems=bufs.map(b=>[b.getChannelData(0)",
   "      if(stale()) return false;\n      const stems=bufs.map(b=>[b.getChannelData(0)"],
  ["    }catch(err){ this.ready=false; this.lesson=null; progress(null); toast('The stems could not be loaded. Separate the song again.'); return false; }\n    finally{ this.loading=false; }",
   "    }catch(err){ if(stale()) return false; this.ready=false; this.lesson=null; progress(null); toast('The stems could not be loaded. Separate the song again.'); return false; }\n    finally{ if(!stale()) this.loading=false; }"],
  ["    this.loading=true; this.ready=false; this.lesson=lessonId; this.mode='mix';",
   "    this.loading=true; this.ready=false; this.lesson=lessonId; this.mode='mix'; this.gen=(this.gen||0)+1;"],
], 'K3');
k3 = cut(k3, 'function renderLearnLayout(){', "$('#lSource').addEventListener", 'K3 layout');
k3 = cut(k3, 'function renderStemTiles(){', 'function stemPoll(){', 'K3 stem tiles');
k3 = cut(k3, '/* The bass line heard in a full mix', '/** 44.1 kHz stereo', 'K3 bass stem');

//---------------------------------------------------------------- K6 the click
let k6 = slice('/*──────────────── the click ────────────────', '/*──────────────── audio device ────────────────');
k6 = edit(k6, [
  ["const key=JSON.stringify([ck.output,ck.beats,ck.eighths,ck.accent,ck.separate,ck.standalone,a.outputPairs,a.clickPair,on]);",
   "const key=JSON.stringify([ck.output,ck.beats,ck.eighths,ck.accent,ck.separate,ck.standalone,a.outputPairs,a.clickCh,a.outCh,a.outMono,on]);"],
  ["  const none=el('option','','None (outputs 1-2 only)'); none.value=0; P.appendChild(none);\n  pairs.forEach((p,i)=>{ if(i===0) return; const o=el('option'); o.textContent='Outputs '+p; o.value=i; if(i===a.clickPair) o.selected=true; P.appendChild(o); });\n  if(!a.clickPair) none.selected=true;",
   "  // Any pair the main mix is not using.\n  const none=el('option','','None'); none.value=-1; P.appendChild(none);\n  const usesMain=i=>a.outMono?(a.outCh===2*i||a.outCh===2*i+1):(a.outCh===2*i);\n  pairs.forEach((p,i)=>{ if(usesMain(i)) return; const o=el('option'); o.textContent='Outputs '+p; o.value=2*i; if(2*i===a.clickCh) o.selected=true; P.appendChild(o); });\n  if(a.clickCh==null||a.clickCh<0) none.selected=true;"],
  ["$('#clkPair').onchange=e=>{ applyAudio({clickPair:+e.target.value}); clkKey=''; };",
   "$('#clkPair').onchange=e=>{ applyAudio({clickCh:+e.target.value}); clkKey=''; };"],
  // Time signatures and the loop length. The dots show the bar: the "1" in the
  // accent colour, and in x/8 the start of each group of the felt beat.
  ["  if(D.children.length!==n){ D.innerHTML=''; for(let i=0;i<n;i++) D.appendChild(el('i',i===0?'one':'')); }",
   "  const unit=ck.unit||4, grpOf=i=>unit===8&&(n%3===0&&n>3?i%3===0:n===5?(i===0||i===3):n===7?(i===0||i===2||i===4):i%2===0);\n  if(D.children.length!==n||D.dataset.u!==String(unit)){ D.innerHTML=''; D.dataset.u=unit; for(let i=0;i<n;i++) D.appendChild(el('i',i===0?'one':(grpOf(i)?'grp':''))); }"],
  ["  [...$('#clkCount').children].forEach(b=>b.classList.toggle('on',+b.dataset.n===(ck.countIn||0)));",
   "  [...$('#clkCount').children].forEach(b=>b.classList.toggle('on',+b.dataset.n===(ck.countIn||0)));\n  [...$('#loopBars').children].forEach(b=>b.classList.toggle('on',+b.dataset.n===(ck.loopBars||0)));"],
  ["const key=JSON.stringify([ck.output,ck.beats,ck.eighths,ck.accent,ck.separate,ck.standalone,a.outputPairs,a.clickCh,a.outCh,a.outMono,on]);",
   "const key=JSON.stringify([ck.output,ck.beats,ck.unit,ck.loopBars,ck.eighths,ck.accent,ck.separate,ck.standalone,a.outputPairs,a.clickCh,a.outCh,a.outMono,on]);"],
  ["  $('#clkTag').textContent=on?(ck.beats||4)+'/4':'Off';",
   "  $('#clkTag').textContent=on?(ck.beats||4)+'/'+(ck.unit||4):'Off';"],
  ["  $('#clkBeatsSel').innerHTML=''; for(let i=1;i<=12;i++){ const o=el('option','',String(i)); o.value=i; if(i===(ck.beats||4)) o.selected=true; $('#clkBeatsSel').appendChild(o); }",
   "  const SIGS=['2/4','3/4','4/4','5/4','6/4','7/4','5/8','6/8','7/8','9/8','12/8'], cur=(ck.beats||4)+'/'+(ck.unit||4);\n  ['#clkBeatsSel','#loopSig'].forEach(sel=>{ const S2=$(sel); if(!S2) return; S2.innerHTML='';\n    SIGS.concat(SIGS.includes(cur)?[]:[cur]).forEach(v=>{ const o=el('option','',esc(v)); o.value=v; if(v===cur) o.selected=true; S2.appendChild(o); }); });\n  $('#clkLoopBars').value=String(ck.loopBars||0);"],
  ["$('#clkBeatsSel').onchange=e=>setClick({beats:+e.target.value});",
   "const setSig=v=>{ const [b,u]=String(v).split('/').map(Number); if(b>0) setClick({beats:b,unit:u===8?8:4}); };\n$('#clkBeatsSel').onchange=e=>setSig(e.target.value);\n$('#loopSig').onchange=e=>setSig(e.target.value);\n$('#loopBars').onclick=e=>{ const b=e.target.closest('button'); if(b) setClick({loopBars:+b.dataset.n}); };\n$('#clkLoopBars').onchange=e=>setClick({loopBars:+e.target.value});"],
], 'K6');

//---------------------------------------------------------------- K8 test gate
const k8 = slice('/*──────────────── test-build gate ────────────────', '/*──────────────── about ────────────────*/');

//---------------------------------------------------------------- assemble
const defs = `
<!-- Shared SVG materials: the cream pedal-knob cap and the black amp-knob cap. -->
<svg width="0" height="0" style="position:absolute" aria-hidden="true"><defs>
  <radialGradient id="capg" cx=".38" cy=".3" r=".85"><stop stop-color="#fbf5e8"/><stop offset="1" stop-color="#bfb197"/></radialGradient>
  <radialGradient id="akg" cx=".38" cy=".32" r=".8"><stop stop-color="#4a423b"/><stop offset=".55" stop-color="#1a1614"/><stop offset="1" stop-color="#0b0908"/></radialGradient>
  <radialGradient id="kbody" cx=".4" cy=".35" r=".75"><stop stop-color="#3a3530"/><stop offset=".55" stop-color="#171412"/><stop offset="1" stop-color="#080707"/></radialGradient>
  <radialGradient id="kcap" cx=".42" cy=".38" r=".7"><stop stop-color="#4a443d"/><stop offset="1" stop-color="#161311"/></radialGradient>
</defs></svg>
`;
let html = part('part1-head.html') + part('part2-body.html') + defs + '<script>\n' +
  P.js + '\n' + k1 + '\n' + part('part3-rig.js') + '\n' + k2 + '\n' + part('part4-lib.js') + '\n' + part('part4b-edits.js') + '\n' +
  '/* ═════════════════════════════ LEARN ═════════════════════════════\n' +
  '   A song (audio or video) is imported, decoded by the web view, and analysed\n' +
  '   in a worker (analyzer.js): chords, key, notes, the beat and the bar. The\n' +
  '   result and the file are kept in IndexedDB, so a song reopens instantly.\n' +
  '   Playback goes through one AudioWorklet (stemplayer.js) whether or not the\n' +
  '   song has been separated, which is what gives every song speed, pitch, a\n' +
  '   click on its own beat and a count-in. YouTube is a native view laid over a\n' +
  '   placeholder; it gets the transport but no analysis. */\n' +
  k3 + '\n' + part('part5-learn.js') + '\n' + part('part5b-cover.js') + '\n' + part('part5c-timing.js') + '\n' + k6 + '\n' + part('part6-wire.js') + '\n' + k8 + '\n' + part('part7-tail.js') +
  '</script>\n</body>\n</html>\n';

// small fixes to the parts themselves
html = edit(html, [
  ["sub.textContent=(sg&&i>=0?(sg.name+' · scene '+'ABCDEF'[i]||String(i+1)):'PRESET')+(st.dirty?' · EDITED':'');",
   "sub.textContent=(sg&&i>=0?(sg.name.toUpperCase()+' · SCENE '+('ABCDEF'[i]||String(i+1))+(st.override?' · SET VERSION':'')):'PRESET')+(st.dirty?' · EDITED':'');"],
  ["setInterval(()=>{ if(view==='settings'&&spanel==='audio'&&!AUD.busy&&J) refreshAudio(); },2000);",
   "setInterval(()=>{ if(view==='settings'&&spanel==='audio'&&!AUD.busy&&!ddOpen&&J) refreshAudio(); },2000);"],
], 'final');
html = html.replace('#view-learn.on{grid-template-rows', '#view-learn{position:relative}\n#view-learn.on{grid-template-rows');

// On-screen text reads like a manual: short statements, the action named,
// no chat. Every entry must be found, so a reworded source fails the build.
const all = (s, a, b) => s.split(a).join(b);
const COPY = [
  ["' — it starts with an empty board'", "'. The board is empty.'"],
  ["' — add presets with +'", "'. Add presets with +.'"],
  ["' — you can keep playing'", "'. Playing continues.'"],
  ['MIDI Learn: move a control on your device', 'MIDI Learn: move a control on the device.'],
  ['Move a control on your MIDI device', 'MIDI Learn: move a control on the device.'],
  ['Pitch works on songs you import', 'Pitch is available for imported songs only.'],
  ['Reset — everything silenced', 'Reset. All sound stopped.'],
  ['Save to preset — every set that uses it', 'Save to preset (all sets that use it)'],
  ['Separate the song to mute your part and play along.', 'Separate the song into stems to mute the original part.'],
  ['Stems work on songs you import, not on YouTube.', 'Stems are available for imported songs, not for YouTube.'],
  ['That is not a Top End test key. Check that you pasted the whole key.', 'Not a valid Top End test key. Paste the complete key.'],
  ['That key was not issued for Top End. Check that you pasted the whole key.', 'This key was issued for another product. Paste a Top End key.'],
  ['The knobs keep their settings when you change model, the way a modelling amp should. Captures of your own amps (NAM) are not supported yet; cabinet impulse files are, under Cab › Your IR.',
   'Knob settings are kept when the model changes. Amp captures (NAM) are not supported. Cabinet impulse files load under Cab › Your IR.'],
  ['This computer’s clock is behind the date the key was issued. Correct the clock and try again.', 'The system clock is set before the issue date of the key. Correct the clock, then enter the key again.'],
  ['Feels like standing next to the amp', 'Imperceptible'],
  ['Fine for most playing', 'Suitable for most playing'],
  ['You may feel the delay — try a smaller buffer', 'Noticeable. Select a smaller buffer.'],
  ['Your part is muted. Play along.', 'Original part muted.'],
  ['Enter the test key you were sent. There is no sound until you do.', 'Enter the test key. Audio is muted until a valid key is entered.'],
  ['A backup is one zip of your presets, sets, loops and IRs. Songs you imported stay in Top End; separated stems are left out because they are rebuilt from the song.',
   'A backup is one zip file of presets, sets, loops and IRs. Imported songs stay in Top End. Separated stems are not included; they are rebuilt from the song.'],
  ['Choose a song you have opened in Learn. A separated song plays as stems, so any part can be muted.', 'Select a song imported in Learn. A separated song plays as stems; each stem can be muted.'],
  ['Choose a song from the list above.', 'Select a song from the list above.'],
  ['Global — scene changes never switch it off', 'Global. Scene changes do not affect it.'],
  ['If the instrument is heard twice — dry and processed — turn off direct monitoring on the interface (its hardware mix or front-panel switch).',
   'If the instrument is heard twice (dry and processed), turn off direct monitoring on the interface.'],
  ['Type the MIDI CC your controller sends', 'MIDI CC number sent by the controller'],
  ['Play a note — it is shown where your hand is on the neck', 'Play a note. It is shown at the position of the hand.'],
  ["'No beat found for this song'", "'No beat detected. Enter the tempo manually.'"],
  ['Separation is busy with another song. Try again when it finishes.', 'Another song is being separated. Wait until it finishes.'],
  ['Separation could not start. Try again.', 'Separation could not start.'],
  ['The song could not be prepared. Try again.', 'The song could not be prepared.'],
  ['Stems ready — Mute guitar to play your part', 'Stems ready. Mute guitar removes the original part.'],
  ['The count-in needs the song’s beat — import the song to find it', 'Count-in requires a detected beat. Import the song as audio.'],
  ['This file is too large to keep. Import it again next time to play it.', 'File too large to store. It is available until the app closes.'],
  ["'This lesson has no audio to separate'", "'This song has no audio to separate.'"],
  ['This song plays from its video, which cannot change key. Re-import it as audio.', 'Pitch is not available for video. Import the song as audio.'],
  ['Set Punch In first, then Punch Out later in the song.', 'Set Punch In before Punch Out.'],
  ['The device would not take that setting. Choose another.', 'The device rejected this setting.'],
  ["'Nothing to export yet'", "'Nothing to export.'"],
  ["'The backup could not be written'", "'Backup failed.'"],
  ["'Could not export'", "'Export failed.'"],
  ['No takes yet. Press record to play over the song.', 'No takes. Press Record to record over the song.'],
];
for (const [a, b] of COPY) { if (!html.includes(a)) throw new Error('copy not found: ' + a); html = all(html, a, b); }

// the product: wording, palette, theme
for (const [a, b] of P.text) { if (!html.includes(a)) throw new Error(KEY + ' text not found: ' + a); html = all(html, a, b); }
for (const [a, b] of P.colours) html = all(html, a, b);
if (P.css) { const i = html.lastIndexOf('</style>', html.indexOf('</head>')); html = html.slice(0, i) + P.css + html.slice(i); }
fs.writeFileSync(path.join(UI, 'index.html'), html);
console.log('index.html written:', html.length, 'bytes,', html.split('\n').length, 'lines');
