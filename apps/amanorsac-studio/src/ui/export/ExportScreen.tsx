import { useState } from 'react';
import { sessionStats } from '../../engine/session';
import { useApp } from '../../state/appStore';
import { bridge, services } from '../../state/services';
import { Button, Panel } from '../common';
import { GoldDivider } from '../hardware';

export function ExportScreen() {
  const session = useApp((s) => s.session);
  const presets = useApp((s) => s.presets);
  const result = useApp((s) => s.exportResult);
  const setResult = useApp((s) => s.setExportResult);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);

  if (!session) return null;
  const stats = sessionStats(session);

  const doExport = async () => {
    setError(null);
    const dir = await bridge.chooseDir();
    if (!dir) return;
    setBusy(true);
    try {
      const r = await services.exportSession(session, dir, presets.length ? presets : services.defaultPresetsFor(session));
      setResult(r);
    } catch (e) {
      setError((e as Error).message);
    } finally {
      setBusy(false);
    }
  };

  return (
    <div className="as-col" style={{ maxWidth: '760px', margin: '0 auto' }}>
      <Panel screws>
        <div className="as-col">
          <div className="as-title">Export Instrument</div>
          <GoldDivider />
          <div className="as-plaque as-value">
            {session.config.instrumentName} · {stats.done} of {stats.total} takes captured ({stats.percent}%)
          </div>
          {stats.done < stats.total && (
            <div className="as-warning">Session incomplete — export will skip missing takes and flag them.</div>
          )}
          <div className="as-row">
            <Button primary onClick={() => void doExport()} disabled={busy || stats.done === 0}>
              {busy ? 'Exporting…' : 'Export .jmi Bundle + Zip'}
            </Button>
          </div>
          {error && <div className="as-warning">{error}</div>}
        </div>
      </Panel>
      {result && (
        <Panel>
          <div className="as-col">
            <div className="as-title">
              {result.valid ? 'Export complete — bundle valid' : 'Export finished with validation errors'}
            </div>
            <GoldDivider />
            <div className="as-value">{result.bundlePath}</div>
            <div className="as-value">{result.zipPath}</div>
            {result.aofasPath && <div className="as-value">Sealed to library: {result.aofasPath}</div>}
            {result.aofasError && (
              <div className="as-warning">
                The bundle exported, but sealing it into the library failed: {result.aofasError}
              </div>
            )}
            <div className="as-value">
              {result.stats.sampleCount} samples · {result.stats.presetCount} presets ·{' '}
              {(result.stats.totalBytes / (1024 * 1024)).toFixed(1)} MB
            </div>
            {result.issues.length > 0 && (
              <div className="as-col" style={{ gap: 'calc(var(--as-size-gap-unit) * 0.5)' }}>
                {result.issues.slice(0, 20).map((i, k) => (
                  <div key={k} className="as-label">
                    [{i.level}] {i.path}: {i.message}
                  </div>
                ))}
                {result.issues.length > 20 && <div className="as-label">… {result.issues.length - 20} more</div>}
              </div>
            )}
            <div className="as-row">
              <Button onClick={() => void bridge.showItem(result.bundlePath)}>Show in Folder</Button>
            </div>
          </div>
        </Panel>
      )}
    </div>
  );
}
