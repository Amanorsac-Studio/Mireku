
/*──────────────── Learn: guitar helpers ────────────────*/
/** Open strings, low to high (MIDI). Guitar: E2 A2 D3 G3 B3 E4; bass: E1 A1 D2 G2, B0 added on a five. */
let TUNING=PRODUCT.tunings[L.strings]||PRODUCT.tunings[PRODUCT.strings];
/** String names, low to high; a top string with the bottom's name is written small (e). */
const strNames=()=>TUNING.map((p,i)=>{ const n=NN_SHARP[((p%12)+12)%12]; return i===TUNING.length-1&&i>0&&n===NN_SHARP[((TUNING[0]%12)+12)%12]?n.toLowerCase():n; });
function setStrings(n){ if(!PRODUCT.tunings[n]) return; L.strings=n; TUNING=PRODUCT.tunings[n]; saveLearnPrefs(); renderTunerStrings(); if(typeof renderLearnLayout==='function') renderLearnLayout(); }
function renderTunerStrings(){ const h=$('#tStrings'); if(!h) return; h.innerHTML=''; strNames().forEach(n=>h.appendChild(el('span',null,esc(n)))); }
const minorish=()=>['minor','dorian','mpent','blues'].includes(L.key.scale);
/** A chord's job in the key: its roman numeral and its colour. Colours follow
    the relative major, so a song's vi chord is the same rose in A minor as in
    C major — the colour means the same thing whatever key the song is in. */
function chordFn(ch){
  if(!ch) return {rn:'',col:'var(--dx)'};
  const majRoot=minorish()?(L.key.root+3)%12:L.key.root;
  const iv=((ch.root-majRoot)%12+12)%12;
  const col={0:'--d1',2:'--d2',4:'--d3',5:'--d4',7:'--d5',9:'--d6',11:'--d7'}[iv]||'--dx';
  let rn;
  if(!minorish()) rn={0:'I',2:'ii',4:'iii',5:'IV',7:'V',9:'vi',11:'vii°'}[iv];
  else { const m=((ch.root-L.key.root)%12+12)%12; rn={0:'i',2:'ii°',3:'III',5:'iv',7:'v',8:'VI',10:'VII'}[m]; if(m===7&&(ch.q==='maj'||ch.q==='7')) rn='V'; }
  return {rn:rn||'·', col:'var('+col+')'};
}
const cssVar=v=>{ const m=/var\((--[a-z0-9]+)\)/.exec(v); return m?getComputedStyle(document.documentElement).getPropertyValue(m[1]).trim():v; };
function chordTones(ch){ const iv={maj:[0,4,7],min:[0,3,7],'7':[0,4,7,10],maj7:[0,4,7,11],m7:[0,3,7,10],sus4:[0,5,7],sus2:[0,2,7],dim:[0,3,6],aug:[0,4,8]}[ch.q]||[0,4,7];
  return iv.map(i=>(ch.root+i)%12); }
const IVN={0:'R',1:'♭9',2:'9',3:'m3',4:'3',5:'4',6:'♭5',7:'5',8:'♯5',9:'6',10:'♭7',11:'7'};
const ivName=(p,root)=>IVN[((p-root)%12+12)%12];

/* Chord shapes. Open shapes where the guitar has them; otherwise the E-shape
   (root on the 6th string) or A-shape (root on the 5th) barre, whichever sits
   lower on the neck. -1 is a string not played. Strings low to high. */
const OPEN={
  'C:maj':'x32010','A:maj':'x02220','G:maj':'320003','E:maj':'022100','D:maj':'xx0232','F:maj':'xx3211',
  'A:min':'x02210','E:min':'022000','D:min':'xx0231','B:min':'x24432',
  'C:7':'x32310','A:7':'x02020','G:7':'320001','E:7':'020100','D:7':'xx0212','B:7':'x21202',
  'A:m7':'x02010','E:m7':'020000','D:m7':'xx0211',
  'C:maj7':'x32000','A:maj7':'x02120','G:maj7':'320002','E:maj7':'021100','D:maj7':'xx0222','F:maj7':'xx3210',
  'A:sus4':'x02230','D:sus4':'xx0233','E:sus4':'022200','A:sus2':'x02200','D:sus2':'xx0230','G:sus4':'330013','C:sus2':'x30033'};
const NAMES_SH=['C','C#','D','D#','E','F','F#','G','G#','A','A#','B'];
const SHAPE_E={maj:[0,2,2,1,0,0],min:[0,2,2,0,0,0],'7':[0,2,0,1,0,0],m7:[0,2,0,0,0,0],maj7:[0,null,1,1,0,null],sus4:[0,2,2,2,0,0],aug:[0,3,2,1,1,0]};
const SHAPE_A={maj:[null,0,2,2,2,0],min:[null,0,2,2,1,0],'7':[null,0,2,0,2,0],m7:[null,0,2,0,1,0],maj7:[null,0,2,1,2,0],sus4:[null,0,2,2,3,0],sus2:[null,0,2,2,0,0],dim:[null,0,1,2,1,null],aug:[null,0,3,2,2,1]};
function chordShape(ch){
  const key=NAMES_SH[ch.root]+':'+ch.q;
  if(OPEN[key]) return OPEN[key].split('').map(c=>c==='x'?-1:+c);
  const opts=[];
  const fE=((ch.root-4)%12+12)%12||12, fA=((ch.root-9)%12+12)%12||12;
  if(SHAPE_E[ch.q]) opts.push(SHAPE_E[ch.q].map(o=>o==null?-1:o+fE));
  if(SHAPE_A[ch.q]) opts.push(SHAPE_A[ch.q].map(o=>o==null?-1:o+fA));
  if(!opts.length) opts.push(SHAPE_E.maj.map(o=>o+fE));
  const lo=s=>Math.min(...s.filter(f=>f>0));
  return opts.sort((a,b)=>lo(a)-lo(b))[0];
}
function shapeSVG(ch){
  if(!ch) return '';
  const fr=chordShape(ch), used=fr.filter(f=>f>0);
  const maxF=used.length?Math.max(...used):0, minF=used.length?Math.min(...used):1;
  const base=maxF<=4?1:minF;                       // first fret shown
  const W=100, H=138, x0=14, dx=14.4, y0=28, dy=20, n=5;
  let s='<svg viewBox="0 0 '+W+' '+H+'">';
  if(base===1) s+='<rect x="'+(x0-1)+'" y="'+(y0-4)+'" width="'+(dx*5+2)+'" height="4" rx="1.5" fill="#f2ece4"/>';
  else s+='<text x="'+(x0-5)+'" y="'+(y0+dy*0.65)+'" text-anchor="end" font-family="JetBrains Mono" font-size="9" fill="#a89e91">'+base+'</text>';
  for(let i=0;i<6;i++) s+='<line x1="'+(x0+i*dx)+'" y1="'+y0+'" x2="'+(x0+i*dx)+'" y2="'+(y0+n*dy)+'" stroke="#5a5047" stroke-width="'+(i<3?1.6:1.1)+'"/>';
  for(let f=1;f<=n;f++) s+='<line x1="'+x0+'" y1="'+(y0+f*dy)+'" x2="'+(x0+5*dx)+'" y2="'+(y0+f*dy)+'" stroke="#3a332c" stroke-width="1.2"/>';
  const fn=cssVar(chordFn(ch).col);
  fr.forEach((f,i)=>{ const x=x0+i*dx;
    if(f<0){ s+='<text x="'+x+'" y="'+(y0-9)+'" text-anchor="middle" font-family="JetBrains Mono" font-size="9" fill="#867c70">×</text>'; return; }
    const p=TUNING[i]+f, lab=ivName(p,ch.root), root=lab==='R';
    if(f===0){ s+='<circle cx="'+x+'" cy="'+(y0-12)+'" r="4.5" fill="none" stroke="'+(root?fn:'#a89e91')+'" stroke-width="1.5"/>'; return; }
    const y=y0+(f-base+0.5)*dy;
    s+='<circle cx="'+x+'" cy="'+y+'" r="7.6" fill="'+(root?fn:'#f2ece4')+'"/><text x="'+x+'" y="'+(y+3.2)+'" text-anchor="middle" font-family="JetBrains Mono" font-size="7.5" font-weight="700" fill="#1c1306">'+lab+'</text>';
  });
  ['E','A','D','G','B','e'].forEach((n2,i)=>{ s+='<text x="'+(x0+i*dx)+'" y="'+(y0+n*dy+14)+'" text-anchor="middle" font-family="JetBrains Mono" font-size="8.5" fill="#867c70">'+n2+'</text>'; });
  return s+'</svg>';
}
/** Where the player's hand is, so a note is shown at one position — the one
    nearest the last note played — not at every place it exists on the neck. */
function playedPos(p){
  if(L.hand==null) L.hand=5;
  const pos=fretFor(p,L.hand);
  if(pos&&pos.f>0) L.hand=L.hand*0.6+pos.f*0.4;
  return pos;
}
/** Where on the neck to play a pitch: the lowest-effort spot near the hand. */
function fretFor(p,hand){
  let best=null, score=1e9;
  TUNING.forEach((o,i)=>{ const f=p-o; if(f<0||f>17) return; const sc=Math.abs(f-hand)+i*0.25+(f===0?-0.5:0); if(sc<score){ score=sc; best={s:i,f}; } });
  return best;
}

/*──────────────── beats ────────────────
   The detected grid can be corrected: a typed or tapped tempo and the time of
   a "1" replace it, and are saved with the song. Everything that counts beats
   (the ring, the click, the bars of chords) reads grid(). */
/** The song's timing, from the analysis alone (so Learn and Cover agree).
 *  `timing` is what the player set: where the song starts and ends, and its
 *  tempo sections, each starting on a "1"; a section of 0 BPM is free time
 *  (a rubato intro) with no beats. Without it, the detected beat is used.
 *  Returns the beats, each beat's place in its bar (0 = the "1") and the
 *  tempo of the first counted section. */
function gridOf(a){
  if(!a) return null;
  const dur=a.duration||0;
  const tm=a.timing||(a.userGrid&&a.userGrid.bpm>0?{sections:[{t:a.userGrid.t0,bpm:a.userGrid.bpm}]}:null);
  if(!tm||!tm.sections||!tm.sections.length){
    const b=a.beats||[], d=a.downbeat||0;
    return {tempo:a.tempo||0, beats:b, inBar:b.map((_,i)=>(((i-d)%4)+4)%4), downbeat:d, user:false, start:0, end:dur, sections:null};
  }
  const secs=tm.sections.slice().sort((x,y)=>x.t-y.t), beats=[], inBar=[];
  secs.forEach((s,k)=>{
    if(!(s.bpm>0)) return;
    const step=60/s.bpm, until=k+1<secs.length?secs[k+1].t:dur;
    let t=s.t, n=0;
    // The first section reaches back to the start of the audio (a pickup).
    if(k===0){ const m=Math.floor(t/step+1e-6); t-=m*step; n=-m; }
    for(;t<until-1e-6&&t<dur;t+=step,n++){ if(t<0) continue; beats.push(+t.toFixed(4)); inBar.push(((n%4)+4)%4); }
  });
  const main=secs.find(s=>s.bpm>0), i0=inBar.indexOf(0);
  return {tempo:main?main.bpm:0, beats, inBar, downbeat:i0<0?0:i0%4, user:true,
          start:Math.max(0,tm.start||0), end:tm.end>0?Math.min(dur,tm.end):dur, sections:secs};
}
/** The tempo at time t (0 inside a free section). */
function tempoAt(gr,t){
  if(!gr) return 0;
  if(!gr.sections) return gr.tempo||0;
  let bpm=gr.sections[0].bpm; for(const s of gr.sections) if(s.t<=t+1e-6) bpm=s.bpm; return bpm;
}
function grid(){
  const a=L.analysis; if(!a) return null;
  const k=JSON.stringify(a.timing||a.userGrid||null)+'|'+a.duration+'|'+(a.beats||[]).length;
  if(L._gk!==k){ L._grid=gridOf(a); L._gk=k; }
  return L._grid;
}
function beatAt(t){
  const gr=grid(); if(!gr||gr.beats.length<2) return null;
  const b=gr.beats; let lo=0, hi=b.length-1;
  if(t<b[0]) return null;
  while(lo<hi){ const m=(lo+hi+1)>>1; if(b[m]<=t) lo=m; else hi=m-1; }
  const next=b[Math.min(b.length-1,lo+1)], len=Math.max(0.05,next-b[lo]);
  if(t-b[lo]>2.5*len) return null;                     // inside a free section
  return {i:lo, inBar:gr.inBar[lo], frac:clamp((t-b[lo])/len,0,1)};
}

/*──────────────── layout ────────────────*/
function renderLearnLayout(){
  [...$('#lSource').children].forEach(b=>b.classList.toggle('on',b.dataset.k===L.source));
  [...$('#lShow').children].forEach(b=>b.classList.toggle('on',b.dataset.k===L.show));
  const yt=L.source==='yt', has=!!L.lesson;
  $('#lLesson').hidden=yt; $('#lImport').hidden=yt; $('#lMore').hidden=yt||!has;
  $('#lYt').hidden=!yt; $('#lSongArea').hidden=yt;
  // YouTube: the video takes the room; there is no analysis for the chord ring,
  // the stems or the tempo to show.
  $('#view-learn').classList.toggle('yt',yt);
  $('#lMeta').hidden=yt; $('#lTempo').hidden=yt; $('#lDetail').hidden=yt;
  $('#lEmpty').hidden=yt||has;
  const video=has && L.lesson.kind==='video' && !!L.objectUrl;
  $('#lSideSeg').hidden=!video;
  if(!video) L.side='stems';
  [...$('#lSideSeg').children].forEach(b=>b.classList.toggle('on',b.dataset.k===L.side));
  $('#lStems').hidden=L.side!=='stems'; $('#lMedia').hidden=!(video&&L.side==='video');
  $('#lSideTitle').textContent=video&&L.side==='video'?'Video':'Stems';
  $('#lStemHint').hidden=!(L.side==='stems'&&SP.mode==='stems');
  const a=L.analysis;
  $('#lMeta').textContent=has&&a?[(a.tempo?Math.round(a.tempo)+' BPM':''),(SP.mode==='stems'?'4 stems':''),fmtT(a.duration)].filter(Boolean).join(' · '):'';
  $('#tCount').classList.toggle('on',!!L.countIn); $('#tClick').classList.toggle('on',!!L.click);
  $('#tPitch').textContent=(L.pitch>0?'+':'')+L.pitch+' st';
  [...$('#lDetail').children].forEach(b=>b.classList.toggle('on',b.dataset.k===L.detail));
  const gr=grid(); $('#lTempo').textContent=gr&&gr.tempo?Math.round(gr.tempo)+' BPM'+(gr.user?' ·':''):'Tempo';
  $('#lTempo').title=gr&&gr.user?'Tempo set by hand':'Tempo found in the song';
  const leg=$('#lLegend'); leg.innerHTML='';
  const fns=minorish()?[['i','--d6'],['III','--d1'],['iv','--d2'],['v / V','--d3'],['VI','--d4'],['VII','--d5']]
                      :[['I','--d1'],['ii','--d2'],['iii','--d3'],['IV','--d4'],['V','--d5'],['vi','--d6']];
  fns.forEach(([n,c])=>{ const s=el('span',null,'<i></i>'+n); s.style.setProperty('--c','var('+c+')'); leg.appendChild(s); });
  const k=el('span',null,'SAME COLOUR = SAME JOB IN THE KEY'); k.style.marginLeft='auto'; leg.appendChild(k);
  refreshStemBar();
  ytSync();
  requestAnimationFrame(sizeCanvases);
}
$('#lSideSeg').addEventListener('click',e=>{ const k=e.target.dataset&&e.target.dataset.k; if(!k) return; L.side=k; renderLearnLayout(); });
$('#lDetail').addEventListener('click',e=>{ const k=e.target.dataset&&e.target.dataset.k; if(!k) return; L.detail=k; saveLearnPrefs(); renderLearnLayout(); });
$('#lTempo').onclick=()=>openTimingEditor('learn');
/** Tempo: typed, tapped, halved or doubled, and where the "1" is. Saved with the song. */
function setGrid(g){
  if(!L.analysis) return;
  const keep=L.analysis.timing||{};
  setTiming(L.lesson&&L.lesson.id, L.analysis, g?{sections:[{t:g.t0,bpm:g.bpm}],start:keep.start||0,end:keep.end||0}:null);
}
/** Saves a song's timing (null: back to the detected beat) with the song,
 *  and refreshes whichever of Learn and Cover has it open. */
function setTiming(id, analysis, timing){
  if(!analysis) return;
  delete analysis.userGrid;
  if(timing) analysis.timing=timing; else delete analysis.timing;
  if(id) LDB.tx('readwrite').then(st=>{ const q=st.get(id); q.onsuccess=()=>{ const x=q.result; if(x){ x.analysis=analysis; st.put(x); } }; });
  if(L.analysis&&(L.analysis===analysis||(L.lesson&&L.lesson.id===id))){ L.analysis.timing=analysis.timing; delete L.analysis.userGrid; L._gk=null; L._ck=null; SP.sync(); renderLearnLayout(); }
  if(typeof C!=='undefined'&&C.id===id&&C.a){ C.a.timing=analysis.timing; delete C.a.userGrid; pushCoverClick(); renderCover(); }
}
$('#lShow').addEventListener('click',e=>{ const k=e.target.dataset&&e.target.dataset.k; if(!k) return; L.show=k; saveLearnPrefs(); renderLearnLayout(); });
$('#lGoYt').onclick=()=>{ L.source='yt'; saveLearnPrefs(); renderLearnLayout(); };

/*──────────────── stems: tiles and status ────────────────*/
/** The stem the player is here to replace: on a guitar, "other" — the 4-stem
    separation puts guitar and keys together there (a 6-stem model with a
    guitar of its own is not available for this runtime yet). */
function playAlongIndex(){ return PRODUCT.mineStem; }
function renderStemTiles(){
  const host=$('#lStemTiles'); if(!host) return;
  const live=SP.mode==='stems'&&SP.ready;
  if(host.childElementCount!==STEMS.length){
    host.innerHTML='';
    STEMS.forEach((s,i)=>{
      const t=el('div','stem'); t.dataset.i=i;
      t.innerHTML='<canvas></canvas><div class="nm"></div><div class="lv"></div>';
      const solo=el('button','solo','S'); solo.setAttribute('aria-label','Hear '+s.n+' alone');
      solo.onclick=e=>{ e.stopPropagation(); if(SP.mode!=='stems') return; SP.mix[i].solo=!SP.mix[i].solo; SP.push(); };
      t.appendChild(solo);
      // Tap mutes; a drag across the tile sets the level. One control, because
      // a separate fader per stem would not fit a thumb.
      let dragging=false, moved=false;
      const setFrom=ev=>{ const r=t.getBoundingClientRect(); SP.mix[i].level=clamp((ev.clientX-r.left)/r.width,0,1); SP.push(); };
      t.addEventListener('pointerdown',ev=>{ if(ev.target===solo||SP.mode!=='stems') return; dragging=true; moved=false; t.setPointerCapture(ev.pointerId); });
      t.addEventListener('pointermove',ev=>{ if(!dragging) return; if(Math.abs(ev.movementX)>0){ moved=true; setFrom(ev); } });
      t.addEventListener('pointerup',()=>{ if(!dragging) return; dragging=false; if(!moved){ SP.mix[i].mute=!SP.mix[i].mute; SP.push(); } });
      host.appendChild(t);
    });
  }
  const g=SP.gains(), peaks=(L.analysis&&L.analysis.peaks)||[];
  [...host.children].forEach((t,i)=>{
    const m=SP.mix[i]||{level:1};
    t.classList.toggle('muted',!live||g[i]===0);
    t.classList.toggle('soloed',live&&!!m.solo);
    t.classList.toggle('mine',i===playAlongIndex());
    t.querySelector('.nm').textContent=STEMS[i].n;
    t.querySelector('.lv').textContent=live?(g[i]===0?'Muted':Math.round(m.level*100)+'%'):'—';
    const c=t.querySelector('canvas'), {w,h,d}=sizeCanvas(c), x=c.getContext('2d');
    x.setTransform(d,0,0,d,0,0); x.clearRect(0,0,w,h);
    if(peaks.length){ x.fillStyle=i===playAlongIndex()?'#e3a447':'#a89e91'; x.globalAlpha=live?(g[i]===0?.25:.75):.25;
      const step=Math.max(2,Math.floor(w/90)), lvl=live?m.level:1;
      for(let px=0;px<w;px+=step){ const v=peaks[Math.floor(px/w*peaks.length)]||0, a=Math.max(1,v*h*0.48*lvl*(0.85+0.15*Math.sin(px*0.7+i))); x.fillRect(px,h/2-a,Math.max(1,step-1),2*a); }
      x.globalAlpha=1; }
  });
  const mine=playAlongIndex(), al=$('#lStemAlong');
  al.classList.toggle('on',live&&!!SP.mix[mine].mute);
  al.textContent=live&&SP.mix[mine].mute?'Unmute '+PRODUCT.part:'Mute '+PRODUCT.part;
  if(live) $('#lStemMsg').textContent=SP.mix[mine].mute?'Your part is muted. Play along.':PRODUCT.stemNote;
}
function refreshStemBar(){
  if(!$('#lStemTiles')) return;
  const usable=L.source==='song'&&!!L.lesson;
  const busy=['receiving','loading','separating','writing'].includes(stemStatus.state)&&stemStatus.job===L.lessonId;
  const live=SP.mode==='stems'&&SP.ready;
  $('#lStemBusyBar').hidden=!busy; $('#lStemCancel').hidden=!busy;
  $('#lStemGo').hidden=!usable||busy||live;
  $('#lStemAlong').hidden=!live;
  const msg=$('#lStemMsg');
  if(busy){ msg.textContent=(stemStatus.stage||'Separating')+' — you can keep playing'; $('#lStemBusyB').style.width=Math.round(100*(stemStatus.progress||0))+'%'; }
  else if(live) msg.textContent=SP.mix[playAlongIndex()].mute?'Your part is muted. Play along.':PRODUCT.stemNote;
  else if(usable) msg.textContent='Separate the song to mute your part and play along.';
  else msg.textContent=L.source==='yt'?'Stems work on songs you import, not on YouTube.':'Import a song to start.';
  renderStemTiles();
}

/*──────────────── canvases ────────────────*/
function sizeCanvas(c){ const r=c.getBoundingClientRect(), d=window.devicePixelRatio||1;
  const w=Math.max(1,Math.round(r.width*d)), h=Math.max(1,Math.round(r.height*d));
  if(c.width!==w||c.height!==h){ c.width=w; c.height=h; } return {w:r.width,h:r.height,d}; }
function sizeCanvases(){ ['#lOverCanvas','#lLaneCanvas','#lNeck'].forEach(s=>{ const c=$(s); if(c) sizeCanvas(c); }); renderStemTiles(); }
/** The chords to show. The analyser hears every change; a player wants the
    harmony of each bar. Bars: one chord per bar, or two when the second half
    of the bar clearly holds another chord. Beats: one per beat, merged. */
function chordsFor(detail){
  const a=L.analysis, gr=grid(); if(!a) return [];
  if(!gr||gr.beats.length<4) return a.chords;
  const key=detail+'|'+L._gk+'|'+a.chords.length;
  if(L._ck===key) return L._cl;
  const b=gr.beats, cs=a.chords, step=60/(gr.tempo||120);
  // the chord holding most of each beat
  const per=[]; let j=0;
  for(let i=0;i<b.length;i++){
    const s=b[i], e=i+1<b.length?b[i+1]:s+step; let best=null, bo=0;
    while(j<cs.length&&cs[j].e<=s) j++;
    for(let k=j;k<cs.length&&cs[k].s<e;k++){ const o=Math.min(e,cs[k].e)-Math.max(s,cs[k].s); if(o>bo){ bo=o; best=cs[k]; } }
    per.push({s,e,c:bo>0.3*(e-s)?best:null});
  }
  const out=[], push=(s,e,c)=>{ if(!c) return; const last=out[out.length-1];
    if(last&&last.name===c.name&&Math.abs(last.e-s)<0.05) last.e=e; else out.push({s,e,name:c.name,root:c.root,q:c.q,bass:c.bass}); };
  if(detail==='beats'){ per.forEach(x=>push(x.s,x.e,x.c)); }
  else{
    const major=list=>{ const n={}; let best=null, bn=0; list.forEach((x,i)=>{ if(!x.c) return; const k=x.c.name; n[k]=(n[k]||0)+1+(i===0?0.5:0); if(n[k]>bn){ bn=n[k]; best=x.c; } }); return best; };
    const bars=[]; let from=0;
    for(let i=1;i<=per.length;i++) if(i===per.length||gr.inBar[i]===0||per[i].s-per[i-1].e>0.05){ bars.push([from,i]); from=i; }
    for(const [i0,i1] of bars){
      const span=per.slice(i0,i1); if(!span.length) continue;
      const h1=span.slice(0,2), h2=span.slice(2);
      const c1=major(h1), c2=major(h2);
      const solid=h=>h.length>=2&&h[0].c&&h[1].c&&h[0].c.name===h[1].c.name;
      if(c1&&c2&&c1.name!==c2.name&&solid(h1)&&solid(h2)){ push(span[0].s,h1[h1.length-1].e,c1); push(h2[0].s,span[span.length-1].e,c2); }
      else push(span[0].s,span[span.length-1].e,major(span));
    }
  }
  // free sections have no beats: their chords are the detected ones
  if(gr.sections) gr.sections.forEach((s,k)=>{ if(s.bpm>0) return; const e=k+1<gr.sections.length?gr.sections[k+1].t:a.duration;
    cs.forEach(c=>{ if(c.e>s.t&&c.s<e) out.push({s:Math.max(c.s,s.t),e:Math.min(c.e,e),name:c.name,root:c.root,q:c.q,bass:c.bass}); }); });
  out.sort((x,y)=>x.s-y.s);
  L._ck=key; L._cl=out; return out;
}
function chordAt(t){ const a=L.analysis; if(!a) return null; const cs=chordsFor(L.detail);
  let lo=0, hi=cs.length-1; while(lo<=hi){ const m=(lo+hi)>>1; if(cs[m].e<=t) lo=m+1; else if(cs[m].s>t) hi=m-1; else return {c:cs[m],i:m}; }
  return {c:null,i:lo}; }
function notesAt(t,role){ const a=L.analysis; if(!a) return []; return a.notes.filter(n=>n.s<=t && n.e>t && (!role||n.role===role)); }
const FONT=()=>getComputedStyle(document.body).getPropertyValue('--font-ui');

/** The whole song: what has been played is bright, what is to come is dim,
    the A–B loop is amber. Tap to jump; drag to set a loop. */
function drawOverview(t){
  const c=$('#lOverCanvas'); if(!c||!c.offsetParent) return;
  const {w,h,d}=sizeCanvas(c), g=c.getContext('2d'); g.setTransform(d,0,0,d,0,0);
  g.clearRect(0,0,w,h);
  const a=L.analysis, dur=T.dur()||(a&&a.duration)||0; if(!dur) return;
  const X=s=>s/dur*w;
  if(L.loop.a!=null){ const xa=X(L.loop.a), xb=L.loop.b!=null?X(L.loop.b):X(t);
    g.fillStyle=L.loop.on?'rgba(227,164,71,.13)':'rgba(255,255,255,.05)'; g.fillRect(xa,0,xb-xa,h); }
  // chord changes as faint ticks along the top, coloured by function
  if(a) for(const ch of chordsFor(L.detail)){ g.fillStyle=cssVar(chordFn(ch).col); g.globalAlpha=.55; g.fillRect(X(ch.s),0,Math.max(1,X(ch.e)-X(ch.s)-1),3); }
  g.globalAlpha=1;
  const pk=(a&&a.peaks)||[], mid=h/2+3;
  for(let px=1;px<w;px+=3){
    const v=pk.length?pk[Math.floor(px/w*pk.length)]||0:0.2, amp=Math.max(1,v*(h*0.42));
    g.fillStyle=(px/w*dur)<t?'#d8cfc3':'#5d554b';
    g.fillRect(px,mid-amp,1.8,2*amp);
  }
  for(let s=30;s<dur;s+=30){ g.fillStyle='#867c70'; g.font='600 9.5px '+FONT(); g.fillText(fmtT(s),X(s)+3,h-4); g.fillStyle='rgba(255,255,255,.06)'; g.fillRect(X(s),6,1,h-6); }
  [[L.loop.a,'A'],[L.loop.b,'B']].forEach(([v,lab])=>{ if(v==null) return; const x=X(v);
    g.fillStyle='#e3a447'; g.fillRect(x-1,0,2,h); g.beginPath(); g.roundRect?g.roundRect(x-8,h-17,16,14,3):g.rect(x-8,h-17,16,14); g.fill();
    g.fillStyle='#1c1306'; g.font='700 9.5px '+FONT(); g.textAlign='center'; g.fillText(lab,x,h-7); g.textAlign='start'; });
  const xp=X(t); g.fillStyle='#f2ece4'; g.fillRect(xp-1,0,2,h); g.beginPath(); g.arc(xp,4,4,0,7); g.fill();
}
/** The chords around the playhead, coloured by their job in the key. */
function laneSpan(){ const gr=grid(); return gr&&gr.tempo?Math.max(8,16*60/gr.tempo):12; }
function drawLane(t){
  const c=$('#lLaneCanvas'); if(!c||!c.offsetParent) return;
  const {w,h,d}=sizeCanvas(c), g=c.getContext('2d'); g.setTransform(d,0,0,d,0,0);
  g.clearRect(0,0,w,h);
  const a=L.analysis; if(!a) return;
  const span=laneSpan(), t0=t-span*0.3, X=s=>(s-t0)/span*w;
  for(const ch of chordsFor(L.detail)){
    if(ch.e<t0||ch.s>t0+span) continue;
    const x=Math.max(0,X(ch.s)), x2=Math.min(w,X(ch.e)), cur=ch.s<=t&&ch.e>t, f=chordFn(ch), col=cssVar(f.col);
    g.fillStyle=cur?col:'#0f0d0c'; g.beginPath(); g.roundRect?g.roundRect(x+1,0,Math.max(0,x2-x-3),h,8):g.rect(x+1,0,Math.max(0,x2-x-3),h); g.fill();
    if(!cur){ g.globalAlpha=.18; g.fillStyle=col; g.fill(); g.globalAlpha=1; g.fillStyle=col; g.fillRect(x+1,4,3,h-8); }
    if(x2-x>30){ g.fillStyle=cur?'#1a1310':'#f2ece4'; g.font='700 14px '+FONT(); g.fillText(ch.name,x+10,h/2+1);
      g.fillStyle=cur?'rgba(26,19,16,.7)':'#a89e91'; g.font='600 9px '+FONT(); g.fillText(f.rn,x+10,h/2+13); }
  }
  if(L.loop.a!=null&&L.loop.b!=null){ const xa=X(L.loop.a), xb=X(L.loop.b); g.strokeStyle='#e3a447'; g.lineWidth=2; g.strokeRect(xa+1,1,xb-xa-2,h-2); }
  const gb=grid(); if(gb&&gb.beats){ g.fillStyle='rgba(255,255,255,.18)'; for(const b of gb.beats){ if(b<t0) continue; if(b>t0+span) break; g.fillRect(X(b),h-4,1,4); } }
  const xp=X(t); g.fillStyle='#f2ece4'; g.fillRect(xp-1,-2,2,h+4);
}
/** The neck: chord tones, the scale, the notes the song plays now, or tab. */
function drawNeck(t){
  const c=$('#lNeck'); if(!c||!c.offsetParent) return;
  const {w,h,d}=sizeCanvas(c), g=c.getContext('2d'); g.setTransform(d,0,0,d,0,0);
  g.clearRect(0,0,w,h);
  const song=L.source==='song'&&L.analysis, at=song?chordAt(t):null, ch=at&&at.c;
  if(L.show==='tab'&&song) return drawTab(g,w,h,t);
  const frets=15, nut=60, pad=12, fw=(w-nut-pad)/frets, top=14, bot=h-24, S1=TUNING.length-1, gap=(bot-top)/S1;
  const Y=i=>bot-i*gap, Xf=f=>f===0?nut-24:nut+(f-0.5)*fw;
  g.fillStyle='#1d1915'; g.fillRect(nut,top-8,w-nut-pad,bot-top+16);
  [3,5,7,9,15].forEach(f=>{ g.fillStyle='#2c2722'; g.beginPath(); g.arc(nut+(f-0.5)*fw,(top+bot)/2,4.5,0,7); g.fill(); });
  g.fillStyle='#2c2722'; g.beginPath(); g.arc(nut+11.5*fw,(top+bot)/2-gap,4.5,0,7); g.arc(nut+11.5*fw,(top+bot)/2+gap,4.5,0,7); g.fill();
  g.fillStyle='#e9e1d6'; g.fillRect(nut-4,top-8,4,bot-top+16);
  g.font='600 10px '+FONT();
  for(let f=1;f<=frets;f++){ g.fillStyle='#4a4138'; g.fillRect(nut+f*fw-1,top-8,2,bot-top+16); g.fillStyle='#6f665c'; g.textAlign='center'; g.fillText(String(f),nut+(f-0.5)*fw,h-6); }
  g.textAlign='start';
  strNames().forEach((n,i)=>{ g.fillStyle='#8a7f72'; g.fillRect(nut,Y(i)-(0.5+(S1-i)*0.22*PRODUCT.gauge),w-nut-pad,1+(S1-i)*0.45*PRODUCT.gauge);
    g.fillStyle='#867c70'; g.font='600 11px '+FONT(); g.fillText(n,6,Y(i)+4); });
  const live=L.live.note, tones=ch?chordTones(ch):null, rootCol=ch?cssVar(chordFn(ch).col):'#e3a447';
  // What the player plays: one position each, the newest brightest. Shown in
  // every view; in "Me" it is all that is shown.
  const now=[]; if(live>=0) now.push(live); (L.midi||[]).forEach(m=>{ if(now.indexOf(m)<0) now.push(m); });
  if(now.join()!==L._lastNow){ L._lastNow=now.join(); now.forEach(p=>{ const q=playedPos(p); if(q){ L.trail=(L.trail||[]).filter(x=>!(x.s===q.s&&x.f===q.f)); L.trail.push({...q,p,t:performance.now()}); } }); if(L.trail&&L.trail.length>6) L.trail.splice(0,L.trail.length-6); }
  const songNow=song?new Set(notesAt(t).filter(PRODUCT.myNotes).map(n=>n.p)):new Set();
  for(let i=0;i<TUNING.length;i++) for(let f=0;f<=frets;f++){
    const p=TUNING[i]+f, x=Xf(f), y=Y(i), pc=((p%12)+12)%12;
    let lab=null, fill='#2f2924', stroke='#5d554b', ink='#d8cfc3', r=10;
    if(L.show==='me') continue;
    if(L.show==='tones'&&tones){ if(!tones.includes(pc)) continue; lab=ivName(p,ch.root); if(lab==='R'){ fill=rootCol; stroke='none'; ink='#1a1310'; } }
    else if(L.show==='scale'){ if(!inKey(p)) continue; lab=nn(p); if((pc-L.key.root+12)%12===0){ fill='#e3a447'; stroke='none'; ink='#1c1306'; } }
    else if(L.show==='notes'){ if(!songNow.has(p)) continue; lab=nn(p); fill='#8aa6cf'; stroke='none'; ink='#0f1520'; r=11; }
    else if(L.show==='tones'&&!tones){ if(!inKey(p)) continue; lab=nn(p); }
    else continue;
    if(f===0){ fill='#131110'; }
    g.beginPath(); g.arc(x,y,r,0,7); g.fillStyle=fill; g.fill(); if(stroke!=='none'){ g.lineWidth=1.2; g.strokeStyle=stroke; g.stroke(); }
    g.fillStyle=ink; g.font='700 '+(lab.length>2?8.5:10)+'px '+FONT(); g.textAlign='center'; g.fillText(lab,x,y+3.5); g.textAlign='start';
  }
  // what the player is playing right now, ringed in green wherever it sits
  const nowT=performance.now();
  // A note held is bright; once released it fades over about two seconds.
  (L.trail||[]).forEach(q=>{ if(now.indexOf(q.p)<0){ if(q.off==null) q.off=nowT; } else q.off=null; });
  L.trail=(L.trail||[]).filter(q=>q.off==null||nowT-q.off<2200);
  L.trail.forEach(q=>{ if(q.f>frets) return; const cur=q.off==null;
    const alpha=cur?1:Math.max(0,0.55-(nowT-q.off)/1000*0.25); if(alpha<=0) return;
    const x=Xf(q.f), y=Y(q.s); g.globalAlpha=alpha;
    g.beginPath(); g.arc(x,y,cur?14:11,0,7); g.fillStyle=cur?'#86b892':'rgba(134,184,146,.35)'; g.fill();
    g.fillStyle=cur?'#0b1a10':'#d8e8dc'; g.font='700 '+(cur?11:10)+'px '+FONT(); g.textAlign='center'; g.fillText(nn(q.p),x,y+4); g.textAlign='start'; g.globalAlpha=1; });
  if(L.show==='me'&&!now.length&&!(L.trail||[]).length){ g.fillStyle='#867c70'; g.font='600 12px '+FONT(); g.textAlign='center'; g.fillText('Play a note — it is shown where your hand is on the neck',w/2,(top+bot)/2+4); g.textAlign='start'; }
}
/** Tab of the notes the song plays (not the bass), placed near one hand position. */
function drawTab(g,w,h,t){
  const top=16, bot=h-16, S1=TUNING.length-1, gap=(bot-top)/S1, span=Math.max(6,laneSpan()*0.75), t0=t-span*0.2, left=36, X=s=>left+(s-t0)/span*(w-left-8);
  strNames().slice().reverse().forEach((n,k)=>{ const y=top+k*gap; g.fillStyle='#4a4138'; g.fillRect(left,y,w-left-8,1); g.fillStyle='#867c70'; g.font='600 11px '+FONT(); g.fillText(n,10,y+4); });
  const at=chordAt(t), ch=at&&at.c; let hand=!ch?0:PRODUCT.shapes?Math.max(0,Math.min(...chordShape(ch).filter(f=>f>0).concat([12]))-1)
    :Math.max(0,((ch.root-TUNING[0])%12+12)%12-1);       // bass: the root on the lowest string
  const notes=(L.analysis.notes||[]).filter(n=>PRODUCT.myNotes(n)&&n.e>t0&&n.s<t0+span);
  g.font='700 12px '+FONT(); g.textAlign='center';
  for(const n of notes){ const pos=fretFor(n.p,hand); if(!pos) continue; const x=X(n.s), y=top+(S1-pos.s)*gap, now=n.s<=t&&n.e>t;
    g.fillStyle='#131110'; g.fillRect(x-9,y-8,18,16); g.fillStyle=now?'#e3a447':(n.s<t?'#6f665c':'#d8cfc3'); g.fillText(String(pos.f),x,y+4.5); }
  g.textAlign='start';
  const xp=X(t); g.fillStyle='#f2ece4'; g.fillRect(xp-1,top-8,2,bot-top+16);
  g.fillStyle='#867c70'; g.font='600 10px '+FONT(); g.fillText('Notes the song plays, placed near the chord’s position — a guide, not a transcription',left,h-2);
}

/*──────────────── now: ring, shape, next ────────────────*/
let lastShapeKey='', lastNextKey='';
function updateNow(t){
  const song=L.source==='song'&&L.analysis;
  const at=song?chordAt(t):null, cur=at&&at.c;
  const f=chordFn(cur), col=cssVar(f.col);
  $('#nChord').textContent=cur?cur.name:'—';
  const bt=song?beatAt(t):null;
  $('#nBeat').textContent=cur?(f.rn+(bt?' · BEAT '+(bt.inBar+1)+'/4':'')):(L.source==='yt'?'YOUTUBE':' ');
  // the ring: four beats of the bar, filled as they pass
  let s='';
  const cx=88, r=78, arc=(a0,a1)=>{ const p=a=>[cx+r*Math.cos(a*Math.PI/180),cx+r*Math.sin(a*Math.PI/180)]; const [x0,y0]=p(a0),[x1,y1]=p(a1); return 'M'+x0.toFixed(1)+' '+y0.toFixed(1)+'A'+r+' '+r+' 0 '+(a1-a0>180?1:0)+' 1 '+x1.toFixed(1)+' '+y1.toFixed(1); };
  let filled=0;
  if(bt) filled=bt.inBar+bt.frac;
  else if(cur) filled=4*clamp((t-cur.s)/Math.max(0.1,cur.e-cur.s),0,1);
  for(let b=0;b<4;b++){ const a0=-90+b*90+4, a1=-90+(b+1)*90-4;
    s+='<path d="'+arc(a0,a1)+'" fill="none" stroke="#2c2722" stroke-width="10" stroke-linecap="round"/>';
    const part=clamp(filled-b,0,1); if(part>0.02&&cur) s+='<path d="'+arc(a0,a0+(a1-a0)*part)+'" fill="none" stroke="'+col+'" stroke-width="10" stroke-linecap="round"/>'; }
  $('#nRingSvg').innerHTML=s;
  const sk=cur?cur.name+'|'+cur.root+cur.q:'';
  if(sk!==lastShapeKey){ $('#nShape').innerHTML=cur&&PRODUCT.shapes?shapeSVG(cur):''; $('#nShape').hidden=!PRODUCT.shapes; lastShapeKey=sk; }
  // next three different chords
  const nx=[];
  const CL=chordsFor(L.detail);
  if(song&&at){ for(let i=(cur?at.i+1:at.i);i<CL.length&&nx.length<3;i++){ const c=CL[i]; if(!nx.length||nx[nx.length-1].name!==c.name) nx.push(c); } }
  const nk=nx.map(c=>c.name).join('|');
  if(nk!==lastNextKey){ const host=$('#nNext'); host.innerHTML='';
    nx.forEach(c=>{ const p=el('span','pillc','<i></i>'+esc(c.name)); p.style.setProperty('--c',chordFn(c).col); host.appendChild(p); });
    if(!nx.length) host.appendChild(el('span','cap','—'));
    lastNextKey=nk; }
  const lv=L.live.note, el1=$('#nLive');
  if(lv>=0){ const inCh=cur&&chordTones(cur).includes(((lv%12)+12)%12);
    el1.textContent=nn(lv,true)+' · '+(L.live.cents>0?'+':'')+Math.round(L.live.cents)+'¢'+(inCh?' · in the chord':''); }
  else el1.textContent='Play a note';
}

/*──────────────── frame loop ────────────────*/
function learnFrame(){
  if(!L.visible){ L.raf=0; return; }
  const t=T.time();
  if(T.sp()) syncVideoToStems();
  $('#tTime').innerHTML=fmtT(t)+' <em>/ '+fmtT(T.dur())+'</em>';
  const paused=T.paused();
  if(learnFrame._p!==paused){ learnFrame._p=paused;
    $('#tPlayIcon').innerHTML=paused?'<path d="M5 3l10 6-10 6z" fill="currentColor"/>':'<rect x="4" y="3" width="3.5" height="12" rx="1" fill="currentColor"/><rect x="10.5" y="3" width="3.5" height="12" rx="1" fill="currentColor"/>';
    $('#tPlay').setAttribute('aria-label',paused?'Play':'Pause'); }
  if(L.source==='song'){ drawOverview(t); drawLane(t); }
  drawNeck(t);
  updateNow(t);
  L.raf=requestAnimationFrame(learnFrame);
}
function learnShow(on){
  L.visible=on;
  if(on){ if(!L.lessons.length) refreshLessons(); renderKeySelects(); renderLearnLayout(); T.setRate(L.rate); if(!L.raf) L.raf=requestAnimationFrame(learnFrame); }
  else { ytSync(); const v=$('#lVideo'); if(!v.paused) v.pause(); SP.pause(); }
}
/* The overview: tap to jump, drag to set a loop. The lane: tap to jump. */
(function(){
  const ov=$('#lOverview'); let x0=null, a0=0, drag=false;
  const tAt=e=>{ const r=ov.getBoundingClientRect(); return clamp((e.clientX-r.left)/r.width,0,1)*(T.dur()||0); };
  ov.addEventListener('pointerdown',e=>{ if(!T.dur()) return; ov.setPointerCapture(e.pointerId); x0=e.clientX; a0=tAt(e); drag=false; });
  ov.addEventListener('pointermove',e=>{ if(x0==null) return; if(Math.abs(e.clientX-x0)>10){ drag=true; const b=tAt(e); L.loop={a:Math.min(a0,b),b:Math.max(a0,b),on:true}; T.pushLoop(); } });
  ov.addEventListener('pointerup',e=>{ if(x0==null) return; if(!drag) T.seekTo(tAt(e)); x0=null; });
  const ln=$('#lLane');
  ln.addEventListener('pointerdown',e=>{ if(!L.analysis) return; const r=ln.getBoundingClientRect(), span=laneSpan(), t=T.time(), t0=t-span*0.3;
    T.seekTo(t0+(e.clientX-r.left)/r.width*span); });
})();

/*──────────────── live input, from the engine tick ────────────────*/
function learnTick(t){
  const lv=t.live||{};
  L.live={note:lv.note!=null?lv.note:-1, count:lv.count||0, cents:lv.cents||0};
  L.midi=t.midi||[];
}

/*──────────────── transport extras: start, pitch, count-in, click ────────────────*/
$('#tStart').onclick=()=>T.seekTo(L.loop.on&&L.loop.a!=null?L.loop.a:0);
function setPitch(st){
  if(L.source==='yt'){ toast('Pitch works on songs you import'); return; }
  if(!SP.ready&&L.lesson){ toast('This song plays from its video, which cannot change key. Re-import it as audio.'); return; }
  L.pitch=clamp(st,-12,12); SP.setPitch(L.pitch);
  $('#tPitch').textContent=(L.pitch>0?'+':'')+L.pitch+' st';
}
$('#tDown').onclick=()=>setPitch(L.pitch-1);
$('#tUp').onclick=()=>setPitch(L.pitch+1);
$('#tPitch').ondblclick=()=>setPitch(0);
$('#tCount').onclick=()=>{ L.countIn=!L.countIn; saveLearnPrefs(); renderLearnLayout(); if(L.countIn&&!(L.analysis&&L.analysis.tempo)) toast('The count-in needs the song’s beat — import the song to find it'); };
$('#tClick').onclick=()=>{ L.click=!L.click; SP.setClick(L.click); saveLearnPrefs(); renderLearnLayout(); if(L.click&&!(L.analysis&&L.analysis.beats&&L.analysis.beats.length)) toast('No beat found for this song'); };
