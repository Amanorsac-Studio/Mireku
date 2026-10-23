/* ═════════════════════════════ TIMING ═════════════════════════════
   Where a song starts and ends, and its tempo: a free-time intro, tempo
   changes, and each section's "1" lined up with the hits by dragging the audio
   under the beat lines. Opened from Learn (Tempo) and Cover (Timing); both read
   the result through gridOf(). Edits are heard live (the click follows them)
   and kept only on Save. */
let TE=null;

/** Loudness of the song at 200 points a second, for lining up hits (the
 *  analysis' own waveform is far too coarse for that). Decoded once per song. */
const TE_ENV={};
async function timingEnvelope(id){
  if(TE_ENV[id]) return TE_ENV[id];
  const x=await LDB.tx('readonly').then(st=>new Promise(res=>{ const q=st.get(id); q.onsuccess=()=>res(q.result); q.onerror=()=>res(null); }));
  if(!x||!x.blob) return null;
  const AC=window.AudioContext||window.webkitAudioContext, ctx=new AC();
  try{
    const buf=await x.blob.arrayBuffer();
    const ab=await new Promise((res,rej)=>ctx.decodeAudioData(buf,res,rej));
    const rate=200, hop=Math.max(1,Math.round(ab.sampleRate/rate)), n=Math.ceil(ab.length/hop);
    const env=new Float32Array(n), chans=[];
    for(let c=0;c<Math.min(2,ab.numberOfChannels);c++) chans.push(ab.getChannelData(c));
    for(let k=0;k<n;k++){ let m=0; const e=Math.min(ab.length,(k+1)*hop);
      for(const d of chans) for(let i=k*hop;i<e;i+=2){ const v=d[i]<0?-d[i]:d[i]; if(v>m) m=v; } env[k]=m; }
    TE_ENV[id]={env,rate};
    return TE_ENV[id];
  } catch(_){ return null; }
  finally{ try{ ctx.close(); }catch(_){} }
}

function openTimingEditor(where){
  const id=where==='cover'?C.id:(L.lesson&&L.lesson.id);
  const a=where==='cover'?C.a:L.analysis;
  if(!id||!a){ toast('No song loaded.'); return; }
  const g0=gridOf(a);
  const orig=a.timing?JSON.parse(JSON.stringify(a.timing)):null;
  const legacy=a.userGrid&&a.userGrid.bpm>0?{t:a.userGrid.t0,bpm:a.userGrid.bpm}:null;
  const firstOne=g0&&g0.beats.length?g0.beats[Math.max(0,g0.inBar.indexOf(0))]:0;
  const tm=orig||{start:0,end:a.duration,sections:[legacy||{t:firstOne,bpm:Math.round(((g0&&g0.tempo)||120)*10)/10}]};
  if(!(tm.end>0)) tm.end=a.duration;
  TE={where,id,a,tm,orig,sel:0,c:(tm.sections[0].t||0)+3,span:10,dur:a.duration,env:null,saved:false,taps:[]};

  // the player: Learn's transport, or the engine in Cover
  const P=where==='cover'
    ? { time:()=>C.playing?Math.max(0,C.pos):C.from, playing:()=>C.playing,
        play:t=>{ C.from=t; cmd('cover',{action:'play',from:t,countIn:0,bpm:120,record:false}); },
        stop:()=>cmd('cover',{action:'stop'}), seek:t=>{ C.from=t; if(C.playing){ cmd('cover',{action:'stop'}); setTimeout(()=>cmd('cover',{action:'play',from:t,countIn:0,bpm:120,record:false}),60); } },
        click:on=>{ C.pref.click=on; saveCoverPrefs(); pushCoverClick(); }, clickOn:()=>!!C.pref.click }
    : { time:()=>T.time(), playing:()=>!T.paused(),
        play:t=>{ T.seekTo(t); if(T.paused()) T.toggle(); }, stop:()=>{ if(!T.paused()) T.toggle(); }, seek:t=>T.seekTo(t),
        click:on=>{ L.click=on; SP.setClick(on); saveLearnPrefs(); renderLearnLayout(); }, clickOn:()=>!!L.click };
  TE.P=P;

  const body=el('div','te');
  body.innerHTML=
    '<div class="te-row"><div class="te-secs" id="teSecs"></div>'+
      '<button class="btn small" id="teAdd">+ Tempo change at playhead</button><button class="btn small ghost" id="teDel">Remove</button></div>'+
    '<div class="te-row te-ctl">'+
      '<span class="cap">Section</span><b class="num" id="teAt">0:00.0</b>'+
      '<button class="swt" id="teFree"><i></i>Free time</button>'+
      '<span class="cap" style="margin-left:6px">Tempo</span>'+
      '<div class="stepper"><button id="teDn" aria-label="Slower">−</button><input class="field num" id="teBpm" type="number" min="30" max="300" step="0.1" aria-label="Tempo in BPM"><button id="teUp" aria-label="Faster">+</button></div>'+
      '<button class="btn small" id="teHalf">½×</button><button class="btn small" id="teDouble">2×</button>'+
      '<button class="btn small" id="teTap">Tap</button><button class="btn small" id="teOne">“1” at playhead</button></div>'+
    '<div class="te-detail" id="teDetail"><canvas id="teDetailC"></canvas><span class="te-load" id="teLoad">Reading the waveform…</span></div>'+
    '<div class="te-over" id="teOver"><canvas id="teOverC"></canvas></div>'+
    '<div class="te-row">'+
      '<button class="ib play" id="tePlay" aria-label="Play"><svg width="16" height="16" viewBox="0 0 18 18"><path d="M5 3l10 6-10 6z" fill="currentColor"/></svg></button>'+
      '<span class="time num" id="teTime">0:00.0</span>'+
      '<button class="swt" id="teClick"><i></i>Click</button>'+
      '<button class="btn small" id="teStart">Song starts here</button><button class="btn small" id="teEnd">Song ends here</button>'+
      '<div class="stepper" style="margin-left:auto"><button id="teZoomOut" aria-label="Zoom out">−</button><span class="cap">Zoom</span><button id="teZoomIn" aria-label="Zoom in">+</button></div></div>'+
    '<p class="help te-help">Drag the waveform to line the beat lines up with the hits; the bright line is the “1”. Tap the waveform to move the playhead. Drag the handles on the strip below to trim the start and the end.</p>';

  openSheet({title:'Song timing', body, wide:true, actions:[
    {label:'Use detected beat', kind:'ghost', onClick:()=>{ TE.tm={start:TE.tm.start,end:TE.tm.end,sections:[{t:firstOne,bpm:Math.round(((a.tempo)||120)*10)/10}]}; TE.sel=0; teApply(); }},
    {label:'Cancel', onClick:()=>teClose(false)},
    {label:'Save', kind:'primary', onClick:()=>teClose(true)}]});
  $('#sheet').classList.add('timing');

  // controls
  $('#teAdd').onclick=()=>{ const t=teSnap(P.time()); const cur=teSec(); TE.tm.sections.push({t,bpm:cur.bpm>0?cur.bpm:(cur.prev||120)}); teSort(); TE.sel=TE.tm.sections.findIndex(s=>s.t===t); teApply(); };
  $('#teDel').onclick=()=>{ if(TE.tm.sections.length<2) return; TE.tm.sections.splice(TE.sel,1); TE.sel=Math.max(0,TE.sel-1); teApply(); };
  $('#teFree').onclick=()=>{ const s=teSec(); if(s.bpm>0){ s.prev=s.bpm; s.bpm=0; } else s.bpm=s.prev||120; teApply(); };
  const setBpm=v=>{ const s=teSec(); if(!(v>=30&&v<=300)) return; s.bpm=Math.round(v*10)/10; teApply(); };
  $('#teBpm').onchange=e=>setBpm(+e.target.value);
  $('#teDn').onclick=()=>setBpm((teSec().bpm||120)-0.1);
  $('#teUp').onclick=()=>setBpm((teSec().bpm||120)+0.1);
  $('#teHalf').onclick=()=>setBpm((teSec().bpm||120)/2);
  $('#teDouble').onclick=()=>setBpm((teSec().bpm||120)*2);
  $('#teOne').onclick=()=>{ const s=teSec(), t=P.time(); if(TE.sel>0&&t<=TE.tm.sections[TE.sel-1].t) return; s.t=t; teSort(); teApply(); };
  $('#teTap').onclick=()=>{
    // While the song plays: tap on each beat, starting on a "1".
    const now=P.time(), taps=TE.taps; if(taps.length&&(now-taps[taps.length-1]>2.5||now<taps[taps.length-1])) taps.length=0; taps.push(now);
    if(taps.length>=4){ const n=taps.length, mx=(n-1)/2, my=taps.reduce((x,y)=>x+y,0)/n; let sxy=0,sxx=0;
      taps.forEach((y,i)=>{ sxy+=(i-mx)*(y-my); sxx+=(i-mx)*(i-mx); }); const step=sxy/sxx;
      if(step>0.2&&step<2){ const s=teSec(); s.bpm=Math.round(600/step)/10; s.t=my-mx*step; teSort(); teApply(); } }
    $('#teTap').textContent=taps.length<4?'Tap ('+taps.length+')':'Tap';
  };
  $('#teStart').onclick=()=>{ TE.tm.start=Math.min(P.time(),TE.tm.end-1); teApply(); };
  $('#teEnd').onclick=()=>{ TE.tm.end=Math.max(P.time(),TE.tm.start+1); teApply(); };
  $('#tePlay').onclick=()=>{ if(P.playing()) P.stop(); else P.play(P.time()); };
  $('#teClick').onclick=()=>{ P.click(!P.clickOn()); teRender(); };
  $('#teZoomIn').onclick=()=>{ TE.span=Math.max(3,TE.span/1.5); teDraw(); };
  $('#teZoomOut').onclick=()=>{ TE.span=Math.min(60,TE.span*1.5); teDraw(); };

  // the detail view: drag the audio, tap to move the playhead
  const det=$('#teDetail');
  det.addEventListener('pointerdown',ev=>{
    const r=det.getBoundingClientRect(), x0=ev.clientX, s=teSec(), t0=s.t, c0=TE.c; let moved=false;
    det.setPointerCapture(ev.pointerId);
    const mv=e=>{ const dx=e.clientX-x0; if(!moved&&Math.abs(dx)<4) return; moved=true;
      const dt=dx/r.width*TE.span;
      // the audio follows the finger and the beat lines stay where they are
      const lo=TE.sel>0?TE.tm.sections[TE.sel-1].t+0.05:-60, hi=TE.sel+1<TE.tm.sections.length?TE.tm.sections[TE.sel+1].t-0.05:TE.dur;
      s.t=clamp(t0-dt,lo,hi); TE.c=c0-(t0-s.t); teApply(true); };
    const up=e=>{ det.removeEventListener('pointermove',mv); det.removeEventListener('pointerup',up);
      if(!moved){ const t=TE.c-TE.span/2+(e.clientX-r.left)/r.width*TE.span; P.seek(clamp(t,0,TE.dur)); }
      else teApply(); };
    det.addEventListener('pointermove',mv); det.addEventListener('pointerup',up);
  });
  // the strip: the view window, and the start and end handles
  const ov=$('#teOver');
  ov.addEventListener('pointerdown',ev=>{
    const r=ov.getBoundingClientRect(), T2=x=>clamp((x-r.left)/r.width*TE.dur,0,TE.dur), X=t=>t/TE.dur*r.width+r.left;
    const grab=Math.abs(ev.clientX-X(TE.tm.start))<12?'start':Math.abs(ev.clientX-X(TE.tm.end))<12?'end':'view';
    ov.setPointerCapture(ev.pointerId);
    const mv=e=>{ const t=T2(e.clientX);
      if(grab==='start') TE.tm.start=Math.min(t,TE.tm.end-1); else if(grab==='end') TE.tm.end=Math.max(t,TE.tm.start+1); else TE.c=t;
      if(grab==='view') teDraw(); else teApply(true); };
    mv(ev);
    const up=()=>{ ov.removeEventListener('pointermove',mv); ov.removeEventListener('pointerup',up); teApply(); };
    ov.addEventListener('pointermove',mv); ov.addEventListener('pointerup',up);
  });

  teApply(true);
  timingEnvelope(id).then(e=>{ if(!TE||TE.id!==id) return; TE.env=e; $('#teLoad').hidden=true; if(!e) $('#teLoad').textContent='No audio file for this song.'; teDraw(); });
  // follow the playhead; a sheet closed by tapping outside counts as Cancel
  const loop=()=>{ if(!TE) return;
    if(!$('#scrim').classList.contains('on')){ teClose(false,true); return; }
    const t=P.time(); if(P.playing()&&(t<TE.c-TE.span/2||t>TE.c+TE.span/2-0.5)) TE.c=t+TE.span*0.3;
    $('#teTime').textContent=teFmt(t);
    $('#tePlay').classList.toggle('on',P.playing());
    teDraw(); TE.raf=requestAnimationFrame(loop); };
  TE.raf=requestAnimationFrame(loop);
}

const teSec=()=>TE.tm.sections[TE.sel]||TE.tm.sections[0];
function teSort(){ const s=teSec(); TE.tm.sections.sort((x,y)=>x.t-y.t); TE.sel=TE.tm.sections.indexOf(s); }
const teFmt=t=>{ t=Math.max(0,t||0); const m=Math.floor(t/60), s=t-m*60; return m+':'+(s<10?'0':'')+s.toFixed(1); };
/** The beat nearest t, else t: a tempo change lands on a beat. */
function teSnap(t){ const g=gridOf(TE.a); if(!g||!g.beats.length) return t; let b=g.beats[0]; g.beats.forEach(x=>{ if(Math.abs(x-t)<Math.abs(b-t)) b=x; }); return Math.abs(b-t)<0.4?b:t; }

/** Edits are heard at once: the working timing is applied to the song. */
function teApply(light){
  TE.a.timing=JSON.parse(JSON.stringify(TE.tm));
  delete TE.a.userGrid;
  if(TE.where==='learn'||(L.lesson&&L.lesson.id===TE.id)){ L._gk=null; L._ck=null; if(!light){ SP.sync(); renderLearnLayout(); } }
  if(TE.where==='cover'&&!light){ pushCoverClick(); renderCover(); }
  teRender();
}
function teRender(){
  if(!TE) return;
  const host=$('#teSecs'); if(!host) return; host.innerHTML='';
  TE.tm.sections.forEach((s,i)=>{ const b=el('button',i===TE.sel?'on':'',(s.bpm>0?s.bpm.toFixed(1)+' BPM':'Free')+' <small>'+teFmt(s.t)+'</small>');
    b.onclick=()=>{ TE.sel=i; TE.c=s.t+TE.span*0.3; teRender(); }; host.appendChild(b); });
  const s=teSec();
  $('#teAt').textContent=teFmt(s.t);
  $('#teFree').classList.toggle('on',!(s.bpm>0));
  const f=$('#teBpm'); if(document.activeElement!==f) f.value=s.bpm>0?s.bpm:'';
  ['teBpm','teDn','teUp','teHalf','teDouble','teTap'].forEach(k=>{ $('#'+k).disabled=!(s.bpm>0)&&k!=='teTap'; });
  $('#teDel').disabled=TE.tm.sections.length<2;
  $('#teClick').classList.toggle('on',TE.P.clickOn());
  teDraw();
}
function teDraw(){
  if(!TE) return;
  const c=$('#teDetailC'); if(!c||!c.offsetParent) return;
  let {w,h,d}=sizeCanvas(c), g=c.getContext('2d'); g.setTransform(d,0,0,d,0,0); g.clearRect(0,0,w,h);
  const v0=TE.c-TE.span/2, X=t=>(t-v0)/TE.span*w, gr=gridOf(TE.a), mid=h/2;
  // outside the song: shaded
  g.fillStyle='rgba(0,0,0,.5)'; if(TE.tm.start>v0) g.fillRect(0,0,X(TE.tm.start),h); if(TE.tm.end<v0+TE.span) g.fillRect(X(TE.tm.end),0,w-X(TE.tm.end),h);
  // the audio
  const E=TE.env;
  if(E){ g.fillStyle='#8f877c';
    for(let px=0;px<w;px+=2){ const t=v0+px/w*TE.span, k=Math.floor(t*E.rate); if(k<0||k>=E.env.length) continue;
      let m=0; const k2=Math.min(E.env.length,Math.floor((v0+(px+2)/w*TE.span)*E.rate)+1); for(let j=k;j<k2;j++) if(E.env[j]>m) m=E.env[j];
      const a=Math.max(1,m*h*0.46); g.fillRect(px,mid-a,1.5,2*a); } }
  // the beats: the "1" bright with its bar number, the others faint
  if(gr){ let bar=0;
    gr.beats.forEach((b,i)=>{ if(gr.inBar[i]===0) bar++; if(b<v0-0.1||b>v0+TE.span+0.1) return; const x=X(b), one=gr.inBar[i]===0;
      g.fillStyle=one?'rgba(227,164,71,.95)':'rgba(255,255,255,.22)'; g.fillRect(x-(one?1:0.5),0,one?2:1,h);
      if(one){ g.font='700 10px '+FONT(); g.fillText(String(bar),x+3,11); } }); }
  // section starts
  TE.tm.sections.forEach((s,i)=>{ if(s.t<v0||s.t>v0+TE.span) return; const x=X(s.t);
    g.fillStyle=i===TE.sel?'#86b892':'#5e8a69'; g.fillRect(x-1,0,2,h);
    g.font='700 10.5px '+FONT(); const lab=s.bpm>0?s.bpm.toFixed(1)+' BPM':'Free time'; g.fillText(lab,x+4,h-6); });
  // playhead
  const t=TE.P.time(); if(t>=v0&&t<=v0+TE.span){ g.fillStyle='#f2ece4'; g.fillRect(X(t)-1,0,2,h); }

  // the strip: the whole song, the view window, the trim handles
  const o=$('#teOverC'); ({w,h,d}=sizeCanvas(o)); g=o.getContext('2d'); g.setTransform(d,0,0,d,0,0); g.clearRect(0,0,w,h);
  const D=TE.dur||1, OX=t=>t/D*w;
  const pk=(TE.a.peaks&&TE.a.peaks.length?TE.a.peaks:(TE.where==='cover'&&C.st&&C.st.peaks))||[];
  g.fillStyle='#5d554b'; for(let px=1;px<w;px+=2){ const v=pk.length?pk[Math.floor(px/w*pk.length)]||0:0.15, a=Math.max(1,v*h*0.42); g.fillRect(px,h/2-a,1.2,2*a); }
  g.fillStyle='rgba(0,0,0,.55)'; g.fillRect(0,0,OX(TE.tm.start),h); g.fillRect(OX(TE.tm.end),0,w-OX(TE.tm.end),h);
  g.strokeStyle='#f2ece4'; g.lineWidth=1; g.strokeRect(OX(v0),1,Math.max(2,OX(TE.span)),h-2);
  [[TE.tm.start,'S'],[TE.tm.end,'E']].forEach(([tt,lab])=>{ const x=OX(tt); g.fillStyle='#e3a447'; g.fillRect(x-1.5,0,3,h);
    g.beginPath(); g.roundRect?g.roundRect(x-8,h/2-8,16,16,4):g.rect(x-8,h/2-8,16,16); g.fill();
    g.fillStyle='#1c1306'; g.font='700 10px '+FONT(); g.textAlign='center'; g.fillText(lab,x,h/2+3.5); g.textAlign='start'; });
  g.fillStyle='#f2ece4'; g.fillRect(OX(t)-0.5,0,1,h);
}
function teClose(save,already){
  if(!TE) return;
  cancelAnimationFrame(TE.raf);
  const t=TE; TE=null;
  $('#sheet').classList.remove('timing');
  if(save){ setTiming(t.id,t.a,t.tm); toast('Timing saved.'); }
  else{ if(t.orig) t.a.timing=t.orig; else delete t.a.timing;
    if(L.lesson&&L.lesson.id===t.id){ L._gk=null; L._ck=null; SP.sync(); renderLearnLayout(); }
    if(t.where==='cover'){ pushCoverClick(); renderCover(); } }
  if(!already) closeSheet();
}
