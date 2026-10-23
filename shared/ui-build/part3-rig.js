
/* ═════════════════════════════ PEDALS ═════════════════════════════
   Every pedal has its own paint, the way a real board does — colour here is
   the pedal's identity, never a state (state is the LED and the dimmed body). */
const PAINT={
  noiseGate:'#5d636b', clearDrive:'#d8a64a', tubeOd:'#6f9f5a', fuzz:'#9c4a3f', boost:'#c9c3b4',
  optoComp:'#7fa88a', fetComp:'#3e6a7a', graphicEq:'#34383f', paraEq:'#4b5563',
  envFilter:'#a8bf4a', octaver:'#6a5d8f', wah:'#2e2b28',
  chorus:'#5f86a6', flanger:'#7c6fa8', phaser:'#d07a3a', tremolo:'#a8564e',
  analogDelay:'#4f8f8a', digitalDelay:'#3f7fa0', roomVerb:'#d9cfbd', ambientVerb:'#8ea6b4'};
const CATC={Utility:'#5d636b',Drive:'#d8a64a',Dynamics:'#7fa88a',Filter:'#a8bf4a',Modulation:'#5f86a6',Time:'#4f8f8a'};
const paintOf=b=>PAINT[b.type||b.id]||CATC[b.category]||'#5d636b';
/** Dark or light lettering, whichever reads on the paint. */
function inkOn(hex){ const n=parseInt(hex.slice(1),16), r=n>>16&255, g=n>>8&255, b=n&255;
  return (0.2126*r+0.7152*g+0.0722*b)>140?'#1c1408':'#f6efe4'; }
/* Faceplates have room for short names; long ones print the way a real pedal would. */
const SHORT={THRESHOLD:'THRESH',SENSITIVITY:'SENS',RESONANCE:'RESO',FEEDBACK:'FDBK',RELEASE:'REL',ATTACK:'ATK',
  SUSTAIN:'SUS',PRESENCE:'PRES',DISTANCE:'DIST','MID FREQ':'MID F','LOW CUT':'LO CUT','HIGH CUT':'HI CUT',
  DIRECTION:'SWEEP',DIVISION:'DIV','WOW/FLUTTER':'WOW','PING-PONG':'P-PONG',POSITION:'POS'};
const short=s=>{ const u=String(s||'').toUpperCase(); return (SHORT[u]||u).slice(0,7); };

/** A cream pedal knob cap (display only on the tile; edited in the sheet). */
function capKnob(t){
  const a=(-225+270*t)*Math.PI/180;
  return '<svg viewBox="0 0 100 100"><circle cx="50" cy="54" r="36" fill="rgba(0,0,0,.28)"/>'+
    '<circle cx="50" cy="50" r="36" fill="url(#capg)"/>'+
    '<line x1="'+(50+8*Math.cos(a)).toFixed(1)+'" y1="'+(50+8*Math.sin(a)).toFixed(1)+'" x2="'+(50+31*Math.cos(a)).toFixed(1)+'" y2="'+(50+31*Math.sin(a)).toFixed(1)+'" stroke="#1c1306" stroke-width="7" stroke-linecap="round"/></svg>';
}
function pedalTile(b,chain){
  const paint=paintOf(b), ink=inkOn(paint);
  const u=el('div','ped'+(b.bypassed?' off':'')+(sel.kind==='pedal'&&sel.chain===chain&&sel.index===b.index?' sel':''));
  u.style.setProperty('--c',paint); u.style.setProperty('--t',ink);
  u.setAttribute('role','button'); u.setAttribute('aria-label',b.name+(b.bypassed?', off':', on'));
  const knobs=(b.params||[]).filter(p=>p.kind==='float').slice(0,2);
  u.innerHTML='<span class="pk">'+knobs.map(p=>capKnob(norm(p))).join('')+'</span><span class="pn">'+esc(b.name)+'</span><span class="pled"></span>';
  const sw=el('button','psw'); sw.setAttribute('aria-label',(b.bypassed?'Turn on ':'Turn off ')+b.name);
  sw.onclick=e=>{ e.stopPropagation(); cmd('set',{path:b.bypassPath,value:b.bypassed?0:1}); };
  u.appendChild(sw);
  u.onclick=()=>{ sel={kind:'pedal',chain,index:b.index}; openPedalSheet(chain,b.index); };
  holdMenu(u,(x,y)=>pedalMenu(chain,b,x,y));
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
  const box=el('div','ctls'); (b.params||[]).forEach(p=>box.appendChild(control(p,{col:'#e3a447'}))); body.appendChild(box);
  openSheet({title:b.name, body, wide:(b.params||[]).length>6, actions:[
    {label:'Remove', kind:'danger', onClick:()=>{ closeSheet(); cmd('removeBlock',{chain,index:b.index}); }},
    {label:'◀ Move', onClick:()=>{ if(b.index>0){ closeSheet(); cmd('moveBlock',{chain,from:b.index,to:b.index-1}); } }},
    {label:'Move ▶', onClick:()=>{ if(b.index<list.length-1){ closeSheet(); cmd('moveBlock',{chain,from:b.index,to:b.index+1}); } }},
    {label:b.bypassed?'Turn on':'Turn off', onClick:()=>{ closeSheet(); cmd('set',{path:b.bypassPath,value:b.bypassed?0:1}); }},
    {label:'Done', kind:'primary', onClick:closeSheet}]});
}

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
  // lamp + the model's switches
  const lc=el('div','lampcol'); lc.appendChild(el('span','lamp'));
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
  const spk=(PRODUCT.speakers[cabModel()]||{c:[]}).c, rr=(PRODUCT.speakers[cabModel()]||{r:96}).r;
  gr.style.backgroundImage=spk.map(([x,y])=>cone(x,y,rr)).concat([st.g]).join(',');
  gr.appendChild(el('span','plate','Top End'));
  const tray=el('div','tray');
  tray.appendChild(el('span','cap','Cab'));
  const cm=cabModel();
  CABS.forEach((c,i)=>{ const b=el('button','cabchip'+(i===cm?' on':''),esc(c.name)); b.onclick=()=>{ send('cab.model',i); if(i===PRODUCT.cabIr) setTimeout(openCabSheet,120); }; tray.appendChild(b); });
  const ax=pick(S.cab,'axis');
  if(ax && cm!==PRODUCT.cabIr && cm!==PRODUCT.cabDi){
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
  if(cm===PRODUCT.cabIr||cm===PRODUCT.cabDi) more.style.marginLeft='auto';
  tray.appendChild(more);
  gr.appendChild(tray);
  box.appendChild(gr);
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
  const note=el('div','help','The knobs keep their settings when you change model, the way a modelling amp should. Captures of your own amps (NAM) are not supported yet; cabinet impulse files are, under Cab › Your IR.');
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
  P(S.out,'master','Master'); P(S.out,'limiter','Limiter'); P(S.loop,'level','Loop level'); P(S.loop,'quantize','Loop snap'); P(S.loop,'playThrough','Hear '+PRODUCT.part);
  // Bass: the clean DI blended under the amp, and the tracked synth voice.
  if(PRODUCT.rigExtras){ P(S.in,'diOn','DI blend'); P(S.in,'diLevel','DI level'); P(S.in,'diLowPass','DI low-pass');
    P(S.synth,'on','Synth'); P(S.synth,'level','Synth level'); P(S.synth,'octave','Synth octave'); P(S.synth,'cutoff','Synth cutoff'); P(S.synth,'wave','Synth wave'); P(S.synth,'route','Synth route'); }
  openSheet({title:'Input and output', body, wide:true, actions:[{label:'Done',kind:'primary',onClick:closeSheet}]});
}

/* ═════════════════════════════ BOARD ═════════════════════════════ */
function renderBoard(){
  const c=$('#chain'); c.innerHTML='';
  const jack=(label)=>{ const j=el('button','jack','<span>'+label+'</span>'); j.setAttribute('aria-label',label+' — input and output settings'); j.onclick=openRigSheet; return j; };
  c.appendChild(jack('INPUT'));
  const pre=S.pre||[], post=S.post||[];
  pre.forEach(b=>c.appendChild(pedalTile(b,'pre')));
  c.appendChild(ampMini());
  post.forEach(b=>c.appendChild(pedalTile(b,'post')));
  const add=el('button','addp','+'); add.setAttribute('aria-label','Add a pedal');
  add.onclick=()=>openCatalog(null,null);
  c.appendChild(add);
  c.appendChild(jack('OUTPUT'));
  $('#boardCount').textContent=(n=>n+(n===1?' pedal':' pedals'))(pre.length+post.length);
  fitBoard();
  renderAmp();
}
/** The board is scaled to the width it has, never scrolled. */
function fitBoard(){
  const fit=$('#boardFit'), c=$('#chain'); if(!fit||!c||!fit.clientWidth) return;
  c.style.transform='none';
  const need=c.scrollWidth, have=fit.clientWidth;
  let s=Math.min(1,have/Math.max(1,need));
  c.style.transformOrigin='0 0';       // shrink toward the top-left, so the box below can be cut to the scaled height
  const apply=()=>{ c.style.transform='scale('+s.toFixed(4)+')'; fit.style.height=Math.ceil(c.offsetHeight*s)+'px'; };
  apply();
  // A short window: the card has less height than the pedals need, so shrink to fit that too.
  const card=fit.closest('.card');
  for(let k=0;k<3&&card&&card.scrollHeight>card.clientHeight+1;k++){
    const over=card.scrollHeight-card.clientHeight; s=Math.max(0.5,s-over/Math.max(1,c.offsetHeight)); apply(); }
}
window.addEventListener('resize',()=>{ fitBoard(); fitScenes(); });

/* ═════════════════════════════ CATALOG ═════════════════════════════ */
function openCatalog(chain,index){
  const body=el('div');
  const cats={};
  (S.catalog||[]).forEach(c=>{ (cats[c.category]=cats[c.category]||[]).push(c); });
  // Drive, dynamics and filters belong in front of the amp; modulation and
  // time after the cab — the placement a guitarist would choose, unless they
  // came here from a particular gap on the board.
  const place=cat=>chain || (['Modulation','Time'].indexOf(cat)>=0?'post':'pre');
  ['Drive','Dynamics','Filter','Modulation','Time','Utility'].forEach(cat=>{
    if(!cats[cat]) return;
    const g=el('div','catgroup'); g.appendChild(el('span','cap',esc(cat)));
    const grid=el('div','catgrid');
    cats[cat].forEach(c=>{
      const b=el('button','catitem');
      b.innerHTML='<span class="sw" style="--c:'+(PAINT[c.type]||CATC[cat])+'"></span><span>'+esc(c.name)+'</span>';
      b.onclick=()=>{ closeSheet(); const ch=place(cat), list=ch==='post'?(S.post||[]):(S.pre||[]);
        const at=index!=null?index:list.length;
        cmd('addBlock',{chain:ch,type:c.type,index:at}).then(ok=>{ if(!ok) toast(ch==='post'?'After the cab is full (4 pedals)':'The board is full (8 pedals)'); else sel={kind:'pedal',chain:ch,index:at}; }); };
      grid.appendChild(b);
    });
    g.appendChild(grid); body.appendChild(g);
  });
  openSheet({title:'Add a pedal', body, wide:true});
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
  for(let i=0;i<pages;i++){ const b=el('button',i===scenePage?'on':'','<i></i>'); b.setAttribute('aria-label','Page '+(i+1)); b.onclick=()=>{ scenePage=i; renderStage(); }; pg.appendChild(b); }
  const lim=pick(S.out,'limiter');
  $('#limTag').textContent = lim && lim.value>=0.5 ? 'Limiter on' : 'Limiter off';
}
