
/*──────────────── about, library actions ────────────────*/
cmd('about').then(a=>{ if(a&&a.version) $('#aboutVersion').textContent=a.version; });
document.querySelectorAll('[data-link]').forEach(b=>b.onclick=()=>cmd('openLink',{which:b.dataset.link}));
$('#btnNotices').onclick=()=>fetch('notices.txt').then(r=>r.text()).then(t=>{
  const pre=el('pre','notices'); pre.textContent=t;
  openSheet({title:'Third-party notices',body:pre,wide:true,actions:[{label:'Close',kind:'primary',onClick:closeSheet}]}); });
$('#btnFolder').onclick=()=>cmd('openFolder');
$('#btnRescan').onclick=()=>cmd('rescan').then(()=>toast('Library rescanned'));
$('#btnBackup').onclick=()=>{ toast('Backing up…'); cmd('backup').then(p=>toast(p?'Saved to Documents › Top End Backups':'The backup could not be written')); };

/* ═════════════════════════════ WIRING ═════════════════════════════ */
function showView(v){
  view=v;
  if(v!=='library') homeRig();          // the rig lives on the Rig page unless the Library is showing it
  document.querySelectorAll('.view').forEach(s=>s.classList.toggle('on',s.id==='view-'+v));
  document.querySelectorAll('#tabs button').forEach(t=>t.classList.toggle('on',t.dataset.view===v));
  learnShow(v==='learn');
  coverShow(v==='cover');
  if(v==='rig') requestAnimationFrame(()=>{ fitBoard(); fitScenes(); });
  if(v==='library') requestAnimationFrame(renderLibrary);
  // The device can change while the app runs (an interface unplugged, a driver
  // panel used), so the panel is read fresh every time Settings is opened.
  if(v==='settings') refreshAudio();
}
document.querySelectorAll('#tabs button').forEach(t=>t.onclick=()=>showView(t.dataset.view));
$('#presetPill').onclick=openPresetMenu;
$('#btnSave').onclick=()=>saveLive($('#btnSave'));
$('#btnNewSet').onclick=()=>newSet();
$('#btnBypass').onclick=()=>cmd('bypassAll',{on:!(S&&S.bypassAll)});
$('#btnTuner').onclick=()=>cmd('tuner',{on:!(S&&S.tunerActive)});
$('#tunerClose').onclick=()=>cmd('tuner',{on:false});
$('#btnTap').onclick=()=>cmd('tap');
$('#bpmBtn').onclick=()=>ask('Tempo (BPM)',String(Math.round((S&&S.tempo)||120)),'Set').then(t=>{ const v=parseFloat(t); if(v>=30&&v<=300) cmd('tempo',{bpm:v}); });
$('#btnReset').onclick=()=>{ cmd('panic'); toast('Reset — everything silenced'); };
$('#btnPrev').onclick=()=>cmd('scene',{delta:-1});
$('#btnNext').onclick=()=>cmd('scene',{delta:1});
$('#latChip').onclick=()=>{ showView('settings'); showPanel('audio'); };
$('#loopRec').onclick=()=>cmd('loop',{action:'record'});
$('#loopStop').onclick=()=>cmd('loop',{action:'stop'});
$('#loopUndo').onclick=()=>cmd('loop',{action:'undo'});
$('#loopClear').onclick=()=>cmd('loop',{action:'clear'});
$('#loopExport').onclick=()=>cmd('loop',{action:'export'}).then(p=>toast(p?'Loop saved to Documents':'Nothing to export yet'));

window.addEventListener('keydown',e=>{
  if(e.target && /input|select|textarea/i.test(e.target.tagName)) return;
  if(gateOn()) return;
  if($('#scrim').classList.contains('on')){ if(e.key==='Escape') closeSheet(); return; }
  const k=e.key.toLowerCase();
  if(view==='learn'){
    if(k===' '){ e.preventDefault(); T.toggle(); return; }
    if(k==='a'){ $('#tA').click(); return; }
    if(k==='b'){ $('#tB').click(); return; }
    if(k==='arrowleft'){ T.seek(-5); return; }
    if(k==='arrowright'){ T.seek(5); return; }
  }
  if(view==='cover'){
    if(k===' '){ e.preventDefault(); coverStart(false); return; }
    if(k==='r'){ $('#cRec').click(); return; }
    if(k==='i'){ coverPunch('a'); return; }
    if(k==='o'){ coverPunch('b'); return; }
    if(k==='home'){ coverSeek(0); return; }
  }
  if(k===' '){ e.preventDefault(); cmd('tap'); }
  else if(k==='t') cmd('tuner',{on:!(S&&S.tunerActive)});
  else if(k==='x') cmd('bypassAll',{on:!(S&&S.bypassAll)});
  else if(k==='l') cmd('loop',{action:'record'});
  else if(k==='escape'){ closeMenu(); cmd('learn',{path:''}); if(S&&S.tunerActive) cmd('tuner',{on:false}); }
  else if(k==='arrowright') cmd('scene',{delta:1});
  else if(k==='arrowleft') cmd('scene',{delta:-1});
  else if(k>='1'&&k<='6') cmd('scene',{index:+k-1});
});

if(J){ J.addEventListener('state',applyState); J.addEventListener('tick',applyTick);
       J.addEventListener('ytStatus',d=>{ if(!d) return; L.yt=d;
         const f=$('#lYtSearch'); if(document.activeElement!==f) f.placeholder=d.title?d.title:'Search YouTube or paste a link'; });
       J.addEventListener('confirmClose',()=>openCloseSheet());
       J.addEventListener('cover',coverStatus);
       J.addEventListener('audioDevices',()=>{ refreshAudio().then(a=>{ if(a&&!a.open) toast('The audio device is not available. Choose one in Settings › Audio.'); }); });
       cmd('refresh'); }
document.addEventListener('visibilitychange',ytSync);

// The instrument: the tuner's string names, and on a bass four or five strings.
(function(){
  renderTunerStrings();
  const row=$('#tuningRow'), name=$('#tuningName');
  const counts=Object.keys(PRODUCT.tunings).map(Number);
  name.textContent=PRODUCT.tuningName+' · '+strNames().join(' ');
  if(counts.length<2) return;
  row.querySelector('span').textContent='Strings';
  const seg=el('div','seg'); seg.setAttribute('role','radiogroup'); seg.setAttribute('aria-label','Strings');
  counts.forEach(k=>{ const b=el('button',k===L.strings?'on':'',k+' strings');
    b.onclick=()=>{ setStrings(k); [...seg.children].forEach(x=>x.classList.toggle('on',x===b)); name.textContent=PRODUCT.tuningName+' · '+strNames().join(' '); };
    seg.appendChild(b); });
  row.appendChild(seg);
})();
