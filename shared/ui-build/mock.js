// Layout mocks for Low End's Rig page: the built page plus a CSS variant,
// screenshotted by the harness.   node mock.js
const fs = require('fs'), path = require('path'), { execSync } = require('child_process');
const SRC = 'C:/Amanorsac Studio/products/sampler/apps/lowend/resources/ui';
const VARIANTS = {
  'A-mirror': `
/* A: the rig on the right, the controls on the left */
#view-rig.on{grid-template-columns:minmax(290px,1fr) minmax(0,1.62fr)}
.rigL{order:2} .rigR{order:1}`,
  'B-amp-first': `
/* B: the amp first and the board below it; the looper at the top of the controls */
.rigL{grid-template-rows:minmax(0,1fr) auto}
.rigL .amp{order:1} .rigL .board{order:2}
.rigR{grid-template-rows:auto minmax(0,1fr) auto}
.rigR .looper{order:1} .rigR .scenes{order:2} .rigR .card:last-child{order:3}`,
  'C-wide-board': `
/* C: the board across the top; the amp and the controls side by side below */
#view-rig.on{grid-template-columns:minmax(0,1.62fr) minmax(290px,1fr);grid-template-rows:auto minmax(0,1fr)}
.rigL{display:contents}
.rigL .board{grid-column:1/3;grid-row:1}
.rigL .amp{grid-column:1;grid-row:2}
.rigR{grid-column:2;grid-row:2;grid-template-rows:auto minmax(0,1fr) auto}
.rigR .looper{order:1} .rigR .scenes{order:2} .rigR .card:last-child{order:3}`,
};
for (const [name, css] of Object.entries(VARIANTS)) {
  const dir = path.join(__dirname, 'mock', name);
  fs.rmSync(dir, { recursive: true, force: true });
  fs.cpSync(SRC, dir, { recursive: true });
  let html = fs.readFileSync(path.join(dir, 'index.html'), 'utf8');
  const i = html.lastIndexOf('</style>', html.indexOf('</head>'));
  html = html.slice(0, i) + css + '\n' + html.slice(i);
  fs.writeFileSync(path.join(dir, 'index.html'), html);
  execSync('node harness.js mock-shots/' + name + ' low', { cwd: __dirname, env: { ...process.env, UI_DIR: dir, MOCK: '1' }, stdio: 'inherit' });
}
