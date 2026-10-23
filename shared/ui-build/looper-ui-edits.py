# One-off: the looper card gets the time signature and the loop length; the
# Click settings choose a time signature instead of a bare beat count.
import io, os
HERE = os.path.dirname(os.path.abspath(__file__))
def edit(name, pairs):
    p = os.path.join(HERE, name); s = io.open(p, encoding='utf-8').read()
    for a, b in pairs:
        if a not in s: raise SystemExit(name + ': not found: ' + a[:90])
        s = s.replace(a, b, 1)
    io.open(p, 'w', encoding='utf-8', newline='').write(s); print('edited', name)

edit('part2-body.html', [
    ('''        <div class="clickbar">''',
     '''        <div class="loopbar">
          <select id="loopSig" aria-label="Time signature"></select>
          <div class="seg" id="loopBars" role="radiogroup" aria-label="Loop length"><button data-n="0">Free</button><button data-n="4">4 bars</button><button data-n="8">8 bars</button><button data-n="16">16 bars</button></div>
        </div>
        <div class="clickbar">'''),
    ('''            <div class="field2"><span class="cap">Beats per bar</span><select id="clkBeatsSel" aria-label="Beats per bar"></select></div>''',
     '''            <div class="field2"><span class="cap">Time signature</span><select id="clkBeatsSel" aria-label="Time signature"></select></div>
            <div class="field2"><span class="cap">Loop length</span><select id="clkLoopBars" aria-label="Loop length"><option value="0">Free: ends where REC is pressed</option><option value="1">1 bar</option><option value="2">2 bars</option><option value="4">4 bars</option><option value="8">8 bars</option><option value="16">16 bars</option></select></div>'''),
])
edit('part1-head.html', [
    ('.clickbar{display:flex;align-items:center;gap:8px;margin-top:10px}',
     '.clickbar{display:flex;align-items:center;gap:8px;margin-top:10px}\n'
     '.loopbar{display:flex;align-items:center;gap:8px;margin-top:10px}\n'
     '.loopbar select{flex:none;width:84px}\n'
     '.loopbar .seg{flex:1;display:flex}\n'
     '.loopbar .seg button{flex:1;padding:0 6px}\n'
     '/* the "1" of the bar: larger and in the accent colour, so it is seen as well as heard */\n'
     '.beatdots i.one{background:var(--amber-deep)}\n'
     '.beatdots i.one.now{background:var(--amber);box-shadow:0 0 0 3px var(--amber-soft)}\n'
     '.beatdots i.grp{width:12px;height:12px}'),
])
edit('part4-lib.js', [
    ("  const names={idle:'Idle',armed:'Play to start',recording:'Recording',closing:'Closing on the bar',playing:'Playing',overdub:'Overdub',stopped:'Stopped'};",
     "  const names={idle:'Idle',armed:'Play to start',cued:'Starts on the next bar',recording:'Recording',closing:'Closing on the bar',playing:'Playing',overdub:'Overdub',stopped:'Stopped'};"),
    ("  else if(st==='recording') $('#loopLen').textContent=lp.grid?'Ends on the bar':'Free length';",
     "  else if(st==='recording'){ const lb=(t.click&&t.click.loopBars)||0; $('#loopLen').textContent=lb?'Ends after '+lb+(lb===1?' bar':' bars'):(lp.grid?'Ends on the bar':'Free length'); }\n"
     "  else if(st==='cued') $('#loopLen').textContent='With the click';"),
])
print('done')
