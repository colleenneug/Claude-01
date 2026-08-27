/* ============================================================
   THE CONDUCTOR — the campaign's final mission.

   Its shield is the song, and you cannot shoot a song. It sings a
   phrase on the four resonance nodes around the dais; play the phrase
   back by shooting those nodes in the same order and the shield drops
   long enough to hurt it. Miss a note and it starts the bar again,
   longer and faster, and punishes you for the interruption.

   Four phases, each a longer phrase and a bigger chunk of health.
   ============================================================ */
(function (SF) {
  'use strict';

  const NOTE_HZ = [392.0, 523.25, 659.25, 783.99];      // G4 C5 E5 G5
  const NODE_COLOUR = [0x5eeaff, 0xffb454, 0x7dff9b, 0xff5ea8];

  /* phase: [sequence length, playback speed, health chunk, exposed window] */
  const PHASES = [
    { len: 3, gap: 0.62, chunk: 0.25, window: 20 },
    { len: 4, gap: 0.54, chunk: 0.25, window: 17 },
    { len: 5, gap: 0.46, chunk: 0.25, window: 15 },
    { len: 6, gap: 0.40, chunk: 0.25, window: 13 }
  ];

  function create(ctx) {
    const { scene, level, lights, ai, hud, player } = ctx;

    /* ---------- the boss itself ---------- */
    const arena = level.zones.deckzero;
    const boss = ai.spawn('conductor', arena.cx, arena.cz + 6);
    const totalHp = Math.round(2600 * (ctx.hpScale || 1));
    boss.maxHp = totalHp;
    boss.hp = totalHp;
    boss.shielded = true;
    boss.rig.group.scale.setScalar(1.5);

    /* ---------- resonance nodes ---------- */
    const nodes = level.nodeAnchors.map((a, i) => {
      const mat = new THREE.MeshStandardMaterial({
        color: 0x0a0d12, emissive: new THREE.Color(NODE_COLOUR[i]),
        emissiveIntensity: 0.35, roughness: 0.3, metalness: 0.6
      });
      const mesh = new THREE.Mesh(new THREE.IcosahedronGeometry(0.72, 1), mat);
      mesh.position.set(a.x, a.y, a.z);
      mesh.castShadow = true;
      scene.add(mesh);
      const halo = lights.attach(NODE_COLOUR[i], 0.5, 9, { dynamic: false });
      halo.set(a.x, a.y, a.z);
      return { i, mesh, mat, halo, pos: new THREE.Vector3(a.x, a.y, a.z),
               glow: 0.35, cooldown: 0, hz: NOTE_HZ[i] };
    });

    const state = {
      phase: 0,
      mode: 'intro',        // intro | singing | listening | exposed | beaten
      t: 0,
      sequence: [],
      playIdx: 0,
      inputIdx: 0,
      exposedT: 0,
      phaseFloor: totalHp,  // hp at which the current exposed window ends
      crescendo: 0,
      summonT: 18,
      attackT: 2.4,
      failures: 0
    };

    /* ---------- telegraph ring for the area attack ---------- */
    const ringMat = new THREE.MeshBasicMaterial({
      color: 0xff3fa0, transparent: true, opacity: 0, side: THREE.DoubleSide,
      blending: THREE.AdditiveBlending, depthWrite: false
    });
    const ring = new THREE.Mesh(new THREE.RingGeometry(1, 1.25, 48), ringMat);
    ring.rotation.x = -Math.PI / 2;
    ring.position.set(arena.cx, 0.85, arena.cz + 6);
    scene.add(ring);

    /* ---------- puzzle flow ---------- */
    function beginPhase(n) {
      state.phase = n;
      const ph = PHASES[Math.min(n, PHASES.length - 1)];
      state.sequence = [];
      for (let i = 0; i < ph.len; i++) {
        // never repeat the same node twice in a row: it reads as one long note
        let pick = Math.floor(Math.random() * 4);
        if (i > 0 && pick === state.sequence[i - 1]) pick = (pick + 1 + Math.floor(Math.random() * 3)) % 4;
        state.sequence.push(pick);
      }
      boss.shielded = true;
      state.mode = 'singing';
      state.playIdx = 0;
      state.inputIdx = 0;
      state.t = 0.8;
      hud.bossShield(true, 'SHIELDED — PLAY THE PHRASE BACK');
      hud.bossPuzzle(state.sequence.length, 0, NODE_COLOUR);
      hud.say('CONDUCTOR', n === 0 ? 'SING, OR BE TUNED.' : 'AGAIN. FROM THE TOP.', 4200);
    }

    function sing(dt) {
      const ph = PHASES[Math.min(state.phase, PHASES.length - 1)];
      state.t -= dt;
      if (state.t > 0) return;
      if (state.playIdx < state.sequence.length) {
        const n = nodes[state.sequence[state.playIdx]];
        n.glow = 3.2;
        SF.audio.note(n.hz, ph.gap * 0.9);
        state.playIdx++;
        state.t = ph.gap;
      } else {
        state.mode = 'listening';
        state.inputIdx = 0;
        hud.bossShield(true, 'YOUR TURN — SHOOT THE NODES IN ORDER');
        hud.bossPuzzle(state.sequence.length, 0, NODE_COLOUR);
      }
    }

    /* Called when a shot lands on a node. */
    function hitNode(i) {
      const n = nodes[i];
      if (n.cooldown > 0) return;
      n.cooldown = 0.22;                     // one shotgun blast is one press
      n.glow = 3.0;
      SF.audio.note(n.hz, 0.28);

      if (state.mode !== 'listening') {      // idle poke, or during the phrase
        if (state.mode === 'singing') { state.t = 0.15; }
        return;
      }

      if (i === state.sequence[state.inputIdx]) {
        state.inputIdx++;
        hud.bossPuzzle(state.sequence.length, state.inputIdx, NODE_COLOUR);
        SF.audio.sfx.hitmarker();
        if (state.inputIdx >= state.sequence.length) breakShield();
      } else {
        wrongNote();
      }
    }

    function breakShield() {
      const ph = PHASES[Math.min(state.phase, PHASES.length - 1)];
      boss.shielded = false;
      state.mode = 'exposed';
      state.exposedT = ph.window;
      state.phaseFloor = Math.max(0, boss.hp - totalHp * ph.chunk);
      hud.bossShield(false, 'SHIELD DOWN');
      hud.bossPuzzle(0, 0, NODE_COLOUR);
      hud.banner('SHIELD DOWN — HURT IT');
      SF.audio.sfx.emp();
      player.state.shake = 1.6;
      for (const n of nodes) n.glow = 2.2;
      hud.say('VOSS', 'That is a rest. It has to stop and listen. Go.', 4000);
    }

    function wrongNote() {
      state.failures++;
      state.inputIdx = 0;
      state.playIdx = 0;
      state.mode = 'singing';
      state.t = 1.1;
      hud.bossShield(true, 'WRONG NOTE — LISTEN AGAIN');
      hud.bossPuzzle(state.sequence.length, 0, NODE_COLOUR);
      SF.audio.sfx.deny();
      // the interruption costs you
      ctx.onPlayerHit(14 * (ctx.dmgScale || 1), boss.pos);
      for (const n of nodes) n.glow = 0.1;
    }

    function reshield() {
      state.mode = 'singing';
      beginPhase(Math.min(state.phase + 1, PHASES.length - 1));
    }

    /* ---------- attacks ---------- */
    function crescendo() {
      state.crescendo = 1.35;                // telegraph time
      hud.say('CONDUCTOR', 'CRESCENDO.', 1600);
      SF.audio.sfx.alarm();
    }

    function resolveCrescendo() {
      const d = Math.hypot(player.position.x - boss.pos.x, player.position.z - boss.pos.z);
      const R = 13;
      if (d < R) {
        const falloff = 1 - d / R;
        ctx.onPlayerHit(Math.round(34 * falloff * (ctx.dmgScale || 1)) + 6, boss.pos);
        player.state.shake = 3;
      }
      SF.audio.sfx.shotHeavy();
      ringMat.opacity = 0;
    }

    /* ---------- per-frame ---------- */
    function update(dt, playerPos) {
      if (state.mode === 'beaten') return;

      for (const n of nodes) {
        n.cooldown = Math.max(0, n.cooldown - dt);
        const rest = state.mode === 'listening' ? 0.9 : 0.35;
        n.glow += (rest - n.glow) * Math.min(1, 4 * dt);
        n.mat.emissiveIntensity = n.glow;
        n.halo.setIntensity(n.glow * 0.9);
        n.mesh.rotation.y += dt * 0.6;
        n.mesh.position.y = n.pos.y + Math.sin(performance.now() / 700 + n.i) * 0.12;
        n.halo.set(n.mesh.position.x, n.mesh.position.y, n.mesh.position.z);
      }

      if (boss.dead || boss.hp <= 0) {
        state.mode = 'beaten';
        hud.bossDefeated();
        for (const n of nodes) n.glow = 0;
        ctx.onDefeated();
        return;
      }

      hud.bossHealth(boss.hp, totalHp, state.phase, PHASES.length, boss.shielded);

      if (state.mode === 'intro') {
        state.t -= dt;
        if (state.t <= 0) beginPhase(0);
        return;
      }
      if (state.mode === 'singing') sing(dt);

      if (state.mode === 'exposed') {
        state.exposedT -= dt;
        if (state.exposedT <= 0 || boss.hp <= state.phaseFloor) reshield();
      }

      /* Attacks run in every mode; the shield phases are meant to be fought
         under pressure, not stood still through. */
      state.attackT -= dt;
      if (state.attackT <= 0) {
        state.attackT = state.mode === 'exposed' ? 2.6 : 1.9;
        crescendo();
      }
      if (state.crescendo > 0) {
        state.crescendo -= dt;
        const t = 1 - Math.max(0, state.crescendo) / 1.35;
        ring.scale.setScalar(0.6 + t * 12.4);
        ringMat.opacity = 0.55 * (1 - t) + 0.15;
        ring.position.set(boss.pos.x, 0.85, boss.pos.z);
        if (state.crescendo <= 0) resolveCrescendo();
      }

      state.summonT -= dt;
      if (state.summonT <= 0) {
        state.summonT = 22;
        if (ai.alive < 7) {
          for (let i = 0; i < 2 + state.phase; i++) {
            const ang = Math.random() * Math.PI * 2;
            ai.spawn('thrall', arena.cx + Math.cos(ang) * 17, arena.cz + Math.sin(ang) * 15);
          }
          hud.say('CONDUCTOR', 'THE CHOIR WILL ASSIST.', 3200);
        }
      }
      void playerPos;
    }

    /* Ray against the nodes, so a shot can "press" one. Returns the nearest
       node hit, or null. */
    const oc = new THREE.Vector3();
    function rayNode(origin, dir, range) {
      let best = null, bestD = range;
      for (const n of nodes) {
        oc.copy(n.mesh.position).sub(origin);
        const t = oc.dot(dir);
        if (t < 0 || t > bestD) continue;
        if (oc.lengthSq() - t * t > 0.95 * 0.95) continue;    // generous hit radius
        best = { node: n, dist: t, point: origin.clone().addScaledVector(dir, t) };
        bestD = t;
      }
      return best;
    }

    function destroy() {
      for (const n of nodes) { n.halo.release(); scene.remove(n.mesh); }
      scene.remove(ring);
    }

    return { boss, nodes, state, update, rayNode, hitNode, destroy, totalHp };
  }

  SF.boss = { create, PHASES };
})(window.SF);
