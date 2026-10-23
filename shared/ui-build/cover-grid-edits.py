# One-off: Cover reads the same timing as Learn (gridOf): tempo sections,
# the song's start, and the beats sent to the engine's click.
import io, os
HERE = os.path.dirname(os.path.abspath(__file__))
p = os.path.join(HERE, 'part5b-cover.js')
s = io.open(p, encoding='utf-8').read()
def rep(a, b):
    global s
    if a not in s: raise SystemExit('not found: ' + a[:90])
    s = s.replace(a, b, 1)

a = s.index('/** The song\'s bar grid: the tempo typed in Learn')
b = s.index('/** The Cover overlay:')
s = s[:a] + '''/** The song's timing, shared with Learn: beats, bars, tempo sections, start. */
const coverGrid=()=>{ const g=gridOf(C.a); return g&&g.beats.length>1?g:null; };
/** The "1" of the bar at or before t (t itself where there are no bars). */
function coverBar(t){
  const g=coverGrid(); if(!g) return Math.max(0,t);
  let at=null; g.beats.forEach((b,i)=>{ if(g.inBar[i]===0&&b<=t+1e-3) at=b; });
  return at==null?Math.max(0,t):at;
}
const coverStartAt=()=>{ const g=gridOf(C.a); return g?g.start:0; };
''' + s[b:]

rep("  C.id=id; C.name=x.name; C.a=x.analysis||null; C.from=0; C.punch={a:null,b:null};",
    "  C.id=id; C.name=x.name; C.a=x.analysis||null; C.from=coverStartAt(); C.punch={a:null,b:null};")
rep("""  const g=coverGrid();
  cmd('cover',{action:'click',on:!!(C.pref.click&&g),bpm:g?g.bpm:120,t0:g?g.t0:0,beats:4,db:-6});""",
    """  const g=coverGrid();
  cmd('cover',{action:'click',on:!!(C.pref.click&&g),bpm:g?(g.tempo||120):120,t0:0,beats:4,db:-6});
  // the beats themselves: free intros and tempo changes are clicked where they are
  cmd('cover',{action:'clickBeats',beats:g?g.beats:[],accents:g?g.inBar.map(x=>x===0):[]});""")
rep("""  if(record&&C.punch.a!=null) from=coverBar(Math.max(0,C.punch.a-(g?4*60/g.bpm:2)));
  else if(bars>0) from=coverBar(from);
  cmd('cover',{action:'play',from,countIn:bars*4,bpm:g?g.bpm:120,record:!!record});""",
    """  const bpmAt=t=>tempoAt(g,t)||(g&&g.tempo)||120;
  if(record&&C.punch.a!=null) from=coverBar(Math.max(0,C.punch.a-4*60/bpmAt(C.punch.a)));
  else if(bars>0) from=coverBar(from);
  // a count-in into a free section counts at the first tempo of the song
  cmd('cover',{action:'play',from,countIn:bars*4,bpm:bpmAt(from),record:!!record});""")
rep("g?Math.round(g.bpm)+' BPM':null,", "g&&g.tempo?Math.round(g.tempo)+' BPM'+(g.sections&&g.sections.length>1?' (changes)':''):null,")
rep("""  if(gr){ const bar=4*60/gr.bpm; if(X(bar)>14){ g.fillStyle='rgba(255,255,255,.05)'; for(let t=gr.t0;t<dur;t+=bar) g.fillRect(X(t),6,1,h-6); } }""",
    """  if(gr){ const ones=gr.beats.filter((b,i)=>gr.inBar[i]===0);
    if(ones.length>1&&X(ones[1]-ones[0])>14){ g.fillStyle='rgba(255,255,255,.05)'; ones.forEach(t=>g.fillRect(X(t),6,1,h-6)); }
    // where the song starts and ends, if trimmed
    g.fillStyle='rgba(0,0,0,.45)'; if(gr.start>0) g.fillRect(0,0,X(gr.start),h); if(gr.end<dur-0.05) g.fillRect(X(gr.end),0,w-X(gr.end),h); }""")
rep("$('#cStart').onclick=()=>coverSeek(0);",
    "$('#cStart').onclick=()=>coverSeek(coverStartAt());\n$('#cTiming').onclick=()=>openTimingEditor('cover');")
# the ready event re-sends the click beats (a reload at a new sample rate)
rep("  if(d.state==='ready'&&(!was||was.song!==d.song||was.state!=='ready')) pushCoverMix();",
    "  if(d.state==='ready'&&(!was||was.song!==d.song||was.state!=='ready')){ pushCoverMix(); pushCoverClick(); }")
io.open(p, 'w', encoding='utf-8', newline='').write(s)

p = os.path.join(HERE, 'part2-body.html')
h = io.open(p, encoding='utf-8').read()
a = '<button class="swt" id="cClick"><i></i>Click</button>'
assert a in h
h = h.replace(a, '<button class="btn" id="cTiming">Timing</button>\n        ' + a, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(h)

# the old tempo sheet is replaced by the timing editor
p = os.path.join(HERE, 'part5-learn.js')
l = io.open(p, encoding='utf-8').read()
a = l.index('function openTempoSheet(){')
b = l.index("$('#lShow').addEventListener", a)
l = l[:a] + l[b:]
io.open(p, 'w', encoding='utf-8', newline='').write(l)
print('ok')
