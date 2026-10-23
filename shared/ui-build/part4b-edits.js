
/* ═════════════════════════════ UNSAVED EDITS ═════════════════════════════
   Edits are kept when the preset changes and are written to disk, so nothing is
   lost. They are saved deliberately: to the preset (every set that uses it), or
   to the open set only (that scene's own version), or discarded. */
const edited=id=>(S&&S.edits||[]).some(e=>e.preset===id);
const editLabel=e=>e.song?(e.songName+' · scene '+('ABCDEF'[e.scene]||String(e.scene+1))+(e.override?' · set version':'')):'Preset';
/** Save the live rig. With a set open, the target is chosen. */
function saveLive(anchor){
  const live=(S.edits||[]).find(e=>e.key===S.liveKey);
  const doSave=how=>cmd('resolveEdit',{key:S.liveKey,how}).then(ok=>toast(ok?(how==='set'?'Saved to '+live.songName+' only':'Saved to preset'):'Nothing to save'));
  if(!live){ toast('No changes to save'); return; }
  if(!live.song){ doSave('preset'); return; }
  const r=anchor?anchor.getBoundingClientRect():{left:innerWidth/2,bottom:80};
  openMenu(r.left,r.bottom+6,[
    {label:'Save to preset — every set that uses it', onClick:()=>doSave('preset')},
    {label:'Save to '+live.songName+' only', onClick:()=>doSave('set')},
    {label:'Discard changes', onClick:()=>cmd('resolveEdit',{key:S.liveKey,how:'discard'})}]);
}
/** Closing with edits pending: one row per edit, each with its own choice. */
function openCloseSheet(){
  const list=(S&&S.edits)||[];
  if(!list.length){ cmd('quit'); return; }
  const choice={};
  const body=el('div');
  body.appendChild(el('p','help','The following presets have changes that are not saved. Choose what to do with each, then close.'));
  const tbl=el('div'); tbl.style.marginTop='12px';
  list.forEach(e=>{
    choice[e.key]=e.override?'set':'preset';
    const row=el('div','kv'); row.style.minHeight='56px';
    const left=el('div'); left.innerHTML='<b style="font-family:var(--font-ui);font-weight:700">'+esc(e.name)+'</b><div class="cap" style="margin-top:3px">'+esc(editLabel(e))+'</div>';
    const seg=el('div','seg');
    const opts=[['preset','Save to preset']].concat(e.song?[['set','Save to set only']]:[]).concat([['discard','Discard']]);
    opts.forEach(([k,label])=>{ const b=el('button',k===choice[e.key]?'on':''); b.textContent=label;
      b.onclick=()=>{ choice[e.key]=k; [...seg.children].forEach(x=>x.classList.toggle('on',x===b)); }; seg.appendChild(b); });
    row.appendChild(left); row.appendChild(seg); tbl.appendChild(row);
  });
  body.appendChild(tbl);
  const run=async(all)=>{ closeSheet();
    for(const e of list) await cmd('resolveEdit',{key:e.key,how:all||choice[e.key]});
    cmd('quit'); };
  openSheet({title:'Unsaved changes', body, wide:true, actions:[
    {label:'Cancel', onClick:closeSheet},
    {label:'Discard all and close', kind:'danger', onClick:()=>run('discard')},
    {label:'Save and close', kind:'primary', onClick:()=>run(null)}]});
}
