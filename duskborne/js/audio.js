/* ============================================================
   Procedural sound bank. No audio assets ship with this project — every
   cue is synthesised at call time with the Web Audio API, the same
   "nothing to download" approach the rest of the repo takes.
   ============================================================ */
(function (DB) {
  'use strict';

  let ctx = null;
  function ensure() {
    if (!ctx) {
      const AC = window.AudioContext || window.webkitAudioContext;
      ctx = new AC();
    }
    if (ctx.state === 'suspended') ctx.resume();
    return ctx;
  }

  function env(gain, t0, attack, decay, peak) {
    gain.gain.cancelScheduledValues(t0);
    gain.gain.setValueAtTime(0.0001, t0);
    gain.gain.exponentialRampToValueAtTime(peak, t0 + attack);
    gain.gain.exponentialRampToValueAtTime(0.0001, t0 + attack + decay);
  }

  function tone(freq, opts) {
    opts = opts || {};
    const c = ensure();
    const t0 = c.currentTime;
    const osc = c.createOscillator();
    osc.type = opts.type || 'sine';
    osc.frequency.setValueAtTime(freq, t0);
    if (opts.sweepTo) osc.frequency.exponentialRampToValueAtTime(opts.sweepTo, t0 + (opts.dur || 0.2));
    const gain = c.createGain();
    env(gain, t0, opts.attack || 0.005, opts.dur || 0.18, opts.gain || 0.25);
    osc.connect(gain).connect(c.destination);
    osc.start(t0);
    osc.stop(t0 + (opts.dur || 0.18) + (opts.attack || 0.005) + 0.05);
  }

  function noiseBurst(opts) {
    opts = opts || {};
    const c = ensure();
    const dur = opts.dur || 0.15;
    const buf = c.createBuffer(1, c.sampleRate * dur, c.sampleRate);
    const data = buf.getChannelData(0);
    for (let i = 0; i < data.length; i++) data[i] = (Math.random() * 2 - 1) * (1 - i / data.length);
    const src = c.createBufferSource();
    src.buffer = buf;
    const filt = c.createBiquadFilter();
    filt.type = opts.filter || 'lowpass';
    filt.frequency.value = opts.freq || 1800;
    const gain = c.createGain();
    gain.gain.setValueAtTime(opts.gain || 0.3, c.currentTime);
    gain.gain.exponentialRampToValueAtTime(0.0001, c.currentTime + dur);
    src.connect(filt).connect(gain).connect(c.destination);
    src.start();
  }

  const sfx = {
    hover: function () { tone(720, { type: 'sine', dur: 0.05, gain: 0.08 }); },
    select: function () { tone(420, { type: 'triangle', dur: 0.12, sweepTo: 640, gain: 0.16 }); },
    back: function () { tone(360, { type: 'triangle', dur: 0.12, sweepTo: 220, gain: 0.14 }); },
    confirm: function () {
      tone(300, { type: 'sine', dur: 0.14, sweepTo: 520, gain: 0.18 });
      setTimeout(function () { tone(640, { type: 'sine', dur: 0.16, gain: 0.14 }); }, 90);
    },
    shootHeavy: function () { noiseBurst({ dur: 0.12, freq: 900, gain: 0.28 }); tone(120, { type: 'square', dur: 0.06, gain: 0.12 }); },
    shootLight: function () { noiseBurst({ dur: 0.05, freq: 2200, gain: 0.16 }); },
    shootSmg: function () { noiseBurst({ dur: 0.04, freq: 2600, gain: 0.14 }); },
    reload: function () { tone(200, { type: 'square', dur: 0.08, gain: 0.1 }); setTimeout(function () { tone(340, { type: 'square', dur: 0.08, gain: 0.1 }); }, 220); },
    hitMarker: function () { tone(1400, { type: 'square', dur: 0.04, gain: 0.12 }); },
    headshot: function () { tone(1800, { type: 'square', dur: 0.05, gain: 0.16 }); tone(2400, { type: 'square', dur: 0.05, gain: 0.1 }); },
    hurt: function () { noiseBurst({ dur: 0.18, freq: 500, gain: 0.22 }); },
    ability: function () { tone(180, { type: 'sawtooth', dur: 0.4, sweepTo: 60, gain: 0.22 }); },
    kill: function () { tone(260, { type: 'triangle', dur: 0.1, gain: 0.14 }); setTimeout(function () { tone(180, { type: 'triangle', dur: 0.14, gain: 0.12 }); }, 70); },
    victory: function () {
      [0, 130, 260, 400].forEach(function (d, i) {
        setTimeout(function () { tone(330 + i * 110, { type: 'sine', dur: 0.3, gain: 0.2 }); }, d);
      });
    },
    defeat: function () { tone(220, { type: 'sawtooth', dur: 0.9, sweepTo: 60, gain: 0.2 }); },
    bossRoar: function () { noiseBurst({ dur: 0.5, freq: 300, gain: 0.3 }); tone(90, { type: 'sawtooth', dur: 0.6, gain: 0.18 }); }
  };

  DB.audio = { ensure: ensure, sfx: sfx };
})(window.DB || (window.DB = {}));
