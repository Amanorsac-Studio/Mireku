/** Web MIDI wrapper — thin, renderer-only. Engine logic never touches this
 * directly; it receives events through callbacks. */

export interface MidiDeviceInfo {
  id: string;
  name: string;
}

export interface MidiEvents {
  onNoteOn?(note: number, velocity: number): void;
  onNoteOff?(note: number): void;
  onControlChange?(cc: number, value: number): void;
}

/** Why `init()` failed, so a picker can explain itself rather than sit empty. */
export type MidiPermission = 'granted' | 'denied' | 'unsupported';

export class MidiManager {
  private access: MIDIAccess | null = null;
  private input: MIDIInput | null = null;
  private output: MIDIOutput | null = null;
  events: MidiEvents = {};

  permission: MidiPermission = 'unsupported';
  /**
   * `performance.now()` of the last message from the selected input, and the
   * last note number seen. The Settings panel's activity lamp reads these, so
   * it lights whether or not any screen is currently consuming MIDI events.
   */
  activityAt = 0;
  lastNote: number | null = null;
  /** fired when the OS adds or removes a MIDI port */
  onPortsChanged: (() => void) | null = null;

  async init(): Promise<boolean> {
    if (typeof navigator === 'undefined' || !('requestMIDIAccess' in navigator)) {
      this.permission = 'unsupported';
      return false;
    }
    if (this.access) return true;
    try {
      this.access = await navigator.requestMIDIAccess({ sysex: false });
      this.access.onstatechange = () => this.onPortsChanged?.();
      this.permission = 'granted';
      return true;
    } catch {
      this.permission = 'denied';
      return false;
    }
  }

  get ready(): boolean {
    return this.access !== null;
  }

  get selectedInputId(): string | null {
    return this.input?.id ?? null;
  }

  get selectedOutputId(): string | null {
    return this.output?.id ?? null;
  }

  listInputs(): MidiDeviceInfo[] {
    if (!this.access) return [];
    return [...this.access.inputs.values()].map((i) => ({ id: i.id, name: i.name ?? i.id }));
  }

  listOutputs(): MidiDeviceInfo[] {
    if (!this.access) return [];
    return [...this.access.outputs.values()].map((o) => ({ id: o.id, name: o.name ?? o.id }));
  }

  selectInput(id: string | null): void {
    if (this.input) this.input.onmidimessage = null;
    this.input = null;
    if (!id || !this.access) return;
    const input = this.access.inputs.get(id);
    if (!input) return;
    this.input = input;
    input.onmidimessage = (e: MIDIMessageEvent) => {
      const data = e.data;
      if (!data || data.length < 2) return;
      const status = data[0] & 0xf0;
      // activity is recorded before dispatch so the Settings lamp lights even
      // when no screen has installed handlers
      if (status !== 0xf0) this.activityAt = performance.now();
      if (status === 0x90 || status === 0x80) this.lastNote = data[1];
      if (status === 0x90 && data[2] > 0) this.events.onNoteOn?.(data[1], data[2]);
      else if (status === 0x80 || (status === 0x90 && data[2] === 0)) this.events.onNoteOff?.(data[1]);
      else if (status === 0xb0) this.events.onControlChange?.(data[1], data[2]);
    };
  }

  selectOutput(id: string | null): void {
    this.output = null;
    if (!id || !this.access) return;
    this.output = this.access.outputs.get(id) ?? null;
  }

  sendNoteOn(note: number, velocity: number): void {
    this.output?.send([0x90, note, velocity]);
  }

  sendNoteOff(note: number): void {
    this.output?.send([0x80, note, 0]);
  }

  /**
   * Raw MIDI bytes out of the selected output. The patch queue's Bank Select
   * (CC0 / CC32) and Program Change go through here; keeping it generic means
   * no per-message method has to be added for every future control.
   */
  send(bytes: number[]): void {
    this.output?.send(bytes);
  }
}

export const midi = new MidiManager();
