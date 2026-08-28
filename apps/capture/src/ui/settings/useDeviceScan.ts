/**
 * One device-discovery hook shared by the Settings panel and the New Session
 * wizard, so both pickers behave identically: they scan on mount, they re-scan
 * by themselves when the OS reports a hot-plug, they can be re-scanned by
 * hand, and they always report *why* a list is empty instead of rendering a
 * silent empty dropdown.
 */

import { useCallback, useEffect, useState } from 'react';
import { audioIo, type AudioDeviceInfo, type AudioPermission } from '../../engine/audioIo';
import { midi, type MidiDeviceInfo, type MidiPermission } from '../../engine/midi';

/** Windows fires a burst of devicechange events; coalesce them. */
const HOTPLUG_DEBOUNCE_MS = 400;

export interface DeviceScan {
  audioInputs: AudioDeviceInfo[];
  audioPermission: AudioPermission;
  midiInputs: MidiDeviceInfo[];
  midiOutputs: MidiDeviceInfo[];
  midiPermission: MidiPermission;
  /** true until the first scan resolves */
  scanning: boolean;
  rescan(): void;
}

export function useDeviceScan(active = true): DeviceScan {
  const [audioInputs, setAudioInputs] = useState<AudioDeviceInfo[]>([]);
  const [audioPermission, setAudioPermission] = useState<AudioPermission>('granted');
  const [midiInputs, setMidiInputs] = useState<MidiDeviceInfo[]>([]);
  const [midiOutputs, setMidiOutputs] = useState<MidiDeviceInfo[]>([]);
  const [midiPermission, setMidiPermission] = useState<MidiPermission>('granted');
  const [scanning, setScanning] = useState(true);
  const [nonce, setNonce] = useState(0);

  const rescan = useCallback(() => setNonce((n) => n + 1), []);

  useEffect(() => {
    if (!active) return;
    let live = true;
    setScanning(true);
    void (async () => {
      const scan = await audioIo.scanInputs();
      if (!live) return;
      setAudioInputs(scan.devices);
      setAudioPermission(scan.permission);

      await midi.init();
      if (!live) return;
      setMidiPermission(midi.permission);
      setMidiInputs(midi.listInputs());
      setMidiOutputs(midi.listOutputs());
      setScanning(false);
    })();
    return () => {
      live = false;
    };
  }, [active, nonce]);

  // OS hot-plug: an interface switched on or a keyboard was unplugged.
  // Debounced, because Windows emits a burst of `devicechange` events when an
  // interface appears (and one more whenever a stream is opened).
  useEffect(() => {
    if (!active) return;
    let timer: ReturnType<typeof setTimeout> | undefined;
    const bounced = () => {
      clearTimeout(timer);
      timer = setTimeout(rescan, HOTPLUG_DEBOUNCE_MS);
    };
    const offAudio = audioIo.onDeviceChange(bounced);
    const previous = midi.onPortsChanged;
    midi.onPortsChanged = () => {
      previous?.();
      bounced();
    };
    return () => {
      clearTimeout(timer);
      offAudio();
      midi.onPortsChanged = previous;
    };
  }, [active, rescan]);

  return { audioInputs, audioPermission, midiInputs, midiOutputs, midiPermission, scanning, rescan };
}
