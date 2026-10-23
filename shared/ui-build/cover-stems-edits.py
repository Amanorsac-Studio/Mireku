# One-off: Cover can separate a song itself, and the mix is chosen by which
# parts play (In / Out, with quick choices) rather than by muting.
import io, os
HERE = os.path.dirname(os.path.abspath(__file__))

def rep(name, pairs):
    p = os.path.join(HERE, name)
    s = io.open(p, encoding='utf-8').read()
    for a, b in pairs:
        if a not in s:
            raise SystemExit(name + ': not found: ' + a[:90])
        s = s.replace(a, b, 1)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)
    print('edited', name)

rep('part2-body.html', [
    ('<div class="head"><h2>Mix</h2><span class="cap">Tap to mute · S to solo · drag for level</span></div>',
     '<div class="head"><h2>Mix</h2><div class="mixtools" id="cMixTools"></div><span class="cap mixhint">Tap a part to take it in or out · drag for level</span></div>'),
    ('<div class="lprog" id="cProg" hidden><div class="t" id="cProgT">Preparing the song</div><div class="bar"><b id="cProgB"></b></div></div>',
     '<div class="lprog" id="cProg" hidden><div class="t" id="cProgT">Preparing the song</div><div class="bar"><b id="cProgB"></b></div><button class="btn small" id="cProgCancel" hidden>Cancel</button></div>'),
])

rep('part1-head.html', [
    ('.stem.take{border-color:var(--amber-deep)}',
     '.stem.take{border-color:var(--amber-deep)}\n'
     '.mixtools{display:flex;align-items:center;gap:8px;margin-left:auto;min-width:0}\n'
     '.mixtools .note{color:var(--faint);font-size:12.5px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}\n'
     '.cmix .head .mixhint{margin-left:12px;white-space:nowrap}\n'
     '.stem .in{position:absolute;right:42px;top:9px;font-family:var(--font-num);font-size:10px;letter-spacing:.08em;color:var(--amber)}\n'
     '.stem.muted .in{color:var(--faint)}\n'
     '@media (max-width:1280px){ .cmix .head .mixhint{display:none} }'),
])

rep('part5b-cover.js', [
    # a shared progress overlay
    ("const coverDur=()=>",
     "/** The Cover overlay: preparing a song, or separating it. null hides it. */\n"
     "function coverProg(t,f){ const p=$('#cProg'); p.hidden=t==null; if(t!=null){ $('#cProgT').textContent=t; $('#cProgB').style.width=Math.round((f||0)*100)+'%'; } }\n"
     "const coverDur=()=>"),
    ("  const prog=(t,f)=>{ const p=$('#cProg'); p.hidden=t==null; if(t!=null){ $('#cProgT').textContent=t; $('#cProgB').style.width=Math.round((f||0)*100)+'%'; } };\n",
     "  const prog=coverProg;\n"),
    # In / Out wording on the tiles
    ("    t.querySelector('.lv').textContent=!ready?'—':off?'Muted':",
     "    const tag=t.querySelector('.in'); if(tag) tag.textContent=!ready?'':off?'OUT':'IN';\n"
     "    t.querySelector('.lv').textContent=!ready?'—':off?'Not playing':"),
    ("      t.innerHTML='<div class=\"nm\"></div><div class=\"lv\"></div>';",
     "      t.innerHTML='<div class=\"nm\"></div><span class=\"in\"></span><div class=\"lv\"></div>';"),
    # the quick choices and the separate button, drawn with the tiles
    ("function renderCoverTiles(){",
     "/** Which parts play: all of them, all but the player's own, or the drums alone. */\n"
     "const COVER_MIXES=[\n"
     "  {k:'all',   label:'Full song',              on:i=>true},\n"
     "  {k:'minus', label:'Without '+PRODUCT.part,  on:i=>i!==PRODUCT.mineStem},\n"
     "  {k:'drums', label:'Drums only',             on:i=>i===0}];\n"
     "function coverMixIs(m,n){ return C.pref.stems.slice(0,n).every((s,i)=>!s.solo&&s.mute===!m.on(i)); }\n"
     "function renderCoverTools(){\n"
     "  const host=$('#cMixTools'), st=C.st, ready=st&&st.state==='ready'; host.innerHTML='';\n"
     "  if(!ready) return;\n"
     "  const n=st.stems.length;\n"
     "  if(n<2){\n"
     "    host.appendChild(el('span','note','Not separated: the song plays as one part.'));\n"
     "    const b=el('button','btn small primary','Separate stems'); b.onclick=coverSeparate; host.appendChild(b);\n"
     "    return;\n"
     "  }\n"
     "  const seg=el('div','seg'); seg.setAttribute('aria-label','Parts that play');\n"
     "  COVER_MIXES.forEach(m=>{ const b=el('button',coverMixIs(m,n)?'on':'',esc(m.label));\n"
     "    b.onclick=()=>{ C.pref.stems.forEach((s,i)=>{ s.solo=false; s.mute=i<n?!m.on(i):s.mute; }); saveCoverPrefs(); pushCoverMix(); renderCoverTiles(); };\n"
     "    seg.appendChild(b); });\n"
     "  host.appendChild(seg);\n"
     "}\n"
     "/** Separates the song from Cover (the same job as Learn's), then reloads it as stems. */\n"
     "async function coverSeparate(){\n"
     "  if(!C.id||C.busy) return;\n"
     "  const id=C.id;\n"
     "  if(L.lessonId!==id) await openLesson(id);              // separation works on the open lesson\n"
     "  const saved=progress; progress=coverProg; C.busy=true;\n"
     "  try{ await startSeparation(); } finally{ progress=saved; }\n"
     "  const cancel=$('#cProgCancel'); cancel.hidden=false; cancel.onclick=()=>cmd('stem',{action:'cancel'});\n"
     "  try{\n"
     "    for(;;){\n"
     "      const st=await cmd('stem',{action:'status',id});\n"
     "      const busy=st&&['receiving','loading','separating','writing'].indexOf(st.state)>=0;\n"
     "      if(!busy){\n"
     "        if(st&&st.hasStems){ await cmd('cover',{action:'load',id,name:C.name}); toast('Stems ready.'); }\n"
     "        else if(st&&st.state==='error'&&st.error) toast(st.error);\n"
     "        return;\n"
     "      }\n"
     "      coverProg((st.stage||'Separating')+'. Playing continues on the other tabs.',st.progress||0);\n"
     "      await new Promise(r=>setTimeout(r,800));\n"
     "    }\n"
     "  } finally { cancel.hidden=true; coverProg(null); C.busy=false; }\n"
     "}\n"
     "function renderCoverTiles(){\n"
     "  renderCoverTools();"),
])
