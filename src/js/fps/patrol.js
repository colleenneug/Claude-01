/* ============================================================
   Patrol zones.

   The destinations are places rather than gauntlets. A population of
   hostiles lives in the zone and is topped up as you thin it out, and
   every so often a public event fires somewhere on the map: it is
   announced, it puts a marker in the world, and it runs on a timer
   whether or not you go to it. Nothing pushes you along. Extraction is
   always available.

   In co-op the host owns the population and the events and broadcasts
   the event state; hostiles ride the existing enemy snapshot.
   ============================================================ */
(function (SF) {
  'use strict';

  /* The zone is a kilometre across, so the population is streamed rather
     than placed: hostiles live in a band around the player, and anything
     that falls too far behind is retired so the count stays affordable.
     Event spawns are exempt — those belong to their site. */
  const SPAWN_MIN = 55;            // never populate on top of a player
  const SPAWN_MAX = 150;           // ...nor beyond sight of one
  const DESPAWN = 260;             // retire stragglers past this
  const RESPAWN_EVERY = 2.2;       // seconds between top-ups
  const EVENT_GAP = [55, 95];      // idle time between events
  const EVENT_TIME = 150;          // seconds to complete one

  /* Each event type says how to start it, how to tell if it is finished,
     and what it is worth. */
  const EVENTS = [
    {
      id: 'breach', name: 'SITE BREACH',
      line: 'Something is coming up out of the ground. Put it back.',
      build(c) {
        c.spawnGroup(c.anchor, 7, 0.15);
        return { kind: 'clear' };
      },
      progress: (c) => 1 - c.aliveTagged() / Math.max(1, c.tagged),
      done: (c) => c.aliveTagged() === 0,
      reward: 1
    },
    {
      id: 'elite', name: 'HIGH-VALUE TARGET',
      line: 'One of theirs is out in the open. It will not be for long.',
      build(c) {
        const boss = c.spawnOne(c.spec.faction.elite, c.anchor, 0);
        boss.maxHp = Math.round(boss.maxHp * 1.35);
        boss.hp = boss.maxHp;
        c.spawnGroup(c.anchor, 4, 0.5);
        return { kind: 'elite', bossUid: boss.uid };
      },
      progress: (c) => {
        const b = c.ai.byUid(c.data.bossUid);
        return b ? 1 - b.hp / b.maxHp : 1;
      },
      done: (c) => {
        const b = c.ai.byUid(c.data.bossUid);
        return !b || b.dead;
      },
      reward: 2
    },
    {
      id: 'relay', name: 'RELAY CAPTURE',
      line: 'The relay will answer if someone stands there long enough.',
      build(c) {
        c.spawnGroup(c.anchor, 5, 0.3);
        return { kind: 'capture', charge: 0 };
      },
      tick(c, dt, playerNear) {
        if (playerNear) c.data.charge = Math.min(1, c.data.charge + dt / 22);
        else c.data.charge = Math.max(0, c.data.charge - dt / 60);
        if (c.data.charge < 1 && Math.random() < dt * 0.22 && c.aliveTagged() < 8) {
          c.spawnGroup(c.anchor, 1, 0.7);
        }
      },
      progress: (c) => c.data.charge,
      done: (c) => c.data.charge >= 1,
      reward: 2,
      needsPresence: true
    }
  ];

  function create(ctx) {
    const { scene, level, ai, hud, player, spec, lights } = ctx;
    const hosting = ctx.hosting !== false;
    const R = level.bounds.maxX - 6;

    const state = {
      nextEvent: 20 + Math.random() * 20,
      event: null,          // { def, anchor, t, data, tagged, uids:Set }
      respawnT: RESPAWN_EVERY,
      completed: 0
    };

    /* ---------- event marker ---------- */
    const markerMat = new THREE.MeshBasicMaterial({
      color: 0xffb454, transparent: true, opacity: 0.32,
      blending: THREE.AdditiveBlending, depthWrite: false, side: THREE.DoubleSide
    });
    const marker = new THREE.Mesh(new THREE.CylinderGeometry(2.6, 2.6, 46, 18, 1, true), markerMat);
    marker.visible = false;
    scene.add(marker);
    const markerLight = lights.attach(0xffb454, 0, 30, { dynamic: false });

    /* ---------- helpers handed to event definitions ---------- */
    const anchors = level.eventAnchors || [];

    function pointNear(anchor, spread) {
      for (let i = 0; i < 20; i++) {
        const ang = Math.random() * Math.PI * 2;
        const d = 3 + Math.random() * (6 + spread * 18);
        const x = anchor.x + Math.cos(ang) * d;
        const z = anchor.z + Math.sin(ang) * d;
        if (Math.hypot(x, z) < R) return { x, z };
      }
      return { x: anchor.x, z: anchor.z };
    }

    function pickType() {
      const mix = spec.faction.mix;
      let roll = Math.random();
      for (const [t, w] of mix) { roll -= w; if (roll <= 0) return t; }
      return mix[0][0];
    }

    function spawnAt(type, x, z, tag) {
      const e = ai.spawn(type, x, z);
      e.maxHp = Math.round(e.maxHp * (ctx.hpScale || 1));
      e.hp = e.maxHp;
      if (tag && state.event) state.event.uids.add(e.uid);
      return e;
    }

    const helper = {
      ai: ai, spec: spec,
      get anchor() { return state.event.anchor; },
      get data() { return state.event.data; },
      get tagged() { return state.event.tagged; },
      spawnOne(type, anchor, spread) {
        const p = pointNear(anchor, spread);
        const e = spawnAt(type, p.x, p.z, true);
        e.alerted = true;
        state.event.tagged++;
        return e;
      },
      spawnGroup(anchor, n, spread) {
        for (let i = 0; i < n; i++) helper.spawnOne(pickType(), anchor, spread);
      },
      aliveTagged() {
        let n = 0;
        for (const uid of state.event.uids) {
          const e = ai.byUid(uid);
          if (e && !e.dead) n++;
        }
        return n;
      }
    };

    /* ---------- ambient population ---------- */
    function population() {
      return ai.enemies.filter((e) => !e.dead).length;
    }

    function topUp() {
      const cap = spec.faction.population;
      if (population() >= cap) return;
      for (let i = 0; i < 24; i++) {
        const ang = Math.random() * Math.PI * 2;
        const d = SPAWN_MIN + Math.random() * (SPAWN_MAX - SPAWN_MIN);
        const x = player.position.x + Math.cos(ang) * d;
        const z = player.position.z + Math.sin(ang) * d;
        if (Math.hypot(x, z) > R - 8) continue;          // keep inside the zone
        spawnAt(pickType(), x, z, false);
        return;
      }
    }

    /* Retire anything that has been left far behind, so walking across the
       zone does not drag its whole population along. */
    function cull() {
      for (const e of ai.enemies) {
        if (e.dead) continue;
        if (state.event && state.event.uids.has(e.uid)) continue;
        const d = Math.hypot(e.pos.x - player.position.x, e.pos.z - player.position.z);
        if (d > DESPAWN) ai.retire(e);
      }
    }

    /* ---------- events ---------- */
    function startEvent() {
      if (!anchors.length) return;
      const def = EVENTS[Math.floor(Math.random() * EVENTS.length)];
      const anchor = anchors[Math.floor(Math.random() * anchors.length)];

      state.event = { def: def, anchor: anchor, t: EVENT_TIME, data: {}, tagged: 0, uids: new Set() };
      state.event.data = def.build(helper) || {};

      marker.position.set(anchor.x, 22, anchor.z);
      marker.visible = true;
      markerLight.set(anchor.x, 6, anchor.z);
      markerLight.setIntensity(3.2);

      hud.event(def.name, def.line, EVENT_TIME, 0);
      hud.banner('PUBLIC EVENT — ' + def.name);
      SF.audio.sfx.alarm();
      broadcast();
    }

    function endEvent(won) {
      if (!state.event) return;
      const def = state.event.def;
      if (won) {
        state.completed++;
        hud.banner('EVENT COMPLETE');
        SF.audio.sfx.win();
        if (ctx.onReward) ctx.onReward(def.reward, def.name);
      } else {
        hud.banner('EVENT LOST');
        SF.audio.sfx.lose();
      }
      // anything still tagged wanders off into the general population
      state.event = null;
      marker.visible = false;
      markerLight.setIntensity(0);
      hud.clearEvent();
      state.nextEvent = EVENT_GAP[0] + Math.random() * (EVENT_GAP[1] - EVENT_GAP[0]);
      broadcast();
    }

    /* Clients are told what the event is, not asked to simulate it. */
    function broadcast() {
      if (!ctx.net || !ctx.net.active || !hosting) return;
      const e = state.event;
      ctx.net.send({ t: 'event', d: { kind: 'patrol', ev: e ? {
        id: e.def.id, name: e.def.name, line: e.def.line,
        t: Math.round(e.t), p: +e.def.progress(helper).toFixed(2),
        x: e.anchor.x, z: e.anchor.z
      } : null } });
    }

    function applyRemote(ev) {
      if (ev) {
        marker.position.set(ev.x, 22, ev.z);
        marker.visible = true;
        markerLight.set(ev.x, 6, ev.z);
        markerLight.setIntensity(3.2);
        hud.event(ev.name, ev.line, ev.t, ev.p);
        state.remoteAnchor = { x: ev.x, z: ev.z };
      } else {
        marker.visible = false;
        markerLight.setIntensity(0);
        hud.clearEvent();
        state.remoteAnchor = null;
      }
    }

    let netAccum = 0;

    function update(dt) {
      marker.rotation.y += dt * 0.25;

      // distance readout works for host and client alike
      const a = state.event ? state.event.anchor : state.remoteAnchor;
      if (a) {
        hud.eventDistance(Math.round(Math.hypot(a.x - player.position.x, a.z - player.position.z)));
      }

      if (!hosting) return;

      state.respawnT -= dt;
      if (state.respawnT <= 0) { state.respawnT = RESPAWN_EVERY; cull(); topUp(); topUp(); }

      if (state.event) {
        const e = state.event;
        e.t -= dt;
        const near = Math.hypot(e.anchor.x - player.position.x,
                                e.anchor.z - player.position.z) < 9;
        if (e.def.tick) e.def.tick(helper, dt, near);

        const progress = e.def.progress(helper);
        hud.event(e.def.name, e.def.line, Math.max(0, e.t), progress);

        netAccum += dt;
        if (netAccum > 1) { netAccum = 0; broadcast(); }

        if (e.def.done(helper)) endEvent(true);
        else if (e.t <= 0) endEvent(false);
      } else {
        state.nextEvent -= dt;
        if (state.nextEvent <= 0) startEvent();
      }
    }

    function destroy() {
      markerLight.release();
      scene.remove(marker);
    }

    /* Seed the band around the landing pad so the zone is already inhabited. */
    if (hosting) for (let i = 0; i < spec.faction.population; i++) topUp();

    return { update, destroy, applyRemote, state,
             get eventName() { return state.event ? state.event.def.name : null; } };
  }

  SF.patrol = { create, EVENTS };
})(window.SF);
