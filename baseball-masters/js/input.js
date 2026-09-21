/* Baseball Masters — the live at-bat controller.
   Drives one Game instance pitch-by-pitch, switching between a pitching
   meter (when the user's team is fielding) and a PCI/timing swing
   (when the user's team is batting), animating results in between. */
(function (BM) {
  'use strict';
  function clamp(v, lo, hi) { return v < lo ? lo : (v > hi ? hi : v); }

  /**
   * @param opts.game       a BM.Game instance
   * @param opts.field      BM.render.Field
   * @param opts.pitchView  BM.render.PitchView
   * @param opts.hooks      { onLog, onHud, onPitchPhase, onBatPhase, onResult, onGameOver }
   */
  function AtBatController(opts) {
    this.game = opts.game;
    this.field = opts.field;
    this.pv = opts.pitchView;
    this.hooks = opts.hooks || {};
    this.state = 'idle';
    this.aim = { x: 0, z: ZONE_MID() };
    this.pci = { x: 0, z: ZONE_MID() };
    this.selectedPitch = null;
    this.buntArmed = false;
    this.paused = false;
    this.raf = null;
    this._tick = this.tick.bind(this);
  }

  function ZONE_MID() { return (BM.ZONE.bot + BM.ZONE.top) / 2; }

  AtBatController.prototype.log = function (t, kind) { if (this.hooks.onLog) this.hooks.onLog(t, kind); };
  AtBatController.prototype.hud = function () { if (this.hooks.onHud) this.hooks.onHud(this.game); };

  AtBatController.prototype.start = function () {
    this.hud();
    this.field.drawStadium(this.game.stadium);
    this.field.drawFielders(this.game.fieldingTeam());
    this.beginPitchDecision();
    if (!this.raf) this.raf = requestAnimationFrame(this._tick);
  };

  AtBatController.prototype.stop = function () {
    if (this.raf) cancelAnimationFrame(this.raf);
    this.raf = null;
  };

  /* --------------------------------------------------------- pitch phase */

  AtBatController.prototype.beginPitchDecision = function () {
    if (this.game.over) { this.finishGame(); return; }
    this.aim = { x: 0, z: ZONE_MID() };
    if (this.game.userIsPitching()) {
      this.state = 'select-pitch';
      this.selectedPitch = this.game.pitcher().arsenal[0];
      if (this.hooks.onPitchPhase) this.hooks.onPitchPhase(this.game.pitcher(), this.game.batter());
    } else {
      this.state = 'cpu-wind-up';
      setTimeout(() => this.throwCpuPitch(), 260);
    }
  };

  AtBatController.prototype.choosePitchType = function (id) { this.selectedPitch = id; };
  AtBatController.prototype.moveAim = function (dx, dz) {
    this.aim.x = clamp(this.aim.x + dx, -1.7, 1.7);
    this.aim.z = clamp(this.aim.z + dz, 0.3, 4.3);
    this.drawAimFrame();
  };
  AtBatController.prototype.setAim = function (x, z) {
    this.aim.x = clamp(x, -1.9, 1.9);
    this.aim.z = clamp(z, 0.2, 4.5);
    this.drawAimFrame();
  };
  AtBatController.prototype.drawAimFrame = function () {
    this.pv.drawBackground(); this.pv.drawZone();
    this.pv.drawAimReticle(this.aim.x, this.aim.z, '#ffd24a');
  };

  /** Kick off the meter minigame: a marker sweeps 0..1..0; user locks it. */
  AtBatController.prototype.startMeter = function () {
    this.state = 'meter';
    this.meterT = 0;
    this.meterSpeed = 1.55 + Math.random() * 0.35;
    this.drawAimFrame();
    if (this.hooks.onMeterPhase) this.hooks.onMeterPhase();
  };
  AtBatController.prototype.meterValue = function () {
    const phase = (Math.sin(this.meterT * this.meterSpeed * Math.PI * 2 - Math.PI / 2) + 1) / 2;
    return phase; // 0..1, oscillates
  };
  AtBatController.prototype.lockMeter = function () {
    const v = this.meterValue();
    // Sweet spot sits near the top of the sweep (v = 0.85); miss it either
    // way and accuracy falls off.
    const acc = clamp(1 - Math.abs(v - 0.85) * 1.15, 0.05, 1);
    this.throwUserPitch(acc);
  };

  AtBatController.prototype.throwUserPitch = function (accuracy) {
    const pitch = this.game.makePitch(this.selectedPitch, this.aim, accuracy);
    this.game.tirePitcher();
    this.log((this.game.userIsBatting() ? '' : 'You deal a ') + pitch.name + ' — ' + pitch.velo + ' mph', 'pitch');
    this.beginBallFlight(pitch);
  };

  AtBatController.prototype.throwCpuPitch = function () {
    const call = this.game.cpuPitchCall();
    const pitch = this.game.makePitch(call.id, call.aim, call.accuracy);
    this.game.tirePitcher();
    this.log(this.game.pitcher().name + ' deals a ' + pitch.name + ' — ' + pitch.velo + ' mph', 'pitch');
    this.beginBallFlight(pitch);
  };

  /* ---------------------------------------------------------- ball flight */

  AtBatController.prototype.beginBallFlight = function (pitch) {
    this.pitch = pitch;
    this.pitchStart = performance.now();
    this.pci = { x: 0, z: ZONE_MID() };
    this.swungAt = null;
    this.state = this.game.userIsBatting() ? 'batting' : 'cpu-batting';
    if (this.state === 'cpu-batting') {
      this.cpuSwing = this.game.cpuSwingDecision(pitch);
      // CPU "reacts" at a time proportional to how early/late its timing error puts it.
      this.cpuSwingT = pitch.flightTime * 1000 + (this.cpuSwing ? this.cpuSwing.timingError : 0);
    }
  };

  AtBatController.prototype.movePCI = function (dx, dz) {
    this.pci.x = clamp(this.pci.x + dx, -2.2, 2.2);
    this.pci.z = clamp(this.pci.z + dz, -0.2, 5.2);
  };
  AtBatController.prototype.setPCI = function (x, z) {
    this.pci.x = clamp(x, -2.4, 2.4);
    this.pci.z = clamp(z, -0.4, 5.4);
  };

  AtBatController.prototype.setSwingType = function (t) { this.pendingSwingType = t; };

  /** User commits to a swing right now. */
  AtBatController.prototype.swingNow = function (type) {
    if (this.state !== 'batting' || this.swungAt !== null) return;
    this.swungAt = performance.now() - this.pitchStart;
    this.swingTypeUsed = type || this.pendingSwingType || 'normal';
  };

  AtBatController.prototype.tick = function (now) {
    this.raf = requestAnimationFrame(this._tick);
    if (this.state === 'meter') {
      const dt = this._lastMeter ? (now - this._lastMeter) / 1000 : 0.016;
      this._lastMeter = now;
      this.meterT += dt;
      if (this.hooks.onMeterTick) this.hooks.onMeterTick(this.meterValue());
      return;
    }
    this._lastMeter = null;

    if (this.state === 'batting' || this.state === 'cpu-batting') {
      const elapsed = now - this.pitchStart;
      const total = this.pitch.flightTime * 1000;
      const u = clamp(elapsed / total, 0, 1);
      this.pv.drawBackground(); this.pv.drawZone();
      this.pv.drawBall(this.game, this.pitch, u, this.pitch.def.color);
      if (this.state === 'batting') {
        const radius = this.game.pciRadius(this.game.batter(), this.pendingSwingType || 'normal', true);
        this.pv.drawPCI(this.pci.x, this.pci.z, radius);
        if (this.swungAt !== null && elapsed >= this.swungAt) {
          this.resolveUserSwing(total);
          return;
        }
      } else if (this.cpuSwing && elapsed >= this.cpuSwingT) {
        this.resolveCpuSwing();
        return;
      }
      if (elapsed >= total + 90) {
        // Pitch reaches the plate with no swing: a take.
        if (this.state === 'batting') this.resolveTake();
        else this.resolveCpuTake();
      }
      return;
    }

    if (this.state === 'play-anim') this.animatePlay(now);
  };

  AtBatController.prototype.resolveUserSwing = function (totalMs) {
    const timingError = this.swungAt - totalMs;
    const window = this.game.timingWindow(this.game.batter(), true);
    const swing = { type: this.swingTypeUsed, timingError: timingError, pci: { x: this.pci.x, z: this.pci.z } };
    const ev = this.game.applyPitch(this.pitch, swing, true);
    this.afterPitchEvent(ev);
  };
  AtBatController.prototype.resolveTake = function () {
    const ev = this.game.applyPitch(this.pitch, null, true);
    this.afterPitchEvent(ev);
  };
  AtBatController.prototype.resolveCpuSwing = function () {
    const ev = this.game.applyPitch(this.pitch, this.cpuSwing, false);
    this.afterPitchEvent(ev);
  };
  AtBatController.prototype.resolveCpuTake = function () {
    const ev = this.game.applyPitch(this.pitch, null, false);
    this.afterPitchEvent(ev);
  };

  AtBatController.prototype.afterPitchEvent = function (ev) {
    this.hud();
    if (ev.call === 'strike') this.log('Called strike', 'strike');
    else if (ev.call === 'ball') this.log('Ball', 'ball');
    else if (ev.call === 'swinging strike') this.log('Swing and a miss', 'strike');
    else if (ev.call === 'foul') this.log('Foul ball', 'foul');
    else if (ev.play) {
      this.beginPlayAnimation(ev.play);
      return;
    }
    // Count update only (no ball in play) — short beat, then continue.
    setTimeout(() => this.afterOutcomeContinue(), 260);
  };

  /* --------------------------------------------------------- play replay */

  AtBatController.prototype.beginPlayAnimation = function (play) {
    this.state = 'play-anim';
    this.playAnimStart = performance.now();
    this.currentPlay = play;
    this.log(play.desc + (play.flight ? ' (' + Math.round(play.flight.distance) + ' ft)' : ''),
      play.kind === 'HR' ? 'hr' : (play.outs > 0 ? 'out' : 'hit'));
  };

  AtBatController.prototype.animatePlay = function (now) {
    const play = this.currentPlay;
    const f = play.flight;
    const dur = Math.min(f.hangTime, 5.5);
    const t = (now - this.playAnimStart) / 1000;
    this.field.drawStadium(this.game.stadium);
    this.field.drawFielders(this.game.fieldingTeam(), play.fielder);
    this.field.drawRunners(this.game.bases);
    if (t <= dur) {
      this.field.drawTrail(f.path, t);
      this.field.drawBallAt(f.path, t);
      return;
    }
    // Ball has landed — hold the frame briefly, then advance.
    if (!this._landedAt) this._landedAt = now;
    if (now - this._landedAt < 650) return;
    this._landedAt = null;
    this.hud();
    setTimeout(() => this.afterOutcomeContinue(), 120);
  };

  AtBatController.prototype.afterOutcomeContinue = function () {
    if (this.game.over) { this.finishGame(); return; }
    this.field.drawStadium(this.game.stadium);
    this.field.drawFielders(this.game.fieldingTeam());
    this.field.drawRunners(this.game.bases);
    this.beginPitchDecision();
  };

  AtBatController.prototype.finishGame = function () {
    this.state = 'over';
    this.hud();
    if (this.hooks.onGameOver) this.hooks.onGameOver(this.game);
  };

  BM.input = { AtBatController: AtBatController };
})(window.BM = window.BM || {});
