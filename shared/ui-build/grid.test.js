// The song timing grid (gridOf in part5-learn.js): free intros, tempo
// changes, trims, and tempos typed in older versions.   node grid.test.js
const fs = require('fs'), path = require('path');
const src = fs.readFileSync(path.join(__dirname, 'part5-learn.js'), 'utf8');
const a = src.indexOf('function gridOf('), b = src.indexOf('function grid(){');
eval(src.slice(a, b).replace('function gridOf', 'globalThis.gridOf=function').replace('function tempoAt', 'globalThis.tempoAt=function'));
let fails = 0;
const ok = (c, m) => { console.log((c ? 'ok  ' : 'FAIL') + ' ' + m); if (!c) fails++; };

let g = gridOf({ duration: 60, timing: { start: 2, end: 58, sections: [{ t: 0, bpm: 0 }, { t: 12.4, bpm: 96 }] } });
ok(g.beats[0] === 12.4 && g.inBar[0] === 0, 'free intro: the first beat is the "1" where the tempo starts');
ok(g.beats.every(t => t >= 12.4), 'no beats inside the free intro');
ok(Math.abs(g.beats[4] - (12.4 + 4 * 0.625)) < 1e-3 && g.inBar[4] === 0, 'bar 2 starts four beats later');
ok(g.start === 2 && g.end === 58, 'start and end trims come through');
ok(tempoAt(g, 5) === 0 && tempoAt(g, 20) === 96, 'tempo at a time: free, then 96');

g = gridOf({ duration: 60, timing: { sections: [{ t: 1, bpm: 90 }, { t: 30, bpm: 120 }] } });
const i30 = g.beats.findIndex(t => Math.abs(t - 30) < 1e-6);
ok(g.beats[0] < 1 && g.beats[0] >= 0, 'the first section reaches back to the start (pickup)');
ok(i30 > 0 && g.inBar[i30] === 0, 'a tempo change starts on a "1"');
ok(Math.abs(g.beats[i30 + 1] - 30.5) < 1e-6, 'after it, 120 BPM');
ok(g.beats.filter(t => t >= 1 && t < 30).every((t, k, arr) => k === 0 || Math.abs(t - arr[k - 1] - 60 / 90) < 1e-3), 'before it, 90 BPM');

g = gridOf({ duration: 20, userGrid: { bpm: 100, t0: 0.6 } });
ok(g.user && g.tempo === 100 && g.inBar[g.beats.findIndex(t => Math.abs(t - 0.6) < 1e-6)] === 0, 'a tempo typed in an older version still applies');

console.log(fails ? fails + ' FAILED' : 'OK');
process.exitCode = fails ? 1 : 0;
