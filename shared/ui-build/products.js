// The two apps built from one set of UI parts. Everything that is the
// instrument or the brand lives here; the parts read PRODUCT at run time.
const WEAVE = "(a,b)=>'repeating-linear-gradient(45deg,'+a+' 0 1.5px,'+b+' 1.5px 3px)'";

const top = {
  ui: 'C:/Amanorsac Studio/products/sampler/apps/topend/resources/ui/',
  prefix: 'te',
  inst: 'guitar', strings: 6,
  js: `const PRODUCT={ name:'Top End', part:'guitar', strings:6,
  tunings:{6:[40,45,50,55,59,64]}, tuningName:'Standard E', gauge:1,
  mineStem:2, stemNote:'Guitar and keys share a stem. Mute it to play your part.',
  shapes:true, myNotes:n=>n.role!=='bass',
  ampKnobs:['gain','bass','mid','treble','presence','master'], ampSwitches:['bright','tight'],
  cabIr:3, cabDi:-1, rigExtras:false,
  speakers:{0:{c:[['50%','52%']],r:96},1:{c:[['30%','50%'],['70%','50%']],r:96},2:{c:[['30%','30%'],['70%','30%'],['30%','74%'],['70%','74%']],r:70}} };`,
  text: [], colours: [], css: '',
};

const low = {
  ui: 'C:/Amanorsac Studio/products/sampler/apps/lowend/resources/ui/',
  prefix: 'le',
  inst: 'bass', strings: 4,
  js: `const PRODUCT=(()=>{ const W=${WEAVE}; return { name:'Low End', part:'bass', strings:4,
  tunings:{4:[28,33,38,43],5:[23,28,33,38,43]}, tuningName:'Standard', gauge:1.6,
  mineStem:1, stemNote:'The bass has a stem of its own. Mute it to play your part.',
  shapes:false, myNotes:n=>n.role==='bass',
  ampKnobs:['gain','bass','mid','midFreq','treble','master'], ampSwitches:['bright','deep','ultraLo','ultraHi'],
  cabIr:5, cabDi:4, rigExtras:true,
  speakers:{0:{c:[['50%','50%']],r:124},1:{c:[['30%','50%'],['70%','50%']],r:84},
            2:{c:[['30%','30%'],['70%','30%'],['30%','74%'],['70%','74%']],r:64},
            3:{c:[['30%','14%'],['70%','14%'],['30%','38%'],['70%','38%'],['30%','62%'],['70%','62%'],['30%','86%'],['70%','86%']],r:40}},
  amps:[
    {name:'Studio Clean', sub:'SOLID STATE · CLEAN',  p:'linear-gradient(#e6e9ef,#c9ced8)', pp:'#8a93a3', k:'#15171b', g:W('#3a3f48','#22262d'), ink:'#1a1d22', logo:'#15171b', subc:'#5a6272', onbg:'#1a1d22', onfg:'#e6e9ef'},
    {name:'Classic Tube', sub:'6L6 · TUBE HEAD',       p:'linear-gradient(#3a2a1c,#24190f)', pp:'#a07a45', k:'#f2e3c4', g:W('#5a3d25','#3a2617'), ink:'#f2e3c4', logo:'#f5d08a', subc:'#c9ab78', onbg:'#f2e3c4', onfg:'#24190f'},
    {name:'Flip Top',     sub:'TUBE COMBO · ROUND',    p:'linear-gradient(#d7dce4,#b9c0cc)', pp:'#4a6a9a', k:'#18202c', g:W('#2a3a55','#1b283b'), ink:'#18202c', logo:'#1b283b', subc:'#4a5a72', onbg:'#18202c', onfg:'#d7dce4'},
    {name:'Monolith',     sub:'MODERN STACK · 300 W',  p:'linear-gradient(#2a2d33,#17191d)', pp:'#9aa0aa', k:'#d9dde5', g:W('#2a2a2a','#151515'), ink:'#d9dde5', logo:'#f43f5e', subc:'#9aa0aa', onbg:'#d9dde5', onfg:'#17191d'}],
  cabs:[{name:'1×15',sub:'Round, deep'},{name:'2×10',sub:'Punchy mids'},{name:'4×10',sub:'The stage cab'},
        {name:'8×10',sub:'Big and slow'},{name:'No cab',sub:'The amp, no speaker'},{name:'Your IR',sub:'An impulse file'}] }; })();`,
  // Brand and instrument wording. Applied to the whole page, in order.
  text: [
    ['Guitar amp, pedalboard, looper and music tutor.', 'Bass amp, pedalboard, looper and music tutor.'],
    ['Separate it to mute the guitar and play along.', 'Separate it to mute the bass and play along.'],
    ['Mute guitar', 'Mute bass'],
    ['Your guitar as a bowed string: every note swells in like a bow, holds, and takes a player’s vibrato. Violin, viola or cello.',
     'Your bass as a bowed string: every note swells in like a bow, holds, and takes a player’s vibrato. Cello, double bass or viola.'],
    ["an expression pedal goes on the Wah's Position", 'an expression pedal can drive any knob'],
    ['TOP END', 'LOW END'], ['Top End', 'Low End'], ['topend', 'lowend'],
    ["'te.cover'", "'le.cover'"],
    // the mark: Top End's arch, turned over
    ['<path d="M2 11 Q8 1 14 11"', '<path d="M2 5 Q8 15 14 5"'],
    ['<circle cx="2" cy="11" r="1.6"', '<circle cx="2" cy="5" r="1.6"'],
    ['<circle cx="14" cy="11" r="1.6"', '<circle cx="14" cy="5" r="1.6"'],
  ],
  // Warm greys and amber to Low End's cool greys and blue. Pedal paint and
  // amp faceplates are artwork and keep their own colours.
  colours: [
    ['#e3a447', '#3B82F6'], ['rgba(227,164,71', 'rgba(59,130,246'], ['#b8792a', '#2563EB'],
    ['#f2ece4', '#ECEEF3'], ['#867c70', '#808AA0'], ['#a89e91', '#9098AC'], ['#2c2722', '#232937'],
    ['#d8cfc3', '#C9D0DE'], ['#5d554b', '#4A5266'], ['#131110', '#0A0C11'], ['#0f0d0c', '#07090D'],
    ['#4a4138', '#3A4152'], ['#8a7f72', '#7D8699'], ['#6f665c', '#5E677A'], ['#1d1915', '#10131A'],
    ['#2f2924', '#1C2130'], ['#e9e1d6', '#DDE2EA'], ['#1d1812', '#0E1420'], ['#2a2521', '#1B202B'],
    ['#5a5047', '#465066'], ['#3a332c', '#2C3344'], ['#1a1714', '#11141B'], ['#221e1a', '#171B24'], ['#2a251f', '#1E2330'],
  ],
  css: `
/* Low End: the same layout in its own cool palette, blue accent. */
:root{
  --bg:#0A0C11; --s1:#11141B; --s2:#171B24; --s3:#1E2330; --well:#07090D;
  --line:#232937; --line2:#2C3344;
  --ink:#ECEEF3; --muted:#9098AC; --faint:#808AA0;
  --amber:#3B82F6; --amber-deep:#2563EB; --amber-ink:#F5F8FF; --amber-soft:rgba(59,130,246,.16);
  --good:#34D399; --warn:#F5A524; --bad:#F43F5E;
}
/* Low End's Rig page: the amp first, the board below it, and the looper at
   the top of the controls (mock B, approved 2026-10-22). Portrait keeps the
   shared stacked layout. */
.rigL{grid-template-rows:minmax(0,1fr) auto}
.rigL .amp{order:1} .rigL .board{order:2}
.rigL>.amp,.rigL>.pbrowse{grid-row:1;grid-column:1} .rigL>.board{grid-row:2;grid-column:1}
.rigR .looper{order:1} .rigR .scenes{order:2} .rigR .card:last-child{order:3}
@media (min-width:901px) and (orientation:landscape){ .rigR{grid-template-rows:auto minmax(0,1fr) auto} }
`,
};

module.exports = { top, low };
