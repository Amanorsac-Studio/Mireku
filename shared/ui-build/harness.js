// Offline harness: the real index.html with a stand-in for the JUCE bridge that
// replays a state captured from the running app. Layout checks and screenshots
// at desktop and iPad sizes, in headless Chrome, without touching the app.
//   node harness.js <outdir> [top|low]
const fs = require('fs'), path = require('path'), { spawn } = require('child_process');
const HERE = __dirname, PRODUCT = process.argv[3] || 'top';
const UI = process.env.UI_DIR || ('C:/Amanorsac Studio/products/sampler/apps/' + (PRODUCT === 'low' ? 'lowend' : 'topend') + '/resources/ui');
// MOCK=1: one size, the Rig tab only, no layout checks beyond a screenshot
const MOCK = process.env.MOCK === '1';
const out = process.argv[2] || path.join(HERE, 'harness-shots');
fs.mkdirSync(out, { recursive: true });
const H = path.join(HERE, 'harness'); fs.rmSync(H, { recursive: true, force: true });
fs.cpSync(UI, H, { recursive: true });
const cap = JSON.parse(fs.readFileSync(path.join(HERE, PRODUCT === 'low' ? 'app-state-low.json' : 'app-state.json'), 'utf8'));
const mock = `<script>
window.__CAP__=${JSON.stringify(cap)};
window.__COVER__=(()=>{ const len=(${JSON.stringify(cap.analysis&&cap.analysis.duration||200)}); const pk=n=>Array.from({length:400},(_,i)=>{ const t=i/400*len; return t>n[0]&&t<n[1]?+(0.35+0.25*Math.abs(Math.sin(i*0.37))).toFixed(3):0; });
  return {state:'ready',error:'',song:'h',songName:'Sunday Test Song',length:len,stems:['Drums','Bass','Guitar & keys','Vocals'],
    takes:[{number:1,from:0,to:len,punched:false,saved:true,peaks:pk([0,len])},{number:2,from:8,to:14,punched:true,saved:true,peaks:pk([0,len])}],
    selected:2,latencyMs:11.6,exporting:false,export:null}; })();
(function(){ const L={}; let t0=Date.now();
  const fire=(n,d)=>(L[n]||[]).forEach(f=>f(d));
  window.__JUCE__={backend:{
    addEventListener(n,f){ (L[n]=L[n]||[]).push(f); return [n,f]; },
    removeEventListener(x){ if(x&&L[x[0]]) L[x[0]]=L[x[0]].filter(f=>f!==x[1]); },
    emitEvent(ev,p){ if(ev!=='__juce__invoke') return; const [name,args]=p.params; let r=true;
      if(name==='refresh') setTimeout(()=>fire('state',__CAP__.S),10);
      if(name==='audio') r=args.action==='describe'?__CAP__.audio:true;
      if(name==='stem') r=args.action==='status'?{state:'idle',modelReady:true}:false;
      if(name==='about') r={version:'1.0.0'};
      if(name==='cover') r=args.action==='status'?window.__COVER__:(args.action==='has'?true:true);
      setTimeout(()=>fire('__juce__complete',{promiseId:p.resultId,result:r}),5); } } };
  setInterval(()=>{ const t=(Date.now()-t0)/1000; fire('tick',{in:0.2+0.1*Math.sin(t*3),out:0.4,tempo:96,loop:{state:'idle',layers:[false,false,false,false]},
    click:{on:true,beats:4,beat:Math.floor(t*1.6)%4,level:-6,sound:0,countIn:1,output:0,separate:false,standalone:true},
    tuner:{has:false}, live:{note:52,count:1,cents:4}, cover:{pos:9.5,playing:true,recording:true,countIn:false}}); },100);
})();
</script>`;
let html = fs.readFileSync(path.join(H, 'index.html'), 'utf8').replace('</head>', mock + '\n</head>');
// once the page has state, open the captured song's analysis in Learn (no audio)
html = html.replace('</body>', `<script>setTimeout(()=>{ L.analysis=__CAP__.analysis; L.lesson=__CAP__.lesson; L.lessonId=L.lesson&&L.lesson.id; L.key={root:7,scale:'major'}; renderKeySelects(); },800);</script></body>`);
fs.writeFileSync(path.join(H, 'index.html'), html);

const chrome = 'C:/Program Files/Google/Chrome/Application/chrome.exe';
const prof = path.join(HERE, 'harness-profile');
const proc = spawn(chrome, ['--headless=new', '--remote-debugging-port=9360', '--user-data-dir=' + prof, '--hide-scrollbars', '--window-size=1280,828', 'about:blank'], { stdio: 'ignore' });
const sleep = ms => new Promise(r => setTimeout(r, ms));
(async () => {
  let pages; for (let i = 0; i < 40; i++) { try { pages = await (await fetch('http://127.0.0.1:9360/json')).json(); if (pages.length) break; } catch (_) {} await sleep(250); }
  const pg = pages.find(p => p.type === 'page');
  const ws = new WebSocket(pg.webSocketDebuggerUrl);
  let id = 0; const wait = {}; const errors = [];
  ws.onmessage = e => { const m = JSON.parse(e.data); if (m.id && wait[m.id]) { wait[m.id](m); delete wait[m.id]; }
    if (m.method === 'Runtime.exceptionThrown') errors.push(m.params.exceptionDetails.exception ? m.params.exceptionDetails.exception.description : m.params.exceptionDetails.text); };
  await new Promise(r => ws.onopen = r);
  const send = (method, params = {}) => new Promise(r => { const i = ++id; wait[i] = r; ws.send(JSON.stringify({ id: i, method, params })); });
  const ev = async x => { const r = await send('Runtime.evaluate', { expression: x, awaitPromise: true, returnByValue: true }); return r.result && r.result.result ? r.result.result.value : r.result; };
  const shot = async n => { const r = await send('Page.captureScreenshot', { format: 'png' }); fs.writeFileSync(path.join(out, n), Buffer.from(r.result.data, 'base64')); };
  await send('Runtime.enable'); await send('Page.enable');
  await send('Emulation.setDeviceMetricsOverride', { width: 1280, height: 828, deviceScaleFactor: 1, mobile: false });
  await send('Page.navigate', { url: 'file:///' + path.join(H, 'index.html').replace(/\\/g, '/') });
  await sleep(2500);
  const overflow = `(()=>{ const bad=[]; const vh=innerHeight, vw=innerWidth;
    document.querySelectorAll('.view.on, .view.on *').forEach(n=>{ const r=n.getBoundingClientRect(); if(!r.width||!r.height) return;
      if(getComputedStyle(n).position==='fixed') return;
      let p=n.parentElement, clipped=false; while(p&&p!==document.body){ const o=getComputedStyle(p).overflow; if(o!=='visible'){ const q=p.getBoundingClientRect(); if(r.top>=q.bottom||r.left>=q.right){ clipped=true; break; } } p=p.parentElement; }
      if(!clipped&&(r.bottom>vh+1||r.right>vw+1)) bad.push((n.id||n.className||n.tagName).toString().slice(0,30)+' '+Math.round(r.right)+'x'+Math.round(r.bottom)); });
    return [...new Set(bad)].slice(0,8); })()`;
  const masked = `(()=>{ const bad=[]; const cards=[...document.querySelectorAll('.view.on .card, .view.on .amp')].filter(c=>c.offsetParent);
    document.querySelectorAll('.view.on .stem').forEach(t=>{ if(!t.offsetParent) return; const r=t.getBoundingClientRect(); const lv=t.querySelector('.lv'); if(lv){ const q=lv.getBoundingClientRect(); if(q.bottom>r.bottom+1) bad.push('tile cut:'+t.textContent.slice(0,14)+' '+Math.round(r.height)+'px'); } });
    cards.forEach(c=>{ if(c.scrollHeight>c.clientHeight+3) bad.push('cut:'+(c.id||c.className).split(' ')[0]+' '+c.scrollHeight+'>'+c.clientHeight); });
    for(let i=0;i<cards.length;i++) for(let j=i+1;j<cards.length;j++){ const a=cards[i].getBoundingClientRect(), b=cards[j].getBoundingClientRect();
      if(cards[i].contains(cards[j])||cards[j].contains(cards[i])) continue;
      if(a.left<b.right-2&&b.left<a.right-2&&a.top<b.bottom-2&&b.top<a.bottom-2) bad.push('overlap:'+(cards[i].id||cards[i].className).split(' ')[0]+'/'+(cards[j].id||cards[j].className).split(' ')[0]); }
    return bad.slice(0,8); })()`;
  for (const [w, h] of (MOCK ? [[1280, 800]] : [[1024, 680], [1024, 700], [1100, 720], [1280, 800], [1366, 768], [1440, 900], [1920, 1080], [1024, 768], [768, 1024]])) {
    await send('Emulation.setDeviceMetricsOverride', { width: w, height: h, deviceScaleFactor: 1, mobile: false });
    for (const v of (MOCK ? ['rig'] : ['rig', 'learn', 'cover', 'library', 'settings'])) {
      await ev(`showView('${v}')`); await sleep(700);
      if (v === 'cover') { await ev(`(()=>{ const s=$('#cSong'); s.innerHTML='<option value="h">Sunday Test Song</option>'; s.value='h'; s._ddSync&&s._ddSync(); C.id='h'; C.a=__CAP__.analysis; C.punch={a:8,b:14}; C.recFrom=7; coverStatus(__COVER__); })()`); await sleep(500); }
      if (v === 'learn') { await ev(`T.seekTo&&0`); }
      const o = await ev(overflow), m = await ev(masked);
      if (o.length || m.length) console.log(w + 'x' + h, v, 'overflow:', JSON.stringify(o), 'masked:', JSON.stringify(m));
      await shot(w + 'x' + h + '-' + v + '.png');
    }
  }
  // the timing editor and Learn in YouTube mode
  for (const [w, h] of (MOCK ? [] : [[1280, 800], [1024, 680], [768, 1024]])) {
    await send('Emulation.setDeviceMetricsOverride', { width: w, height: h, deviceScaleFactor: 1, mobile: false });
    await ev(`showView('learn')`); await sleep(500);
    await ev(`openTimingEditor('learn')`); await sleep(700);
    const cut = await ev(`(()=>{ const s=document.querySelector('#sheet'); const r=s.getBoundingClientRect(); const b=s.querySelector('.sb'); return {sheet:Math.round(r.height), vh:innerHeight, scroll:b.scrollHeight>b.clientHeight+2}; })()`);
    console.log(w + 'x' + h, 'timing editor', JSON.stringify(cut));
    await shot(w + 'x' + h + '-timing.png');
    await ev(`teClose(false)`); await sleep(200);
    await ev(`(()=>{ L.source='yt'; renderLearnLayout(); })()`); await sleep(500);
    const o = await ev(overflow), m = await ev(masked);
    if (o.length || m.length) console.log(w + 'x' + h, 'youtube', 'overflow:', JSON.stringify(o), 'masked:', JSON.stringify(m));
    await shot(w + 'x' + h + '-youtube.png');
    await ev(`(()=>{ L.source='song'; renderLearnLayout(); })()`);
  }
  console.log('checked 9 sizes x 5 tabs');
  console.log('errors:', errors.length ? errors.join('\n  ') : 'none');
  ws.close(); proc.kill();
})().catch(e => { console.error(e); proc.kill(); process.exit(1); });
