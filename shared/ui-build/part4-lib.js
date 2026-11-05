
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
  if(lib.tab==='presets') return (S.presets||[]).filter(p=>(hit(p.name)||hit(p.category||'')||hit(p.packName||''))
    &&(!lib.cat||lib.cat==='All'||p.category===lib.cat)&&srcHit(p));
  if(lib.tab==='packs') return packList().filter(p=>hit(p.name)||hit(p.author));
  if(lib.tab==='sets') return (S.songs||[]).filter(s=>hit(s.name));
  if(lib.tab==='songs') return (L.lessons||[]).filter(s=>hit(s.name));
  return [...(S.captures||[]).map(c=>({id:'nam:'+c.key,key:c.key,name:c.name,kind:'nam'})),
          ...(S.irs||[]).map((f,i)=>({id:String(i),name:String(f).split(/[\\/]/).pop(),path:f,kind:'ir'}))].filter(x=>hit(x.name));
}
/** Where a preset came from: the factory, a pack the player installed, or their own. */
const srcOf=p=>p.packId?'pack:'+p.packId:p.factory?'factory':'mine';
const srcHit=p=>!lib.src||lib.src==='all'||srcOf(p)===lib.src;
/** The packs to list: the factory library, every installed pack, and the player's own presets. */
function packList(){
  const pr=S.presets||[], n=f=>pr.filter(f).length;
  return [{id:'factory',name:'Factory',author:'Amanorsac Studio',colour:0xffe3a447,count:n(p=>p.factory&&!p.packId),builtin:true,
           description:'Every preset that comes with '+PRODUCT.name+'.'},
    ...(S.packs||[]).map(p=>Object.assign({},p,{count:n(x=>x.packId===p.id)})),
    {id:'mine',name:'My Presets',author:'You',colour:0xff5d636b,count:n(p=>!p.factory&&!p.packId),mine:true,
     description:'The presets you have made or saved yourself.'}];
}
const packHex=c=>'#'+((c>>>0)&0xffffff).toString(16).padStart(6,'0');
function packCover(p,big){
  const c=packHex(p.colour||0xff2a8fa8), u=el('span','pcover'+(big?' big':''));
  u.style.background='linear-gradient(150deg,'+c+','+c+'aa 60%,#0009)';
  u.appendChild(el('b','',esc((p.name||'?').trim().charAt(0).toUpperCase())));
  return u;
}
function chainSwatches(p){
  const all=[...(p.pre||[]),...(p.post||[])].slice(0,6);
  let s='<span class="sw6">';
  for(let i=0;i<6;i++){ const b=all[i]; s+='<i'+(b?' style="background:'+(PAINT[b.id]||CATC[b.category]||'#5d636b')+(b.off?';opacity:.35':'')+'"':'')+'></i>'; }
  return s+'</span>';
}
/** Category filters, with how many presets each holds (only categories in use). */
function renderLibFilters(){
  const box=$('#libFilters'); box.innerHTML=''; box.hidden=lib.tab!=='presets';
  if(lib.tab!=='presets') return;
  const pr=(S.presets||[]).filter(srcHit), cats=['All',...(S.categories||[])];
  pr.forEach(p=>{ if(p.category&&cats.indexOf(p.category)<0) cats.push(p.category); });
  const chips=el('div','cchips');
  cats.forEach(c=>{ const n=c==='All'?pr.length:pr.filter(p=>p.category===c).length; if(!n&&c!=='All') return;
    const b=el('button','cchip'+((lib.cat||'All')===c?' on':''),esc(c)+'<i>'+n+'</i>');
    b.onclick=()=>{ lib.cat=c; lib.page=0; lib.id=null; renderLibrary(); }; chips.appendChild(b); });
  box.appendChild(chips);
  const row=el('div','csrc'); row.appendChild(el('span','cap','Showing'));
  const s=el('select'); s.setAttribute('aria-label','Which presets');
  [['all','All presets'],['factory','Factory'],['mine','My presets'],...(S.packs||[]).map(p=>['pack:'+p.id,p.name])]
    .forEach(([v,t])=>{ const o=el('option'); o.value=v; o.textContent=t; if((lib.src||'all')===v) o.selected=true; s.appendChild(o); });
  s.onchange=()=>{ lib.src=s.value; lib.page=0; lib.id=null; renderLibrary(); };
  row.appendChild(s); box.appendChild(row);
}
function renderLibrary(){
  libSig=libSignature();
  [...$('#libSeg').children].forEach(b=>b.classList.toggle('on',b.dataset.k===lib.tab));
  const nr=$('#libNewRow'); nr.innerHTML='';
  const nb=(label,kind,fn)=>{ const b=el('button','btn'+(kind?' '+kind:''),esc(label)); b.onclick=fn; nr.appendChild(b); };
  if(lib.tab==='packs'){ nb('Install a pack…','primary',chooseAndReadPack); nb('Make a pack','',()=>openMakePack()); }
  else { nb('+ New preset','',newPresetHere); nb('+ New set','',()=>newSet()); nb('Make a pack','',()=>openMakePack()); }
  renderLibFilters();
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
      const tag=edited(x.id)?'<span class="k" style="color:var(--amber);border-color:var(--amber-deep)">EDITED</span>'
        :at>=0&&at<6?'<span class="k">'+L2[at]+'</span>'
        :x.packId?'<span class="k pk">'+esc(String(x.packName||'').toUpperCase())+'</span>'
        :x.category?'<span class="k">'+esc(x.category.toUpperCase())+'</span>':(x.id===S.presetId?'<span class="live"></span>':'<span></span>');
      html=chainSwatches(x)+'<span style="min-width:0"><span class="t">'+esc(x.name)+(x.id===S.presetId?' <span class="live" style="display:inline-block;vertical-align:middle"></span>':'')+'</span><span class="s">'+esc(((AMPS[x.amp]||{}).name||'')+' · '+((n=>n+(n===1?' pedal':' pedals'))((x.pre||[]).length+(x.post||[]).length)))+'</span></span>'+tag;
    } else if(lib.tab==='packs'){
      const r=el('button','li pli'+(x.id===lib.id?' on':''));
      r.appendChild(packCover(x));
      r.insertAdjacentHTML('beforeend','<span style="min-width:0"><span class="t">'+esc(x.name)+'</span><span class="s">'+esc(x.author)+' · '+x.count+(x.count===1?' preset':' presets')+'</span></span><span class="k'+(x.builtin||x.mine?'':' pk')+'">'+(x.builtin?'BUILT IN':x.mine?'YOURS':'INSTALLED')+'</span>');
      r.onclick=()=>{ lib.id=x.id; renderLibrary(); };
      list.appendChild(r); return;
    } else if(lib.tab==='sets'){
      html='<span class="sw6"><i style="background:var(--amber)"></i><i></i><i></i></span><span style="min-width:0"><span class="t">'+esc(x.name)+'</span><span class="s">'+x.scenes.length+' SCENES · '+Math.round(x.tempo)+' BPM</span></span>'+(x.id===S.songId?'<span class="live"></span>':'<span></span>');
    } else if(lib.tab==='songs'){
      html='<span class="sw6"><i style="background:var(--d6)"></i><i style="background:var(--d1)"></i><i style="background:var(--d5)"></i></span><span style="min-width:0"><span class="t">'+esc(x.name)+'</span><span class="s">'+(x.kind==='video'?'VIDEO':'AUDIO')+'</span></span>'+(L.lesson&&x.id===L.lesson.id?'<span class="live"></span>':'<span></span>');
    } else {
      html='<span class="sw6"><i style="background:'+(x.kind==='nam'?'#22394a':'#8ea6b4')+'"></i></span><span style="min-width:0"><span class="t">'+esc(x.name)+'</span><span class="s">'+(x.kind==='nam'?'AMP CAPTURE':'IMPULSE RESPONSE')+'</span></span><span></span>';
    }
    const r=el('button','li'+(x.id===lib.id?' on':''),html);
    r.onclick=()=>{ lib.id=x.id; renderLibrary(); };
    list.appendChild(r);
  });
  if(!items.length) list.appendChild(el('div','empty',lib.q?'Nothing matches “'+esc(lib.q)+'”.':
    lib.tab==='songs'?'No songs yet. Import one on the Learn tab.':lib.tab==='captures'?'No captures yet. Drop a .nam amp capture or a .wav cab IR on the app.':'Nothing here yet.'));
  const pg=$('#libPager'); pg.innerHTML=''; pg.hidden=pages<2;
  for(let i=0;i<pages;i++){ const b=el('button',i===lib.page?'on':'','<i></i>'); b.setAttribute('aria-label','Page '+(i+1)); b.onclick=()=>{ lib.page=i; renderLibrary(); }; pg.appendChild(b); }
  renderLibDetail(items.find(x=>x.id===lib.id));
}
function libActs(btns){ const a=el('div','acts'); btns.forEach(([label,kind,fn])=>{ const b=el('button','btn'+(kind?' '+kind:''),esc(label)); b.onclick=fn; a.appendChild(b); }); return a; }
function renderLibDetail(x){
  homeRig();                            // take the rig back before the page is rebuilt
  const d=$('#libDetail'); d.innerHTML='';
  if(!x){ d.appendChild(el('div','empty','Choose something on the left.')); return; }
  if(lib.tab==='presets'){ presetDetail(d,x); return; }
  if(lib.tab==='packs'){ packDetail(d,x); return; }
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
  const t=el('div');
  if(x.kind==='nam'){
    t.innerHTML='<span class="cap">Capture · your amp (NAM)</span><h1>'+esc(x.name)+'</h1>';
    h.appendChild(t);
    h.appendChild(libActs([['Use on this rig','primary',()=>useCapture(x)],['Add a capture…','',()=>chooseCapture()],
      ['Open folder','',()=>cmd('showCaptures')],
      ['Remove','danger',()=>confirmBox('Remove “'+x.name+'”?','The file goes to the Recycle Bin. Presets that use it play straight through until you add it again.')
        .then(ok=>{ if(ok){ lib.id=null; cmd('removeCapture',{key:x.key}); } })]]));
    d.appendChild(h);
    d.appendChild(el('div','help','A Neural Amp Modeler capture is a learned copy of a real amp. It plays on the Your Amp pedal, with the built-in amp switched off. Every capture is checked, and timed on this computer, before it is used.'));
    return;
  }
  t.innerHTML='<span class="cap">Capture · impulse response</span><h1>'+esc(x.name)+'</h1>';
  h.appendChild(t);
  h.appendChild(libActs([['Use on this rig','primary',()=>{ send('cab.model',PRODUCT.cabIr); send('cab.ir',+x.id); toast('Cab › Your IR: '+x.name); }],['Open folder','',()=>cmd('openFolder')],['Rescan','',()=>cmd('rescan')]]));
  d.appendChild(h);
  d.appendChild(el('div','help','An impulse response is a recording of a real cabinet and microphone. Put .wav files in the IRs folder, then Rescan.'));
}
/* ───────────── YOUR AMP CAPTURES ─────────────
   Like a pack, the page reads the file and hands its text to the engine, which
   builds and times it before copying it into the Amp Captures folder. */
const CAPTURE_MAX=32*1024*1024;
function readCaptureFile(file,then){
  if(!file) return;
  if(!/\.nam$/i.test(String(file.name||''))){ toast('That is not an amp capture (.nam file)'); return; }
  if(file.size>CAPTURE_MAX){ toast('That file is too big to be an amp capture'); return; }
  const fr=new FileReader();
  toast('Checking '+file.name+'…');
  fr.onload=()=>cmd('importCapture',{name:file.name,text:String(fr.result||'')}).then(r=>{
    if(!r||!r.ok){ toast((r&&r.error)||'That capture could not be read'); if(then) then(null); return; }
    toast('Added '+r.name+(r.rate&&Math.abs(r.rate-48000)>1?' ('+Math.round(r.rate/100)/10+' kHz capture)':''));
    if(then) then(r); });
  fr.onerror=()=>toast('That file could not be read');
  fr.readAsText(file);
}
function chooseCapture(then){
  const i=document.createElement('input'); i.type='file'; i.accept='.nam';
  i.onchange=()=>readCaptureFile(i.files&&i.files[0],then||(r=>{ if(r){ lib={tab:'captures',id:'nam:'+r.key,page:0,q:''}; if(view==='library') renderLibrary(); } }));
  i.click();
}
/** Put a capture on the rig: on the Your Amp pedal if there is one, else a new one first on the board. */
function useCapture(x){
  const at=(S.pre||[]).find(b=>b.type==='nam');
  const set=b=>{ const p=(b.params||[]).find(q=>q.id==='capture'); if(p) cmd('set',{path:p.path,value:x.key}); };
  if(at){ set(at); toast('Your Amp › '+x.name); return; }
  if((S.pre||[]).length>=8){ toast('The board is full. Remove a pedal first.'); return; }
  cmd('addBlock',{chain:'pre',type:'nam',index:(S.pre||[]).length}).then(()=>setTimeout(()=>{ const b=(S.pre||[]).find(q=>q.type==='nam'); if(b) set(b); toast('Added Your Amp with '+x.name); },120));
}
$('#libSeg').addEventListener('click',e=>{ const k=e.target.dataset&&e.target.dataset.k; if(k){ lib={tab:k,id:null,page:0,q:lib.q}; renderLibrary(); } });
$('#libSearch').addEventListener('input',e=>{ lib.q=e.target.value; lib.page=0; lib.id=null; renderLibrary(); });
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
  if(dnd||document.querySelector('.dial:active,.akn:active,.rng:active,.pkn:active')) return;
  renderBoard(); renderStage(); renderSettings();
  /* The engine sends state about once a second. Rebuilding the Library each time moved the
     rig out and back and re-fitted it, which made the page shake; only redraw on a change. */
  if(view==='library'&&libSignature()!==libSig) renderLibrary();
  if(presetMenuOpen()&&pmSignature()!==pm.sig) renderPresetMenu();
}
let libSig='';
const libSignature=()=>S?JSON.stringify([S.presets,S.packs,S.songs,S.irs,S.categories,S.presetId,S.songId,S.name,S.dirty,S.edits]):'';

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

/* ═════════════════════════════ THE RIG, IN THE LIBRARY ═════════════════════════════
   The loaded preset is edited in the Library with the very same board and amp
   as the Rig page: the nodes themselves are moved here while the Library shows
   it, and put back when it does not, so the two can never look or behave
   differently. A preset that is not loaded shows its pedals at rest, with Load. */
const rigParts=()=>[$('.card.board'),$('#ampBox'),$('#pBrowse')].filter(Boolean);
function homeRig(){
  const home=$('#view-rig .rigL'), parts=rigParts();
  if(!home||!parts.length||parts[0].parentElement===home) return;
  const first=home.firstChild; parts.forEach(n=>home.insertBefore(n,first));
  requestAnimationFrame(fitBoard);
}
function mountRig(box){
  box.classList.add('rigL'); rigParts().forEach(n=>box.appendChild(n));
  requestAnimationFrame(()=>{ fitBoard(); });
}
/** A preset that is not loaded: its pedals and amp as they sit on the board. */
function rigPreview(x){
  const wrap=el('div','libprev'), fit=el('div','boardfit'), c=el('div','chain');
  const face=b=>{ const cat=(S.catalog||[]).find(k=>k.type===b.id)||{params:[]};
    return pedalFace({type:b.id,name:b.name,category:b.category,params:cat.params},b.off?'off':''); };
  (x.pre||[]).forEach(b=>c.appendChild(face(b)));
  const st=AMPS[x.amp]||AMPS[0], a=el('div','ampmini'); styleAmp(a,st);
  a.innerHTML='<div class="pl"><i></i><i></i><i></i><i></i></div><div class="gr"></div><b>'+esc(st.name)+'</b>';
  c.appendChild(a);
  (x.post||[]).forEach(b=>c.appendChild(face(b)));
  fit.appendChild(c); wrap.appendChild(fit);
  const cta=el('div','libcta');
  cta.appendChild(el('span','cap',esc(st.name)+' · '+esc((CABS[x.cab]||CABS[1]).name)+' · knobs shown at rest'));
  const go=el('button','btn primary','Load to hear and edit'); go.onclick=()=>cmd('loadPreset',{id:x.id});
  cta.appendChild(go); wrap.appendChild(cta);
  requestAnimationFrame(()=>{ c.style.transform='none'; const s=Math.min(1,(fit.clientWidth-8)/Math.max(1,c.scrollWidth),(fit.clientHeight-8)/Math.max(1,c.offsetHeight));
    c.style.transformOrigin='0 0'; c.style.transform='scale('+s.toFixed(3)+')'; });
  return wrap;
}
/* ───────────── PRESET MENU (the name at the top of the Rig) ─────────────
   Tap a name to open it out: its pedals and amp shown small, with Load. */
const pm={open:null,q:'',sig:''};
const pmSignature=()=>JSON.stringify([S.presets,S.presetId,pm.open,pm.q]);
function miniChain(x){
  const fit=el('div','pmfit'), c=el('div','chain');
  const face=b=>{ const cat=(S.catalog||[]).find(k=>k.type===b.id)||{params:[]};
    return pedalFace({type:b.id,name:b.name,category:b.category,params:cat.params},b.off?'off':''); };
  (x.pre||[]).forEach(b=>c.appendChild(face(b)));
  const st=AMPS[x.amp]||AMPS[0], a=el('div','ampmini'); styleAmp(a,st);
  a.innerHTML='<div class="pl"><i></i><i></i><i></i><i></i></div><div class="gr"></div><b>'+esc(st.name)+'</b>';
  c.appendChild(a);
  (x.post||[]).forEach(b=>c.appendChild(face(b)));
  fit.appendChild(c);
  requestAnimationFrame(()=>{ const s=Math.min(.5,(fit.clientWidth-4)/Math.max(1,c.scrollWidth));
    c.style.transformOrigin='0 0'; c.style.transform='scale('+s.toFixed(3)+')'; fit.style.height=Math.ceil(c.offsetHeight*s+4)+'px'; });
  return fit;
}
const presetMenuOpen=()=>$('#scrim').classList.contains('on')&&!!$('#sheet .pmenu');
function renderPresetMenu(){
  const list=$('#sheet .pmenu .plist'); if(!list) return;
  pm.sig=pmSignature();
  const q=pm.q.trim().toLowerCase(), keep=list.scrollTop;
  const items=(S.presets||[]).filter(p=>!q||[p.name,p.category,p.packName].some(s=>String(s||'').toLowerCase().includes(q)));
  list.innerHTML='';
  items.forEach(p=>{
    const open=pm.open===p.id, loaded=p.id===S.presetId;
    const r=el('div','pmrow'+(open?' open':'')+(loaded?' on':''));
    const n=(p.pre||[]).length+(p.post||[]).length;
    const head=el('button','li'+(loaded?' on':''),chainSwatches(p)+'<span style="min-width:0"><span class="t">'+esc(p.name)
      +(loaded?' <span class="live" style="display:inline-block;vertical-align:middle"></span>':'')+'</span><span class="s">'
      +esc(((AMPS[p.amp]||{}).name||'')+' · '+n+(n===1?' pedal':' pedals'))+'</span></span>'
      +(p.category?'<span class="k">'+esc(p.category.toUpperCase())+'</span>':'<span></span>'));
    head.setAttribute('aria-expanded',open);
    head.onclick=()=>{ pm.open=open?null:p.id; renderPresetMenu(); };
    r.appendChild(head);
    if(open){
      const body=el('div','pmbody');
      body.appendChild(miniChain(p));
      const acts=el('div','pmacts');
      const go=el('button','btn'+(loaded?'':' primary'),loaded?'Loaded':'Load');
      go.onclick=()=>{ closeSheet(); if(!loaded) cmd('loadPreset',{id:p.id}); };
      const lb=el('button','btn','Open in Library');
      lb.onclick=()=>{ closeSheet(); lib={tab:'presets',id:p.id,page:0,q:'',cat:'All',src:'all'}; showView('library'); };
      acts.appendChild(go); acts.appendChild(lb); body.appendChild(acts);
      r.appendChild(body);
    }
    list.appendChild(r);
  });
  if(!items.length) list.appendChild(el('div','empty','Nothing matches “'+esc(pm.q)+'”.'));
  list.scrollTop=keep;
}
function openPresetMenu(){
  pm.open=S.presetId; pm.q='';
  const body=el('div','pmenu');
  const f=el('input','field'); f.placeholder='Search presets'; f.setAttribute('aria-label','Search presets');
  f.oninput=()=>{ pm.q=f.value; renderPresetMenu(); };
  body.appendChild(f);
  body.appendChild(el('div','plist'));
  openSheet({title:'Presets', body, actions:[
    {label:'New', onClick:()=>ask('New preset','New Rig','Create').then(n=>{ if(n) cmd('newPreset',{name:n}); })},
    {label:'Duplicate', onClick:()=>{ closeSheet(); cmd('duplicatePreset',{id:S.presetId}).then(()=>toast('Duplicated')); }},
    {label:'Rename', onClick:()=>ask('Rename preset',S.name,'Rename').then(n=>{ if(n&&S.presetId) cmd('renamePreset',{id:S.presetId,name:n}); })},
    {label:'Save as…', onClick:()=>ask('Save as new preset',S.name,'Save').then(n=>{ if(n) cmd('savePreset',{name:n,asNew:true}).then(()=>toast('Saved')); })},
    {label:'Save', kind:'primary', onClick:()=>{ closeSheet(); cmd('savePreset',{}).then(()=>toast('Saved')); }}]});
  renderPresetMenu();
  const on=$('#sheet .pmrow.on'); if(on) on.scrollIntoView({block:'center'});
}
function chooseCategory(x,anchor){
  const r=anchor.getBoundingClientRect();
  openMenu(r.left,r.bottom+4,[...(S.categories||[]),'None'].map(c=>({label:(x.category===c||(!x.category&&c==='None')?'✓ ':'')+c,
    onClick:()=>cmd('setCategory',{id:x.id,category:c==='None'?'':c}).then(()=>toast(c==='None'?'No category':'Moved to '+c))})));
}
function newPresetHere(){
  ask('New preset','New Rig','Create').then(n=>{ if(n) cmd('newPreset',{name:n}).then(id=>{ lib={tab:'presets',id,page:0,q:'',cat:'All',src:'all'};
    toast('Created '+n+'. Build it here: + adds a pedal.'); }); });
}
function presetDetail(d,x){
  const loaded=x.id===S.presetId, dirty=edited(x.id);
  const h=el('div','lhead');
  const t=el('div');
  t.innerHTML='<span class="cap">Preset'+(x.history&&x.history[0]?' · saved '+esc(fmtWhen(x.history[0].t)):'')+(dirty?' · changes not saved':'')+'</span><h1>'+esc(x.name)+'</h1>'
    +'<div class="ptags">'+(x.category?'<span class="k">'+esc(x.category.toUpperCase())+'</span>':'')
    +(x.packId?'<span class="k pk">FROM '+esc(String(x.packName||'').toUpperCase())+' · '+esc(String(x.packAuthor||'').toUpperCase())+'</span>':x.factory?'<span class="k">FACTORY</span>':'<span class="k">YOURS</span>')+'</div>';
  h.appendChild(t);
  const acts=libActs([
    [loaded?'Loaded':'Load', loaded?'':'primary', ()=>cmd('loadPreset',{id:x.id})],
    ['Rename','',()=>ask('Rename preset',x.name,'Rename').then(n=>{ if(n) cmd('renamePreset',{id:x.id,name:n}); })],
    ['Category ▾','',e=>chooseCategory(x,e.currentTarget)],
    ['Duplicate','',()=>cmd('duplicatePreset',{id:x.id}).then(id=>{ if(id){ lib.id=id; toast('Duplicated'); } })],
    ['Add to a pack','',()=>openMakePack({ids:[x.id]})],
    ['Delete','danger',()=>{ const usedIn=(S.songs||[]).filter(s=>s.scenes.some(sc=>sc.id===x.id)).map(s=>s.name);
      confirmBox('Delete “'+x.name+'”?',usedIn.length?'It will also come out of '+usedIn.join(', ')+'.':'This cannot be undone.').then(ok=>{ if(ok){ lib.id=null; cmd('deletePreset',{id:x.id}); } }); }]]
    .concat(dirty&&loaded?[['Save','primary',e=>saveLive(e&&e.currentTarget)]]:[]));
  h.appendChild(acts);
  d.appendChild(h);
  const body=el('div','libbody');
  const rig=el('div','librig');
  body.appendChild(rig);
  const side=el('div','libside');
  side.appendChild(el('h3',null,'Used in'));
  const used=el('div','used'), L2='ABCDEF';
  const sets=(S.songs||[]).filter(s=>s.scenes.some(sc=>sc.id===x.id));
  sets.slice(0,3).forEach(s=>{ const i=s.scenes.findIndex(sc=>sc.id===x.id);
    const b=el('button',null,esc(s.name)+'<span>SCENE '+(i<6?L2[i]:String(i+1))+'</span>');
    b.onclick=()=>{ lib={tab:'sets',id:s.id,page:0,q:''}; renderLibrary(); }; used.appendChild(b); });
  if(!sets.length) used.appendChild(el('div','help','Not in any set yet. Add it from a set’s + tile.'));
  side.appendChild(used);
  side.appendChild(el('h3',null,'History'));
  const hist=el('div','hist');
  (x.history||[]).slice(0,4).forEach((c,i)=>{ const r=el('div','hr'+(i===0?' cur':'')); r.innerHTML='<i></i><span style="font-family:var(--font-ui);font-size:12.5px;color:var(--muted)">'+esc(c.x)+'</span><span>'+esc(fmtWhen(c.t))+'</span>'; hist.appendChild(r); });
  if(!(x.history||[]).length) hist.appendChild(el('div','help','Each save is noted here, with what changed.'));
  side.appendChild(hist);
  body.appendChild(side);
  d.appendChild(body);
  if(loaded) mountRig(rig); else rig.appendChild(rigPreview(x));
}

/* ═════════════════════════════ PACKS ═════════════════════════════ */
const packPresets=p=>(S.presets||[]).filter(x=>p.builtin?(x.factory&&!x.packId):p.mine?(!x.factory&&!x.packId):x.packId===p.id);
function packDetail(d,p){
  const h=el('div','lhead phead');
  h.appendChild(packCover(p,true));
  const t=el('div'); t.style.cssText='flex:1;min-width:0';
  t.innerHTML='<span class="cap">Preset pack</span><h1>'+esc(p.name)+'</h1><div class="ptags"><span class="k">BY '+esc(String(p.author||'').toUpperCase())+'</span><span class="k">'+p.count+(p.count===1?' PRESET':' PRESETS')+'</span></div>'
    +(p.description?'<p class="help" style="margin:8px 0 0;max-width:560px">'+esc(p.description)+'</p>':'');
  h.appendChild(t);
  const btns=[];
  if(!p.builtin) btns.push(['Share this pack','primary',()=>sharePack(p)]);
  if(!p.builtin&&!p.mine) btns.push(['Remove pack','danger',()=>confirmBox('Remove “'+p.name+'”?','Its '+p.count+' presets come out of the Library and any set that uses them. You can install the pack again from its file.')
    .then(ok=>{ if(ok) cmd('removePack',{id:p.id}).then(n=>{ lib.id=null; toast('Removed '+n+' presets'); }); })]);
  h.appendChild(libActs(btns));
  d.appendChild(h);
  d.appendChild(el('h3','pinh','In this pack'));
  const grid=el('div','pgrid'); d.appendChild(grid);
  const list=packPresets(p), per=Math.max(3,Math.floor(((d.clientHeight||500)-260)/48))*3;
  list.slice(0,per).forEach(x=>{ const b=el('button','pgi',esc(x.name)+'<span>'+esc((x.category||'').toUpperCase())+'</span>');
    b.onclick=()=>{ lib={tab:'presets',id:x.id,page:0,q:'',cat:'All',src:p.builtin?'factory':p.mine?'mine':'pack:'+p.id}; renderLibrary(); }; grid.appendChild(b); });
  if(list.length>per) d.appendChild(el('div','help','+ '+(list.length-per)+' more on the Presets tab.'));
  if(!p.builtin) d.appendChild(el('div','sharehow','<b>How sharing works</b><ol><li><b>Share this pack</b> saves it as one file in Documents › Amanorsac Studio › '+esc(PRODUCT.name)+' › Packs and shows it to you.</li><li>Send the file any way you like: WhatsApp, email, Discord, a drive.</li><li>Whoever gets it drags it onto '+esc(PRODUCT.name)+'. Nothing goes online from the app.</li></ol>'));
}
function sharePack(p){
  const ids=packPresets(p).map(x=>x.id);
  if(!ids.length){ toast('This pack has no presets yet'); return; }
  writePack({id:p.mine?'':p.id,name:p.mine?'My Presets':p.name,author:p.mine?'':p.author,description:p.description||'',colour:p.colour,ids});
}
function writePack(o){
  cmd('writePack',o).then(r=>{
    if(!r||!r.file){ toast('Could not write the pack'); return; }
    const body=el('div','packdone');
    body.appendChild(packCover(o,true));
    body.appendChild(el('div','','<p style="margin:0 0 6px"><b>'+esc(r.file)+'</b> is in Documents › Amanorsac Studio › '+esc(PRODUCT.name)+' › Packs.</p><p class="help">Send it any way you like. Anyone with '+esc(PRODUCT.name)+' drags it onto the app to install it. '+o.ids.length+(o.ids.length===1?' preset, ':' presets, ')+Math.max(1,Math.round(r.bytes/1024))+' KB.</p>'));
    openSheet({title:'Pack saved', body, actions:[{label:'Done',onClick:closeSheet},{label:'Show the file',kind:'primary',onClick:()=>{ closeSheet(); cmd('showPackFile'); }}]});
  });
}
/** Make a pack: name, who made it, a few words, a cover colour, and the presets. */
function openMakePack(pre){
  const COLS=[0xff2a8fa8,0xff9c4a3f,0xffe3a447,0xff6f9f5a,0xff7b3fa0,0xff5d636b,0xffd9789b,0xff3f7fa0];
  const st={ids:new Set((pre&&pre.ids)||[]),colour:COLS[0],cat:'All',page:0};
  const body=el('div','makepack');
  const L1=el('div');
  const field=(label,node)=>{ const f=el('div','fld'); f.appendChild(el('span','cap',label)); f.appendChild(node); L1.appendChild(f); return node; };
  const nm=field('Pack name',el('input','field')); nm.maxLength=60; nm.placeholder='Sunday Pads';
  const au=field('Made by',el('input','field')); au.maxLength=60; au.placeholder='Your name';
  try{ au.value=localStorage.getItem(PRODUCT.prefix+'.packAuthor')||''; }catch(_){}
  const de=field('About it',el('textarea','field')); de.maxLength=500; de.rows=3; de.placeholder='What these presets are for';
  const cols=el('div','pcols');
  COLS.forEach(c=>{ const b=el('button','pcol'+(c===st.colour?' on':'')); b.style.background=packHex(c); b.setAttribute('aria-label','Cover colour');
    b.onclick=()=>{ st.colour=c; [...cols.children].forEach(x=>x.classList.toggle('on',x===b)); }; cols.appendChild(b); });
  field('Cover colour',cols);
  L1.appendChild(el('div','okmsg','✓ Your audio interface, MIDI and settings never go in a pack — only sounds.'));
  body.appendChild(L1);
  const R1=el('div'); body.appendChild(R1);
  const draw=()=>{
    R1.innerHTML='';
    const top=el('div','pkbar'); top.appendChild(el('b','','Choose presets'));
    ['All','Mine',...(S.categories||[])].forEach(c=>{ const n=c==='All'?1:c==='Mine'?(S.presets||[]).filter(p=>!p.factory&&!p.packId).length:(S.presets||[]).filter(p=>p.category===c).length; if(!n) return;
      const b=el('button','cchip'+(st.cat===c?' on':''),esc(c)); b.onclick=()=>{ st.cat=c; st.page=0; draw(); }; top.appendChild(b); });
    R1.appendChild(top);
    const all=(S.presets||[]).filter(p=>st.cat==='All'||(st.cat==='Mine'?(!p.factory&&!p.packId):p.category===st.cat));
    const per=16, pages=Math.max(1,Math.ceil(all.length/per)); st.page=Math.min(st.page,pages-1);
    const g=el('div','pick');
    all.slice(st.page*per,st.page*per+per).forEach(p=>{ const on=st.ids.has(p.id);
      const b=el('button','ck'+(on?' on':''),'<span class="box">'+(on?'✓':'')+'</span><span class="n">'+esc(p.name)+'</span><small>'+esc((p.category||'').toUpperCase())+'</small>');
      b.onclick=()=>{ if(st.ids.has(p.id)) st.ids.delete(p.id); else if(st.ids.size<200) st.ids.add(p.id); draw(); }; g.appendChild(b); });
    R1.appendChild(g);
    const foot=el('div','pkfoot'); foot.appendChild(el('span','help',st.ids.size+' chosen · up to 200 in a pack'));
    const pg=el('div','pager');
    if(pages>1) for(let i=0;i<pages;i++){ const b=el('button',i===st.page?'on':'','<i></i>'); b.setAttribute('aria-label','Page '+(i+1)); b.onclick=()=>{ st.page=i; draw(); }; pg.appendChild(b); }
    foot.appendChild(pg); R1.appendChild(foot);
  };
  draw();
  openSheet({title:'Make a pack', body, wide:true, actions:[{label:'Cancel',onClick:closeSheet},{label:'Save pack file',kind:'primary',onClick:()=>{
    const name=nm.value.trim();
    if(!name){ nm.focus(); toast('Give the pack a name'); return; }
    if(!st.ids.size){ toast('Choose at least one preset'); return; }
    try{ localStorage.setItem(PRODUCT.prefix+'.packAuthor',au.value.trim()); }catch(_){}
    closeSheet(); writePack({name,author:au.value.trim(),description:de.value.trim(),colour:st.colour,ids:[...st.ids]}); }}]});
  setTimeout(()=>nm.focus(),50);
}

/* Installing: the file is read here, by the page, and its text is handed to the
   engine, which checks every part of it before anything is added. */
const PACK_MAX=8*1024*1024;
function readPackFile(file){
  if(!file) return;
  const ext=(S.packExt||'.tepack').toLowerCase(), nm=String(file.name||'');
  if(!/\.(tepack|lepack)$/i.test(nm)){ toast('That is not a preset pack ('+ext+' file)'); return; }
  if(file.size>PACK_MAX){ toast('That file is too big to be a preset pack'); return; }
  const fr=new FileReader();
  fr.onload=()=>{ const text=String(fr.result||'');
    cmd('readPack',{text}).then(r=>{ if(!r||!r.ok){ toast((r&&r.error)||'That pack could not be read'); return; } confirmInstall(r,text); }); };
  fr.onerror=()=>toast('That file could not be read');
  fr.readAsText(file);
}
function chooseAndReadPack(){
  const i=document.createElement('input'); i.type='file'; i.accept='.tepack,.lepack';
  i.onchange=()=>readPackFile(i.files&&i.files[0]); i.click();
}
function confirmInstall(r,text){
  const info=r.info||{}, n=(r.presets||[]).length;
  const body=el('div','installpk');
  const L1=el('div'); L1.appendChild(packCover(info,true));
  L1.appendChild(el('div','ptags','<span class="k">BY '+esc(String(info.author||'').toUpperCase())+'</span><span class="k">'+n+(n===1?' PRESET':' PRESETS')+'</span>'));
  if(info.description) L1.appendChild(el('p','help',esc(info.description)));
  body.appendChild(L1);
  const R1=el('div'); R1.appendChild(el('b','','What will be added'));
  const g=el('div','pgrid two');
  (r.presets||[]).slice(0,10).forEach(p=>g.appendChild(el('div','pgi',esc(p.name)+'<span>'+esc(String(p.category||'').toUpperCase())+'</span>')));
  R1.appendChild(g);
  if(n>10) R1.appendChild(el('div','help','…and '+(n-10)+' more.'));
  R1.appendChild(el('div','okmsg','✓ Checked: '+n+(n===1?' preset':' presets')+' for '+esc(PRODUCT.name)+', nothing but sounds.'
    +(r.skipped?' '+r.skipped+(r.skipped===1?' pedal this version does not have was':' pedals this version does not have were')+' left out.':'')
    +(r.installed?' You have this pack already; installing replaces it.':' Your own presets are not changed.')));
  body.appendChild(R1);
  openSheet({title:'Install “'+(info.name||'pack')+'”?', body, wide:true, actions:[{label:'Cancel',onClick:closeSheet},
    {label:'Install '+n+(n===1?' preset':' presets'),kind:'primary',onClick:()=>{ closeSheet();
      cmd('installPack',{text}).then(res=>{ if(!res||!res.ok){ toast((res&&res.error)||'Could not install'); return; }
        lib={tab:'packs',id:info.id,page:0,q:''}; if(view!=='library') showView('library'); else renderLibrary();
        toast('Installed '+res.added+' presets from '+info.name); }); }}]});
}
/* Drop a pack anywhere on the app to install it. */
(()=>{
  const ov=el('div','packdrop','<div>Drop to add it<small>A preset pack or an amp capture. It is checked before anything is added.</small></div>'); document.body.appendChild(ov);
  const hasFile=e=>e.dataTransfer&&[...(e.dataTransfer.types||[])].indexOf('Files')>=0;
  let depth=0;
  window.addEventListener('dragenter',e=>{ if(!hasFile(e)||(view!=='library'&&view!=='rig')) return; depth++; ov.classList.add('on'); });
  window.addEventListener('dragleave',e=>{ if(!hasFile(e)) return; if(--depth<=0){ depth=0; ov.classList.remove('on'); } });
  window.addEventListener('dragover',e=>{ if(hasFile(e)) e.preventDefault(); });
  window.addEventListener('drop',e=>{ if(!hasFile(e)) return; depth=0; ov.classList.remove('on');
    const files=[...e.dataTransfer.files], f=files.find(f=>/\.(tepack|lepack)$/i.test(f.name)), nam=files.find(f=>/\.nam$/i.test(f.name));
    if(nam){ e.preventDefault(); e.stopPropagation(); readCaptureFile(nam,r=>{ if(r&&view==='library'){ lib={tab:'captures',id:'nam:'+r.key,page:0,q:''}; renderLibrary(); } }); return; }
    if(!f) return;                      // other files (songs, IRs) are handled where they are dropped
    e.preventDefault(); e.stopPropagation(); readPackFile(f); },true);
})();
