/* Procedural bleeps — no asset files, everything is synthesised on demand. */
(function (SF) {
  'use strict';

  let ctx = null;
  let master = null;
  let enabled = true;

  function ensure() {
    if (ctx) return ctx;
    const AC = window.AudioContext || window.webkitAudioContext;
    if (!AC) { enabled = false; return null; }
    ctx = new AC();
    master = ctx.createGain();
    master.gain.value = 0.16;
    master.connect(ctx.destination);
    return ctx;
  }

  function tone(freq, dur, type, vol, slideTo) {
    if (!enabled) return;
    const c = ensure();
    if (!c) return;
    if (c.state === 'suspended') c.resume();

    const osc = c.createOscillator();
    const gain = c.createGain();
    osc.type = type || 'sine';
    osc.frequency.setValueAtTime(freq, c.currentTime);
    if (slideTo) osc.frequency.exponentialRampToValueAtTime(slideTo, c.currentTime + dur);

    gain.gain.setValueAtTime(0.0001, c.currentTime);
    gain.gain.exponentialRampToValueAtTime(vol == null ? 0.5 : vol, c.currentTime + 0.012);
    gain.gain.exponentialRampToValueAtTime(0.0001, c.currentTime + dur);

    osc.connect(gain); gain.connect(master);
    osc.start(); osc.stop(c.currentTime + dur + 0.02);
  }

  function noise(dur, vol, filterFreq) {
    if (!enabled) return;
    const c = ensure();
    if (!c) return;
    const len = Math.floor(c.sampleRate * dur);
    const buf = c.createBuffer(1, len, c.sampleRate);
    const data = buf.getChannelData(0);
    for (let i = 0; i < len; i++) data[i] = (Math.random() * 2 - 1) * (1 - i / len);

    const src = c.createBufferSource();
    src.buffer = buf;
    const filt = c.createBiquadFilter();
    filt.type = 'lowpass';
    filt.frequency.value = filterFreq || 1400;
    const gain = c.createGain();
    gain.gain.value = vol == null ? 0.35 : vol;

    src.connect(filt); filt.connect(gain); gain.connect(master);
    src.start();
  }

  const sfx = {
    hover:   () => tone(1180, 0.05, 'square', 0.055),
    click:   () => { tone(720, 0.07, 'square', 0.14); tone(1440, 0.05, 'sine', 0.07); },
    confirm: () => { tone(520, 0.1, 'sawtooth', 0.11); setTimeout(() => tone(880, 0.16, 'sine', 0.13), 70); },
    back:    () => { tone(500, 0.09, 'square', 0.1, 260); },
    deny:    () => { tone(180, 0.18, 'square', 0.14, 90); },
    type:    () => tone(1600 + Math.random() * 500, 0.014, 'square', 0.022),
    hit:     () => { noise(0.2, 0.4, 900); tone(140, 0.16, 'sawtooth', 0.22, 60); },
    crit:    () => { noise(0.3, 0.5, 2200); tone(300, 0.3, 'sawtooth', 0.26, 70); },
    heal:    () => { tone(620, 0.14, 'sine', 0.14); setTimeout(() => tone(930, 0.2, 'sine', 0.13), 80); },
    shield:  () => { tone(340, 0.24, 'triangle', 0.16, 780); },
    ability: () => { tone(880, 0.1, 'sawtooth', 0.12, 1500); },
    enemy:   () => { tone(220, 0.2, 'sawtooth', 0.16, 110); noise(0.16, 0.22, 600); },
    levelup: () => { [523, 659, 784, 1047].forEach((f, i) => setTimeout(() => tone(f, 0.26, 'triangle', 0.15), i * 95)); },
    win:     () => { [392, 523, 659, 880].forEach((f, i) => setTimeout(() => tone(f, 0.34, 'sine', 0.16), i * 130)); },
    lose:    () => { [420, 330, 250, 160].forEach((f, i) => setTimeout(() => tone(f, 0.42, 'sawtooth', 0.15), i * 160)); },
    alarm:   () => { [0, 1, 2].forEach((i) => setTimeout(() => tone(760, 0.16, 'square', 0.13, 420), i * 220)); },
    boot:    () => tone(90, 0.6, 'sine', 0.12, 220),
    open:    () => { tone(260, 0.3, 'triangle', 0.12, 620); noise(0.3, 0.14, 500); }
  };

  SF.audio = {
    sfx,
    unlock() { const c = ensure(); if (c && c.state === 'suspended') c.resume(); },
    toggle(on) { enabled = on == null ? !enabled : on; return enabled; },
    get enabled() { return enabled; }
  };
})(window.SF);
