# One-off: the beat grid becomes tempo sections (a free intro, tempo changes),
# with each beat's place in the bar, read by Learn, the click and Cover.
import io, os
HERE = os.path.dirname(os.path.abspath(__file__))

def rep(name, pairs):
    p = os.path.join(HERE, name) if not os.path.isabs(name) else name
    s = io.open(p, encoding='utf-8').read()
    for a, b in pairs:
        if a not in s:
            raise SystemExit(name + ': not found: ' + a[:90])
        s = s.replace(a, b, 1)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)
    print('edited', name)

OLD_GRID = s0 = None
p = os.path.join(HERE, 'part5-learn.js')
s = io.open(p, encoding='utf-8').read()
a = s.index('function grid(){')
b = s.index('function beatAt(t){')
c = s.index('\n}\n', b) + 3
NEW = r'''/** The song's timing, from the analysis alone (so Learn and Cover agree).
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
'''
s = s[:a] + NEW + s[c:]
io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('replaced grid')

rep('part5-learn.js', [
    # bars start where a beat is a "1"; free sections keep the detected chords
    ("  const key=detail+'|'+gr.beats.length+'|'+gr.downbeat+'|'+gr.tempo+'|'+a.chords.length+'|'+(gr.beats[0]||0);",
     "  const key=detail+'|'+L._gk+'|'+a.chords.length;"),
    ("""    const firstBar=((gr.downbeat%4)+4)%4;
    const bars=[]; if(firstBar>0) bars.push([0,firstBar]);
    for(let i=firstBar;i<per.length;i+=4) bars.push([i,Math.min(per.length,i+4)]);""",
     """    const bars=[]; let from=0;
    for(let i=1;i<=per.length;i++) if(i===per.length||gr.inBar[i]===0||per[i].s-per[i-1].e>0.05){ bars.push([from,i]); from=i; }"""),
    ("""  L._ck=key; L._cl=out; return out;""",
     """  // free sections have no beats: their chords are the detected ones
  if(gr.sections) gr.sections.forEach((s,k)=>{ if(s.bpm>0) return; const e=k+1<gr.sections.length?gr.sections[k+1].t:a.duration;
    cs.forEach(c=>{ if(c.e>s.t&&c.s<e) out.push({s:Math.max(c.s,s.t),e:Math.min(c.e,e),name:c.name,root:c.root,q:c.q,bass:c.bass}); }); });
  out.sort((x,y)=>x.s-y.s);
  L._ck=key; L._cl=out; return out;"""),
    # saving: the timing is kept with the song
    ("""function setGrid(g){
  if(!L.analysis) return;
  if(g) L.analysis.userGrid=g; else delete L.analysis.userGrid;
  L._gk=null; L._ck=null;
  if(L.lesson) LDB.tx('readwrite').then(st=>{ const q=st.get(L.lesson.id); q.onsuccess=()=>{ const x=q.result; if(x){ x.analysis=L.analysis; st.put(x); } }; });
  SP.sync(); renderLearnLayout();
}""",
     """function setGrid(g){
  if(!L.analysis) return;
  setTiming(L.lesson&&L.lesson.id, L.analysis, g?{sections:[{t:g.t0,bpm:g.bpm}],start:(L.analysis.timing||{}).start||0,end:(L.analysis.timing||{}).end||0}:null);
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
}"""),
    ("$('#lTempo').onclick=()=>openTempoSheet();", "$('#lTempo').onclick=()=>openTimingEditor('learn');"),
    ("  if(cur>=0) return; // placeholder", "  if(cur>=0) return; // placeholder"),
] if False else [
    ("  const key=detail+'|'+gr.beats.length+'|'+gr.downbeat+'|'+gr.tempo+'|'+a.chords.length+'|'+(gr.beats[0]||0);",
     "  const key=detail+'|'+L._gk+'|'+a.chords.length;"),
    ("""    const firstBar=((gr.downbeat%4)+4)%4;
    const bars=[]; if(firstBar>0) bars.push([0,firstBar]);
    for(let i=firstBar;i<per.length;i+=4) bars.push([i,Math.min(per.length,i+4)]);""",
     """    const bars=[]; let from=0;
    for(let i=1;i<=per.length;i++) if(i===per.length||gr.inBar[i]===0||per[i].s-per[i-1].e>0.05){ bars.push([from,i]); from=i; }"""),
    ("""  L._ck=key; L._cl=out; return out;""",
     """  // free sections have no beats: their chords are the detected ones
  if(gr.sections) gr.sections.forEach((s,k)=>{ if(s.bpm>0) return; const e=k+1<gr.sections.length?gr.sections[k+1].t:a.duration;
    cs.forEach(c=>{ if(c.e>s.t&&c.s<e) out.push({s:Math.max(c.s,s.t),e:Math.min(c.e,e),name:c.name,root:c.root,q:c.q,bass:c.bass}); }); });
  out.sort((x,y)=>x.s-y.s);
  L._ck=key; L._cl=out; return out;"""),
    ("""function setGrid(g){
  if(!L.analysis) return;
  if(g) L.analysis.userGrid=g; else delete L.analysis.userGrid;
  L._gk=null; L._ck=null;
  if(L.lesson) LDB.tx('readwrite').then(st=>{ const q=st.get(L.lesson.id); q.onsuccess=()=>{ const x=q.result; if(x){ x.analysis=L.analysis; st.put(x); } }; });
  SP.sync(); renderLearnLayout();
}""",
     """function setGrid(g){
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
}"""),
    ("$('#lTempo').onclick=()=>openTempoSheet();", "$('#lTempo').onclick=()=>openTimingEditor('learn');"),
])

# The Learn click accents the bar's "1" from the grid, tempo changes included.
rep('build.js', [
    ("this.node.port.postMessage({type:'beats',beats:a.beats||[],downbeat:a.downbeat||0,tempo:a.tempo||0});",
     "this.node.port.postMessage({type:'beats',beats:a.beats||[],downbeat:a.downbeat||0,tempo:a.tempo||0,accents:(a.inBar||[]).map(x=>x===0)});"),
])
rep(r'C:\Amanorsac Studio\products\sampler\apps\topend\resources\ui\stemplayer.js', [
    ("case 'beats': this.beats = m.beats || []; this.downbeat = m.downbeat || 0; this.tempo = m.tempo || 0; this.seekBeat(); break;",
     "case 'beats': this.beats = m.beats || []; this.downbeat = m.downbeat || 0; this.tempo = m.tempo || 0; this.accents = m.accents || null; this.seekBeat(); break;"),
    ("this.trigger ((this.nextBeat % 4) === this.downbeat);",
     "this.trigger (this.accents ? !! this.accents[this.nextBeat] : (this.nextBeat % 4) === this.downbeat);"),
])
