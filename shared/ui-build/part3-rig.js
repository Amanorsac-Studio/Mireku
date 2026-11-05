/* ═════════════════════════════ PEDALS ═════════════════════════════
   Every pedal is drawn as a real enclosure, in layers: painted metal with its
   own finish (powder coat, hammertone, brushed, sparkle), our own printed
   artwork, then the hardware - knobs you turn right on the board, the LED, the
   footswitch, jacks and screws. Colour is the pedal's identity, never a state
   (state is the LED and the dimmed body). */
const PAINT={
  noiseGate:'#5d636b', clearDrive:'#d8a64a', tubeOd:'#6f9f5a', fuzz:'#9c4a3f', boost:'#c9c3b4',
  optoComp:'#7fa88a', fetComp:'#3e6a7a', graphicEq:'#34383f', paraEq:'#4b5563',
  envFilter:'#a8bf4a', octaver:'#6a5d8f', wah:'#2e2b28',
  chorus:'#5f86a6', flanger:'#7c6fa8', phaser:'#d07a3a', tremolo:'#a8564e',
  analogDelay:'#4f8f8a', digitalDelay:'#3f7fa0', roomVerb:'#d9cfbd', ambientVerb:'#8ea6b4', bow:'#8a3a1f', nam:'#22394a'};
const CATC={Utility:'#5d636b',Drive:'#d8a64a',Dynamics:'#7fa88a',Filter:'#a8bf4a',Modulation:'#5f86a6',Time:'#4f8f8a',Amp:'#22394a'};
const paintOf=b=>PAINT[b.type||b.id]||CATC[b.category]||'#5d636b';
/** The metal's finish, which gives each enclosure its texture. */
const FINISH={noiseGate:'brushed', clearDrive:'sparkle', tubeOd:'powder', fuzz:'hammer', boost:'brushed',
  optoComp:'powder', fetComp:'hammer', graphicEq:'brushed', paraEq:'brushed', envFilter:'sparkle', octaver:'sparkle', wah:'hammer',
  chorus:'sparkle', flanger:'sparkle', phaser:'powder', tremolo:'hammer', analogDelay:'hammer', digitalDelay:'brushed', roomVerb:'powder', ambientVerb:'sparkle', bow:'wood', nam:'hammer'};
const CATFIN={Utility:'brushed',Drive:'powder',Dynamics:'powder',Filter:'sparkle',Modulation:'sparkle',Time:'hammer',Amp:'hammer'};
const finishOf=b=>FINISH[b.type||b.id]||CATFIN[b.category]||'powder';
/** Our own screen-printed marks, one per family, in the pedal's ink. */
const ART={
  Utility:'<path d="M2 12h10M18 12h6M30 12h14M50 12h6" stroke-width="2.4"/>',
  Drive:'<path d="M2 16l7-9 6 7 8-11 6 10 8-8 6 9 7-6" stroke-width="2.2"/>',
  Dynamics:'<path d="M2 6h14l6 6-6 6H2M58 6H44l-6 6 6 6h14" stroke-width="2"/>',
  Filter:'<path d="M2 18C14 18 18 4 26 4s10 14 30 14" stroke-width="2.2"/>',
  Modulation:'<path d="M2 12c5-8 9-8 14 0s9 8 14 0 9-8 14 0 9 8 14 0" stroke-width="2.2"/>',
  Time:'<path d="M8 4v16M20 6v12M30 8v8M38 9.5v5M45 11v2" stroke-width="2.6"/>'};
/** A pedal with a mark of its own rather than its family's: the Bow wears a violin's f-holes,
    Your Amp a waveform going in and a learned one coming out. */
const ARTBY={bow:'<path d="M17 3c-3 0-3 4 0 5s3 4 0 9-2 5 1 4M43 3c3 0 3 4 0 5s-3 4 0 9 2 5-1 4" stroke-width="2"/><circle cx="17" cy="3" r="1.6"/><circle cx="43" cy="3" r="1.6"/>',
  nam:'<path d="M2 12c3-6 5-6 8 0s5 6 8 0" stroke-width="2"/><circle cx="26" cy="6" r="1.8"/><circle cx="26" cy="18" r="1.8"/><circle cx="34" cy="12" r="1.8"/><path d="M19 12l5-5M19 12l5 5M28 7l4 4M28 17l4-4" stroke-width="1.3"/><path d="M38 12h3l2-7 3 14 3-11 2 4h7" stroke-width="2"/>'};
const DESC={noiseGate:'Silences hum and hiss between notes. It works best first on the board.',
  clearDrive:'A transparent overdrive that keeps your guitar sounding like itself.',
  tubeOd:'A mid-pushed tube-style overdrive. Tightens the amp for leads.',
  fuzz:'A thick, sustaining fuzz.', boost:'A clean volume push into the amp.',
  optoComp:'A smooth, slow compressor that evens out clean parts.', fetComp:'A fast, punchy compressor for tight picking.',
  graphicEq:'Shape your tone band by band.', paraEq:'Precise tone shaping with a sweepable mid.',
  envFilter:'An auto-wah that follows how hard you play.', octaver:'Adds octaves below and above your note.',
  wah:'A classic wah. Map an expression pedal to Position.',
  chorus:'A shimmering, doubled sound.', flanger:'A jet-like sweep.', phaser:'A swirling phase sweep.', tremolo:'A pulsing volume.',
  analogDelay:'Warm, darkening repeats.', digitalDelay:'Clean, precise repeats.', roomVerb:'A small, natural room.',
  ambientVerb:'A big ambient reverb for pads and swells.',
  bow:'Your guitar as a bowed string: every note swells in like a bow, holds, and takes a player’s vibrato. Violin, viola or cello. Section turns chords into a string section; Solo follows a single line and plays it on the string.',
  nam:'Your own amp. Load a Neural Amp Modeler capture (.nam) of a real amp and play through it. For a capture of an amp alone, leave the cab on; for a whole rig, turn the cab off too.'};
const CATDESC={Utility:'A utility pedal.',Drive:'A drive pedal, in front of the amp.',Dynamics:'Dynamics and tone shaping.',
  Filter:'A filter or pitch pedal.',Modulation:'Modulation, after the cab.',Time:'Delay or reverb, after the cab.',Amp:'An amp, from a capture.'};
/** Dark or light lettering, whichever reads on the paint. */
function inkOn(hex){ const n=parseInt(hex.slice(1),16), r=n>>16&255, g=n>>8&255, b=n&255;
  return (0.2126*r+0.7152*g+0.0722*b)>140?'#1c1408':'#f6efe4'; }
/* Faceplates have room for short names; long ones print the way a real pedal would. */
const SHORT={THRESHOLD:'THRESH',SENSITIVITY:'SENS',RESONANCE:'RESO',FEEDBACK:'FDBK',RELEASE:'REL',ATTACK:'ATK',
  SUSTAIN:'SUS',PRESENCE:'PRES',DISTANCE:'DIST','MID FREQ':'MID F','LOW CUT':'LO CUT','HIGH CUT':'HI CUT',
  DIRECTION:'SWEEP',DIVISION:'DIV','WOW/FLUTTER':'WOW','PING-PONG':'P-PONG',POSITION:'POS'};
const short=s=>{ const u=String(s||'').toUpperCase(); return (SHORT[u]||u).slice(0,7); };
const knobAngle=t=>(-135+270*t).toFixed(1)+'deg';
const faceKnobs=b=>(b.params||[]).filter(p=>p.kind==='float'&&p.id!=='capture').slice(0,4);
/** Your Amp: the capture a pedal has chosen, by the key the engine gave it. */
const captureOf=b=>{ const k=Math.round(((b.params||[]).find(p=>p.id==='capture')||{value:0}).value);
  return k>0?((S.captures||[]).find(c=>c.key===k)||{key:k,name:'',missing:true}):null; };

/** The enclosure and its hardware. Knobs are live only on the board. */
function pedalFace(b,cls){
  const paint=paintOf(b), ink=inkOn(paint), knobs=faceKnobs(b);
  const u=el('div','ped fin-'+finishOf(b)+(knobs.length>3?' w':'')+(ink==='#1c1408'?' lite':'')+(cls?' '+cls:''));
  u.style.setProperty('--c',paint); u.style.setProperty('--t',ink);
  u.innerHTML='<i class="tex"></i><i class="sc"></i><i class="sc"></i><i class="sc"></i><i class="sc"></i><i class="jk l"></i><i class="jk r"></i>'+
    '<span class="pk">'+knobs.map(p=>'<span class="pkn"><span class="kb"><i class="cap" style="--a:'+knobAngle(norm(p))+'"></i><i class="gl"></i></span><b>'+esc(short(p.label))+'</b></span>').join('')+'</span>'+
    '<span class="pled"></span><svg class="art" viewBox="0 0 60 24" fill="none" stroke="currentColor" stroke-linecap="round" stroke-linejoin="round">'+(ARTBY[b.type||b.id]||ART[b.category]||ART.Utility)+'</svg>'+
    '<span class="pn">'+esc((b.type||b.id)==='nam'?((captureOf(b)||{}).name||b.name):b.name)+'</span><span class="pft"></span>';
  return u;
}
function pedalTile(b,chain){
  const u=pedalFace(b,(b.bypassed?'off':'')+(sel.kind==='pedal'&&sel.chain===chain&&sel.index===b.index?' sel':''));
  u.setAttribute('role','button'); u.setAttribute('aria-label',b.name+(b.bypassed?', off':', on'));
  u.dataset.chain=chain; u.dataset.index=b.index;
  // The knobs turn on the pedal itself: drag up or down, double-click to reset,
  // hold or right-click for MIDI Learn.
  [...u.querySelectorAll('.pkn')].forEach((k,i)=>{ const p=faceKnobs(b)[i];
    k.setAttribute('aria-label',p.label);
    bindKnob(k,p,()=>k.querySelector('.cap').style.setProperty('--a',knobAngle(norm(p)))); });
  const sw=el('button','psw'); sw.setAttribute('aria-label',(b.bypassed?'Turn on ':'Turn off ')+b.name);
  sw.onclick=e=>{ e.stopPropagation(); cmd('set',{path:b.bypassPath,value:b.bypassed?0:1}); };
  sw.addEventListener('pointerdown',e=>e.stopPropagation());
  u.querySelector('.pft').appendChild(sw);
  u.onclick=()=>{ sel={kind:'pedal',chain,index:b.index}; openPedalSheet(chain,b.index); };
  holdMenu(u,(x,y)=>pedalMenu(chain,b,x,y));
  dragFrom(u,{chain,index:b.index,name:b.name});
  return u;
}
/** Long-press (touch) or right-click (mouse) opens a menu; a plain tap does not. */
function holdMenu(node,fn){
  let t=0, x=0, y=0, fired=false;
  node.addEventListener('pointerdown',e=>{ if(e.button===2) return; fired=false; x=e.clientX; y=e.clientY;
    t=setTimeout(()=>{ fired=true; fn(x,y); },550); });
  const clear=()=>clearTimeout(t);
  node.addEventListener('pointerup',clear); node.addEventListener('pointercancel',clear);
  node.addEventListener('pointermove',e=>{ if(Math.abs(e.clientX-x)>8||Math.abs(e.clientY-y)>8) clear(); });
  node.addEventListener('click',e=>{ if(fired){ e.stopImmediatePropagation(); e.preventDefault(); fired=false; } },true);
  node.addEventListener('contextmenu',e=>{ e.preventDefault(); fn(e.clientX,e.clientY); });
}
function pedalMenu(chain,b,x,y){
  const list=chain==='post'?S.post:S.pre;
  openMenu(x,y,[
    {label:'Edit '+b.name, onClick:()=>openPedalSheet(chain,b.index)},
    {label:'Move left', onClick:()=>{ if(b.index>0) cmd('moveBlock',{chain,from:b.index,to:b.index-1}); }},
    {label:'Move right', onClick:()=>{ if(b.index<list.length-1) cmd('moveBlock',{chain,from:b.index,to:b.index+1}); }},
    {label:'Replace…', onClick:()=>cmd('removeBlock',{chain,index:b.index}).then(()=>openCatalog(chain,b.index))},
    {label:'Remove', onClick:()=>cmd('removeBlock',{chain,index:b.index}).then(()=>{ sel={kind:'amp'}; })}]);
}
function openPedalSheet(chain,index){
  const list=chain==='post'?S.post:S.pre, b=(list||[]).find(x=>x.index===index);
  if(!b) return;
  const body=el('div');
  const top=el('div'); top.style.cssText='display:flex;align-items:center;gap:10px;margin-bottom:16px';
  const sw=el('span'); sw.style.cssText='width:28px;height:40px;border-radius:6px;flex:none;background:'+paintOf(b);
  top.appendChild(sw);
  top.appendChild(el('span','tag',esc(b.category)+(chain==='post'?' · after the cab':' · in front of the amp')));
  body.appendChild(top);
  if(b.type==='nam') body.appendChild(capturePicker(b));
  const box=el('div','ctls'); (b.params||[]).filter(p=>p.id!=='capture').forEach(p=>box.appendChild(control(p,{col:'#e3a447'}))); body.appendChild(box);
  openSheet({title:b.name, body, wide:(b.params||[]).length>6, actions:[
    {label:'Remove', kind:'danger', onClick:()=>{ closeSheet(); cmd('removeBlock',{chain,index:b.index}); }},
    {label:'◀ Move', onClick:()=>{ if(b.index>0){ closeSheet(); cmd('moveBlock',{chain,from:b.index,to:b.index-1}); } }},
    {label:'Move ▶', onClick:()=>{ if(b.index<list.length-1){ closeSheet(); cmd('moveBlock',{chain,from:b.index,to:b.index+1}); } }},
    {label:b.bypassed?'Turn on':'Turn off', onClick:()=>{ closeSheet(); cmd('set',{path:b.bypassPath,value:b.bypassed?0:1}); }},
    {label:'Done', kind:'primary', onClick:closeSheet}]});
}

/** Your Amp's capture row: which .nam it plays, add one, or open the folder. */
function capturePicker(b){
  const row=el('div','capick'), cur=captureOf(b), path=(b.params.find(p=>p.id==='capture')||{}).path;
  const s=el('select'); s.setAttribute('aria-label','Capture');
  const opt=(v,t)=>{ const o=el('option'); o.value=v; o.textContent=t; s.appendChild(o); return o; };
  opt(0,'No capture (plays straight through)');
  (S.captures||[]).forEach(c=>{ const o=opt(c.key,c.name); if(cur&&cur.key===c.key) o.selected=true; });
  if(cur&&cur.missing){ const o=opt(cur.key,'Missing capture'); o.selected=true; }
  s.onchange=()=>cmd('set',{path,value:+s.value});
  row.appendChild(s);
  const add=el('button','btn','Add a capture…'); add.onclick=()=>chooseCapture(c=>{ if(c) cmd('set',{path,value:c.key}).then(()=>openPedalSheet(b.chain||chainOfPath(path),b.index)); });
  const fo=el('button','btn','Open folder'); fo.onclick=()=>cmd('showCaptures');
  row.appendChild(add); row.appendChild(fo);
  const wrap=el('div'); wrap.appendChild(row);
  wrap.appendChild(el('p','help',cur&&cur.missing?'The capture this preset uses is not in your Amp Captures folder. Add it again, or choose another.'
    :(S.captures||[]).length?'Captures live in Documents › Amanorsac Studio › '+esc(PRODUCT.name)+' › Amp Captures. Drop a .nam file on the app to add one.'
    :'Add a .nam capture of your own amp. It is checked, and timed on this computer, before it is used.'));
  return wrap;
}
const chainOfPath=p=>String(p||'').startsWith('post.')?'post':'pre';

/* ═════════════════════════════ AMPS ═════════════════════════════
   Each model wears its own faceplate. These are the colours of the plate, the
   piping round it, the knobs and the grille cloth — the look a player picks a
   sound by, the same as on a stage. */
const WEAVE=(a,b)=>'repeating-linear-gradient(45deg,'+a+' 0 1.5px,'+b+' 1.5px 3px)';
const AMPS=PRODUCT.amps||[
  {name:'Plexi 68',      sub:'BRIT · EL34 · 50 W', p:'linear-gradient(#ede3cd,#d8c9aa)', pp:'#b8954f', k:'#15110e', g:WEAVE('#6b4a33','#3e2a1e'), ink:'#2a2018', logo:'#3a2a1c', subc:'#7a6650', onbg:'#2a2018', onfg:'#ede3cd'},
  {name:'Black Panel',   sub:'US · 6L6 · CLEAN',   p:'linear-gradient(#232120,#141312)', pp:'#77746f', k:'#e9e5dc', g:WEAVE('#9a9488','#6f6a62'), ink:'#e9e5dc', logo:'#f2ece4', subc:'#a9a296', onbg:'#e9e5dc', onfg:'#141312'},
  {name:'Tweed 57',      sub:'US · 6V6 · COMBO',   p:'linear-gradient(#cdb37f,#b39760)', pp:'#6d5634', k:'#2a1c0e', g:WEAVE('#5a3d25','#3a2617'), ink:'#2a1c0e', logo:'#3a2610', subc:'#5d4523', onbg:'#2a1c0e', onfg:'#e8d6a8'},
  {name:'Top Boost 30',  sub:'BRIT · EL84 · CHIME',p:'linear-gradient(#8a2e2b,#6d2321)', pp:'#c9a35d', k:'#e9dcc4', g:WEAVE('#4a3b32','#2f2520'), ink:'#f0e2c4', logo:'#f6ead0', subc:'#dcc79d', onbg:'#f0e2c4', onfg:'#6d2321'},
  {name:'Modern Hi-Gain',sub:'METAL · 6L6 · 3 STAGE',p:'linear-gradient(#3b3d40,#26282a)', pp:'#9aa0a6', k:'#d7dadd', g:WEAVE('#2a2a2a','#151515'), ink:'#d7dadd', logo:'#eef0f2', subc:'#9aa0a6', onbg:'#d7dadd', onfg:'#26282a'},
  {name:'Solid Clean',   sub:'SOLID STATE · HI-FI', p:'linear-gradient(#e7e5e0,#c9c6bf)', pp:'#8a8780', k:'#1a1a1a', g:WEAVE('#3a3a3a','#1f1f1f'), ink:'#1d1d1d', logo:'#1d1d1d', subc:'#5d5b57', onbg:'#1d1d1d', onfg:'#e7e5e0'}];
const CABS=PRODUCT.cabs||[
  {name:'1×12 Open', sub:'Airy, open back'},
  {name:'2×12 Alnico', sub:'Warm, even'},
  {name:'4×12 V30', sub:'Tight, cutting'},
  {name:'Your IR', sub:'An impulse file'}];
const ampModel=()=>Math.round((pick(S.amp,'model')||{value:0}).value);
const cabModel=()=>Math.round((pick(S.cab,'model')||{value:1}).value);
function styleAmp(node,st){
  node.style.setProperty('--p',st.p); node.style.setProperty('--pp',st.pp); node.style.setProperty('--k',st.k);
  node.style.setProperty('--g',st.g); node.style.setProperty('--ink2',st.ink); node.style.setProperty('--logo',st.logo);
  node.style.setProperty('--sub',st.subc); node.style.setProperty('--onbg',st.onbg); node.style.setProperty('--onfg',st.onfg);
}
function ampMini(){
  const st=AMPS[ampModel()]||AMPS[0];
  const u=el('button','ampmini'+(sel.kind==='amp'?' sel':''));
  styleAmp(u,st);
  u.innerHTML='<div class="pl"><i></i><i></i><i></i><i></i></div><div class="gr"></div><b>'+esc(st.name)+'</b>';
  u.setAttribute('aria-label','Amp: '+st.name+'. Tap to change model');
  u.onclick=openAmpModels;
  return u;
}
/** An amp knob: printed 0-10 scale, black cap, cream pointer. Drawn as an SVG whose pointer can be moved. */
function ampKnobSVG(t,ink){
  let s='<svg viewBox="0 0 100 100">';
  for(let i=0;i<=10;i++){ const a=(-225+270*i/10)*Math.PI/180, r0=i%5?41:38;
    s+='<line x1="'+(50+r0*Math.cos(a)).toFixed(1)+'" y1="'+(50+r0*Math.sin(a)).toFixed(1)+'" x2="'+(50+47*Math.cos(a)).toFixed(1)+'" y2="'+(50+47*Math.sin(a)).toFixed(1)+'" stroke="'+ink+'" stroke-opacity=".75" stroke-width="'+(i%5?1.6:2.4)+'" stroke-linecap="round"/>'; }
  const a=(-225+270*t)*Math.PI/180;
  s+='<circle cx="50" cy="53" r="32" fill="rgba(0,0,0,.25)"/><circle cx="50" cy="50" r="32" fill="url(#akg)"/>'+
     '<circle cx="50" cy="50" r="24" fill="none" stroke="rgba(255,255,255,.07)"/>'+
     '<line class="ptr" x1="'+(50+6*Math.cos(a)).toFixed(1)+'" y1="'+(50+6*Math.sin(a)).toFixed(1)+'" x2="'+(50+29*Math.cos(a)).toFixed(1)+'" y2="'+(50+29*Math.sin(a)).toFixed(1)+'" stroke="#efe6d2" stroke-width="4.5" stroke-linecap="round"/></svg>';
  return s;
}
/** An amp knob's readout: the printed 0-10 scale, or the real value for a knob that is not 0-100 (Mid Freq in Hz). */
const ampVal=p=>p.min===0&&p.max===100?(p.value/10).toFixed(1):fmt(p);
function renderAmp(){
  const box=$('#ampBox'); box.innerHTML='';
  const m=ampModel(), st=AMPS[m]||AMPS[0], vis=S.ampVisible||[];
  styleAmp(box,st);
  const panel=el('div','panel');
  // badge: the model name is the button that changes model
  const badge=el('div','badge');
  const mdl=el('button','mdl',esc(st.name)); mdl.setAttribute('aria-label','Amp model '+st.name+'. Tap to change'); mdl.onclick=openAmpModels;
  badge.appendChild(mdl); badge.appendChild(el('span','sub',esc(st.sub)));
  const ch=pick(S.amp,'channel');
  if(ch){ const c=el('div','chan'); c.setAttribute('role','radiogroup'); c.setAttribute('aria-label','Channel');
    ['CLEAN','CRUNCH','LEAD'].forEach((n,i)=>{ const b=el('button',Math.round(ch.value)===i?'on':'',n);
      b.onclick=()=>{ ch.value=i; send('amp.channel',i); [...c.children].forEach((x,j)=>x.classList.toggle('on',j===i)); }; c.appendChild(b); });
    badge.appendChild(c); }
  panel.appendChild(badge);
  // the six knobs, always in the same place
  const row=el('div','knobrow');
  PRODUCT.ampKnobs.forEach(id=>{
    const p=pick(S.amp,id); if(!p) return;
    const k=el('div','akn'); k.setAttribute('aria-label',p.label);
    const SH={Presence:'Pres',Treble:'Treb',Master:'Mstr','Mid Freq':'Mid F',Contour:'Cont'};
    k.innerHTML='<span class="l"><span class="lf">'+esc(p.label)+'</span><span class="ls">'+esc(SH[p.label]||p.label)+'</span></span>'+ampKnobSVG(norm(p),st.ink)+'<span class="v">'+ampVal(p)+'</span>';
    const redraw=()=>{ const t=norm(p), a=(-225+270*t)*Math.PI/180, ln=k.querySelector('.ptr');
      ln.setAttribute('x1',(50+6*Math.cos(a)).toFixed(1)); ln.setAttribute('y1',(50+6*Math.sin(a)).toFixed(1));
      ln.setAttribute('x2',(50+29*Math.cos(a)).toFixed(1)); ln.setAttribute('y2',(50+29*Math.sin(a)).toFixed(1));
      k.querySelector('.v').textContent=ampVal(p); };
    bindKnob(k,p,redraw);
    if(bound[p.path]!=null){ const cc=el('span','tag','CC'+bound[p.path]); cc.style.cssText='position:absolute;transform:translateY(-14px);font-size:8px;padding:1px 4px'; k.prepend(cc); }
    row.appendChild(k);
  });
  panel.appendChild(row);
  // lamp, the amp's own on/off, then the model's switches
  const aon=pick(S.amp,'on'), ampOn=!aon||aon.value>=0.5;
  box.classList.toggle('ampoff',!ampOn);
  const lc=el('div','lampcol'); lc.appendChild(el('span','lamp'));
  if(aon){ const f=el('button','flip'+(ampOn?' on':''),'<i></i>AMP');
    f.setAttribute('aria-label','Amp '+(ampOn?'on':'off')+'. Off sends the pedals straight to the output');
    f.onclick=()=>{ aon.value=ampOn?0:1; send('amp.on',aon.value); renderAmp(); };
    lc.appendChild(f); }
  PRODUCT.ampSwitches.forEach(id=>{ if(vis.indexOf(id)<0) return; const p=pick(S.amp,id); if(!p) return;
    const f=el('button','flip'+(p.value>=0.5?' on':''),'<i></i>'+esc(p.label.toUpperCase()));
    f.setAttribute('aria-label',p.label+(p.value>=0.5?' on':' off'));
    f.onclick=()=>{ p.value=p.value>=0.5?0:1; f.classList.toggle('on',p.value>=0.5); send(p.path,p.value); };
    lc.appendChild(f); });
  panel.appendChild(lc);
  box.appendChild(panel);
  // grille + glass tray: cab and mic
  const gr=el('div','grille');
  const cone=(x,y,r)=>'radial-gradient(circle at '+x+' '+y+',rgba(0,0,0,.34) 0,rgba(0,0,0,.18) '+(r*0.45)+'px,rgba(0,0,0,.05) '+(r*0.8)+'px,transparent '+r+'px)';
  // Cab Off: no speaker cones on the grille, and the chip row shows Off lit
  const con=pick(S.cab,'on'), cabOn=!con||con.value>=0.5;
  box.classList.toggle('caboff',!cabOn);
  const spk=cabOn?(PRODUCT.speakers[cabModel()]||{c:[]}).c:[], rr=(PRODUCT.speakers[cabModel()]||{r:96}).r;
  gr.style.backgroundImage=spk.map(([x,y])=>cone(x,y,rr)).concat([st.g]).join(',');
  gr.appendChild(el('span','plate','Top End'));
  const tray=el('div','tray');
  tray.appendChild(el('span','cap','Cab'));
  const cm=cabModel();
  if(con){ const off=el('button','cabchip'+(cabOn?'':' on'),'Off'); off.setAttribute('aria-label','Cab off: no speaker, the amp goes straight out');
    off.onclick=()=>{ con.value=cabOn?0:1; send('cab.on',con.value); renderAmp(); }; tray.appendChild(off); }
  CABS.forEach((c,i)=>{ const b=el('button','cabchip'+(i===cm&&cabOn?' on':''),esc(c.name));
    b.onclick=()=>{ if(con&&!cabOn){ con.value=1; send('cab.on',1); } send('cab.model',i); if(i===PRODUCT.cabIr) setTimeout(openCabSheet,120); }; tray.appendChild(b); });
  const ax=pick(S.cab,'axis');
  if(ax && cabOn && cm!==PRODUCT.cabIr && cm!==PRODUCT.cabDi){
    const mic=el('div','mic'); mic.appendChild(el('span','cap','Mic'));
    mic.appendChild(document.createTextNode('Centre'));
    const r=el('div','rng'); r.setAttribute('role','slider'); r.setAttribute('aria-label','Mic position, centre to edge');
    const fill=el('i'), knob=el('b'); r.appendChild(fill); r.appendChild(knob);
    const draw=()=>{ const t=norm(ax); fill.style.width=(t*100)+'%'; knob.style.left=(t*100)+'%'; };
    draw();
    const setX=e=>{ const q=r.getBoundingClientRect(); ax.value=denorm(ax,clamp((e.clientX-q.left)/q.width,0,1)); draw(); send(ax.path,ax.value,true); };
    r.addEventListener('pointerdown',e=>{ r.setPointerCapture(e.pointerId); setX(e); e.stopPropagation(); });
    r.addEventListener('pointermove',e=>{ if(r.hasPointerCapture(e.pointerId)) setX(e); });
    mic.appendChild(r); mic.appendChild(document.createTextNode('Edge'));
    tray.appendChild(mic);
  }
  const more=el('button','cabchip','More'); more.setAttribute('aria-label','Cab and mic settings'); more.onclick=openCabSheet;
  if(!cabOn||cm===PRODUCT.cabIr||cm===PRODUCT.cabDi) more.style.marginLeft='auto';
  tray.appendChild(more);
  gr.appendChild(tray);
  box.appendChild(gr);
  requestAnimationFrame(fitPlate);
}
/** The cab's name plate sits on the grille; on a short window the cab tray would
 *  cover it, so it moves into the tray's own row instead. */
function fitPlate(){
  const gr=$('#ampBox .grille'), pl=gr&&gr.querySelector('.plate'), tr=gr&&gr.querySelector('.tray');
  if(!pl||!tr) return;
  if(pl.parentElement!==gr){ pl.classList.remove('in'); gr.appendChild(pl); }
  const a=pl.getBoundingClientRect(), b=tr.getBoundingClientRect();
  if(a.height&&a.bottom>b.top-4){ pl.classList.add('in'); tr.insertBefore(pl,tr.querySelector('.mic')||tr.lastChild); }
}
function openAmpModels(){
  const body=el('div','models'), m=ampModel();
  AMPS.forEach((st,i)=>{
    const b=el('button','model'+(i===m?' on':''));
    styleAmp(b,st);
    b.innerHTML='<div class="face"><div class="pl"><i></i><i></i><i></i><i></i><i></i></div><div class="gr"></div></div><b>'+esc(st.name)+'</b><span>'+esc(st.sub)+'</span>';
    b.onclick=()=>{ closeSheet(); send('amp.model',i); };
    body.appendChild(b);
  });
  const note=el('div','help','The knobs keep their settings when you change model, the way a modelling amp should. To play a capture of your own amp, add the Your Amp pedal (or drop a .nam file on the window); cabinet impulse files go under Cab › Your IR. The AMP switch on the panel takes the amp out altogether, so the pedals go straight to the output.');
  note.style.marginTop='14px'; body.appendChild(note);
  openSheet({title:'Amp model', body, wide:true});
}
function openCabSheet(){
  const body=el('div'), box=el('div','ctls'), cm=cabModel();
  if(cm===PRODUCT.cabIr){
    const w=el('div','ctl wide'); w.appendChild(el('div','lab','Impulse file'));
    const s=el('select','select'); const irs=S.irs||[];
    if(!irs.length){ const o=el('option'); o.textContent='No IR files yet — put .wav files in the IRs folder'; o.value=-1; s.appendChild(o); s.disabled=true; }
    irs.forEach((n,i)=>{ const o=el('option'); o.textContent=String(n).split(/[\\/]/).pop(); o.value=i; if(Math.round((pick(S.cab,'ir')||{value:-1}).value)===i) o.selected=true; s.appendChild(o); });
    s.onchange=()=>send('cab.ir',parseInt(s.value,10));
    w.appendChild(s); box.appendChild(w);
  }
  ['mic','distance','axis','lowCut','highCut','level'].forEach(id=>{ const p=pick(S.cab,id); if(!p) return;
    if((cm===PRODUCT.cabIr||cm===PRODUCT.cabDi) && ['mic','distance','axis'].indexOf(id)>=0) return; box.appendChild(control(p,{col:'#e3a447'})); });
  body.appendChild(box);
  openSheet({title:(CABS[cm]||CABS[1]).name+' cabinet', body, wide:true,
    actions:[{label:'Open IR folder',onClick:()=>cmd('openFolder')},{label:'Done',kind:'primary',onClick:closeSheet}]});
}
/** Input and output: the rig's trim, gate, master and limiter. */
function openRigSheet(){
  const body=el('div','ctls');
  const P=(list,id,label)=>{ const p=pick(list,id); if(p) body.appendChild(control(p,{label,col:'#e3a447'})); };
  P(S.in,'trim','Input trim'); P(S.in,'gateOn','Gate'); P(S.in,'gateThreshold','Gate thresh'); P(S.in,'gateRelease','Gate release');
  // Mains hum (single coils, a laptop supply) is what the amp turns into a buzz
  // under the notes: the filter is here, where the noise is noticed, and in Settings.
  { const w=el('div','ctl wide'); w.appendChild(el('div','lab','Hum filter'));
    const sg=el('div','seg'); sg.setAttribute('role','radiogroup'); sg.setAttribute('aria-label','Hum filter');
    const HZ=[0,50,60];
    HZ.forEach((hz,j)=>{ const b=el('button',(S.humHz||0)===hz?'on':'',hz?hz+' Hz':'Off');
      b.onclick=()=>{ cmd('hum',{hz}); [...sg.children].forEach((x,k)=>x.classList.toggle('on',k===j)); }; sg.appendChild(b); });
    w.appendChild(sg); body.appendChild(w); }
  P(S.out,'master','Master'); P(S.out,'limiter','Limiter'); P(S.loop,'level','Loop level'); P(S.loop,'quantize','Loop snap'); P(S.loop,'playThrough','Hear '+PRODUCT.part);
  // Bass: the clean DI blended under the amp, and the tracked synth voice.
  if(PRODUCT.rigExtras){ P(S.in,'diOn','DI blend'); P(S.in,'diLevel','DI level'); P(S.in,'diLowPass','DI low-pass');
    P(S.synth,'on','Synth'); P(S.synth,'level','Synth level'); P(S.synth,'octave','Synth octave'); P(S.synth,'cutoff','Synth cutoff'); P(S.synth,'wave','Synth wave'); P(S.synth,'route','Synth route'); }
  openSheet({title:'Input and output', body, wide:true, actions:[{label:'Done',kind:'primary',onClick:closeSheet}]});
}

/* ═════════════════════════════ BOARD ═════════════════════════════ */
function renderBoard(){
  const c=$('#chain'); c.innerHTML='';
  c.appendChild(document.createElementNS('http://www.w3.org/2000/svg','svg')).setAttribute('class','cables');
  const jack=(label)=>{ const j=el('button','jack','<span>'+label+'</span>'); j.setAttribute('aria-label',label+' — input and output settings'); j.onclick=openRigSheet; return j; };
  c.appendChild(jack('INPUT'));
  const pre=S.pre||[], post=S.post||[];
  pre.forEach(b=>c.appendChild(pedalTile(b,'pre')));
  c.appendChild(el('i','brk'));        // where the board breaks onto a second tier when it is full
  c.appendChild(ampMini());
  post.forEach(b=>c.appendChild(pedalTile(b,'post')));
  const add=el('button','addp','+'); add.setAttribute('aria-label','Add a pedal');
  add.onclick=()=>openCatalog(null,null);
  c.appendChild(add);
  c.appendChild(jack('OUTPUT'));
  $('#boardCount').textContent=(n=>n+(n===1?' pedal':' pedals'))(pre.length+post.length);
  renderAmp();                         // first: the board's height depends on the amp's
  fitBoard();
  if(browse.open) renderBrowser();
}
/** Patch cables from each jack to the next, drawn in the chain's own (unscaled) space. */
function drawCables(){
  const c=$('#chain'), svg=c&&c.querySelector('.cables'); if(!svg) return;
  const parts=[...c.children].filter(n=>n!==svg&&!n.classList.contains('addp')&&!n.classList.contains('lift')&&!n.classList.contains('brk'));
  const W=c.scrollWidth, H=c.offsetHeight; svg.setAttribute('viewBox','0 0 '+W+' '+H); svg.setAttribute('width',W); svg.setAttribute('height',H);
  let d='';
  for(let i=0;i+1<parts.length;i++){
    const a=parts[i], b=parts[i+1];
    const x1=a.offsetLeft+a.offsetWidth-2, x2=b.offsetLeft+2, y1=a.offsetTop+(a.classList.contains('ped')?34:a.offsetHeight/2), y2=b.offsetTop+(b.classList.contains('ped')?34:b.offsetHeight/2);
    const lift=Math.max(16,Math.min(44,(x2-x1)*.6+14));
    // down to the second tier: out of the last pedal, round, and into the amp
    const path=Math.abs(y2-y1)>20?'M'+x1+' '+y1+'C'+(x1+70)+' '+(y1+10)+' '+(x2-70)+' '+(y2-10)+' '+x2+' '+y2
      :'M'+x1+' '+y1+'C'+(x1+8)+' '+(y1-lift)+' '+(x2-8)+' '+(y2-lift)+' '+x2+' '+y2;
    d+='<path d="'+path+'" class="cb"/><path d="'+path+'" class="cs"/>';
  }
  svg.innerHTML=d;
}
/** The board is scaled to the width it has, never scrolled. A full board goes
    onto two tiers, like a real one - the pedals in front of the amp above, the
    amp and what follows it below - so the knobs stay big enough to turn. */
function fitBoard(){
  const fit=$('#boardFit'), c=$('#chain'); if(!fit||!c||!fit.clientWidth) return;
  c.style.transform='none';
  const have=fit.clientWidth, amp=c.querySelector('.ampmini');
  // the height the board may take: the column, less what the amp needs to stay whole
  const rig=fit.closest('.rigL'), card=fit.closest('.card'), panel=$('#ampBox .panel');
  const chrome=card?card.offsetHeight-fit.offsetHeight:40;
  // the amp below needs its faceplate, its own padding and a grille tall enough for the cab tray
  const tray=$('#ampBox .tray'), grl=$('#ampBox .grille'), grMin=grl?parseFloat(getComputedStyle(grl).minHeight)||110:110;
  const ampNeed=panel?panel.offsetHeight+36+Math.max(grMin,tray?tray.offsetHeight+20:grMin):0;
  const maxH=rig&&panel?Math.max(120,rig.clientHeight-14-chrome-ampNeed):1e9;
  c.classList.remove('two'); c.style.width='';
  const svg=c.querySelector('.cables'); if(svg){ svg.setAttribute('width',0); svg.setAttribute('height',0); }   // last layout's cables must not count
  const ampX=amp?amp.offsetLeft:0, full=c.scrollWidth;     // the one-tier layout, measured before any wrap
  const measure=two=>{ c.classList.toggle('two',two); c.style.width='';
    if(two){ const w=Math.max(ampX,full-ampX+28); c.style.width=w+'px'; }
    return Math.min(1,have/Math.max(1,c.scrollWidth),maxH/Math.max(1,c.offsetHeight)); };
  let s=measure(false);
  if(s<0.85&&amp&&(S.pre||[]).length&&(S.post||[]).length){ const s2=measure(true); if(s2>s*1.08) s=s2; else s=measure(false); }
  // knobs stay big enough to turn on the Rig; in the Library the whole rig has to fit beside the details
  const floor=rig&&rig.classList.contains('librig')?0.3:0.42;
  s=Math.min(Math.max(floor,s),have/Math.max(1,c.scrollWidth));   // never wider than the board
  c.style.transformOrigin='0 0';       // shrink toward the top-left, so the box below can be cut to the scaled height
  c.style.transform='scale('+s.toFixed(4)+')'; fit.style.height=Math.ceil(c.offsetHeight*s)+'px';
  drawCables();
  fitPlate();
}
window.addEventListener('resize',()=>{ fitBoard(); fitScenes(); fitPlate(); });

/* ═════════════════════════════ DRAG AND DROP ═════════════════════════════
   Pick a pedal up and the board opens a gap where it will land, on either side
   of the amp. Drop it on the bin to remove it. Pedals from the browser drop in
   the same way. Hold or right-click still gives Move left / Move right. */
let dnd=null;
function dragFrom(node,src){
  node.addEventListener('pointerdown',e=>{
    if(e.button!==0||e.target.closest('.pkn,.psw')) return;
    const x0=e.clientX, y0=e.clientY;
    const mv=ev=>{ if(!dnd){ if(Math.hypot(ev.clientX-x0,ev.clientY-y0)<7) return; beginDrag(node,src,ev,x0,y0); } moveDrag(ev); };
    const up=ev=>{ window.removeEventListener('pointermove',mv); window.removeEventListener('pointercancel',up);
      if(dnd) endDrag(ev.type==='pointercancel'); };
    window.addEventListener('pointermove',mv); window.addEventListener('pointerup',up,{once:true}); window.addEventListener('pointercancel',up,{once:true});
  });
}
function boardScale(){ const c=$('#chain'); return c&&c.offsetWidth?c.getBoundingClientRect().width/c.offsetWidth:1; }
function beginDrag(node,src,ev,x0,y0){
  const face=node.classList.contains('ped')?node:node.querySelector('.ped'), r=node.getBoundingClientRect(), s=boardScale();
  const g=face.cloneNode(true); g.classList.add('ghost'); g.classList.remove('sel','off');
  g.style.transform='scale('+s+') rotate(-2deg)'; document.body.appendChild(g);
  // from the board you hold it where you grabbed it; from the browser, by the middle
  const ox=src.chain?(x0-r.left)/(r.width||1):.5, oy=src.chain?(y0-r.top)/(r.height||1):.3;
  dnd={src,ghost:g,node,ox,oy,w:face.offsetWidth,h:face.offsetHeight,s,target:null};
  if(src.chain){ node.classList.add('lift'); $('#boardBin').classList.add('on'); }
  document.body.classList.add('dragging');
  // the click that follows the drop must not open the pedal
  window.addEventListener('click',eatClick,true);
}
function moveDrag(ev){
  const g=dnd.ghost; g.style.left=(ev.clientX-dnd.ox*dnd.w*dnd.s)+'px'; g.style.top=(ev.clientY-dnd.oy*dnd.h*dnd.s)+'px';
  const bin=$('#boardBin'), br=bin.getBoundingClientRect();
  const onBin=!!dnd.src.chain&&ev.clientX>=br.left&&ev.clientX<=br.right&&ev.clientY>=br.top&&ev.clientY<=br.bottom;
  bin.classList.toggle('hot',onBin); g.classList.toggle('bin',onBin);
  const fit=$('#boardFit'), fr=fit.getBoundingClientRect();
  const over=!onBin&&ev.clientY>fr.top-50&&ev.clientY<fr.bottom+50&&ev.clientX>fr.left-20&&ev.clientX<fr.right+20;
  dnd.target=onBin?'bin':over?dropSpot(ev.clientX,ev.clientY):null;
  const m=$('#dropMark');
  if(dnd.target&&dnd.target!=='bin'){ m.style.left=(dnd.target.x-fr.left)+'px'; m.style.top=(dnd.target.top-fr.top)+'px'; m.style.height=dnd.target.h+'px'; m.classList.add('on'); }
  else m.classList.remove('on');
}
/** Where a drop lands: which side of the amp, at what index, on the tier
    nearest the pointer (a full board has two). */
function dropSpot(x,y){
  const c=$('#chain'), items=[...c.children].filter(n=>(n.classList.contains('ped')||n.classList.contains('ampmini'))&&!n.classList.contains('lift'));
  const rs=items.map(n=>n.getBoundingClientRect());
  let row=rs[0], best=1e9; rs.forEach(r=>{ const d=Math.abs(r.top+r.height/2-y); if(d<best){ best=d; row=r; } });
  let chain='pre', idx=0, spot=null, last=null;
  items.forEach((n,i)=>{ const r=rs[i], inRow=Math.abs(r.top-row.top)<24;
    if(inRow&&!spot&&x<r.left+r.width/2) spot={chain,index:idx,x:r.left-8,top:r.top,h:r.height};
    if(n.classList.contains('ampmini')){ chain='post'; idx=0; } else idx++;
    if(inRow) last={chain,index:idx,x:r.right+8,top:r.top,h:r.height}; });
  return spot||last;
}
function endDrag(cancel){
  const d=dnd; dnd=null;
  setTimeout(()=>window.removeEventListener('click',eatClick,true),0);
  d.ghost.remove(); d.node.classList.remove('lift');
  $('#boardBin').classList.remove('on','hot'); $('#dropMark').classList.remove('on'); document.body.classList.remove('dragging');
  const t=cancel?null:d.target, s=d.src;
  if(!t){ drawCables(); return; }
  const full=ch=>toast(ch==='post'?'After the cab is full ('+(S.postCapacity||4)+' pedals)':'The board is full ('+(S.preCapacity||8)+' pedals)');
  if(t==='bin'){ cmd('removeBlock',{chain:s.chain,index:s.index}).then(()=>{ sel={kind:'amp'}; toast('Removed '+s.name); }); return; }
  if(!s.chain){
    cmd('addBlock',{chain:t.chain,type:s.type,index:t.index}).then(ok=>{ if(!ok) full(t.chain); else sel={kind:'pedal',chain:t.chain,index:t.index}; });
    return; }
  if(s.chain===t.chain){ if(t.index!==s.index) cmd('moveBlock',{chain:s.chain,from:s.index,to:t.index}); else drawCables(); }
  else cmd('transferBlock',{from:s.chain,fromIndex:s.index,to:t.chain,toIndex:t.index}).then(ok=>{ if(!ok) full(t.chain); });
  sel={kind:'pedal',chain:t.chain,index:t.index};
}

/* ═════════════════════════════ PEDAL BROWSER ═════════════════════════════
   Opens over the amp: a big preview of the chosen pedal on the left, every
   pedal on the right. Drag one onto the board where you want it, or add it. */
const browse={open:false, cat:'All', q:'', pick:null, page:0, target:null};
const place=cat=>['Modulation','Time'].indexOf(cat)>=0?'post':'pre';
function openCatalog(chain,index){
  browse.open=true; browse.target=chain?{chain,index}:null; browse.page=0;
  if(!browse.pick&&(S.catalog||[]).length) browse.pick=S.catalog[0].type;
  renderBrowser(); $('#pBrowse').classList.add('on');
}
function closeBrowser(){ browse.open=false; $('#pBrowse').classList.remove('on'); }
function addFromBrowser(c){
  const t=browse.target, ch=t?t.chain:place(c.category), list=ch==='post'?(S.post||[]):(S.pre||[]);
  const at=t&&t.index!=null?t.index:list.length;
  browse.target=null;
  cmd('addBlock',{chain:ch,type:c.type,index:at}).then(ok=>{ if(!ok) toast(ch==='post'?'After the cab is full':'The board is full'); else { sel={kind:'pedal',chain:ch,index:at}; toast(c.name+' added'); } });
}
function renderBrowser(){
  const box=$('#pBrowse'); if(!box) return;
  const cat=S.catalog||[], onBoard=new Set([...(S.pre||[]),...(S.post||[])].map(b=>b.type));
  const c=cat.find(x=>x.type===browse.pick)||cat[0]; if(!c) return;
  box.innerHTML='';
  // preview
  const pv=el('div','pbv');
  pv.appendChild(el('span','tag',esc(c.category)+(onBoard.has(c.type)?' · on the board':'')));
  const stage=el('div','pbstage'); stage.appendChild(pedalFace(c)); pv.appendChild(stage);
  pv.appendChild(el('h3','',esc(c.name)));
  pv.appendChild(el('p','',esc(DESC[c.type]||CATDESC[c.category]||'')));
  const go=el('div','pbgo'), add=el('button','btn primary','Add to board'); add.onclick=()=>addFromBrowser(c);
  go.appendChild(add); go.appendChild(el('span','cap','or drag it to the board')); pv.appendChild(go);
  box.appendChild(pv);
  // grid
  const side=el('div','pbside'), bar=el('div','pbbar');
  ['All','Drive','Dynamics','Filter','Modulation','Time','Utility'].filter(k=>k==='All'||cat.some(x=>x.category===k)).forEach(k=>{
    const b=el('button','chip'+(browse.cat===k?' on':''),esc(k)); b.onclick=()=>{ browse.cat=k; browse.page=0; renderBrowser(); }; bar.appendChild(b); });
  const q=el('input','pbq'); q.placeholder='Search pedals'; q.value=browse.q; q.setAttribute('aria-label','Search pedals');
  q.oninput=()=>{ browse.q=q.value; browse.page=0; renderBrowser(); const n=$('#pBrowse .pbq'); n.focus(); n.setSelectionRange(n.value.length,n.value.length); };
  bar.appendChild(q);
  const done=el('button','btn small','Done'); done.onclick=closeBrowser; bar.appendChild(done);
  side.appendChild(bar);
  const grid=el('div','pbgrid'); side.appendChild(grid);
  const pager=el('div','pager'); side.appendChild(pager);
  box.appendChild(side);
  const list=cat.filter(x=>(browse.cat==='All'||x.category===browse.cat)&&(x.name+' '+x.category).toLowerCase().indexOf(browse.q.toLowerCase())>=0);
  // the preview pedal is as big as its stage allows
  const big=stage.firstChild; big.style.transform='scale('+Math.min(1.25,(stage.clientHeight-8)/big.offsetHeight,(stage.clientWidth-16)/big.offsetWidth).toFixed(3)+')';
  // nothing scrolls: as many tiles as fit (measured from a real one), then pages
  const tile=x=>{
    const t=el('button','pbt'+(x.type===c.type?' on':'')+(onBoard.has(x.type)?' used':''));
    const m=el('span','mini'); m.appendChild(pedalFace(x)); t.appendChild(m); t.appendChild(el('span','nm',esc(x.name)));
    t.onclick=()=>{ browse.pick=x.type; renderBrowser(); };
    t.ondblclick=()=>addFromBrowser(x);
    dragFrom(t,{chain:null,type:x.type,name:x.name});
    return t; };
  let per=list.length;
  if(list.length){ grid.style.gridTemplateColumns='repeat(auto-fill,76px)'; const probe=grid.appendChild(tile(list[0])), th=probe.offsetHeight+8, tw=76+8;
    probe.remove();
    const cols=Math.max(3,Math.floor((grid.clientWidth+8)/tw)), rows=Math.max(1,Math.floor((grid.clientHeight+8)/th));
    grid.style.gridTemplateColumns='repeat('+cols+',minmax(0,1fr))'; per=cols*rows; }
  const pages=Math.max(1,Math.ceil(list.length/Math.max(1,per))); browse.page=Math.min(browse.page,pages-1);
  list.slice(browse.page*per,browse.page*per+per).forEach(x=>grid.appendChild(tile(x)));
  if(!list.length) grid.appendChild(el('span','cap','No pedal matches “'+esc(browse.q)+'”'));
  if(pages>1) for(let i=0;i<pages;i++){ const d=el('button',i===browse.page?'on':'','<i></i>'); d.setAttribute('aria-label','Page '+(i+1)); d.onclick=()=>{ browse.page=i; renderBrowser(); }; pager.appendChild(d); }
}

/* ═════════════════════════════ SCENES ═════════════════════════════
   Six to a page, lettered A-F as on a floor unit. A set with more than six
   pages over with a tap; nothing here scrolls. */
const song=()=>(S.songs||[]).find(s=>s.id===S.songId);
let scenePage=0, scenePer=6;
function fitScenes(){
  const g=$('#scenes'); if(!g||!g.clientHeight) return;
  // As many rows of two as fit at a scene's minimum height (56 px + 9 px gap), up to three.
  const per=2*clamp(Math.floor((g.clientHeight+9)/65),1,3);
  if(per!==scenePer){ scenePer=per; if(S) renderStage(); }
}
function renderStage(){
  const sg=song();
  $('#setTitle').textContent = sg ? sg.name : 'All presets';
  const ss=$('#songSel'); ss.innerHTML='';
  const none=el('option'); none.textContent='All presets'; none.value=''; if(!sg) none.selected=true; ss.appendChild(none);
  (S.songs||[]).forEach(s=>{ const o=el('option'); o.textContent=s.name; o.value=s.id; if(s.id===S.songId) o.selected=true; ss.appendChild(o); });
  ss.onchange=()=>{ scenePage=0; if(ss.value) cmd('loadSong',{id:ss.value,scene:0}); else cmd('closeSong'); };

  const items = sg ? sg.scenes.map((sc,i)=>({id:sc.id,name:sc.name,i,miss:sc.missing}))
                   : (S.presets||[]).map((p,i)=>({id:p.id,name:p.name,i}));
  const pages=Math.max(1,Math.ceil((items.length+(sg?1:0))/scenePer));
  const cur=items.findIndex(it=>it.id===S.presetId);
  if(cur>=0 && renderStage._follow!==S.presetId){ scenePage=Math.floor(cur/scenePer); renderStage._follow=S.presetId; }
  scenePage=clamp(scenePage,0,pages-1);
  const w=$('#scenes'); w.innerHTML='';
  w.style.gridTemplateRows='repeat('+(scenePer/2)+',minmax(0,1fr))';
  const L='ABCDEFGHIJKLMNOPQRSTUVWXYZ';
  items.slice(scenePage*scenePer,(scenePage+1)*scenePer).forEach(it=>{
    const b=el('button','scene'+(it.id===S.presetId?' on':'')+(it.miss?' miss':''),
      '<span class="k">'+String(it.i+1).padStart(2,'0')+(edited(it.id)?' · EDITED':'')+'</span><b>'+esc(it.name)+'</b>'+(it.i<6?'<span class="fs">'+L[it.i]+'</span>':''));
    b.onclick=()=>{ if(!it.miss) cmd('scene',{index:it.i}); };
    if(sg) holdMenu(b,(x,y)=>openMenu(x,y,[
      {label:'Move earlier',onClick:()=>{ if(it.i>0) cmd('songScene',{song:sg.id,action:'move',index:it.i,to:it.i-1}); }},
      {label:'Move later',onClick:()=>{ if(it.i<sg.scenes.length-1) cmd('songScene',{song:sg.id,action:'move',index:it.i,to:it.i+1}); }},
      {label:'Remove from '+sg.name,onClick:()=>cmd('songScene',{song:sg.id,action:'remove',index:it.i})}]));
    w.appendChild(b);
  });
  if(sg && w.children.length<scenePer && scenePage===pages-1){
    const add=el('button','scene add','+'); add.setAttribute('aria-label','Add a preset to this set');
    add.onclick=()=>openSetPicker(sg); w.appendChild(add);
  }
  const pg=$('#scenePager'); pg.innerHTML=''; pg.hidden=pages<2;
  if(pages>8){
    // the whole preset list on a small card: arrows and a count, not sixty dots
    const prev=el('button','pn','‹'), cnt=el('span','pc',(scenePage+1)+' / '+pages), next=el('button','pn','›');
    prev.setAttribute('aria-label','Previous page'); next.setAttribute('aria-label','Next page');
    prev.onclick=()=>{ scenePage=Math.max(0,scenePage-1); renderStage(); };
    next.onclick=()=>{ scenePage=Math.min(pages-1,scenePage+1); renderStage(); };
    pg.appendChild(prev); pg.appendChild(cnt); pg.appendChild(next);
  } else
  for(let i=0;i<pages;i++){ const b=el('button',i===scenePage?'on':'','<i></i>'); b.setAttribute('aria-label','Page '+(i+1)); b.onclick=()=>{ scenePage=i; renderStage(); }; pg.appendChild(b); }
  const lim=pick(S.out,'limiter'), vol=pick(S.out,'master');
  $('#limTag').textContent = lim && lim.value>=0.5 ? 'Limiter on' : 'Limiter off';
  // Volume and Limiter on the card itself. Not rebuilt under a finger on the dial.
  const oc=$('#outCtl');
  if(oc && !oc.querySelector('.dial:active')){
    oc.innerHTML='';
    if(vol) oc.appendChild(control(vol,{label:'Volume',col:'#e3a447'}));
    if(lim) oc.appendChild(control(lim,{label:'Limiter'}));
  }
}
