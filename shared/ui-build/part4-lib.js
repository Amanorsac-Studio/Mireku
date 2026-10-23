
/* ═════════════════════════════ LIBRARY ═════════════════════════════
   Four lists — presets, sets, the songs imported into Learn, and captures (IR
   files) — each a page of rows that fits; a long list pages, it never scrolls. */
const fmtWhen=ms=>{ if(!ms) return ''; const d=new Date(ms), now=new Date(), mins=(now-d)/60000;
  if(mins<1) return 'now'; if(mins<60) return Math.round(mins)+' min ago';
  if(d.toDateString()===now.toDateString()) return d.toTimeString().slice(0,5);
  if(mins<60*24*6) return d.toLocaleDateString('en-GB',{weekday:'short'})+' '+d.toTimeString().slice(0,5);
  return d.toLocaleDateString('en-GB',{day:'numeric',month:'short'}); };
function libRows(){ const h=$('#libList').clientHeight||400; return Math.max(3,Math.floor(h/52)); }
function libItems(){
  const q=(lib.q||'').trim().toLowerCase(), hit=n=>!q||String(n).toLowerCase().includes(q);
  if(lib.tab==='presets') return (S.presets||[]).filter(p=>hit(p.name));
  if(lib.tab==='sets') return (S.songs||[]).filter(s=>hit(s.name));
  if(lib.tab==='songs') return (L.lessons||[]).filter(s=>hit(s.name));
  return (S.irs||[]).map((f,i)=>({id:String(i),name:String(f).split(/[\\/]/).pop(),path:f})).filter(x=>hit(x.name));
}
function chainSwatches(p){
  const all=[...(p.pre||[]),...(p.post||[])].slice(0,6);
  let s='<span class="sw6">';
  for(let i=0;i<6;i++){ const b=all[i]; s+='<i'+(b?' style="background:'+(PAINT[b.id]||CATC[b.category]||'#5d636b')+(b.off?';opacity:.35':'')+'"':'')+'></i>'; }
  return s+'</span>';
}
function renderLibrary(){
  [...$('#libSeg').children].forEach(b=>b.classList.toggle('on',b.dataset.k===lib.tab));
  $('#libNewSet').hidden=false;
  const items=libItems(), per=libRows(), pages=Math.max(1,Math.ceil(items.length/per));
  if(!lib.id || !items.find(x=>x.id===lib.id)){
    lib.id = lib.tab==='presets' ? (items.find(x=>x.id===S.presetId)||items[0]||{}).id
           : lib.tab==='sets' ? (items.find(x=>x.id===S.songId)||items[0]||{}).id
           : lib.tab==='songs' ? (items.find(x=>L.lesson&&x.id===L.lesson.id)||items[0]||{}).id : (items[0]||{}).id;
    const at=items.findIndex(x=>x.id===lib.id); if(at>=0) lib.page=Math.floor(at/per);
  }
  lib.page=clamp(lib.page||0,0,pages-1);
  const list=$('#libList'); list.innerHTML='';
  const L2='ABCDEF';
  items.slice(lib.page*per,(lib.page+1)*per).forEach(x=>{
    let html='';
    if(lib.tab==='presets'){
      const sg=song(), at=sg?sg.scenes.findIndex(sc=>sc.id===x.id):-1;
      html=chainSwatches(x)+'<span style="min-width:0"><span class="t">'+esc(x.name)+'</span><span class="s">'+esc(((AMPS[x.amp]||{}).name||'')+' · '+((n=>n+(n===1?' pedal':' pedals'))((x.pre||[]).length+(x.post||[]).length)))+'</span></span>'+
           (edited(x.id)?'<span class="k" style="color:var(--amber);border-color:var(--amber-deep)">EDITED</span>':at>=0&&at<6?'<span class="k">'+L2[at]+'</span>':(x.id===S.presetId?'<span class="live"></span>':'<span></span>'));
    } else if(lib.tab==='sets'){
      html='<span class="sw6"><i style="background:var(--amber)"></i><i></i><i></i></span><span style="min-width:0"><span class="t">'+esc(x.name)+'</span><span class="s">'+x.scenes.length+' SCENES · '+Math.round(x.tempo)+' BPM</span></span>'+(x.id===S.songId?'<span class="live"></span>':'<span></span>');
    } else if(lib.tab==='songs'){
      html='<span class="sw6"><i style="background:var(--d6)"></i><i style="background:var(--d1)"></i><i style="background:var(--d5)"></i></span><span style="min-width:0"><span class="t">'+esc(x.name)+'</span><span class="s">'+(x.kind==='video'?'VIDEO':'AUDIO')+'</span></span>'+(L.lesson&&x.id===L.lesson.id?'<span class="live"></span>':'<span></span>');
    } else {
      html='<span class="sw6"><i style="background:#8ea6b4"></i></span><span style="min-width:0"><span class="t">'+esc(x.name)+'</span><span class="s">IMPULSE RESPONSE</span></span><span></span>';
    }
    const r=el('button','li'+(x.id===lib.id?' on':''),html);
    r.onclick=()=>{ lib.id=x.id; renderLibrary(); };
    list.appendChild(r);
  });
  if(!items.length) list.appendChild(el('div','empty',lib.q?'Nothing matches “'+esc(lib.q)+'”.':
    lib.tab==='songs'?'No songs yet. Import one on the Learn tab.':lib.tab==='captures'?'No impulse files yet. Put .wav IRs in the IRs folder, then Rescan.':'Nothing here yet.'));
  const pg=$('#libPager'); pg.innerHTML=''; pg.hidden=pages<2;
  for(let i=0;i<pages;i++){ const b=el('button',i===lib.page?'on':'','<i></i>'); b.setAttribute('aria-label','Page '+(i+1)); b.onclick=()=>{ lib.page=i; renderLibrary(); }; pg.appendChild(b); }
  renderLibDetail(items.find(x=>x.id===lib.id));
}
function libActs(btns){ const a=el('div','acts'); btns.forEach(([label,kind,fn])=>{ const b=el('button','btn'+(kind?' '+kind:''),esc(label)); b.onclick=fn; a.appendChild(b); }); return a; }
function renderLibDetail(x){
  const d=$('#libDetail'); d.innerHTML='';
  if(!x){ d.appendChild(el('div','empty','Choose something on the left.')); return; }
  if(lib.tab==='presets'){
    const loaded=x.id===S.presetId, dirty=edited(x.id);
    const h=el('div','lhead');
    const t=el('div'); t.innerHTML='<span class="cap">Preset'+(x.history&&x.history[0]?' · saved '+esc(fmtWhen(x.history[0].t)):'')+(dirty?' · changes not saved':'')+'</span><h1>'+esc(x.name)+'</h1>';
    h.appendChild(t);
    h.appendChild(libActs([
      [loaded?'Loaded':'Load', loaded?'':'primary', ()=>cmd('loadPreset',{id:x.id})],
      ['Rename','',()=>ask('Rename preset',x.name,'Rename').then(n=>{ if(n) cmd('renamePreset',{id:x.id,name:n}); })],
      ['Duplicate','',()=>cmd('duplicatePreset',{id:x.id}).then(id=>{ if(id){ lib.id=id; toast('Duplicated'); } })],
      ['Export','',()=>cmd('exportPreset',{id:x.id}).then(p=>toast(p?'Exported to Documents › Amanorsac Studio › '+PRODUCT.name+' › Exports':'Could not export'))],
      ['Delete','danger',()=>{ const usedIn=(S.songs||[]).filter(s=>s.scenes.some(sc=>sc.id===x.id)).map(s=>s.name);
        confirmBox('Delete “'+x.name+'”?',usedIn.length?'It will also come out of '+usedIn.join(', ')+'.':'This cannot be undone.').then(ok=>{ if(ok){ lib.id=null; cmd('deletePreset',{id:x.id}); } }); }]]
      .concat(dirty&&loaded?[['Save','primary',e=>saveLive(e&&e.currentTarget)]]:[])));
    d.appendChild(h);
    // the chain at a glance
    const mc=el('div','minichain'), st=AMPS[x.amp]||AMPS[0];
    const unit=(cls,styleFn,label,off)=>{ const u=el('div','mc'+(cls?' '+cls:'')+(off?' off':'')); const i=el('i'); styleFn(u,i); u.appendChild(i); const s=el('span'); s.textContent=label; u.appendChild(s); return u; };
    const parts=[];
    (x.pre||[]).forEach(b=>parts.push(unit('',(u,i)=>u.style.setProperty('--c',PAINT[b.id]||CATC[b.category]),b.name,b.off)));
    parts.push(unit('amp',(u)=>styleAmp(u,st),st.name));
    parts.push(unit('cab',(u)=>u.style.setProperty('--g',st.g),(CABS[x.cab]||CABS[1]).name));
    (x.post||[]).forEach(b=>parts.push(unit('',(u,i)=>u.style.setProperty('--c',PAINT[b.id]||CATC[b.category]),b.name,b.off)));
    parts.forEach((u,i)=>{ if(i) mc.appendChild(el('span','arrow')); mc.appendChild(u); });
    d.appendChild(mc);
    const g=el('div','libgrid');
    const a=el('div'); a.appendChild(el('h3',null,'Used in'));
    const used=el('div','used'), L2='ABCDEF';
    const sets=(S.songs||[]).filter(s=>s.scenes.some(sc=>sc.id===x.id));
    sets.slice(0,4).forEach(s=>{ const i=s.scenes.findIndex(sc=>sc.id===x.id);
      const b=el('button',null,esc(s.name)+'<span>SCENE '+(i<6?L2[i]+' · FOOTSWITCH '+(i+1):String(i+1))+'</span>');
      b.onclick=()=>{ lib={tab:'sets',id:s.id,page:0,q:''}; renderLibrary(); }; used.appendChild(b); });
    if(!sets.length) used.appendChild(el('div','help','Not in any set yet. Add it from a set’s + tile.'));
    a.appendChild(used); g.appendChild(a);
    const hb=el('div'); hb.appendChild(el('h3',null,'History'));
    const hist=el('div','hist');
    (x.history||[]).slice(0,5).forEach((c,i)=>{ const r=el('div','hr'+(i===0?' cur':'')); r.innerHTML='<i></i><span style="font-family:var(--font-ui);font-size:13px;color:var(--muted)">'+esc(c.x)+'</span><span>'+esc(fmtWhen(c.t))+'</span>'; hist.appendChild(r); });
    if(!(x.history||[]).length) hist.appendChild(el('div','help','Each save is noted here, with what changed.'));
    hb.appendChild(hist); g.appendChild(hb);
    d.appendChild(g);
    return;
  }
  if(lib.tab==='sets'){
    const h=el('div','lhead');
    const t=el('div'); t.innerHTML='<span class="cap">Set · '+x.scenes.length+' scenes · '+Math.round(x.tempo)+' BPM</span><h1>'+esc(x.name)+'</h1>';
    h.appendChild(t);
    h.appendChild(libActs([
      [x.id===S.songId?'Open':'Open', 'primary', ()=>cmd('loadSong',{id:x.id,scene:0}).then(()=>showView('rig'))],
      ['Rename','',()=>ask('Rename set',x.name,'Rename').then(n=>{ if(n) cmd('renameSong',{id:x.id,name:n}); })],
      ['Tempo','',()=>ask('Tempo (BPM)',String(Math.round(x.tempo)),'Set').then(t=>{ const v=parseFloat(t); if(v>=30&&v<=300) cmd('renameSong',{id:x.id,tempo:v}); })],
      ['Delete','danger',()=>confirmBox('Delete “'+x.name+'”?','Its presets are kept.').then(ok=>{ if(ok){ lib.id=null; cmd('deleteSong',{id:x.id}); } })]]));
    d.appendChild(h);
    const sc=el('div'); sc.style.marginTop='14px';
    const per=Math.max(3,Math.floor(((d.clientHeight||420)-170)/49)), L2='ABCDEF';
    x.scenes.slice(0,per).forEach((s,i)=>{
      const r=el('div','sl','<span class="i">'+(i<6?L2[i]:i+1)+'</span><span class="t">'+esc(s.name)+'</span>');
      const ib=(t,label,fn)=>{ const y=el('button','iconbtn',t); y.setAttribute('aria-label',label); y.onclick=fn; r.appendChild(y); };
      ib('▲','Move up',()=>{ if(i>0) cmd('songScene',{song:x.id,action:'move',index:i,to:i-1}); });
      ib('▼','Move down',()=>{ if(i<x.scenes.length-1) cmd('songScene',{song:x.id,action:'move',index:i,to:i+1}); });
      ib('✕','Remove',()=>cmd('songScene',{song:x.id,action:'remove',index:i}));
      sc.appendChild(r);
    });
    if(x.scenes.length>per) sc.appendChild(el('div','help','+ '+(x.scenes.length-per)+' more — open the set to step through them.'));
    d.appendChild(sc);
    const add=el('button','btn','+ Add a preset'); add.style.marginTop='12px'; add.onclick=()=>openSetPicker(x);
    d.appendChild(add);
    return;
  }
  if(lib.tab==='songs'){
    const h=el('div','lhead');
    const t=el('div'); t.innerHTML='<span class="cap">Song · '+(x.kind==='video'?'video':'audio')+'</span><h1>'+esc(x.name)+'</h1>';
    h.appendChild(t);
    h.appendChild(libActs([
      ['Open in Learn','primary',()=>{ L.source='song'; showView('learn'); openLesson(x.id); }],
      ['Rename','',()=>ask('Rename',x.name,'Rename').then(n=>{ if(!n) return; LDB.tx('readonly').then(st=>new Promise(res=>{ const q=st.get(x.id); q.onsuccess=()=>res(q.result); })).then(r=>{ if(!r) return; r.name=n; return LDB.put(r); }).then(()=>refreshLessons()).then(renderLibrary); })],
      ['Delete','danger',()=>confirmBox('Delete “'+x.name+'”?','The song and its analysis are removed from Top End.').then(ok=>{ if(!ok) return;
        if(L.lesson&&L.lesson.id===x.id){ L.lesson=null; L.analysis=null; L.lessonId=null; SP.unload(); }
        cmd('stem',{action:'delete',id:x.id}); LDB.del(x.id).then(()=>refreshLessons()).then(()=>{ lib.id=null; renderLibrary(); }); })]]));
    d.appendChild(h);
    d.appendChild(el('div','help','Songs live inside Top End on this computer. Open one in Learn to see its chords and play along.'));
    return;
  }
  const h=el('div','lhead');
  const t=el('div'); t.innerHTML='<span class="cap">Capture · impulse response</span><h1>'+esc(x.name)+'</h1>';
  h.appendChild(t);
  h.appendChild(libActs([['Use on this rig','primary',()=>{ send('cab.model',3); send('cab.ir',+x.id); toast('Cab › Your IR: '+x.name); }],['Open folder','',()=>cmd('openFolder')],['Rescan','',()=>cmd('rescan')]]));
  d.appendChild(h);
  d.appendChild(el('div','help','An impulse response is a recording of a real cabinet and microphone. Put .wav files in the IRs folder, then Rescan. Amp captures (NAM) are not supported yet.'));
}
$('#libSeg').addEventListener('click',e=>{ const k=e.target.dataset&&e.target.dataset.k; if(k){ lib={tab:k,id:null,page:0,q:lib.q}; renderLibrary(); } });
$('#libSearch').addEventListener('input',e=>{ lib.q=e.target.value; lib.page=0; lib.id=null; renderLibrary(); });
/** Beta: choose presets to send back to the studio. They are copied into the
 *  Beta Presets folder with a note on how to send them; nothing is uploaded. */
function openShareSheet(){
  const body=el('div');
  body.appendChild(el('p','help','Choose the presets to send back. They are copied into Documents › Amanorsac Studio › '+esc(PRODUCT.name)+' › Beta Presets, with a note on how to send them. Nothing is sent automatically.'));
  const credit=el('input','field'); credit.placeholder='Your name for the credit (optional)'; credit.maxLength=60; credit.setAttribute('aria-label','Name for the credit');
  body.appendChild(credit);
  const list=el('div','sharelist'), picks=new Set();
  (S.presets||[]).forEach(p=>{ const row=el('label','sharerow'); const cb=el('input'); cb.type='checkbox';
    cb.onchange=()=>{ if(cb.checked) picks.add(p.id); else picks.delete(p.id); go.disabled=!picks.size; go.textContent=picks.size?'Copy '+picks.size+(picks.size===1?' preset':' presets'):'Copy presets'; };
    row.appendChild(cb); row.appendChild(el('span',null,esc(p.name))); list.appendChild(row); });
  body.appendChild(list);
  openSheet({title:'Share presets', body, actions:[
    {label:'Cancel', onClick:closeSheet},
    {label:'Copy presets', kind:'primary', onClick:()=>{ if(!picks.size) return; cmd('sharePresets',{ids:[...picks],credit:credit.value.trim()}).then(n=>{ closeSheet(); toast(n>0?n+(n===1?' preset copied.':' presets copied.')+' Send them as the note in the folder says.':'Nothing was copied.'); }); }}]});
  const go=[...document.querySelectorAll('#sheet .sf button')].pop(); go.disabled=true;
}
$('#libShare').onclick=openShareSheet;
$('#libNewPreset').onclick=()=>ask('New preset','New Rig','Create').then(n=>{ if(n) cmd('newPreset',{name:n}).then(id=>{ lib={tab:'presets',id,page:0,q:''}; toast('Created '+n+' — it starts with an empty board'); }); });
$('#libNewSet').onclick=()=>newSet();
function newSet(){ return ask('New set','Sunday','Create').then(n=>{ if(!n) return;
  cmd('newSong',{name:n,empty:true}).then(id=>{ lib={tab:'sets',id,page:0,q:''}; toast(n+' — add presets with +'); }); }); }

/* ═════════════════════════════ SETTINGS ═════════════════════════════ */
let spanel='audio';
function showPanel(p){ spanel=p;
  [...$('#snav').children].forEach(b=>b.classList.toggle('on',b.dataset.p===p));
  document.querySelectorAll('.spanel').forEach(x=>x.classList.toggle('on',x.id==='sp-'+p));
  if(p==='audio') refreshAudio(); }
$('#snav').addEventListener('click',e=>{ const b=e.target.closest('button'); if(b&&b.dataset.p) showPanel(b.dataset.p); });
const ROLE={presetNext:'Next scene',presetPrev:'Previous scene',tapTempo:'Tap tempo',tuner:'Tuner',
  loopRecord:'Loop record / play',loopStop:'Loop stop',loopUndo:'Loop undo',loopClear:'Loop clear',panic:'Reset',bypassAll:'Bypass All'};
function ccInput(role,cc){
  const i=el('input','ccin'); i.type='number'; i.min=0; i.max=127; i.inputMode='numeric'; i.placeholder='—';
  i.value=cc!=null?cc:''; i.setAttribute('aria-label',(ROLE[role]||role)+' CC');
  i.onchange=()=>cmd('bindSwitch',{cc:i.value===''?-1:parseInt(i.value,10),role});
  return i;
}
function renderSettings(){
  const b={}; (S.switches||[]).forEach(s=>{ b[s.role]=s.cc; });
  const fs=$('#fswScenes'); fs.innerHTML='';
  'ABCDEF'.split('').forEach(L1=>{ const f=el('div','f'); f.appendChild(el('b',null,L1)); f.appendChild(el('span','cap','Scene '+L1)); f.appendChild(ccInput('scene'+L1,b['scene'+L1])); fs.appendChild(f); });
  const t=$('#switchRows'); t.innerHTML='';
  (S.switchRoles||[]).filter(r=>!/^scene[A-F]$/.test(r)).forEach(role=>{
    const r=el('div','kv'); r.appendChild(el('span',null,esc(ROLE[role]||role))); r.appendChild(ccInput(role,b[role])); t.appendChild(r); });
  const m=$('#bindRows'); m.innerHTML='';
  if(!(S.bindings||[]).length) m.appendChild(el('div','help','None yet.'));
  (S.bindings||[]).slice(0,6).forEach(x=>{
    const r=el('div','kv'); r.appendChild(el('span',null,esc(x.label)+' <span class="cap">'+esc(x.path)+'</span>'));
    const right=el('div'); right.style.cssText='display:flex;align-items:center;gap:8px';
    right.appendChild(el('b',null,'CC '+x.cc));
    const rm=el('button','btn small ghost','Remove'); rm.onclick=()=>cmd('unbind',{path:x.path}); right.appendChild(rm);
    r.appendChild(right); m.appendChild(r);
  });
  const ref=S.tunerRef||440;
  $('#tunerRef').textContent=ref.toFixed(ref%1?1:0)+' Hz'; $('#snavRef').textContent=Math.round(ref);
  $('#tunerRefTag').textContent='A '+Math.round(ref);
  $('#tunerMuteSw').classList.toggle('on',S.tunerMutes!==false);
  $('#tunerMuteTag').hidden=S.tunerMutes===false;
  const trim=pick(S.in,'trim'), tb=$('#audTrim');
  if(trim && !document.querySelector('#audTrim .dial:active')){ tb.innerHTML=''; tb.appendChild(control(trim,{label:'Input trim',col:'#e3a447'})); }
}
$('#refDown').onclick=()=>cmd('tunerRef',{hz:Math.max(415,(S.tunerRef||440)-1)});
$('#refUp').onclick=()=>cmd('tunerRef',{hz:Math.min(466,(S.tunerRef||440)+1)});
$('#tunerMuteSw').onclick=()=>cmd('tunerMute',{on:S.tunerMutes===false});

/* ═════════════════════════════ STATE / TICK ═════════════════════════════ */
function applyState(st){
  const first=!S;
  S=st;
  if(first) setTimeout(refreshAudio,400);
  renderGate(st.tester);
  bound={}; (st.bindings||[]).forEach(b=>{ bound[b.path]=b.cc; });
  const sg=song(), i=sg?sg.scenes.findIndex(x=>x.id===st.presetId):-1;
  $('#rigName').textContent=st.name||'—';
  const sub=$('#rigSub');
  sub.textContent=(sg&&i>=0?(sg.name+' · scene '+'ABCDEF'[i]||String(i+1)):'PRESET')+(st.dirty?' · EDITED':'');
  sub.classList.toggle('dirty',!!st.dirty);
  $('#btnSave').hidden=!st.dirty;
  $('#bpm').textContent=Math.round(st.tempo||120);
  $('#btnTuner').classList.toggle('on',!!st.tunerActive);
  $('#btnBypass').classList.toggle('on',!!st.bypassAll); $('#btnBypass').setAttribute('aria-pressed',!!st.bypassAll);
  $('#btnBypass').textContent=st.bypassAll?'Bypassed':'Bypass'; document.body.classList.toggle('bypassed',!!st.bypassAll);
  $('#tuner').classList.toggle('on',!!st.tunerActive);
  ytSync();
  if(st.learning) toast('MIDI Learn: move a control on your device');
  /* A drag in progress must not be rebuilt out from under the finger. */
  if(document.querySelector('.dial:active,.akn:active,.rng:active')) return;
  renderBoard(); renderStage(); renderLibrary(); renderSettings();
}

const DASH=2*Math.PI*42;
const dbOf=v=>v>0.0001?20*Math.log10(v):-80;
function applyTick(t){
  learnTick(t);
  coverTick(t);
  const meter=(id,vid,v,clip)=>{ const m=$(id); if(!m) return; m.classList.toggle('clip',!!clip);
    const db=dbOf(v); m.firstElementChild.style.width=clamp((db+60)/60*100,0,100)+'%';
    const n=$(vid); if(n) n.textContent=db<=-79?'—':(db<0?'−':'')+Math.abs(db).toFixed(1); };
  meter('#mIn','#mInV',t.in); meter('#mOut','#mOutV',t.out,t.clip);
  // An open interface always delivers some noise. Exact digital silence for
  // four seconds means no audio is arriving: usually another program holds
  // the interface. Said once, until audio arrives again.
  if(t.in===0){ applyTick.zero=(applyTick.zero||0)+1;
    if(applyTick.zero===120&&!applyTick.warned){ applyTick.warned=true;
      toast('No signal from the audio interface. Another program may be using it. Close it, or select another device in Settings › Audio.'); } }
  else { applyTick.zero=0; applyTick.warned=false; }
  if(spanel==='audio' && view==='settings'){
    const fl=$('#audFloor'); if(fl) fl.textContent=t.floor>-119?t.floor.toFixed(0)+' dBFS':'—';
    const ga=$('#gateAutoSw'); if(ga&&S) ga.classList.toggle('on',!!S.gateAuto);
    if(S) document.querySelectorAll('#humSeg button').forEach(b=>b.classList.toggle('on',+b.dataset.hz===(S.humHz||0)));
    const db=dbOf(t.in); $('#audInB').style.width=clamp((db+60)/60*100,0,100)+'%';
    $('#audInV').textContent=db<=-79?'—':(db<0?'−':'')+Math.abs(db).toFixed(1);
    if(db>-1){ $('#audClip').classList.add('on'); clearTimeout(applyTick._clip); applyTick._clip=setTimeout(()=>$('#audClip').classList.remove('on'),1500); }
  }
  $('#bpm').textContent=Math.round(t.tempo||120);

  const lp=t.loop||{}, st=lp.state||'idle';
  const rec=$('#loopRec');
  rec.className='tbtn'+(st==='recording'||st==='closing'?' rec':st==='overdub'?' dub':st==='playing'?' play':'');
  rec.textContent = (st==='idle'||st==='stopped') ? (lp.seconds>0?'PLAY':'REC') : (st==='recording'||st==='closing') ? 'PLAY' : st==='playing' ? 'DUB' : 'PLAY';
  const names={idle:'Idle',armed:'Play to start',cued:'Starts on the next bar',recording:'Recording',closing:'Closing on the bar',playing:'Playing',overdub:'Overdub',stopped:'Stopped'};
  $('#loopState').textContent=names[st]||st;
  $('#loopLen').textContent=lp.seconds>0?lp.seconds.toFixed(2)+' s'+(lp.bars>0.05&&lp.grid?' · '+lp.bars.toFixed(2)+' bars':''):'—';
  $('#loopUndo').disabled=!lp.canUndo;
  $('#loopClear').disabled=!(lp.seconds>0);
  $('#loopArc').setAttribute('stroke-dashoffset',(DASH*(1-(lp.position||0))).toFixed(1));
  const Ly=$('#loopLayers');
  if(Ly.children.length!==(lp.layers||[]).length){
    Ly.innerHTML='';
    (lp.layers||[]).forEach((_,i)=>{ const b=el('button'); b.setAttribute('aria-label','Layer '+(i+1)); b.onclick=()=>cmd('loop',{action:'layer',index:i}); Ly.appendChild(b); });
  }
  (lp.layers||[]).forEach((h,i)=>{ const n=Ly.children[i]; if(n) n.className=(h?'has':'')+(lp.layer===i?' sel':''); });
  if(lp.countingIn){ rec.className='tbtn dub'; rec.textContent='CANCEL'; $('#loopState').textContent='Count-in'; }
  else if(st==='recording'){ const lb=(t.click&&t.click.loopBars)||0; $('#loopLen').textContent=lb?'Ends after '+lb+(lb===1?' bar':' bars'):(lp.grid?'Ends on the bar':'Free length'); }
  else if(st==='cued') $('#loopLen').textContent='With the click';
  renderClick(t.click||{}, !!lp.countingIn);

  const tn=t.tuner||{};
  $('#tunerChip').textContent=tn.has&&S&&S.tunerActive?String(tn.note).replace(/-?\d+$/,''):'Tune';
  if(S && S.tunerActive){
    $('#tNote').textContent=tn.has?String(tn.note).replace(/-?\d+$/,''):'—';
    $('#tNote').classList.toggle('ok',!!tn.inTune);
    $('#tSub').textContent=tn.has?String(tn.note)+' · '+tn.hz.toFixed(1)+' Hz':' ';
    const c=tn.has?clamp(tn.cents,-50,50):0;
    $('#tNeedle').style.left=(50+c)+'%';
    $('#tNeedle').classList.toggle('ok',!!tn.inTune);
    $('#tCents').textContent=tn.has?(c>0?'+':'')+c.toFixed(0)+' cents':' ';
    // The string being tuned, by octave as well as name: the two E strings are
    // two octaves apart, and lighting both would be no help at all.
    const midi=tn.has?Math.round(69+12*Math.log2(tn.hz/(S.tunerRef||440))):-1;
    const open=TUNING;
    let near=-1; if(midi>=0){ let best=99; open.forEach((p,i)=>{ const d=Math.abs(midi-p); if(d<best){ best=d; near=i; } }); if(best>2) near=-1; }
    [...$('#tStrings').children].forEach((s,i)=>{ s.classList.toggle('on',i===near&&!tn.inTune); s.classList.toggle('ok',i===near&&!!tn.inTune); });
  }
}
