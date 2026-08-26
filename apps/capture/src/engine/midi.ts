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

export class MidiManager {
  private access: MIDIAccess | null = null;
  private input: MIDIInput | null = null;
  private output: MIDIOutput | null = null;
  events: MidiEvents = {};

  async init(): Promise<boolean> {
    if (!('requestMIDIAccess' in navigator)) return false;
    try {
      this.access = await navigator.requestMIDIAccess({ sysex: false });
      return true;
    } catch {
      return false;
    }
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
}

export const midi = new MidiManager();
