import React from 'react';
import { createRoot } from 'react-dom/client';
import { App } from './ui/App';
import { applyTheme, weave } from './ui/theme/tokens';
import { microWeaveBackground, weaveDataUrl } from './ui/theme/weave';
import './ui/styles.css';

applyTheme();
// Chassis and panels wear the pack's pure-vector `kente-micro-weave.svg` at low
// opacity (Signature puts it on its browser/dock/rack chassis at 10–12 %),
// layered over the procedural cloth so the surface still reads as material.
document.documentElement.style.setProperty(
  '--as-weave-chassis',
  `${microWeaveBackground(weave.microChassisAlpha)}, ${weaveDataUrl(undefined, weave.chassisAlpha)}`,
);
document.documentElement.style.setProperty(
  '--as-weave-panel',
  `${microWeaveBackground(weave.microPanelAlpha)}, ${weaveDataUrl(undefined, weave.panelAlpha)}`,
);

createRoot(document.getElementById('root')!).render(
  <React.StrictMode>
    <App />
  </React.StrictMode>,
);
