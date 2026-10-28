// Phase 6: bring Low End's engine and plug-in shell up to Top End.
// Shared files are copied with the product renamed; instrument files
// (amp, cab, block params, pedals, pitch range, tuner strings, factory) stay.
const fs = require('fs'), path = require('path');
const APPS = 'C:/Amanorsac Studio/products/sampler/apps';
const TOP = APPS + '/topend', LOW = APPS + '/lowend';
const BACKUP = path.join(__dirname, 'lowend-backup-phase6');

if (!fs.existsSync(BACKUP)) {
  fs.cpSync(LOW + '/src', BACKUP + '/src', { recursive: true });
  fs.cpSync(LOW + '/tests', BACKUP + '/tests', { recursive: true });
  fs.copyFileSync(LOW + '/CMakeLists.txt', BACKUP + '/CMakeLists.txt');
  console.log('backup written:', BACKUP);
}

const rename = s => s
  .replace(/TopEnd/g, 'LowEnd').replace(/topendWindowBounds/g, 'lowEndWindowBounds')
  .replace(/topend/g, 'lowend').replace(/TOPEND/g, 'LOWEND')
  .replace(/Top End/g, 'Low End').replace(/TOP END/g, 'LOW END').replace(/TpEd/g, 'LwEd');

function copy(rel, fix) {
  const from = TOP + '/' + rel, to = LOW + '/' + rename(rel);
  let s = rename(fs.readFileSync(from, 'utf8'));
  if (fix) s = fix(s);
  fs.writeFileSync(to, s);
  console.log('copied', rename(rel));
}
function patch(rel, pairs) {
  const f = LOW + '/' + rel; let s = fs.readFileSync(f, 'utf8');
  for (const [a, b] of pairs) {
    if (!s.includes(a)) throw new Error(rel + ': anchor missing: ' + a.slice(0, 80));
    s = s.replace(a, b);
  }
  fs.writeFileSync(f, s);
  console.log('patched', rel);
}
const between = (s, a, b) => { const i = s.indexOf(a); const j = s.indexOf(b, i); if (i < 0 || j < 0) throw new Error('slice ' + a.slice(0, 40)); return s.slice(i, j); };

//------------------------------------------------------------ shared files
for (const f of ['src/plugin/TopEndProcessor.cpp', 'src/plugin/TopEndProcessor.h', 'src/plugin/TopEndEditor.cpp',
                 'src/plugin/TopEndEditor.h', 'src/plugin/StandaloneApp.cpp',
                 'src/engine/Looper.h', 'src/engine/Metronome.h', 'src/engine/PresetPacks.h',
                 'src/engine/NamModel.h', 'src/engine/nam/NamBridge.h', 'src/engine/nam/NamBridge.cpp'])
  copy(f);
copy('src/engine/CoverPlayer.h', s => s.split('" - Guitar').join('" - Bass'));
copy('tests/ProcessorTest.cpp', s => s
  .replace(/BlockType::clearDrive/g, 'BlockType::bassOd')
  .replace('const double f0 = 110.0;', 'const double f0 = 55.0;          // A1, a bass note'));
// The Learn tab's player and analyser have no instrument in them.
copy('resources/ui/stemplayer.js');
copy('resources/ui/analyzer.js');
copy('tests/learn/analyzer.test.js');
copy('tests/learn/stemplayer.test.js');

//------------------------------------------------------------ RigModel merge
const top = rename(fs.readFileSync(TOP + '/src/engine/RigModel.h', 'utf8'));
{
  const f = LOW + '/src/engine/RigModel.h'; let s = fs.readFileSync(f, 'utf8');
  const histStruct = between(top, '\n    /// What changed at each save, newest first', '\n};') ;
  const rigEnd = s.indexOf('\n};', s.indexOf('struct Rig'));
  if (!s.includes('struct Change')) s = s.slice(0, rigEnd) + '\n' + histStruct + s.slice(rigEnd);
  if (!s.includes('kRigHistory')) {
    const at = s.indexOf('\n', s.indexOf('\n};', s.indexOf('struct Rig')) + 1);
    s = s.slice(0, at) + 'constexpr int kRigHistory = 12;\n' + s.slice(at);
  }
  // overrides on the song
  const ovDecl = between(top, '    /// "Saved to this set only"', '\n    std::map<int, Rig> overrides;') + '\n    std::map<int, Rig> overrides;\n';
  if (!s.includes('std::map<int, Rig> overrides;')) {
    const songAt = s.indexOf('struct Song');
    const end = s.indexOf('\n};', songAt);
    s = s.slice(0, end) + '\n' + ovDecl.replace(/\n$/, '') + s.slice(end);
  }
  // scene footswitch roles
  s = s.replace('loopRecord, loopStop, loopUndo, loopClear, panic, bypassAll, Count',
    'loopRecord, loopStop, loopUndo, loopClear, panic,\n    // One switch per scene of the set (A-F), the way a floor unit is laid out.\n    sceneA, sceneB, sceneC, sceneD, sceneE, sceneF, bypassAll, Count');
  s = s.replace('"loopRecord", "loopStop", "loopUndo", "loopClear", "panic", "bypassAll" };',
    '"loopRecord", "loopStop", "loopUndo", "loopClear", "panic",\n                                 "sceneA", "sceneB", "sceneC", "sceneD", "sceneE", "sceneF", "bypassAll" };');
  // history (de)serialisation
  const hOut = between(top, '    juce::Array<juce::var> h;\n    for (auto& c : r.history)', '    set (o, "history", h);') + '    set (o, "history", h);\n';
  const hIn = between(top, '    if (auto* h = v.getProperty ("history", {}).getArray())', '\n', ) ;
  const hInFull = top.slice(top.indexOf('    if (auto* h = v.getProperty ("history", {}).getArray())'),
                            top.indexOf('r.history.push_back', top.indexOf('getProperty ("history"'))) ;
  const hInEnd = top.indexOf('\n', top.indexOf('r.history.push_back', top.indexOf('getProperty ("history"'))) + 1;
  const hInBlock = top.slice(top.indexOf('    if (auto* h = v.getProperty ("history", {}).getArray())'), hInEnd);
  const desc = between(top, '/// A one-line, human description of what differs', '/// Puts a change at the top') +
               between(top, '/// Puts a change at the top', '\n}\n') + '\n}\n\n';
  // Positions in Low End's own file: the 'return' that ends each function.
  const endOf = (fn, ret) => { const i = s.indexOf(fn); if (i < 0) throw new Error('missing ' + fn); const j = s.indexOf(ret, i); if (j < 0) throw new Error('no end of ' + fn); return j; };
  if (!s.includes('set (o, "history", h);')) { const i = endOf('inline juce::var rigToVar', '\n    return o;') + 1; s = s.slice(0, i) + hOut + s.slice(i); }
  if (!s.includes('getProperty ("history"')) { const i = endOf('inline Rig rigFromVar', '\n    return r;') + 1; s = s.slice(0, i) + hInBlock + s.slice(i); }
  if (!s.includes('describeRigChange')) { const i = s.indexOf('inline juce::var songToVar'); s = s.slice(0, i) + desc + s.slice(i); }
  const ovOut = between(top, '    auto ov = detail::obj();', '    set (o, "overrides", ov);') + '    set (o, "overrides", ov);\n';
  const ovInStart = top.indexOf('    if (auto* ov = v.getProperty ("overrides"');
  const ovIn = top.slice(ovInStart, top.indexOf('rigFromVar (p.value);', ovInStart) + 'rigFromVar (p.value);\n'.length);
  if (!s.includes('"overrides"')) {
    let i = endOf('inline juce::var songToVar', '\n    return o;') + 1; s = s.slice(0, i) + ovOut + s.slice(i);
    i = endOf('inline Song songFromVar', '\n    return s;') + 1; s = s.slice(0, i) + ovIn + s.slice(i);
  }
  const same = between(top, '/// Two rigs SOUND the same', '\n}\n') + '\n}\n\n';
  if (!s.includes('sameSound')) { const i = s.indexOf('inline juce::var setlistToVar'); s = s.slice(0, i) + same + s.slice(i); }
  fs.writeFileSync(f, s);
  console.log('merged RigModel.h');
}
