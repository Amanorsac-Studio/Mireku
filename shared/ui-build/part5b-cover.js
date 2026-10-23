/* ═════════════════════════════ COVER ═════════════════════════════
   A song from Learn played by the engine (CoverPlayer.h), so it reaches the
   audio interface and lines up with what is recorded. The page sends the
   transport and the mix, and draws what the engine reports: the playhead on
   every tick, the song and its takes on a 'cover' event. */
const C={
  id:null, name:'', a:null, st:null,
  pos:0, from:0, playing:false, recording:false, countIn:false, recFrom:null,
  punch:{a:null,b:null}, busy:false, exportWait:false,
  pref:{countIn:1, click:false, hearTake:true, take:1, song:1,
        stems:[0,1,2,3].map(()=>({level:1,mute:false,solo:false})), lastId:null}
};
try{ const p=JSON.parse(localStorage.getItem('te.cover')||'null'); if(p) Object.assign(C.pref,p); }catch(_){}
function saveCoverPrefs(){ try{ localStorage.setItem('te.cover',JSON.stringify(C.pref)); }catch(_){} }
/** A one-part song (never separated) keeps its own level and on/off, so the
 *  choices made for a separated song can never silence it. */
const stemPref=i=>(C.st&&C.st.stems&&C.st.stems.length<2)?(C.pref.single=C.pref.single||{level:1,mute:false,solo:false}):C.pref.stems[i];
const dbFrom=v=>v<=0.001?-60:20*Math.log10(v);

/** The song's timing, shared with Learn: beats, bars, tempo sections, start. */
const coverGrid=()=>{ const g=gridOf(C.a); return g&&g.beats.length>1?g:null; };
/** The "1" of the bar at or before t (t itself where there are no bars). */
function coverBar(t){
  const g=coverGrid(); if(!g) return Math.max(0,t);
  let at=null; g.beats.forEach((b,i)=>{ if(g.inBar[i]===0&&b<=t+1e-3) at=b; });
  return at==null?Math.max(0,t):at;
}
const coverStartAt=()=>{ const g=gridOf(C.a); return g?g.start:0; };
/** The Cover overlay: preparing a song, or separating it. null hides it. */
function coverProg(t,f){ const p=$('#cProg'); p.hidden=t==null; if(t!=null){ $('#cProgT').textContent=t; $('#cProgB').style.width=Math.round((f||0)*100)+'%'; } }
const coverDur=()=>(C.st&&C.st.length)||(C.a&&C.a.duration)||0;
const coverHead=()=>C.playing?Math.max(0,C.pos):C.from;

//---------------------------------------------------------------- songs
function coverSongs(){
  return LDB.all().then(list=>{
    list.sort((a,b)=>(b.created||0)-(a.created||0));
    const s=$('#cSong'); s.innerHTML='';
    const usable=list.filter(x=>x.blob);
    if(!usable.length){ const o=el('option',null,'No songs yet'); o.value=''; s.appendChild(o); }
    usable.forEach(x=>{ const o=el('option',null,esc(x.name)); o.value=x.id; s.appendChild(o); });
    const want=C.id||C.pref.lastId;
    if(want&&usable.some(x=>x.id===want)) s.value=want;
    if(s._ddSync) s._ddSync();
    return usable;
  }).catch(()=>[]);
}

async function coverOpen(id){
  if(!id||C.busy) return;
  const x=await LDB.tx('readonly').then(st=>new Promise(res=>{ const q=st.get(id); q.onsuccess=()=>res(q.result); q.onerror=()=>res(null); }));
  if(!x) return;
  C.id=id; C.name=x.name; C.a=x.analysis||null; C.from=coverStartAt(); C.punch={a:null,b:null};
  C.pref.lastId=id; saveCoverPrefs();
  cmd('cover',{action:'punch',a:-1,b:-1});
  pushCoverClick();
  renderCover();
  if(await cmd('cover',{action:'has',id})){ cmd('cover',{action:'load',id,name:x.name}); return; }

  // Never separated: the engine has no copy of this song yet. It is handed
  // over once and kept with the song's stems.
  if(!x.blob){ toast('This song has no audio file.'); return; }
  C.busy=true;
  const prog=coverProg;
  try{
    prog('Preparing the song',0);
    const buf=await decodeStereo441(x.blob);
    if(C.id!==id) return;
    const frames=buf.length;
    if(!await cmd('cover',{action:'begin',id,frames})){ toast('The song is too long for Cover (20 minutes at most).'); return; }
    const L0=buf.getChannelData(0), R0=buf.numberOfChannels>1?buf.getChannelData(1):L0, CH=1<<18;
    for(let off=0;off<frames;off+=CH){
      const n=Math.min(CH,frames-off), pcm=new Int16Array(n*2);
      for(let i=0;i<n;i++){ pcm[2*i]=Math.max(-32768,Math.min(32767,Math.round(L0[off+i]*32767))); pcm[2*i+1]=Math.max(-32768,Math.min(32767,Math.round(R0[off+i]*32767))); }
      let bin=''; const bytes=new Uint8Array(pcm.buffer);
      for(let i=0;i<bytes.length;i+=8192) bin+=String.fromCharCode.apply(null,bytes.subarray(i,i+8192));
      if(!await cmd('cover',{action:'chunk',id,offset:off,data:btoa(bin)})){ toast('The song could not be prepared. Try again.'); return; }
      if(C.id!==id) return;
      prog('Preparing the song',off/frames);
      await new Promise(r=>setTimeout(r,0));
    }
    if(!await cmd('cover',{action:'end',id,name:x.name})) toast('The song could not be prepared. Try again.');
  } finally { C.busy=false; prog(null); }
}

//---------------------------------------------------------------- engine
function coverStatus(d){
  if(!d) return;
  const was=C.st; C.st=d;
  if(d.state==='error'&&d.error&&(!was||was.error!==d.error)) toast(d.error);
  if(d.state==='ready'&&(!was||was.song!==d.song||was.state!=='ready')){ pushCoverMix(); pushCoverClick(); }
  if(C.exportWait&&!d.exporting){
    C.exportWait=false;
    const r=d.export||{};
    if(r.ok){
      const list=el('div','kv');
      (r.files||[]).forEach(f=>list.appendChild(el('div',null,esc(f.split(/[\\/]/).pop()))));
      if(r.trimDb<0) list.appendChild(el('p','cap','The mix peaked over −0.3 dBFS and was lowered by '+Math.abs(r.trimDb).toFixed(1)+' dB.'));
      openSheet({title:'Exported', body:list, actions:[
        {label:'Show in folder', onClick:()=>{ cmd('cover',{action:'reveal',path:(r.files||[])[0]||''}); closeSheet(); }},
        {label:'Close', kind:'primary', onClick:closeSheet}]});
    } else toast(r.error||'Export failed.');
  }
  renderCover();
}
function coverTick(t){
  const c=t&&t.cover; if(!c) return;
  const wasRec=C.recording;
  C.pos=c.pos; C.playing=!!c.playing; C.recording=!!c.recording; C.countIn=!!c.countIn;
  if(C.recording&&!wasRec) C.recFrom=null;
  if(C.recording&&!C.countIn&&C.recFrom==null) C.recFrom=Math.max(0,C.pos);
  if(!C.recording) C.recFrom=null;
  // a trimmed end stops the song there
  if(C.playing&&!C.countIn&&!C.recording){ const g=gridOf(C.a); if(g&&g.end<coverDur()-0.05&&C.pos>g.end+0.3&&!C.endStop){ C.endStop=true; cmd('cover',{action:'stop'}); } }
  if(!C.playing) C.endStop=false;
  if(view==='cover') drawCoverFrame();
}
function pushCoverMix(){
  const p=C.pref;
  cmd('cover',{action:'levels',song:0,take:dbFrom(p.take),hearTake:p.hearTake});
  [0,1,2,3].forEach(i=>{ const s=stemPref(i); cmd('cover',{action:'stem',index:i,db:dbFrom(s.level),mute:s.mute,solo:s.solo}); });
}
function pushCoverClick(){
  const g=coverGrid();
  cmd('cover',{action:'click',on:!!(C.pref.click&&g),bpm:g?(g.tempo||120):120,t0:0,beats:4,db:-6});
  // the beats themselves: free intros and tempo changes are clicked where they are
  cmd('cover',{action:'clickBeats',beats:g?g.beats:[],accents:g?g.inBar.map(x=>x===0):[]});
}

//---------------------------------------------------------------- transport
function coverStart(record){
  if(!C.st||C.st.state!=='ready') return;
  if(C.playing){ cmd('cover',{action:'stop'}); return; }
  if(typeof SP!=='undefined'&&SP.playing) SP.pause();      // one song at a time
  const g=coverGrid(), bars=g?C.pref.countIn:0;
  let from=C.from;
  // A punch starts a bar before the punch-in, so the player is already playing
  // when the recording begins.
  const bpmAt=t=>tempoAt(g,t)||(g&&g.tempo)||120;
  if(record&&C.punch.a!=null) from=coverBar(Math.max(0,C.punch.a-4*60/bpmAt(C.punch.a)));
  else if(bars>0) from=coverBar(from);
  // a count-in into a free section counts at the first tempo of the song
  cmd('cover',{action:'play',from,countIn:bars*4,bpm:bpmAt(from),record:!!record});
}
function coverSeek(t){
  C.from=clamp(t,0,coverDur());
  if(C.playing&&!C.recording){ cmd('cover',{action:'stop'}); setTimeout(()=>coverStart(false),60); }
  drawCoverFrame();
}
function coverPunch(which){
  const t=coverHead();
  if(which==='a'){ C.punch.a=t; if(C.punch.b!=null&&C.punch.b<=t) C.punch.b=null; }
  else { if(C.punch.a==null||t<=C.punch.a){ toast('Set Punch In first, then Punch Out later in the song.'); return; } C.punch.b=t; }
  cmd('cover',{action:'punch',a:C.punch.a!=null&&C.punch.b!=null?C.punch.a:-1,b:C.punch.a!=null&&C.punch.b!=null?C.punch.b:-1});
  renderCover();
}
function coverExport(){
  if(!C.st||C.st.state!=='ready') return;
  const n=C.st.selected||0;
  const body=el('p',null,n
    ?'Take '+n+' as three 24-bit WAV files: the mix, the '+PRODUCT.part+', and the '+PRODUCT.part+' DI. Every file starts at the beginning of the song, so they line up in any DAW. The mix uses the levels set below.'
    :'The backing track as it is mixed now, as one 24-bit WAV file. Select a take to export it with the backing.');
  openSheet({title:n?'Export take '+n:'Export backing', body, actions:[
    {label:'Cancel', onClick:closeSheet},
    {label:'Export', kind:'primary', onClick:()=>{ closeSheet(); cmd('cover',{action:'export',number:n}).then(ok=>{ if(ok) C.exportWait=true; else toast('Export is already running.'); }); }}]});
}
function coverTakeSheet(n){
  openSheet({title:'Take '+n, body:el('p',null,'Delete removes the take and its files. This cannot be undone.'), actions:[
    {label:'Delete take', kind:'danger', onClick:()=>{ closeSheet(); cmd('cover',{action:'deleteTake',number:n}).then(()=>cmd('cover',{action:'status'}).then(coverStatus)); }},
    {label:'Close', kind:'primary', onClick:closeSheet}]});
}

//---------------------------------------------------------------- drawing
function renderCover(){
  if(view!=='cover') return;
  const st=C.st, ready=st&&st.state==='ready'&&st.song===C.id;
  const hasSongs=$('#cSong').value!=='';
  $('#cEmpty').hidden=!!C.id&&hasSongs;
  if(!hasSongs){ $('#cEmptyT').textContent='No songs yet'; $('#cEmptyP').textContent='Import a song in Learn first. Cover plays it through your interface and records you over it.'; }
  else { $('#cEmptyT').textContent='No song loaded'; $('#cEmptyP').textContent='Choose a song from the list above.'; }
  const g=coverGrid();
  const kind=ready?(st.stems.length>1?'Stems':'Not separated'):(st&&st.state==='loading'?'Loading…':'');
  $('#cMeta').textContent=[ready?fmtT(st.length):null, g&&g.tempo?Math.round(g.tempo)+' BPM'+(g.sections&&g.sections.length>1?' (changes)':''):null, kind||null].filter(Boolean).join(' · ')||' ';
  [...$('#cCount').children].forEach(b=>b.classList.toggle('on',+b.dataset.n===C.pref.countIn));
  $('#cCount').classList.toggle('dim',!g);
  $('#cClick').classList.toggle('on',!!C.pref.click);
  $('#cLat').textContent=ready&&st.latencyMs>0?'Takes placed '+st.latencyMs.toFixed(1)+' ms earlier for the round trip':'';
  $('#cIn').classList.toggle('on',C.punch.a!=null);
  $('#cOut').classList.toggle('on',C.punch.b!=null);
  $('#cExport').disabled=!ready||st.exporting;
  $('#cExport').textContent=st&&st.exporting?'Exporting…':'Export';

  // takes: as many as fit; the rest are one tap away
  const host=$('#cTakes'); host.innerHTML='';
  const takes=ready?st.takes:[];
  if(!takes.length) host.appendChild(el('span','none',ready?'No takes yet. Press record to play over the song.':' '));
  const maxShown=Math.max(2,Math.floor((host.clientWidth||600)/104)-1);
  const shown=takes.length>maxShown?takes.slice(-maxShown):takes;
  shown.forEach(t=>{
    const b=el('button',st.selected===t.number?'on':'','Take '+t.number+'<small>'+(t.punched?'punch':fmtT(t.to-t.from))+'</small>');
    b.onclick=()=>cmd('cover',{action:'take',number:st.selected===t.number?0:t.number}).then(()=>cmd('cover',{action:'status'}).then(coverStatus));
    b.oncontextmenu=e=>{ e.preventDefault(); coverTakeSheet(t.number); };
    let hold=null; b.onpointerdown=()=>{ hold=setTimeout(()=>{ hold=null; coverTakeSheet(t.number); },550); };
    b.onpointerup=b.onpointerleave=()=>{ if(hold){ clearTimeout(hold); hold=null; } };
    host.appendChild(b);
  });
  if(takes.length>shown.length){
    const more=el('button',null,'All takes ('+takes.length+')');
    more.onclick=()=>{
      const list=el('div','takes'); list.style.flexWrap='wrap';
      takes.forEach(t=>{ const b=el('button',st.selected===t.number?'on':'','Take '+t.number);
        b.onclick=()=>{ closeSheet(); cmd('cover',{action:'take',number:t.number}).then(()=>cmd('cover',{action:'status'}).then(coverStatus)); };
        b.oncontextmenu=e=>{ e.preventDefault(); coverTakeSheet(t.number); };
        list.appendChild(b); });
      openSheet({title:'Takes', body:list, actions:[{label:'Close',kind:'primary',onClick:closeSheet}]});
    };
    host.appendChild(more);
  }
  renderCoverTiles();
  drawCoverFrame();
}

/** Which parts play: all of them, all but the player's own, or the drums alone. */
const COVER_MIXES=[
  {k:'all',   label:'Full song',              on:i=>true},
  {k:'minus', label:'Without '+PRODUCT.part,  on:i=>i!==PRODUCT.mineStem},
  {k:'drums', label:'Drums only',             on:i=>i===0}];
function coverMixIs(m,n){ return C.pref.stems.slice(0,n).every((s,i)=>!s.solo&&s.mute===!m.on(i)); }
function renderCoverTools(){
  const host=$('#cMixTools'), st=C.st, ready=st&&st.state==='ready'; host.innerHTML='';
  if(!ready) return;
  const n=st.stems.length;
  if(n<2){
    host.appendChild(el('span','note','Not separated: the song plays as one part.'));
    const b=el('button','btn small primary','Separate stems'); b.onclick=coverSeparate; host.appendChild(b);
    return;
  }
  const seg=el('div','seg'); seg.setAttribute('aria-label','Parts that play');
  COVER_MIXES.forEach(m=>{ const b=el('button',coverMixIs(m,n)?'on':'',esc(m.label));
    b.onclick=()=>{ C.pref.stems.forEach((s,i)=>{ s.solo=false; s.mute=i<n?!m.on(i):s.mute; }); saveCoverPrefs(); pushCoverMix(); renderCoverTiles(); };
    seg.appendChild(b); });
  host.appendChild(seg);
}
/** Separates the song from Cover (the same job as Learn's), then reloads it as stems. */
async function coverSeparate(){
  if(!C.id||C.busy) return;
  const id=C.id;
  if(L.lessonId!==id) await openLesson(id);              // separation works on the open lesson
  const saved=progress; progress=coverProg; C.busy=true;
  try{ await startSeparation(); } finally{ progress=saved; }
  const cancel=$('#cProgCancel'); cancel.hidden=false; cancel.onclick=()=>cmd('stem',{action:'cancel'});
  try{
    for(;;){
      const st=await cmd('stem',{action:'status',id});
      const busy=st&&['receiving','loading','separating','writing'].indexOf(st.state)>=0;
      if(!busy){
        if(st&&st.hasStems){ await cmd('cover',{action:'load',id,name:C.name}); toast('Stems ready.'); }
        else if(st&&st.state==='error'&&st.error) toast(st.error);
        return;
      }
      coverProg((st.stage||'Separating')+'. Playing continues on the other tabs.',st.progress||0);
      await new Promise(r=>setTimeout(r,800));
    }
  } finally { cancel.hidden=true; coverProg(null); C.busy=false; }
}
function renderCoverTiles(){
  renderCoverTools();
  const host=$('#cTiles'), st=C.st, ready=st&&st.state==='ready';
  const names=ready?st.stems:[];
  const n=names.length+1;
  host.style.setProperty('--n',n);
  if(host.childElementCount!==n||host.dataset.k!==names.join('|')){
    host.innerHTML=''; host.dataset.k=names.join('|');
    for(let i=0;i<n;i++){
      const isTake=i===names.length;
      const t=el('div','stem'+(isTake?' take':''));
      t.innerHTML='<div class="nm"></div><span class="in"></span><div class="lv"></div>';
      let solo=null;
      if(!isTake){
        solo=el('button','solo','S'); solo.setAttribute('aria-label','Hear '+names[i]+' alone');
        solo.onclick=e=>{ e.stopPropagation(); const s=stemPref(i); s.solo=!s.solo; saveCoverPrefs(); pushCoverMix(); renderCoverTiles(); };
        t.appendChild(solo);
      }
      let dragging=false, moved=false;
      const setFrom=ev=>{ const r=t.getBoundingClientRect(), v=clamp((ev.clientX-r.left)/r.width,0,1);
        if(isTake) C.pref.take=v; else stemPref(i).level=v; pushCoverMix(); renderCoverTiles(); };
      t.addEventListener('pointerdown',ev=>{ if(ev.target===solo) return; dragging=true; moved=false; t.setPointerCapture(ev.pointerId); });
      t.addEventListener('pointermove',ev=>{ if(!dragging) return; if(Math.abs(ev.movementX)>0){ moved=true; setFrom(ev); } });
      t.addEventListener('pointerup',()=>{ if(!dragging) return; dragging=false;
        if(!moved){ if(isTake) C.pref.hearTake=!C.pref.hearTake; else stemPref(i).mute=!stemPref(i).mute; pushCoverMix(); renderCoverTiles(); }
        saveCoverPrefs(); });
      host.appendChild(t);
    }
  }
  const anySolo=names.length>1&&C.pref.stems.slice(0,names.length).some(s=>s.solo);
  [...host.children].forEach((t,i)=>{
    const isTake=i===names.length;
    const s=isTake?null:stemPref(i);
    const off=isTake?!C.pref.hearTake:(anySolo?!s.solo:s.mute);
    const lvl=isTake?C.pref.take:s.level;
    t.classList.toggle('muted',!ready||off);
    t.classList.toggle('soloed',!!(s&&s.solo));
    t.querySelector('.nm').textContent=isTake?(st&&st.selected?'Take '+st.selected:'Your take'):names[i];
    const tag=t.querySelector('.in'); if(tag) tag.textContent=!ready?'':off?'OUT':'IN';
    t.querySelector('.lv').textContent=!ready?'—':off?'Not playing':(dbFrom(lvl)<=-59?'−∞':(dbFrom(lvl)>=0?'0.0':'−'+Math.abs(dbFrom(lvl)).toFixed(1))+' dB');
    t.style.background='linear-gradient(90deg,rgba(227,164,71,'+(off?.04:.10)+') '+(lvl*100)+'%,var(--well) '+(lvl*100)+'%)';
  });
}

function drawCoverFrame(){
  const dur=coverDur(), head=coverHead();
  $('#cTime').innerHTML=(C.countIn?'<b>Count-in</b>':fmtT(head))+' <em>/ '+fmtT(dur)+'</em>';
  $('#cPlay').classList.toggle('on',C.playing&&!C.recording);
  $('#cPlayIcon').innerHTML=C.playing?'<rect x="4" y="4" width="10" height="10" rx="1.5" fill="currentColor"/>':'<path d="M5 3l10 6-10 6z" fill="currentColor"/>';
  $('#cRec').classList.toggle('on',C.recording);
  $('#cSong').disabled=C.playing;

  const c=$('#cWaveCanvas'); if(!c||!c.offsetParent) return;
  let {w,h,d}=sizeCanvas(c), g=c.getContext('2d'); g.setTransform(d,0,0,d,0,0); g.clearRect(0,0,w,h);
  if(!dur) return;
  const X=s=>s/dur*w;
  const pa=C.punch.a, pb=C.punch.b;
  if(pa!=null){ const xa=X(pa), xb=pb!=null?X(pb):X(Math.max(pa,head)); g.fillStyle='rgba(217,87,74,.14)'; g.fillRect(xa,0,xb-xa,h);
    g.fillStyle='#d9574a'; g.fillRect(xa-1,0,2,h); if(pb!=null) g.fillRect(xb-1,0,2,h); }
  const a=C.a;
  if(a) for(const ch of (C.id===L.lessonId?chordsFor('bars'):(a.chords||[]))){ g.fillStyle=cssVar(chordFn(ch).col); g.globalAlpha=.5; g.fillRect(X(ch.s),0,Math.max(1,X(ch.e)-X(ch.s)-1),3); }
  g.globalAlpha=1;
  const pk=(C.st&&C.st.peaks&&C.st.peaks.length?C.st.peaks:(a&&a.peaks))||[], mid=h/2+2;
  for(let px=1;px<w;px+=3){
    const v=pk.length?pk[Math.floor(px/w*pk.length)]||0:0.2, amp=Math.max(1,v*(h*0.44));
    g.fillStyle=(px/w*dur)<head?'#d8cfc3':'#5d554b'; g.fillRect(px,mid-amp,1.8,2*amp);
  }
  // bar lines, faint, when the grid is known and they are far enough apart
  const gr=coverGrid();
  if(gr){ const ones=gr.beats.filter((b,i)=>gr.inBar[i]===0);
    if(ones.length>1&&X(ones[1]-ones[0])>14){ g.fillStyle='rgba(255,255,255,.05)'; ones.forEach(t=>g.fillRect(X(t),6,1,h-6)); }
    // where the song starts and ends, if trimmed
    g.fillStyle='rgba(0,0,0,.45)'; if(gr.start>0) g.fillRect(0,0,X(gr.start),h); if(gr.end<dur-0.05) g.fillRect(X(gr.end),0,w-X(gr.end),h); }
  for(let s=30;s<dur;s+=30){ g.fillStyle='#867c70'; g.font='600 9.5px '+FONT(); g.fillText(fmtT(s),X(s)+3,h-4); }
  const xp=X(head); g.fillStyle=C.recording?'#d9574a':'#f2ece4'; g.fillRect(xp-1,0,2,h); g.beginPath(); g.arc(xp,4,4,0,7); g.fill();

  // the take lane: the selected take, and the take being recorded
  const t2=$('#cTakeCanvas'); ({w,h,d}=sizeCanvas(t2)); g=t2.getContext('2d'); g.setTransform(d,0,0,d,0,0); g.clearRect(0,0,w,h);
  const st=C.st, sel=st&&st.takes?st.takes.find(t=>t.number===st.selected):null;
  $('#cTakeLabel').textContent=C.recording?'Recording':sel?'Take '+sel.number:'No take selected';
  if(sel&&sel.peaks){ const p=sel.peaks; g.fillStyle=C.pref.hearTake?'#e3a447':'#6b5532';
    for(let px=1;px<w;px+=3){ const v=p[Math.floor(px/w*p.length)]||0; if(v<=0) continue; const amp=Math.max(1,v*h*0.44); g.fillRect(px,h/2-amp,1.8,2*amp); } }
  if(C.recording&&C.recFrom!=null){
    let x0=X(C.recFrom), x1=X(head);
    if(pa!=null&&pb!=null){ x0=Math.max(x0,X(pa)); x1=Math.min(x1,X(pb)); }
    if(x1>x0){ g.fillStyle='rgba(217,87,74,.55)'; g.fillRect(x0,6,x1-x0,h-12); }
  }
  g.fillStyle=C.recording?'#d9574a':'#f2ece4'; g.fillRect(xp-1,0,2,h);
}

function coverShow(on){
  if(!on) return;
  coverSongs().then(list=>{
    const s=$('#cSong');
    if(s.value&&s.value!==(C.st&&C.st.song)) coverOpen(s.value);
    cmd('cover',{action:'status'}).then(coverStatus);
    renderCover();
  });
}

//---------------------------------------------------------------- wiring
$('#cSong').addEventListener('change',e=>coverOpen(e.target.value));
$('#cCount').querySelectorAll('button').forEach(b=>b.onclick=()=>{ C.pref.countIn=+b.dataset.n; saveCoverPrefs(); renderCover(); });
$('#cClick').onclick=()=>{ C.pref.click=!C.pref.click; saveCoverPrefs(); pushCoverClick(); renderCover(); };
$('#cPlay').onclick=()=>coverStart(false);
$('#cRec').onclick=()=>{ if(C.recording) cmd('cover',{action:'stop'}); else coverStart(true); };
$('#cStart').onclick=()=>coverSeek(coverStartAt());
$('#cTiming').onclick=()=>openTimingEditor('cover');
$('#cIn').onclick=()=>coverPunch('a');
$('#cOut').onclick=()=>coverPunch('b');
$('#cPunchClear').onclick=()=>{ C.punch={a:null,b:null}; cmd('cover',{action:'punch',a:-1,b:-1}); renderCover(); };
$('#cExport').onclick=coverExport;
$('#cGoLearn').onclick=()=>showView('learn');
$('#cWave').addEventListener('pointerdown',ev=>{
  const r=$('#cWave').getBoundingClientRect(), dur=coverDur(); if(!dur) return;
  coverSeek((ev.clientX-r.left)/r.width*dur);
});
window.addEventListener('resize',()=>{ if(view==='cover') renderCover(); });
