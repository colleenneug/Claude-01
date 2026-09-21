/* Baseball Masters — bootstrap. Wires the live-game screen (HUD, controls,
   keyboard/mouse input) around BM.Game + BM.input.AtBatController, and
   drives the standalone Home Run Derby mode. */
(function (BM) {
  'use strict';
  const D = BM.data, P = BM.physics;
  function $(sel, root) { return (root || document).querySelector(sel); }
  function $all(sel, root) { return Array.prototype.slice.call((root || document).querySelectorAll(sel)); }
  function clamp(v, lo, hi) { return v < lo ? lo : (v > hi ? hi : v); }

  function App() {
    this.ui = new BM.UI(this);
    this.field = null;
    this.pitchView = null;
    this.stageNode = null;
    this.ac = null;
    this.mode = null; // 'game' | 'derby'
  }

  /**
   * The field/pitch canvases and their controls live once in a <template>
   * and get physically moved (not cloned) into whichever screen — live
   * game or derby — is currently on stage. That keeps exactly one
   * #fieldCanvas / #pitchCanvas in the DOM, so ids never collide.
   */
  App.prototype.mountStage = function (slotId) {
    if (!this.stageNode) {
      const tpl = document.getElementById('game-stage-template');
      this.stageNode = tpl.content.firstElementChild.cloneNode(true);
    }
    document.getElementById(slotId).appendChild(this.stageNode);
    if (!this.field) this.field = new BM.render.Field($('#fieldCanvas'));
    if (!this.pitchView) this.pitchView = new BM.render.PitchView($('#pitchCanvas'));
    this.field.resize();
    this.pitchView.resize();
  };

  App.prototype.init = function () {
    this.ui.initMenu();
    this.ui.wireExhibitionStart((cfg) => this.startExhibition(cfg));
    this.ui.wireDerbyStart((batter, team) => this.startDerby(batter, team));
    this.mountStage('game-slot');
    this.wireGameScreen();
    this.wireDerbyScreen();
    window.addEventListener('resize', () => {
      if (this.field) this.field.resize();
      if (this.pitchView) this.pitchView.resize();
    });
    this.ui.show('screen-menu');
  };

  /* --------------------------------------------------------------- HUD */

  App.prototype.renderHud = function (game) {
    $('#hud-away-name').textContent = game.away.nick;
    $('#hud-home-name').textContent = game.home.nick;
    $('#hud-away-score').textContent = game.score.away;
    $('#hud-home-score').textContent = game.score.home;
    $('#hud-inning').textContent = (game.half === 'T' ? '▲' : '▼') + ' ' + game.inning;
    $('#hud-outs').innerHTML = dots(game.outs, 3, '#ffd24a');
    $('#hud-balls').innerHTML = dots(game.balls, 4, '#5ad7ff');
    $('#hud-strikes').innerHTML = dots(game.strikes, 3, '#ff5a5a');
    $('#hud-base1').classList.toggle('on', !!game.bases[0]);
    $('#hud-base2').classList.toggle('on', !!game.bases[1]);
    $('#hud-base3').classList.toggle('on', !!game.bases[2]);
    if (!game.over) {
      const bat = game.batter(), pit = game.pitcher();
      $('#hud-batter').textContent = bat.name + ' (' + bat.pos + ', ' + bat.tier.name + ' ' + bat.ovr + ')';
      $('#hud-pitcher').textContent = pit.name + ' — ' + Math.round(pit.stamLeft) + '% stamina';
    }
    $('#hud-role').textContent = game.userIsPitching() ? 'YOU ARE PITCHING' : (game.userIsBatting() ? 'YOU ARE BATTING' : 'CPU vs CPU');
    $('#pitch-controls').classList.toggle('hidden', !(game.userIsPitching()));
    $('#bat-controls').classList.toggle('hidden', !(game.userIsBatting()));
  };
  function dots(n, max, color) {
    let h = '';
    for (let i = 0; i < max; i++) h += '<span class="dot' + (i < n ? ' lit' : '') + '" style="--c:' + color + '"></span>';
    return h;
  }

  App.prototype.logLine = function (text, kind) {
    const feed = $('#playlog');
    const line = document.createElement('div');
    line.className = 'log-line log-' + (kind || 'play');
    line.textContent = text;
    feed.insertBefore(line, feed.firstChild);
    while (feed.children.length > 60) feed.removeChild(feed.lastChild);
  };

  /* ---------------------------------------------------------- live game */

  App.prototype.startExhibition = function (cfg) {
    const game = new BM.Game(cfg);
    this.launchLiveGame(game, () => this.showBoxScore(game, () => this.ui.show('screen-menu')));
  };

  App.prototype.launchLiveGame = function (game, onDone) {
    this.mode = 'game';
    this.currentGame = game;
    this.onGameDone = onDone;
    $('#playlog').innerHTML = '';
    // Show the screen first — the canvases size themselves off their
    // container's on-screen dimensions, which read as 0 while still hidden.
    this.ui.show('screen-game');
    this.mountStage('game-slot');

    this.buildPitchButtons(game);

    this.ac = new BM.input.AtBatController({
      game: game, field: this.field, pitchView: this.pitchView,
      hooks: {
        onLog: (t, k) => this.logLine(t, k),
        onHud: (g) => this.renderHud(g),
        onPitchPhase: (pit, bat) => this.onPitchPhase(pit, bat),
        onMeterPhase: () => this.onMeterPhase(),
        onMeterTick: (v) => this.onMeterTick(v),
        onGameOver: (g) => this.onGameOver(g)
      }
    });
    this.ac.start();
  };

  App.prototype.onGameOver = function (g) {
    this.logLine('Final: ' + g.away.name + ' ' + g.score.away + ', ' + g.home.name + ' ' + g.score.home, 'final');
    setTimeout(() => {
      if (this.onGameDone) this.onGameDone();
    }, 900);
  };

  App.prototype.showBoxScore = function (game, onDone) {
    this.ui.show('screen-boxscore');
    const winner = game.score.home > game.score.away ? game.home : game.away;
    $('#box-summary').textContent = game.away.name + ' ' + game.score.away + ' — ' + game.home.name + ' ' + game.score.home +
      '  ·  ' + winner.name + ' win';
    const line = $('#box-line-table');
    let innings = Math.max(game.lineScore.away.length, game.lineScore.home.length, game.inning);
    let head = '<tr><th>Team</th>';
    for (let i = 1; i <= innings; i++) head += '<th>' + i + '</th>';
    head += '<th>R</th><th>H</th><th>E</th></tr>';
    let body = '<tr><td>' + game.away.nick + '</td>';
    for (let i = 0; i < innings; i++) body += '<td>' + (game.lineScore.away[i] || 0) + '</td>';
    body += '<td><b>' + game.score.away + '</b></td><td>' + game.hits.away + '</td><td>' + game.errors.away + '</td></tr>';
    body += '<tr><td>' + game.home.nick + '</td>';
    for (let i = 0; i < innings; i++) body += '<td>' + (game.lineScore.home[i] || 0) + '</td>';
    body += '<td><b>' + game.score.home + '</b></td><td>' + game.hits.home + '</td><td>' + game.errors.home + '</td></tr>';
    line.innerHTML = head + body;

    const bat = $('#box-batting-table');
    bat.innerHTML = '<tr><th>Batter</th><th>AB</th><th>H</th><th>HR</th><th>RBI</th><th>BB</th><th>K</th></tr>' +
      game.away.lineup.concat(game.home.lineup).filter(p => p.stats.pa > 0)
        .map(p => '<tr><td>' + p.name + '</td><td>' + p.stats.ab + '</td><td>' + p.stats.h + '</td><td>' + p.stats.hr +
          '</td><td>' + p.stats.rbi + '</td><td>' + p.stats.bb + '</td><td>' + p.stats.k + '</td></tr>').join('');

    const pit = $('#box-pitching-table');
    pit.innerHTML = '<tr><th>Pitcher</th><th>IP</th><th>H</th><th>ER</th><th>BB</th><th>K</th></tr>' +
      [game.pitchers.away, game.pitchers.home].concat(
        game.away.bullpen.filter(p => p.stats.outs > 0 || p.stats.pitches > 0),
        game.home.bullpen.filter(p => p.stats.outs > 0 || p.stats.pitches > 0),
        game.away.rotation.filter(p => p !== game.pitchers.away && (p.stats.outs > 0)),
        game.home.rotation.filter(p => p !== game.pitchers.home && (p.stats.outs > 0))
      ).filter((p, i, a) => p && a.indexOf(p) === i)
      .map(p => '<tr><td>' + p.name + '</td><td>' + (Math.floor(p.stats.outs / 3) + '.' + (p.stats.outs % 3)) +
        '</td><td>' + p.stats.h + '</td><td>' + p.stats.er + '</td><td>' + p.stats.bb + '</td><td>' + p.stats.k + '</td></tr>').join('');

    $('#box-done').onclick = onDone;
  };

  /* -------------------------------------------------------- pitch phase */

  App.prototype.buildPitchButtons = function (game) {
    const wrap = $('#pitch-type-buttons');
    wrap.innerHTML = '';
    this._pitchBtns = [];
  };

  App.prototype.onPitchPhase = function (pit, bat) {
    const wrap = $('#pitch-type-buttons');
    wrap.innerHTML = '';
    pit.arsenal.forEach((id, i) => {
      const def = D.PITCHES[id];
      const b = document.createElement('button');
      b.className = 'btn pitch-btn';
      b.style.setProperty('--pc', def.color);
      b.textContent = (i + 1) + '. ' + def.name;
      b.addEventListener('click', () => { this.ac.choosePitchType(id); this.highlightPitchBtn(b); });
      wrap.appendChild(b);
    });
    if (wrap.firstChild) this.highlightPitchBtn(wrap.firstChild);
    this.ac.choosePitchType(pit.arsenal[0]);
    $('#pitch-instruction').textContent = 'Pick a pitch, aim in the zone, then throw.';
    $('#meter-wrap').classList.add('hidden');
    this.ac.drawAimFrame();
  };
  App.prototype.highlightPitchBtn = function (btn) {
    $all('.pitch-btn').forEach(b => b.classList.remove('active'));
    btn.classList.add('active');
  };

  App.prototype.onMeterPhase = function () {
    $('#meter-wrap').classList.remove('hidden');
    $('#pitch-instruction').textContent = 'Click / press Space to stop the meter!';
  };
  App.prototype.onMeterTick = function (v) {
    $('#meter-marker').style.left = (v * 100) + '%';
  };

  /* --------------------------------------------------------------- input */

  App.prototype.wireGameScreen = function () {
    const pv = $('#pitchCanvas');

    // Aiming while pitching (mouse).
    pv.addEventListener('mousemove', (e) => {
      if (!this.ac) return;
      const rect = pv.getBoundingClientRect();
      const mx = e.clientX - rect.left, my = e.clientY - rect.top;
      const feet = this.screenToFeet(mx, my);
      if (this.ac.state === 'select-pitch') this.ac.setAim(feet.x, feet.z);
      else if (this.ac.state === 'batting') this.ac.setPCI(feet.x, feet.z);
    });
    pv.addEventListener('touchmove', (e) => {
      if (!this.ac || !e.touches[0]) return;
      const rect = pv.getBoundingClientRect();
      const mx = e.touches[0].clientX - rect.left, my = e.touches[0].clientY - rect.top;
      const feet = this.screenToFeet(mx, my);
      if (this.ac.state === 'select-pitch') this.ac.setAim(feet.x, feet.z);
      else if (this.ac.state === 'batting') this.ac.setPCI(feet.x, feet.z);
      e.preventDefault();
    }, { passive: false });

    pv.addEventListener('click', () => this.primaryAction());
    $('#throw-btn').addEventListener('click', () => this.primaryAction());
    $('#swing-normal').addEventListener('click', () => this.doSwing('normal'));
    $('#swing-contact').addEventListener('click', () => this.doSwing('contact'));
    $('#swing-power').addEventListener('click', () => this.doSwing('power'));
    $('#swing-bunt').addEventListener('click', () => this.doSwing('bunt'));

    document.addEventListener('keydown', (e) => this.onKeyDown(e));
  };

  App.prototype.screenToFeet = function (px, py) {
    return this.pitchView.screenToFeet(px, py);
  };

  App.prototype.primaryAction = function () {
    if (!this.ac) return;
    if (this.mode === 'derby') { this.derbyPrimaryAction(); return; }
    if (this.ac.state === 'select-pitch') this.ac.startMeter();
    else if (this.ac.state === 'meter') this.ac.lockMeter();
  };

  App.prototype.doSwing = function (type) {
    if (!this.ac || this.mode === 'derby') { this.derbySwing(type); return; }
    if (this.ac.state !== 'batting') return;
    this.ac.swingNow(type);
  };

  App.prototype.onKeyDown = function (e) {
    if (!this.ac) return;
    const arrowStep = 0.14;
    if (this.mode !== 'derby' && this.ac.state === 'select-pitch') {
      const n = parseInt(e.key, 10);
      const pit = this.currentGame.pitcher();
      if (n >= 1 && n <= pit.arsenal.length) {
        this.ac.choosePitchType(pit.arsenal[n - 1]);
        const btns = $all('.pitch-btn');
        if (btns[n - 1]) this.highlightPitchBtn(btns[n - 1]);
      }
      if (e.key === 'ArrowLeft') this.ac.moveAim(-arrowStep, 0);
      if (e.key === 'ArrowRight') this.ac.moveAim(arrowStep, 0);
      if (e.key === 'ArrowUp') this.ac.moveAim(0, arrowStep);
      if (e.key === 'ArrowDown') this.ac.moveAim(0, -arrowStep);
      if (e.key === ' ') { e.preventDefault(); this.primaryAction(); }
    } else if (this.ac.state === 'meter') {
      if (e.key === ' ') { e.preventDefault(); this.primaryAction(); }
    } else if (this.ac.state === 'batting') {
      if (e.key === 'ArrowLeft') this.ac.movePCI(-arrowStep, 0);
      if (e.key === 'ArrowRight') this.ac.movePCI(arrowStep, 0);
      if (e.key === 'ArrowUp') this.ac.movePCI(0, arrowStep);
      if (e.key === 'ArrowDown') this.ac.movePCI(0, -arrowStep);
      if (e.key === ' ') { e.preventDefault(); this.doSwing('normal'); }
      if (e.key === 'z' || e.key === 'Z') this.doSwing('contact');
      if (e.key === 'x' || e.key === 'X') this.doSwing('power');
      if (e.key === 'c' || e.key === 'C') this.doSwing('bunt');
    }
  };

  /* ------------------------------------------------------- home run derby */

  App.prototype.wireDerbyScreen = function () {
    $('#derby-quit').addEventListener('click', () => { this.stopDerby(); this.ui.show('screen-menu'); });
  };

  App.prototype.startDerby = function (batter, team) {
    this.mode = 'derby';
    this.derby = { batter: batter, team: team, swings: 0, maxSwings: 10, homers: 0, longest: 0, results: [] };
    this.ui.show('screen-derby');
    this.mountStage('derby-slot');
    $('#pitch-controls').classList.add('hidden');
    $('#bat-controls').classList.remove('hidden');
    $('#derby-swings').textContent = '0 / 10';
    $('#derby-homers').textContent = '0';
    $('#derby-longest').textContent = '0 ft';
    $('#derby-feed').innerHTML = '';
    this.field.drawStadium(D.STADIUMS[0]);
    this.field.drawFielders(team);
    this.derbyNextPitch();
  };

  App.prototype.stopDerby = function () {
    this.mode = null;
    this.ac = null;
  };

  App.prototype.derbyNextPitch = function () {
    if (this.derby.swings >= this.derby.maxSwings) { this.finishDerby(); return; }
    // A generous, dead-straight BP fastball.
    const aim = { x: (Math.random() - 0.5) * 0.5, z: 2.3 + (Math.random() - 0.5) * 0.6 };
    const fakeGame = this.derbyGame();
    const pitch = fakeGame.makePitch('FF', aim, 1);
    pitch.velo = 62; pitch.bx = 0; pitch.bz = 0.15;
    pitch.flightTime = 0.62;
    this.derbyPitch = pitch;
    this.derbyPitchStart = performance.now();
    this.derbyPCI = { x: 0, z: 2.5 };
    this.derbySwungAt = null;
    this.derbyRunning = true;
    if (!this._derbyRaf) this._derbyRaf = requestAnimationFrame(this._derbyTick.bind(this));
  };

  App.prototype.derbyGame = function () {
    if (!this._derbyFakeGame) {
      this._derbyFakeGame = new BM.Game({ away: this.derby.team, home: this.derby.team, stadium: D.STADIUMS[0] });
    }
    this._derbyFakeGame.stadium = D.STADIUMS[0];
    return this._derbyFakeGame;
  };

  App.prototype._derbyTick = function (now) {
    this._derbyRaf = requestAnimationFrame(this._derbyTick.bind(this));
    if (!this.derbyRunning) return;
    const elapsed = now - this.derbyPitchStart;
    const total = this.derbyPitch.flightTime * 1000;
    const u = clamp(elapsed / total, 0, 1);
    const game = this.derbyGame();
    this.pitchView.drawBackground(); this.pitchView.drawZone();
    this.pitchView.drawBall(game, this.derbyPitch, u, '#fff');
    const radius = game.pciRadius(this.derby.batter, 'power', true) * 1.1;
    this.pitchView.drawPCI(this.derbyPCI.x, this.derbyPCI.z, radius);
    if (this.derbySwungAt !== null && elapsed >= this.derbySwungAt) {
      this.derbyRunning = false;
      this.resolveDerbySwing(total);
      return;
    }
    if (elapsed >= total + 140) {
      this.derbyRunning = false;
      this.derby.swings++;
      this.logDerby('Take. Strike.', 'strike');
      this.updateDerbyHud();
      setTimeout(() => this.derbyNextPitch(), 500);
    }
  };

  App.prototype.derbyPrimaryAction = function () { /* mouse click during derby = swing */ this.derbySwing('power'); };
  App.prototype.derbySwing = function (type) {
    if (!this.derbyRunning || this.derbySwungAt !== null) return;
    this.derbySwungAt = performance.now() - this.derbyPitchStart;
    this.derbySwingType = type;
  };

  App.prototype.resolveDerbySwing = function (totalMs) {
    const game = this.derbyGame();
    const bat = this.derby.batter;
    const timingError = this.derbySwungAt - totalMs;
    const swing = { type: this.derbySwingType || 'power', timingError: timingError, pci: { x: this.derbyPCI.x, z: this.derbyPCI.z } };
    game.order.away = 0; game.away.lineup[0] = bat;
    const sr = game.resolveSwing(this.derbyPitch, swing, true);
    this.derby.swings++;
    if (sr.result === 'miss') this.logDerby('Swing and a miss.', 'strike');
    else if (sr.result === 'foul') this.logDerby('Foul ball.', 'foul');
    else {
      const play = game.resolveBattedBall(sr);
      if (play.kind === 'HR') {
        this.derby.homers++;
        this.derby.longest = Math.max(this.derby.longest, Math.round(play.flight.distance));
        this.logDerby('HOME RUN — ' + Math.round(play.flight.distance) + ' ft!', 'hr');
        this.animateDerbyBall(play.flight);
      } else {
        this.logDerby(play.desc + ' — ' + Math.round(play.flight.distance) + ' ft (out)', 'out');
        this.animateDerbyBall(play.flight);
      }
    }
    this.updateDerbyHud();
    setTimeout(() => this.derbyNextPitch(), 1400);
  };

  App.prototype.animateDerbyBall = function (flight) {
    const start = performance.now();
    const dur = Math.min(flight.hangTime, 5.5) * 1000;
    const team = this.derby.team;
    const step = (now) => {
      const t = (now - start) / 1000;
      this.field.drawStadium(D.STADIUMS[0]);
      this.field.drawFielders(team);
      if (t * 1000 <= dur) {
        this.field.drawTrail(flight.path, t);
        this.field.drawBallAt(flight.path, t);
        requestAnimationFrame(step);
      }
    };
    requestAnimationFrame(step);
  };

  App.prototype.updateDerbyHud = function () {
    $('#derby-swings').textContent = this.derby.swings + ' / ' + this.derby.maxSwings;
    $('#derby-homers').textContent = this.derby.homers;
    $('#derby-longest').textContent = this.derby.longest + ' ft';
  };
  App.prototype.logDerby = function (text, kind) {
    const feed = $('#derby-feed');
    const line = document.createElement('div');
    line.className = 'log-line log-' + kind;
    line.textContent = text;
    feed.insertBefore(line, feed.firstChild);
  };

  App.prototype.finishDerby = function () {
    const stubs = this.derby.homers * 40 + this.derby.longest;
    this.ui.awardStubs(stubs, 'derby');
    this.logDerby('Derby over! ' + this.derby.homers + ' home runs. +' + stubs + ' Stubs.', 'final');
    this.stopDerby();
  };

  document.addEventListener('DOMContentLoaded', function () {
    window.BMApp = new App();
    window.BMApp.init();
  });
})(window.BM = window.BM || {});
