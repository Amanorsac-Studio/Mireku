
/*──────────────── audio device ────────────────
   The panel is the app's own, not a native dialog. The engine only ever hands
   us plain data (see TopEndProcessor::AudioHooks); in a plug-in it reports
   nothing is available, because there the host owns the device. */
const AUD={info:null, busy:false};
const fill=(sel,items,current,label)=>{
  sel.innerHTML='';
  (items||[]).forEach(it=>{
    const value=(typeof it==='object')?it.name:it;
    const o=el('option'); o.textContent=label?label(it):String(value);
    o.value=value; if(value===current) o.selected=true;
    sel.appendChild(o);
  });
  sel.disabled=!(items&&items.length);
};
function chips(host,items,current,label,onPick){
  host.innerHTML='';
  (items||[]).forEach(v=>{ const b=el('button',v===current?'on':''); b.textContent=label(v); b.onclick=()=>onPick(v); host.appendChild(b); });
}
function renderAudio(){
  const a=AUD.info;
  if(!a||!a.available){
    $('#audioPanel').style.display='none';
    $('#btnAudLowest').hidden=true; $('#btnAudPanel').hidden=true;
    $('#latMs').textContent='Host'; $('#latDrv').textContent='AUDIO'; $('#snavLat').textContent='Host';
    $('#audNote').textContent='Your DAW manages the audio device for the plug-in.';
    return;
  }
  $('#audioPanel').style.display='';
  // The device: every card on every driver, one list.
  const dv=$('#audDev'); dv.innerHTML='';
  (a.devices||[]).forEach(d=>{ const o=el('option'); o.textContent=d.label+(d.wrapper?'  (generic)':''); o.value=d.type+'|'+d.name;
    if(d.type===a.driver&&d.name===a.output) o.selected=true; dv.appendChild(o); });
  dv.disabled=!(a.devices&&a.devices.length);
  $('#audWrap').hidden=!a.wrapper;
  $('#audInDevRow').hidden=!a.separateInput;
  if(a.separateInput) fill($('#audInDev'),a.inputDevices||a.inputs,a.input);
  // The card's own channels.
  const ic=$('#audInCh'); ic.innerHTML='';
  const ins=a.inputChannels||[];
  ins.forEach((n,i)=>{ const o=el('option'); o.textContent='Input '+(i+1)+' · '+n; o.value=i+'|0'; if(i===a.inCh&&!a.inStereo) o.selected=true; ic.appendChild(o); });
  for(let i=0;i+1<ins.length;i+=2){ const o=el('option'); o.textContent='Inputs '+(i+1)+'–'+(i+2)+' · stereo'; o.value=i+'|1'; if(i===a.inCh&&a.inStereo) o.selected=true; ic.appendChild(o); }
  ic.disabled=!ins.length;
  const oc=$('#audOutCh'); oc.innerHTML='';
  const outs=a.outputChannels||[];
  for(let i=0;i+1<outs.length;i+=2){ const o=el('option'); o.textContent='Outputs '+(i+1)+'–'+(i+2)+' · stereo'; o.value=i+'|0'; if(i===a.outCh&&!a.outMono) o.selected=true; oc.appendChild(o); }
  outs.forEach((n,i)=>{ const o=el('option'); o.textContent='Output '+(i+1)+' · mono · '+n; o.value=i+'|1'; if(i===a.outCh&&a.outMono) o.selected=true; oc.appendChild(o); });
  oc.disabled=!outs.length;
  chips($('#audRateChips'),(a.rates||[]).filter(r=>[44100,48000,88200,96000].includes(r)||r===a.rate),a.rate,r=>(r/1000).toFixed(r%1000?1:0)+' kHz',r=>applyAudio({rate:r}));
  chips($('#audBufChips'),a.buffers,a.buffer,b=>String(b),b=>applyAudio({buffer:b}));
  $('#btnAudPanel').hidden=!a.hasDriverPanel;
  $('#audDrvCap').textContent=a.driver||'';
  const short=String(a.driver||'').replace('Windows Audio','WASAPI').replace(' (Exclusive Mode)',' excl').replace(' (Low Latency Mode)',' low').toUpperCase();
  $('#latDrv').textContent=short.slice(0,12)||'AUDIO';
  if(a.open&&a.rate){
    const ms=x=>1000*x/a.rate;
    const inMs=ms(a.buffer+(a.inputLatency||0)), outMs=ms(a.buffer+(a.outputLatency||0)), dspMs=ms(S&&S.latency||0);
    const rt=inMs+outMs+dspMs;
    $('#audBig').innerHTML=rt.toFixed(1)+'<small>ms</small>';
    $('#latMs').textContent=rt.toFixed(1)+' ms'; $('#snavLat').textContent=rt.toFixed(1)+' ms';
    const feel=$('#audFeel'); feel.classList.toggle('warn',rt>12);
    feel.querySelector('span').textContent=rt<=6?'Feels like standing next to the amp':rt<=12?'Fine for most playing':'You may feel the delay — try a smaller buffer';
    const sp=$('#audSplit'); sp.innerHTML='';
    [[inMs,'var(--d5)'],[Math.max(0.15,dspMs),'var(--amber)'],[outMs,'var(--d2)']].forEach(([v,c])=>{ const i=el('i'); i.style.flex=String(v); i.style.background=c; sp.appendChild(i); });
    $('#audSplitK').innerHTML='<span>IN <b>'+inMs.toFixed(2)+'</b></span><span>TOP END <b>'+dspMs.toFixed(2)+'</b></span><span>OUT <b>'+outMs.toFixed(2)+'</b></span>';
    $('#audCpu').textContent=a.cpu!=null?Math.round(a.cpu*100)+'%':'—';
    $('#audXr').textContent=a.xruns!=null&&a.xruns>=0?String(a.xruns):'Not reported';
    $('#audNote').textContent='';
  }else{
    $('#audBig').innerHTML='—<small>ms</small>'; $('#latMs').textContent='—';
    $('#audNote').textContent=a.error||'No audio device is open.';
  }
}
function refreshAudio(){
  return cmd('audio',{action:'describe'}).then(info=>{ AUD.info=info; renderAudio(); clkKey=''; return info; });
}
function applyAudio(patch){
  if(AUD.busy) return;
  AUD.busy=true;
  // Changing a driver can close and reopen the device, so everything is read
  // back from the engine afterwards rather than assumed.
  cmd('audio',{action:'apply',...patch})
    .then(ok=>{ if(!ok) toast('The device would not take that setting. Choose another.'); })
    .then(refreshAudio)
    .finally(()=>{ AUD.busy=false; });
}
$('#audDev').onchange=e=>{ const [type,...rest]=e.target.value.split('|'); applyAudio({type,device:rest.join('|')}); };
$('#audInDev').onchange=e=>applyAudio({inputDevice:e.target.value});
$('#audInCh').onchange=e=>{ const [c,st]=e.target.value.split('|'); applyAudio({inCh:+c,inStereo:st==='1'}); };
$('#audOutCh').onchange=e=>{ const [c,m]=e.target.value.split('|'); applyAudio({outCh:+c,outMono:m==='1'}); };
$('#gateAutoSw').onclick=()=>cmd('gateAuto',{on:!(S&&S.gateAuto)});
document.querySelectorAll('#humSeg button').forEach(b=>b.onclick=()=>cmd('hum',{hz:+b.dataset.hz}));
$('#btnAudLowest').onclick=()=>cmd('audio',{action:'lowest'}).then(d=>{ refreshAudio(); toast(d&&typeof d==='string'?d:'No low-latency driver found'); });
$('#btnAudPanel').onclick=()=>cmd('audio',{action:'panel'}).then(ok=>{ if(!ok) toast('This driver has no panel'); setTimeout(refreshAudio,600); });
// While Settings > Audio is open, the load and dropout figures stay live.
setInterval(()=>{ if(view==='settings'&&spanel==='audio'&&!AUD.busy&&J) refreshAudio(); },2000);
