// AudioWorklet for the Family mruby wasm build (doc/wasm/ P4c).
//
// The APU task writes mono int16 samples at 15720 Hz into a ring in the
// module's shared memory; this processor trails the write counter and
// linearly interpolates up to the context rate. The write counter itself is
// forwarded over the port (60 Hz) because the page reads it via an exported
// getter; a second of ring makes that cadence irrelevant.
'use strict';

class FmrbApuProcessor extends AudioWorkletProcessor {
  constructor() {
    super();
    this.ring = null;         // Int16Array over the module's SAB memory
    this.ringSamples = 0;
    this.srcRate = 15720;
    this.wr = 0;              // latest write counter (total samples written)
    this.rd = -1;             // fractional read position; -1 = not primed
    this.volume = 1.0;
    this.port.onmessage = (ev) => {
      const d = ev.data;
      if (d.buffer) {
        this.ring = new Int16Array(d.buffer, d.ringPtr, d.ringSamples);
        this.ringSamples = d.ringSamples;
        this.srcRate = d.rate || 15720;
      }
      if (d.wr !== undefined) this.wr = d.wr;
      if (d.volume !== undefined) this.volume = d.volume / 255;
    };
  }

  process(inputs, outputs) {
    const out = outputs[0][0];
    if (!this.ring || !out) return true;

    const step = this.srcRate / sampleRate;
    // Prime (or re-prime after an underrun) a tenth of a second behind the
    // writer: latency nobody notices, slack that absorbs scheduling jitter.
    if (this.rd < 0 || this.rd >= this.wr) {
      if (this.wr < this.srcRate / 10) { out.fill(0); return true; }
      this.rd = this.wr - this.srcRate / 10;
    }
    // Fell too far behind (tab throttled): skip forward rather than drain
    // stale audio for seconds.
    if (this.wr - this.rd > this.ringSamples - this.srcRate / 10) {
      this.rd = this.wr - this.srcRate / 10;
    }

    const mask = this.ringSamples - 1;
    for (let i = 0; i < out.length; i++) {
      if (this.rd + 1 >= this.wr) {
        out.fill(0, i);
        break;
      }
      const idx = Math.floor(this.rd);
      const frac = this.rd - idx;
      const a = this.ring[idx & mask];
      const b = this.ring[(idx + 1) & mask];
      out[i] = ((a + (b - a) * frac) / 32768) * this.volume;
      this.rd += step;
    }
    return true;
  }
}

registerProcessor('fmrb-apu', FmrbApuProcessor);
