/* Baseball Masters — rules engine. Pure logic, no DOM, so it can be unit-tested
   in node and reused by the CPU-vs-CPU simulator. */
(function (BM) {
  'use strict';
  const D = BM.data, P = BM.physics;

  // Strike zone, in feet. x = 0 is the middle of the plate, z = height.
  const ZONE = { halfW: 0.83, bot: 1.55, top: 3.45 };
  ZONE.midZ = (ZONE.bot + ZONE.top) / 2;

  // Difficulty knobs, in the spirit of Rookie -> Legend.
  const DIFFICULTY = {
    rookie:  { label: 'Rookie',   timing: 1.70, pci: 1.40, flight: 1.55, cpuCtl: 0.50, cpuBat: 0.55 },
    veteran: { label: 'Veteran',  timing: 1.30, pci: 1.15, flight: 1.28, cpuCtl: 0.68, cpuBat: 0.72 },
    allstar: { label: 'All-Star', timing: 1.00, pci: 1.00, flight: 1.10, cpuCtl: 0.82, cpuBat: 0.86 },
    hof:     { label: 'Hall of Fame', timing: 0.82, pci: 0.88, flight: 0.98, cpuCtl: 0.92, cpuBat: 0.95 },
    legend:  { label: 'Legend',   timing: 0.68, pci: 0.78, flight: 0.90, cpuCtl: 1.00, cpuBat: 1.00 }
  };

  const SWINGS = {
    contact: { label: 'Contact', pci: 1.30, power: -9,  la: -3 },
    normal:  { label: 'Normal',  pci: 1.00, power: 0,   la: 0 },
    power:   { label: 'Power',   pci: 0.74, power: 10,  la: 3 },
    bunt:    { label: 'Bunt',    pci: 1.70, power: -45, la: -10 }
  };

  function clamp(v, lo, hi) { return v < lo ? lo : (v > hi ? hi : v); }
  function clamp01(v) { return clamp(v, 0, 1); }

  function Game(opts) {
    this.rng = opts.rng || Math.random;
    this.away = opts.away;
    this.home = opts.home;
    this.stadium = opts.stadium || D.STADIUMS[0];
    this.maxInnings = opts.innings || 9;
    this.diff = DIFFICULTY[opts.difficulty || 'veteran'];
    this.difficultyId = opts.difficulty || 'veteran';
    this.userSide = opts.userSide || null;     // 'away' | 'home' | null for sim
    this.mercy = opts.mercy || 0;              // run-rule margin, 0 = off

    this.inning = 1;
    this.half = 'T';
    this.outs = 0;
    this.balls = 0;
    this.strikes = 0;
    this.bases = [null, null, null];           // 1B, 2B, 3B
    this.score = { away: 0, home: 0 };
    this.hits = { away: 0, home: 0 };
    this.errors = { away: 0, home: 0 };
    this.lineScore = { away: [], home: [] };
    this.order = { away: 0, home: 0 };
    this.pitchers = { away: this.away.rotation[0], home: this.home.rotation[0] };
    this.pitchers.away.stamLeft = 100;
    this.pitchers.home.stamLeft = 100;
    this.bullpenUsed = { away: 0, home: 0 };
    this.log = [];
    this.over = false;
    this.lastPlay = null;
    this.pitchLog = [];
  }

  Game.prototype.rand = function (lo, hi) { return lo + this.rng() * (hi - lo); };
  Game.prototype.gauss = function (sigma) {
    let u = 1 - this.rng(), v = this.rng();
    return Math.sqrt(-2 * Math.log(u)) * Math.cos(2 * Math.PI * v) * sigma;
  };

  Game.prototype.battingKey = function () { return this.half === 'T' ? 'away' : 'home'; };
  Game.prototype.fieldingKey = function () { return this.half === 'T' ? 'home' : 'away'; };
  Game.prototype.battingTeam = function () { return this[this.battingKey()]; };
  Game.prototype.fieldingTeam = function () { return this[this.fieldingKey()]; };
  Game.prototype.batter = function () {
    const t = this.battingTeam();
    return t.lineup[this.order[this.battingKey()] % 9];
  };
  Game.prototype.pitcher = function () { return this.pitchers[this.fieldingKey()]; };
  Game.prototype.userIsBatting = function () { return this.userSide === this.battingKey(); };
  Game.prototype.userIsPitching = function () { return this.userSide === this.fieldingKey(); };
  Game.prototype.runnersOn = function () { return this.bases.filter(Boolean).length; };

  Game.prototype.say = function (text, kind) {
    this.log.push({ text: text, kind: kind || 'play', inning: this.inning, half: this.half });
    if (this.log.length > 400) this.log.shift();
  };

  /* ------------------------------------------------------------- pitching */

  Game.prototype.isStrike = function (loc) {
    return Math.abs(loc.x) <= ZONE.halfW && loc.z >= ZONE.bot && loc.z <= ZONE.top;
  };

  /**
   * Build a thrown pitch. `aim` is where the pitcher is trying to put it —
   * accuracy (0..1) plus the pitcher's control decides how close it lands.
   */
  Game.prototype.makePitch = function (pitchId, aim, accuracy) {
    const pit = this.pitcher();
    const def = D.PITCHES[pitchId] || D.PITCHES.FF;
    const fatigue = 0.965 + (pit.stamLeft / 100) * 0.035;      // tired arms lose a tick
    const velo = pit.topVelo * fatigue + def.velo + this.rand(-1.2, 1.2);
    const mvt = 0.55 + (pit.movement / 99) * 0.85;
    const armSign = pit.throws === 'L' ? -1 : 1;

    let bx = def.bx * mvt * armSign;
    let bz = def.bz * mvt;
    if (def.erratic) { bx += this.gauss(0.8); bz += this.gauss(0.6); }

    const ctrlMiss = (1 - pit.control / 99);
    const tired = (1 - pit.stamLeft / 100);
    const spread = (0.10 + ctrlMiss * 0.55 + tired * 0.45) + (1 - clamp01(accuracy)) * 1.45;
    const ang = this.rng() * Math.PI * 2;
    const r = Math.abs(this.gauss(spread * 0.6)) + spread * 0.25;

    const plate = { x: aim.x + Math.cos(ang) * r, z: aim.z + Math.sin(ang) * r };
    plate.x = clamp(plate.x, -2.6, 2.6);
    plate.z = clamp(plate.z, 0.25, 5.0);

    // 60'6" minus reach; the ball bleeds about 8% of its speed on the way in.
    const flightTime = 55.0 / (velo * P.MPH * 0.93);
    return {
      id: def.id, def: def, name: def.name, velo: Math.round(velo),
      plate: plate, bx: bx, bz: bz,
      flightTime: flightTime * this.diff.flight,
      strike: this.isStrike(plate),
      release: { x: armSign * 1.9, z: 6.0 }
    };
  };

  /** Ball position at fraction u (0 = release, 1 = plate), for drawing. */
  Game.prototype.pitchPos = function (pitch, u) {
    const uu = clamp01(u);
    const bend = uu * uu - uu;                 // 0 at both ends, negative between
    return {
      x: pitch.release.x + (pitch.plate.x - pitch.release.x) * uu + pitch.bx * bend * -1.6,
      z: pitch.release.z + (pitch.plate.z - pitch.release.z) * uu + (pitch.bz + 1.4) * bend * 1.6,
      depth: uu
    };
  };

  /** CPU pitch choice: sequence and location shift with the count. */
  Game.prototype.cpuPitchCall = function () {
    const pit = this.pitcher();
    const bat = this.batter();
    const ahead = this.strikes > this.balls;
    const behind = this.balls >= 2 && this.balls > this.strikes;
    const arsenal = pit.arsenal;
    let id;
    if (behind || (this.balls === 3 && this.strikes < 2)) {
      id = arsenal[0];                                   // get one over
    } else if (ahead && this.rng() < 0.62) {
      const off = arsenal.slice(1);
      id = off.length ? off[Math.floor(this.rng() * off.length)] : arsenal[0];
    } else {
      id = arsenal[Math.floor(this.rng() * arsenal.length)];
    }
    const pullSide = bat.bats === 'L' ? 1 : -1;
    let aim;
    if (this.strikes === 2 && this.balls < 3 && this.rng() < 0.62) {
      aim = { x: pullSide * this.rand(0.75, 1.25), z: this.rand(1.1, 2.0) };   // chase pitch
    } else if (behind) {
      aim = { x: this.rand(-0.35, 0.35), z: this.rand(2.1, 2.9) };
    } else {
      aim = { x: this.rand(-0.85, 0.85), z: this.rand(1.7, 3.3) };
    }
    const acc = clamp01(this.diff.cpuCtl * this.rand(0.72, 1.12));
    return { id: id, aim: aim, accuracy: acc };
  };

  /** CPU hitter: decide whether to offer, and with what quality. */
  Game.prototype.cpuSwingDecision = function (pitch) {
    const bat = this.batter();
    const strike = pitch.strike;
    const dx = Math.max(0, Math.abs(pitch.plate.x) - ZONE.halfW);
    const dz = Math.max(0, pitch.plate.z < ZONE.midZ ? ZONE.bot - pitch.plate.z : pitch.plate.z - ZONE.top);
    const miss = Math.hypot(dx, dz);

    let swingP;
    if (strike) {
      swingP = 0.30 + this.strikes * 0.28 + (this.balls === 3 ? -0.06 : 0);
      if (this.strikes === 2) swingP = 0.92;
    } else {
      const disc = bat.discipline / 99;
      swingP = clamp01((0.46 - disc * 0.30) * Math.exp(-miss * 1.9));
      if (this.strikes === 2) swingP = clamp01(swingP + 0.34 * Math.exp(-miss * 1.2));
    }
    if (this.rng() > swingP) return null;

    const skill = this.diff.cpuBat;
    const contactSig = (86 - (bat.contact / 99) * 34) / (0.65 + skill * 0.6);
    const pciSig = (0.55 - (bat.vision / 99) * 0.24) / (0.6 + skill * 0.65);
    let type = 'normal';
    if (this.strikes === 2) type = this.rng() < 0.55 ? 'contact' : 'normal';
    else if (bat.power > 78 && this.rng() < 0.32) type = 'power';

    return {
      type: type,
      timingError: this.gauss(contactSig),
      pci: {
        x: pitch.plate.x + this.gauss(pciSig),
        z: pitch.plate.z + this.gauss(pciSig)
      }
    };
  };

  /* --------------------------------------------------------------- swings */

  Game.prototype.pciRadius = function (batter, swingType, forUser) {
    const sw = SWINGS[swingType] || SWINGS.normal;
    const base = 0.46 * sw.pci * (0.78 + (batter.vision / 99) * 0.50);
    return forUser ? base * this.diff.pci : base;
  };
  Game.prototype.timingWindow = function (batter, forUser) {
    const base = 105 + (batter.contact / 99) * 45;            // ms either side
    return forUser ? base * this.diff.timing : base;
  };

  /**
   * Turn a swing into a batted-ball vector (or a miss/foul tip).
   * swing = { type, timingError (ms, negative = early), pci: {x,z} }
   */
  Game.prototype.resolveSwing = function (pitch, swing, forUser) {
    const bat = this.batter();
    const sw = SWINGS[swing.type] || SWINGS.normal;
    const radius = this.pciRadius(bat, swing.type, forUser);
    const window = this.timingWindow(bat, forUser);

    const dx = pitch.plate.x - swing.pci.x;
    const dz = pitch.plate.z - swing.pci.z;
    const gap = Math.hypot(dx, dz);
    const pciQ = clamp01(1 - gap / radius);
    const te = swing.timingError;
    const timingQ = clamp01(1 - Math.abs(te) / window);

    if (pciQ <= 0 || timingQ <= 0) {
      return { result: 'miss', pciQ: pciQ, timingQ: timingQ };
    }
    const contactQ = pciQ * 0.52 + timingQ * 0.48;
    const perfect = pciQ >= 0.85 && timingQ >= 0.86;

    // Weak, edge-of-the-barrel contact goes foul a lot.
    const foulChance = clamp01(0.62 - contactQ * 0.72) + (Math.abs(te) > window * 0.55 ? 0.16 : 0);
    const foulRoll = this.rng();

    const pullSign = bat.bats === 'L' ? 1 : -1;
    const timingSpray = clamp(-te / window, -1.15, 1.15);
    let spray = pullSign * timingSpray * 34 + (dx * -16) + this.gauss(5.5);

    let ev = 53 + (bat.power / 99) * 38 + contactQ * 26 + sw.power * 0.55;
    if (perfect) ev += 9;
    ev += this.gauss(3.4);
    ev = clamp(ev, 28, 122);

    // PCI sitting under the ball lifts it; on top of it drives it into the dirt.
    let la = 15 + dz * 34 + sw.la + this.gauss(9.5) - (1 - contactQ) * 4;
    if (swing.type === 'bunt') { la = this.rand(-6, 10); ev = clamp(ev * 0.42, 18, 52); }
    la = clamp(la, -35, 78);

    if (foulRoll < foulChance || Math.abs(spray) > 45) {
      return {
        result: 'foul', ev: ev, la: la, spray: clamp(spray, -78, 78),
        pciQ: pciQ, timingQ: timingQ, perfect: false, contactQ: contactQ
      };
    }
    return {
      result: 'contact', ev: Math.round(ev * 10) / 10, la: Math.round(la * 10) / 10,
      spray: clamp(spray, -45, 45), pciQ: pciQ, timingQ: timingQ,
      perfect: perfect, contactQ: contactQ, swingType: swing.type
    };
  };

  /* ------------------------------------------------------- batted-ball play */

  const INFIELD = ['P', '1B', '2B', '3B', 'SS'];

  function fielderByPos(pos) {
    return P.FIELDERS.find(f => f.pos === pos);
  }

  /** Nearest fielder to a spot, with the time each would need to get there. */
  Game.prototype.bestFielder = function (x, y, only) {
    let best = null;
    const team = this.fieldingTeam();
    for (const f of P.FIELDERS) {
      if (only && only.indexOf(f.pos) === -1) continue;
      const d = P.dist(f.x, f.y, x, y);
      const guy = team.lineup.find(p => p.pos === f.pos);
      const rating = guy ? guy.fielding : 70;
      const t = P.pursuitTime(d, rating);
      if (!best || t < best.time) best = { pos: f.pos, dist: d, time: t, rating: rating, x: f.x, y: f.y };
    }
    return best;
  };

  /**
   * Decide what a batted ball becomes.
   * Returns { kind, bases, outs, desc, flight, landing, fielder, sacFly, dp }
   */
  Game.prototype.resolveBattedBall = function (hit) {
    const st = this.stadium;
    const f = P.flight(hit.ev, hit.la, hit.spray);
    const fence = D.fenceAt(st, hit.spray);
    const out = { flight: f, landing: f.landing, ev: hit.ev, la: hit.la, spray: hit.spray };

    // Home run / wall ball: find the point where the ball reaches the fence.
    for (let i = 1; i < f.path.length; i++) {
      const pt = f.path[i];
      const d = Math.hypot(pt.x, pt.y);
      if (d >= fence) {
        if (pt.z > st.wall) {
          out.kind = 'HR'; out.bases = 4; out.outs = 0;
          out.desc = 'Gone! ' + Math.round(f.distance) + ' ft home run';
          return out;
        }
        // Off the wall.
        out.kind = Math.abs(hit.spray) > 33 && this.rng() < 0.35 ? '3B' : '2B';
        out.bases = out.kind === '3B' ? 3 : 2;
        out.outs = 0;
        out.desc = 'Off the wall — ' + (out.bases === 3 ? 'triple' : 'double');
        return out;
      }
      if (pt.z <= 0.01) break;
    }

    const lx = f.landing.x, ly = f.landing.y;
    const landDist = f.distance;

    /* ---- ground balls ----
       Rather than race the ball's roll against a fielder's sprint (which, for
       any plausible roll speed, lets someone always get there and dies the
       play), pick the infielder nearest the ball's line and give them a
       coverage radius: how far off-line they can still glove it. Real MLB
       ground balls go for outs about 3 times in 4 — that's the number this
       is calibrated against. */
    if (hit.la < 10) {
      const dirX = Math.sin(hit.spray * Math.PI / 180), dirY = Math.cos(hit.spray * Math.PI / 180);
      const team = this.fieldingTeam();
      let pick = null;
      for (const pos of INFIELD) {
        const fl = fielderByPos(pos);
        const proj = fl.x * dirX + fl.y * dirY;             // along the ball's line
        if (proj < 8) continue;                             // behind the fielder, can't backpedal to it
        const perp = Math.abs(fl.x * dirY - fl.y * dirX);
        const guy = team.lineup.find(p => p.pos === pos);
        const rating = guy ? guy.fielding : 72;
        // Coverage shrinks on a scorched grounder (less reaction time) and
        // grows with the fielder's range rating.
        const radius = (28 + (rating - 70) * 0.18) * clamp(1.28 - hit.ev / 145, 0.55, 1.35);
        const reach = clamp01(1 - perp / radius);
        if (reach > 0 && (!pick || reach > pick.reach)) {
          pick = { pos: pos, proj: proj, perp: perp, rating: rating, reach: reach,
                   x: proj * dirX, y: proj * dirY, arm: guy ? guy.arm : 72 };
        }
      }
      const runner = this.batter();
      if (pick) {
        // reach is 1.0 hit right at the fielder, ->0 at the edge of their range.
        const fieldChance = clamp01(0.35 + pick.reach * 0.55 + (pick.rating / 99) * 0.10);
        if (this.rng() < fieldChance) {
          const errChance = clamp01((1 - pick.rating / 99) * 0.11 + (hit.ev > 100 ? 0.03 : 0));
          out.fielder = pick.pos;
          if (this.rng() < errChance) {
            out.kind = 'E'; out.bases = 1; out.outs = 0;
            out.desc = 'Booted by the ' + pick.pos + ' — batter safe on the error';
            return out;
          }
          // A very fast runner can occasionally beat a routine throw anyway.
          const beatOut = clamp01((runner.speed - 90) / 260);
          if (this.rng() < beatOut) {
            out.kind = '1B'; out.bases = 1; out.outs = 0;
            out.desc = 'Infield single, ' + pick.pos + " can't make the play in time";
            return out;
          }
          out.kind = 'GO'; out.bases = 0; out.outs = 1;
          out.desc = 'Ground out, ' + pick.pos + ' to first';
          return out;
        }
        out.fielder = pick.pos;
      }
      // Through the infield — a clean hole.
      const of = this.bestFielder(Math.max(-250, Math.min(250, lx)), Math.max(60, ly), ['LF', 'CF', 'RF']);
      if (!out.fielder) out.fielder = of.pos;
      if (of.dist > 90 && hit.ev > 98 && this.rng() < 0.30) {
        out.kind = '2B'; out.bases = 2; out.outs = 0;
        out.desc = 'Rips into the gap — double';
      } else {
        out.kind = '1B'; out.bases = 1; out.outs = 0;
        out.desc = 'Base hit through the infield';
      }
      return out;
    }

    /* ---- balls in the air ---- */
    const catcher = this.bestFielder(lx, ly, null);
    out.fielder = catcher.pos;
    const liner = hit.la < 19 && hit.ev > 92;
    const need = catcher.time + (liner ? 0.30 : 0);
    const isInfielder = INFIELD.indexOf(catcher.pos) >= 0;

    if (need <= f.hangTime && landDist < fence + 6) {
      if (isInfielder && landDist < 130) {
        out.kind = 'FO'; out.outs = 1; out.bases = 0;
        out.desc = (hit.la > 45 ? 'Popped up' : 'Lined out') + ' to the ' + catcher.pos;
      } else {
        out.kind = 'FO'; out.outs = 1; out.bases = 0;
        out.desc = 'Fly out to ' + catcher.pos;
        out.sacFly = landDist > 245 && f.hangTime > 3.2;
      }
      const dropChance = clamp01((1 - catcher.rating / 99) * 0.05);
      if (this.rng() < dropChance) {
        out.kind = 'E'; out.outs = 0; out.bases = 1;
        out.desc = 'Dropped by the ' + catcher.pos + '!';
      }
      return out;
    }

    // Falls in. How far the nearest glove was decides the bases.
    const slack = catcher.time - f.hangTime;
    if (landDist < 165) {
      out.kind = '1B'; out.bases = 1;
      out.desc = 'Bloop single, just out of reach';
    } else if (slack > 1.25 || landDist > fence - 28) {
      out.kind = this.rng() < 0.30 ? '3B' : '2B';
      out.bases = out.kind === '3B' ? 3 : 2;
      out.desc = out.bases === 3 ? 'Into the corner — triple!' : 'Into the gap — double';
    } else if (slack > 0.55) {
      out.kind = '2B'; out.bases = 2;
      out.desc = 'Splits the gap for a double';
    } else {
      out.kind = '1B'; out.bases = 1;
      out.desc = 'Base hit in front of the ' + catcher.pos;
    }
    out.outs = 0;
    return out;
  };

  /* ------------------------------------------------------ applying outcomes */

  Game.prototype.addRun = function (runner) {
    const key = this.battingKey();
    this.score[key]++;
    const idx = this.inning - 1;
    while (this.lineScore[key].length < this.inning) this.lineScore[key].push(0);
    this.lineScore[key][idx]++;
    if (runner && runner.stats) runner.stats.r++;
    const pit = this.pitcher();
    if (pit) pit.stats.er++;
  };

  /** Advance runners for a hit of `bases` total bases. Returns RBI count. */
  Game.prototype.advance = function (bases, batter, opts) {
    opts = opts || {};
    let rbi = 0;
    const newBases = [null, null, null];
    for (let i = 2; i >= 0; i--) {
      const r = this.bases[i];
      if (!r) continue;
      let move = bases;
      if (bases === 1 && !opts.forced) {
        // Extra base on a single: fast runners go first-to-third, second-to-home.
        const extra = (r.speed / 99) * 0.55 + (i === 1 ? 0.25 : 0.05);
        if (this.rng() < extra) move = 2;
      }
      const dest = i + move;
      if (dest >= 3) { this.addRun(r); rbi++; }
      else newBases[dest] = r;
    }
    if (bases >= 4) { this.addRun(batter); rbi++; }
    else if (bases > 0) newBases[bases - 1] = batter;
    this.bases = newBases;
    return rbi;
  };

  Game.prototype.nextBatter = function () {
    this.order[this.battingKey()] = (this.order[this.battingKey()] + 1) % 9;
    this.balls = 0; this.strikes = 0;
  };

  Game.prototype.recordOut = function (n) {
    this.outs += n;
    const pit = this.pitcher();
    if (pit) pit.stats.outs += n;
  };

  /** Apply a completed plate appearance. */
  Game.prototype.applyOutcome = function (res) {
    const bat = this.batter();
    const pit = this.pitcher();
    const key = this.battingKey();
    bat.stats.pa++;
    let rbi = 0;

    switch (res.kind) {
      case 'K':
        bat.stats.ab++; bat.stats.k++; pit.stats.k++;
        this.recordOut(1);
        this.say(bat.name + ' strikes out ' + (res.swinging ? 'swinging' : 'looking'), 'k');
        break;
      case 'BB':
        bat.stats.bb++; pit.stats.bb++;
        rbi = this.walkAdvance(bat);
        this.say(bat.name + ' walks', 'bb');
        break;
      case 'HR': {
        bat.stats.ab++; bat.stats.h++; bat.stats.hr++; bat.stats.tb += 4;
        pit.stats.h++; pit.stats.hr++;
        this.hits[key]++;
        rbi = this.advance(4, bat);
        const tag = rbi === 4 ? 'GRAND SLAM' : (rbi > 1 ? rbi + '-run homer' : 'Solo shot');
        this.say(bat.name + ' — ' + tag + '! ' + Math.round(res.flight.distance) + ' ft', 'hr');
        break;
      }
      case '1B': case '2B': case '3B': {
        const b = res.bases;
        bat.stats.ab++; bat.stats.h++; bat.stats.tb += b;
        pit.stats.h++;
        this.hits[key]++;
        rbi = this.advance(b, bat);
        this.say(bat.name + ': ' + res.desc, 'hit');
        break;
      }
      case 'E':
        bat.stats.ab++;
        this.errors[this.fieldingKey()]++;
        rbi = this.advance(1, bat, { forced: true });
        this.say(res.desc, 'error');
        break;
      case 'GO': {
        bat.stats.ab++;
        const dp = this.tryDoublePlay(res);
        this.recordOut(dp ? 2 : 1);
        if (!dp) rbi += this.groundAdvance();
        this.say(bat.name + ': ' + (dp ? 'grounds into a double play' : res.desc), 'out');
        break;
      }
      case 'FO': {
        bat.stats.ab++;
        this.recordOut(1);
        if (res.sacFly && this.outs < 3 && this.bases[2]) {
          const r = this.bases[2];
          this.bases[2] = null;
          this.addRun(r); rbi++;
          bat.stats.ab--;            // sac flies are not at-bats
          this.say(bat.name + ' lifts a sacrifice fly, ' + r.name + ' scores', 'out');
        } else {
          this.say(bat.name + ': ' + res.desc, 'out');
        }
        break;
      }
      case 'FC':
        bat.stats.ab++;
        this.recordOut(1);
        this.say(bat.name + " reaches on a fielder's choice", 'out');
        break;
    }

    bat.stats.rbi += rbi;
    if (rbi > 0 && res.kind !== 'K') { /* runs already scored in advance() */ }
    this.lastPlay = res;
    this.nextBatter();
    this.checkHalfInning();
    return rbi;
  };

  Game.prototype.walkAdvance = function (bat) {
    let rbi = 0;
    if (!this.bases[0]) { this.bases[0] = bat; return 0; }
    if (!this.bases[1]) { this.bases[1] = this.bases[0]; this.bases[0] = bat; return 0; }
    if (!this.bases[2]) { this.bases[2] = this.bases[1]; this.bases[1] = this.bases[0]; this.bases[0] = bat; return 0; }
    this.addRun(this.bases[2]); rbi = 1;
    this.bases[2] = this.bases[1]; this.bases[1] = this.bases[0]; this.bases[0] = bat;
    return rbi;
  };

  Game.prototype.tryDoublePlay = function (res) {
    if (this.outs >= 2 || !this.bases[0]) return false;
    const runner = this.bases[0];
    const chance = clamp01(0.52 - (runner.speed / 99) * 0.22 - (res.ev > 98 ? 0.10 : 0));
    if (this.rng() < chance) {
      this.bases[0] = null;
      if (this.bases[1]) { this.bases[2] = this.bases[2] || this.bases[1]; this.bases[1] = null; }
      return true;
    }
    return false;
  };

  /** Runners move up on a routine ground out when they aren't forced out. */
  Game.prototype.groundAdvance = function () {
    let rbi = 0;
    if (this.outs >= 3) return 0;
    if (this.bases[2] && this.rng() < 0.42) { this.addRun(this.bases[2]); this.bases[2] = null; rbi++; }
    if (this.bases[1] && !this.bases[2] && this.rng() < 0.55) { this.bases[2] = this.bases[1]; this.bases[1] = null; }
    return rbi;
  };

  /* ------------------------------------------------------------- the count */

  Game.prototype.addBall = function () {
    this.balls++;
    if (this.balls >= 4) return this.applyOutcome({ kind: 'BB' });
    return null;
  };
  Game.prototype.addStrike = function (swinging, foul) {
    if (foul && this.strikes >= 2) return null;             // foul with two strikes
    this.strikes++;
    if (this.strikes >= 3) return this.applyOutcome({ kind: 'K', swinging: swinging });
    return null;
  };

  Game.prototype.checkHalfInning = function () {
    if (this.outs < 3) return;
    this.endHalf();
  };

  Game.prototype.endHalf = function () {
    const key = this.battingKey();
    while (this.lineScore[key].length < this.inning) this.lineScore[key].push(0);
    this.outs = 0; this.balls = 0; this.strikes = 0;
    this.bases = [null, null, null];

    // Walk-off / game-over checks.
    if (this.half === 'B') {
      if (this.inning >= this.maxInnings && this.score.home !== this.score.away) { this.finish(); return; }
      if (this.mercy && Math.abs(this.score.home - this.score.away) >= this.mercy && this.inning >= 5) { this.finish(); return; }
      this.inning++;
      this.half = 'T';
    } else {
      // Home team already ahead and out of innings: no bottom half needed.
      if (this.inning >= this.maxInnings && this.score.home > this.score.away) { this.finish(); return; }
      this.half = 'B';
    }
    this.maybePullPitcher();
    this.say('--- ' + (this.half === 'T' ? 'Top' : 'Bottom') + ' ' + this.inning + ' ---', 'inning');
  };

  Game.prototype.finish = function () {
    this.over = true;
    const w = this.score.home > this.score.away ? this.home : this.away;
    this.say('Final: ' + this.away.name + ' ' + this.score.away + ', ' +
             this.home.name + ' ' + this.score.home + ' — ' + w.nick + ' win', 'final');
  };

  /** Starters tire; the pen takes over when the tank is empty. */
  Game.prototype.maybePullPitcher = function () {
    for (const key of ['away', 'home']) {
      const p = this.pitchers[key];
      if (!p) continue;
      if (p.stamLeft > 18) continue;
      const team = this[key];
      const idx = this.bullpenUsed[key];
      if (idx < team.bullpen.length) {
        const fresh = team.bullpen[idx];
        fresh.stamLeft = 100;
        this.pitchers[key] = fresh;
        this.bullpenUsed[key]++;
        this.say('Pitching change: ' + fresh.name + ' in for ' + this[key].nick, 'sub');
      }
    }
  };

  Game.prototype.tirePitcher = function () {
    const p = this.pitcher();
    if (!p) return;
    p.stats.pitches++;
    const drain = 100 / (28 + (p.stamina / 99) * 78);
    p.stamLeft = Math.max(0, p.stamLeft - drain);
  };

  /** Steal attempt from first or second. Returns a description or null. */
  Game.prototype.attemptSteal = function (baseIdx) {
    const r = this.bases[baseIdx];
    if (!r || this.bases[baseIdx + 1]) return null;
    const c = this.fieldingTeam().lineup.find(p => p.pos === 'C');
    const arm = c ? c.arm : 70;
    const chance = clamp01(0.42 + (r.speed / 99) * 0.48 - (arm / 99) * 0.30);
    if (this.rng() < chance) {
      this.bases[baseIdx + 1] = r; this.bases[baseIdx] = null;
      this.say(r.name + ' steals ' + (baseIdx === 0 ? 'second' : 'third') + '!', 'steal');
      return { safe: true };
    }
    this.bases[baseIdx] = null;
    this.recordOut(1);
    this.say(r.name + ' is thrown out trying to steal', 'out');
    this.checkHalfInning();
    return { safe: false };
  };

  /* ------------------------------------------------- one fully simulated PA */

  /** Play a single pitch with both sides on CPU. Used by sim/quick-play. */
  Game.prototype.simPitch = function () {
    if (this.over) return null;
    const call = this.cpuPitchCall();
    const pitch = this.makePitch(call.id, call.aim, call.accuracy);
    this.tirePitcher();
    const swing = this.cpuSwingDecision(pitch);
    return this.applyPitch(pitch, swing);
  };

  /**
   * Shared resolution path for CPU and human swings.
   * swing = null means the batter took the pitch.
   */
  Game.prototype.applyPitch = function (pitch, swing, forUser) {
    const ev = { pitch: pitch, swing: swing };
    if (!swing) {
      ev.taken = true;
      if (pitch.strike) { ev.call = 'strike'; this.addStrike(false, false); }
      else { ev.call = 'ball'; this.addBall(); }
      return ev;
    }
    const sr = this.resolveSwing(pitch, swing, forUser);
    ev.swingResult = sr;
    if (sr.result === 'miss') {
      ev.call = 'swinging strike';
      this.addStrike(true, false);
      return ev;
    }
    if (sr.result === 'foul') {
      ev.call = 'foul';
      if (swing.type === 'bunt' && this.strikes >= 2) {
        this.applyOutcome({ kind: 'K', swinging: true });
      } else {
        this.addStrike(true, true);
      }
      return ev;
    }
    const play = this.resolveBattedBall(sr);
    play.perfect = sr.perfect;
    ev.play = play;
    ev.call = 'in play';
    this.applyOutcome(play);
    return ev;
  };

  Game.prototype.simGame = function (maxPitches) {
    let guard = 0;
    while (!this.over && guard++ < (maxPitches || 20000)) this.simPitch();
    if (!this.over) this.finish();
    return this;
  };

  Game.prototype.boxLine = function () {
    return {
      away: this.away.name, home: this.home.name,
      scoreAway: this.score.away, scoreHome: this.score.home,
      hitsAway: this.hits.away, hitsHome: this.hits.home,
      innings: this.inning
    };
  };

  BM.ZONE = ZONE;
  BM.DIFFICULTY = DIFFICULTY;
  BM.SWINGS = SWINGS;
  BM.Game = Game;
})(window.BM = window.BM || {});
