import React from 'react';
import { createRoot } from 'react-dom/client';
import { App } from './ui/App';
import { applyTheme, weave } from './ui/theme/tokens';
import { weaveDataUrl } from './ui/theme/weave';
import './ui/styles.css';

applyTheme();
// woven backdrops: faint on the chassis, denser warp on recessed panels
document.documentElement.style.setProperty('--jm-weave-chassis', weaveDataUrl(undefined, weave.chassisAlpha));
document.documentElement.style.setProperty('--jm-weave-panel', weaveDataUrl(undefined, weave.panelAlpha));

createRoot(document.getElementById('root')!).render(
  <React.StrictMode>
    <App />
  </React.StrictMode>,
);
