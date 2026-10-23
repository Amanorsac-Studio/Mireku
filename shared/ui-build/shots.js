// Release screenshots from the RUNNING app, over its DevTools port: every main
// screen at 1600x1000, named NN-<what>.png in the order a new user meets them
// (Release Standard §3). The app must be started with
//   WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS=--remote-debugging-port=<port>
//   node shots.js <port> <outdir> <songName> <presetName>
const fs = require('fs'), path = require('path');
const [port, out, songName, presetName] = process.argv.slice(2);
fs.mkdirSync(out, { recursive: true });
const sleep = ms => new Promise(r => setTimeout(r, ms));
(async () => {
  const pages = await (await fetch('http://127.0.0.1:' + port + '/json')).json();
  const page = pages.find(p => p.url.includes('juce.backend')) || pages[0];
  const ws = new WebSocket(page.webSocketDebuggerUrl);
  let id = 0; const wait = {};
  ws.onmessage = e => { const m = JSON.parse(e.data); if (m.id && wait[m.id]) { wait[m.id](m); delete wait[m.id]; } };
  await new Promise(r => ws.onopen = r);
  const send = (method, params = {}) => new Promise(r => { const i = ++id; wait[i] = r; ws.send(JSON.stringify({ id: i, method, params })); });
  const ev = async x => { const r = await send('Runtime.evaluate', { expression: x, awaitPromise: true, returnByValue: true }); return r.result && r.result.result ? r.result.result.value : null; };
  const shot = async (name) => { const r = await send('Page.captureScreenshot', { format: 'png' }); fs.writeFileSync(path.join(out, name), Buffer.from(r.result.data, 'base64')); console.log('saved', name); };
  await send('Emulation.setDeviceMetricsOverride', { width: 1600, height: 1000, deviceScaleFactor: 1, mobile: false });
  await ev(`closeSheet()`); await sleep(400);

  // a factory preset with a full board
  const pre = await ev(`(async()=>{ const p=(S.presets||[]).find(x=>x.name===${JSON.stringify(presetName||'')}); if(!p) return null; await cmd('loadPreset',{id:p.id}); return p.name; })()`);
  console.log('preset:', pre); await sleep(1500);
  // 01 the rig, as it opens
  await ev(`showView('rig')`); await sleep(1200); await shot('01-rig.png');
  // 02 a pedal open (the first one on the board)
  const opened = await ev(`(()=>{ const b=(S.pre||[])[0]; if(!b) return false; openPedalSheet('pre',b.index); return true; })()`);
  if (opened) { await sleep(700); await shot('02-pedal.png'); await ev(`closeSheet()`); await sleep(300); }
  // 03 Learn with a song
  await ev(`showView('learn')`); await sleep(800);
  const song = await ev(`(async()=>{ const all=await LDB.all(); const want=${JSON.stringify(songName || '')};
      const x=all.find(z=>want&&z.name===want)||all.find(z=>z.analysis&&z.analysis.chords&&z.analysis.chords.length);
      if(!x) return null; L.source='song'; await openLesson(x.id); renderLearnLayout(); return x.name; })()`);
  console.log('Learn song:', song);
  // wait for the stems to finish loading
  await sleep(1500);
  for (let k = 0; k < 90; k++) { if (await ev(`!!(SP.lesson===L.lessonId&&SP.ready&&!SP.loading&&$('#lProg').hidden)`)) break; await sleep(500); }
  await sleep(1500);
  await ev(`(()=>{ const d=(L.analysis&&L.analysis.duration)||30; T.seekTo(d*0.3); })()`); await sleep(1500);
  await shot('03-learn.png');
  // 04 song timing
  await ev(`openTimingEditor('learn')`); await sleep(2500); await shot('04-song-timing.png');
  await ev(`teClose(false)`); await sleep(400);
  // 05 Cover
  await ev(`showView('cover')`); await sleep(1500);
  const cid = await ev(`(async()=>{ const all=await LDB.all(); const x=all.find(z=>z.name===${JSON.stringify(songName || '')}); if(!x) return null; const s=$('#cSong'); s.value=x.id; if(s._ddSync) s._ddSync(); await coverOpen(x.id); return x.id; })()`);
  for (let k = 0; k < 40; k++) { if (await ev(`!!(C.st&&C.st.state==='ready'&&C.st.song===${JSON.stringify(cid || '')})`)) break; await sleep(500); }
  // the player's own part out: the way Cover is used
  await ev(`(()=>{ const b=[...document.querySelectorAll('#cMixTools .seg button')][1]; if(b) b.click(); C.from=Math.min(20,coverDur()*0.4); drawCoverFrame(); })()`);
  await sleep(1500); await shot('05-cover.png');
  // 06 Library, 07 Share presets
  await ev(`(()=>{ lib={tab:'presets',id:S.presetId,page:0,q:''}; showView('library'); renderLibrary(); })()`); await sleep(1200); await shot('06-library.png');
  await ev(`openShareSheet()`); await sleep(700); await shot('07-share-presets.png');
  await ev(`closeSheet()`); await sleep(300);
  // 08 audio settings, 09 the click
  await ev(`showView('settings')`); await sleep(500);
  await ev(`(()=>{ const b=document.querySelector('#snav button[data-p="audio"]'); if(b) b.click(); })()`); await sleep(1500); await shot('08-settings-audio.png');
  await ev(`(()=>{ const b=document.querySelector('#snav button[data-p="click"]'); if(b) b.click(); })()`); await sleep(1000); await shot('09-settings-click.png');
  await ev(`(()=>{ const b=document.querySelector('#snav button[data-p="audio"]'); if(b) b.click(); showView('rig'); })()`);
  await send('Emulation.clearDeviceMetricsOverride');
  ws.close();
})().catch(e => { console.error(e); process.exit(1); });
