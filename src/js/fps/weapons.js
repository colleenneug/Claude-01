/* ============================================================
   Weapons: one per doctrine, plus that doctrine's field ability.

   Fire is hitscan — a ray from the camera, tested against enemy hit
   spheres first and the level second, so shots stop at walls. Every
   weapon carries its own spread curve, recoil pattern and viewmodel.
   ============================================================ */
(function (SF) {
  'use strict';

  const WEAPONS = {
    bulwark: {
      id: 'maul', name: 'MAUL-12', kind: 'BREACHING SHOTGUN',
      damage: 17, pellets: 8, rpm: 75, mag: 6, reserve: 48, reload: 2.6,
      spread: 0.055, adsSpread: 0.036, range: 34, headMult: 1.6,
      recoil: { pitch: 0.075, yaw: 0.012 }, shake: 1.5,
      colour: 0xffb454, tracer: 0xffc46a,
      desc: 'Eight pellets a shell. Devastating inside ten metres, useless past thirty.'
    },
    oracle: {
      id: 'lance', name: 'ARC LANCE', kind: 'INDUCTION RIFLE',
      damage: 26, pellets: 1, rpm: 320, mag: 24, reserve: 168, reload: 2.0,
      spread: 0.012, adsSpread: 0.002, range: 90, headMult: 2.2, pierce: true,
      recoil: { pitch: 0.022, yaw: 0.004 }, shake: 0.5,
      colour: 0x5eeaff, tracer: 0x9ff4ff,
      desc: 'Charged induction bolts. Punches through a target and into whatever stood behind it.'
    },
    wraith: {
      id: 'whisper', name: 'WHISPER', kind: 'SUPPRESSED CARBINE',
      damage: 15, pellets: 1, rpm: 640, mag: 32, reserve: 224, reload: 1.7,
      spread: 0.02, adsSpread: 0.005, range: 70, headMult: 3.0,
      recoil: { pitch: 0.014, yaw: 0.006 }, shake: 0.35,
      colour: 0xff5ea8, tracer: 0xff9ccb,
      desc: 'Subsonic and quiet. Triple damage on a head shot; the ark never hears the first one.'
    }
  };

  const ABILITIES = {
    bulwark: { name: 'AEGIS BARRIER', cooldown: 16, desc: 'Overshield that soaks the next wave of fire.' },
    oracle:  { name: 'SYSTEMS BREACH', cooldown: 14, desc: 'EMP pulse: stuns and staggers everything nearby.' },
    wraith:  { name: 'PHASE STEP',    cooldown: 10, desc: 'Blink forward; briefly untargetable, next shot crits.' }
  };

  function create(ctx) {
    const { scene, camera, player, ai, hud, lights } = ctx;
    /* The issued weapon is the baseline; the equipped item multiplies it. */
    const base = WEAPONS[ctx.classId];
    const mods = ctx.mods || { damage: 1, mag: 1, rpm: 1, reload: 1, spread: 1 };
    const spec = Object.assign({}, base, {
      damage: base.damage * mods.damage,
      mag: Math.max(1, Math.round(base.mag * mods.mag)),
      reserve: Math.round(base.reserve * mods.mag),
      rpm: Math.round(base.rpm * mods.rpm),
      reload: base.reload * mods.reload,
      spread: base.spread * mods.spread,
      adsSpread: base.adsSpread * mods.spread,
      displayName: ctx.itemName || base.name
    });
    const ability = ABILITIES[ctx.classId];

    const w = {
      spec, ability,
      ammo: spec.mag, reserve: spec.reserve,
      reloading: false, reloadT: 0,
      cool: 0, ads: false, adsAmount: 0,
      abilityCool: 0, abilityActive: 0,
      primed: false,             // wraith: guaranteed crit after a phase step
      shots: 0, hits: 0, kills: 0, headshots: 0
    };

    /* ---------- viewmodel ---------- */
    const view = new THREE.Group();
    camera.add(view);
    scene.add(camera);

    const bodyMat = new THREE.MeshStandardMaterial({ color: 0x24282f, metalness: 0.9, roughness: 0.42 });
    const accentMat = new THREE.MeshStandardMaterial({
      color: 0x0a0d12, emissive: new THREE.Color(spec.colour), emissiveIntensity: 1.6,
      metalness: 0.6, roughness: 0.4
    });

    const receiver = new THREE.Mesh(new THREE.BoxGeometry(0.1, 0.12, 0.42), bodyMat);
    receiver.position.set(0, 0, -0.1);
    const barrel = new THREE.Mesh(new THREE.CylinderGeometry(0.028, 0.032, 0.44, 12), bodyMat);
    barrel.rotation.x = Math.PI / 2;
    barrel.position.set(0, 0.012, -0.42);
    const grip = new THREE.Mesh(new THREE.BoxGeometry(0.07, 0.17, 0.09), bodyMat);
    grip.position.set(0, -0.13, 0.02);
    grip.rotation.x = -0.24;
    const stripe = new THREE.Mesh(new THREE.BoxGeometry(0.022, 0.022, 0.3), accentMat);
    stripe.position.set(0.052, 0.03, -0.14);
    const sight = new THREE.Mesh(new THREE.BoxGeometry(0.03, 0.05, 0.03), accentMat);
    sight.position.set(0, 0.086, -0.26);
    view.add(receiver, barrel, grip, stripe, sight);

    const HIP = new THREE.Vector3(0.19, -0.16, -0.42);
    const AIM = new THREE.Vector3(0, -0.082, -0.3);
    view.position.copy(HIP);

    /* muzzle flash: a pooled light plus a billboard, flicked on for a frame or two */
    const flashLight = lights.attach(spec.colour, 0, 10);
    const flashMat = new THREE.SpriteMaterial({
      color: spec.colour, transparent: true, opacity: 0,
      blending: THREE.AdditiveBlending, depthWrite: false
    });
    const flash = new THREE.Sprite(flashMat);
    flash.scale.set(0.5, 0.5, 0.5);
    flash.position.set(0, 0.02, -0.68);
    view.add(flash);

    /* ---------- transient effects, all pre-allocated ----------
       Firing must not allocate: building geometry or materials mid-burst
       shows up as a hitch, and adding lights would recompile every shader
       in the scene. Everything below is built once and recycled. */
    const TRACERS = 32, SPARKS = 64;

    const tracers = [];
    for (let i = 0; i < TRACERS; i++) {
      const geo = new THREE.BufferGeometry();
      geo.setAttribute('position', new THREE.BufferAttribute(new Float32Array(6), 3));
      const line = new THREE.Line(geo, new THREE.LineBasicMaterial({
        color: spec.tracer, transparent: true, opacity: 0,
        blending: THREE.AdditiveBlending, depthWrite: false
      }));
      line.visible = false;
      line.frustumCulled = false;
      scene.add(line);
      tracers.push({ line, life: 0 });
    }
    let tracerNext = 0;

    const sparkGeo = new THREE.SphereGeometry(0.035, 6, 6);
    const sparkMat = new THREE.MeshBasicMaterial({
      color: 0xffd9a0, transparent: true, opacity: 0.95,
      blending: THREE.AdditiveBlending, depthWrite: false
    });
    const sparks = [];
    for (let i = 0; i < SPARKS; i++) {
      const m = new THREE.Mesh(sparkGeo, sparkMat);
      m.visible = false;
      m.frustumCulled = false;
      scene.add(m);
      sparks.push({ mesh: m, life: 0, vel: new THREE.Vector3() });
    }
    let sparkNext = 0;

    function spawnTracer(from, to) {
      const t = tracers[tracerNext];
      tracerNext = (tracerNext + 1) % TRACERS;
      const pos = t.line.geometry.attributes.position;
      pos.setXYZ(0, from.x, from.y, from.z);
      pos.setXYZ(1, to.x, to.y, to.z);
      pos.needsUpdate = true;
      t.life = 0.06;
      t.line.visible = true;
      t.line.material.opacity = 0.9;
    }

    function spawnImpact(point, normal, colour) {
      for (let i = 0; i < 4; i++) {
        const s = sparks[sparkNext];
        sparkNext = (sparkNext + 1) % SPARKS;
        s.mesh.position.copy(point);
        s.mesh.scale.setScalar(1);
        s.mesh.visible = true;
        s.life = 0.32;
        s.vel.set(
          normal.x + (Math.random() - 0.5) * 1.6,
          normal.y + Math.random() * 1.4,
          normal.z + (Math.random() - 0.5) * 1.6
        ).multiplyScalar(2.4);
      }
      lights.flash(point.x, point.y, point.z, colour || 0xffb066, 2.2, 5, 0.12);
    }

    /* ---------- firing ---------- */
    let muzzle = 0;                              // muzzle-flash decay, 1 -> 0
    const muzzleWorld = new THREE.Vector3();
    const muzzleDir = new THREE.Vector3();
    const shotOrigin = new THREE.Vector3();
    const shotDir = new THREE.Vector3();
    const pelletDir = new THREE.Vector3();
    const tracerFrom = new THREE.Vector3();
    const IMPACT_N = new THREE.Vector3();
    const camRight = new THREE.Vector3();
    const camUp = new THREE.Vector3();

    function fire() {
      if (w.reloading || w.cool > 0) return;
      if (w.ammo <= 0) { SF.audio.sfx.dryfire(); w.cool = 0.25; return; }

      w.ammo--;
      w.cool = 60 / spec.rpm;
      w.shots++;

      const spread = (w.ads ? spec.adsSpread : spec.spread) * (player.state.sprinting ? 2.1 : 1);
      const eye = player.eyePosition;
      shotOrigin.set(eye.x, eye.y, eye.z);
      camera.getWorldDirection(shotDir);
      camera.matrixWorld.extractBasis(camRight, camUp, muzzleDir);
      tracerFrom.copy(shotOrigin)
        .addScaledVector(shotDir, 0.75)
        .addScaledVector(camRight, w.adsAmount > 0.5 ? 0.0 : 0.17)
        .addScaledVector(camUp, w.adsAmount > 0.5 ? -0.02 : -0.13);

      let anyHit = false, anyHead = false;
      for (let p = 0; p < spec.pellets; p++) {
        pelletDir.copy(shotDir);
        pelletDir.x += (Math.random() - 0.5) * spread * 2;
        pelletDir.y += (Math.random() - 0.5) * spread * 2;
        pelletDir.z += (Math.random() - 0.5) * spread * 2;
        pelletDir.normalize();
        const dir = pelletDir;

        /* A resonance node in front of everything else takes the shot: the
           boss's shield is opened by playing its phrase back, not by damage. */
        const node = ctx.rayNode && ctx.rayNode(shotOrigin, pelletDir, spec.range);
        if (node) {
          spawnTracer(tracerFrom, node.point);
          spawnImpact(node.point, IMPACT_N.copy(dir).negate(), 0xbfe9ff);
          ctx.onNode(node.node.i);
          anyHit = true;
          continue;
        }

        const shot = ai.raycast(shotOrigin, dir, spec.range, ctx.level.colliders, spec.pierce);
        const end = shot.point || shotOrigin.clone().addScaledVector(dir, spec.range);
        spawnTracer(tracerFrom, end);

        if (shot.enemies && shot.enemies.length) {
          for (const h of shot.enemies) {
            let dmg = spec.damage * (h.head ? spec.headMult : 1);
            if (w.primed) { dmg *= 2; w.primed = false; }
            const dead = ai.damage(h.enemy, dmg, dir);
            anyHit = true;
            if (h.head) anyHead = true;
            if (dead) { w.kills++; hud.killFeed(h.enemy.spec.name); }
            spawnImpact(h.point, IMPACT_N.copy(dir).negate(), 0xff5a6a);
          }
        } else if (shot.point) {
          spawnImpact(shot.point, shot.normal || IMPACT_N.copy(dir).negate(), 0xffc07a);
        }
      }

      if (anyHit) { w.hits++; hud.hitMarker(anyHead); if (anyHead) w.headshots++; }

      player.addRecoil(spec.recoil.pitch * (w.ads ? 0.6 : 1),
                       (Math.random() - 0.5) * spec.recoil.yaw * 2);
      player.state.shake = Math.max(player.state.shake, spec.shake * 0.5);

      muzzle = 1;
      flashMat.opacity = 0.95;
      flash.material.rotation = Math.random() * Math.PI;
      view.position.z += 0.055;

      SF.audio.sfx[spec.id === 'whisper' ? 'shotQuiet' : spec.id === 'lance' ? 'shotEnergy' : 'shotHeavy']();
      hud.refreshAmmo(w);
    }

    function reload() {
      if (w.reloading || w.ammo >= spec.mag || w.reserve <= 0) return;
      w.reloading = true;
      w.reloadT = spec.reload;
      SF.audio.sfx.reload();
    }

    function useAbility() {
      if (w.abilityCool > 0) return;
      w.abilityCool = ability.cooldown;

      if (ctx.classId === 'bulwark') {
        ctx.onOvershield(60);
        SF.audio.sfx.shield();
        hud.banner('AEGIS BARRIER');
      } else if (ctx.classId === 'oracle') {
        const hitCount = ai.pulse(player.position, 13, 26);
        SF.audio.sfx.emp();
        player.state.shake = 2.2;
        hud.banner('SYSTEMS BREACH — ' + hitCount + ' STUNNED');
      } else {
        const forward = muzzleDir;
        camera.getWorldDirection(forward);
        forward.y = 0; forward.normalize();
        const dest = player.position.clone().addScaledVector(forward, 6.5);
        player.state.pos.x = dest.x; player.state.pos.z = dest.z;
        w.primed = true;
        ctx.onPhase(1.4);
        SF.audio.sfx.phase();
        hud.banner('PHASE STEP — NEXT SHOT PRIMED');
      }
      hud.refreshAbility(w);
    }

    /* ---------- per-frame ---------- */
    function update(dt, firing) {
      w.cool = Math.max(0, w.cool - dt);
      w.abilityCool = Math.max(0, w.abilityCool - dt);

      if (w.reloading) {
        w.reloadT -= dt;
        if (w.reloadT <= 0) {
          const need = spec.mag - w.ammo;
          const take = Math.min(need, w.reserve);
          w.ammo += take; w.reserve -= take;
          w.reloading = false;
          hud.refreshAmmo(w);
        }
      } else if (firing) {
        fire();
      }

      // aim-down-sights blend, and the FOV pull that goes with it
      w.adsAmount += ((w.ads && !player.state.sprinting ? 1 : 0) - w.adsAmount) * Math.min(1, 14 * dt);
      const targetPos = HIP.clone().lerp(AIM, w.adsAmount);
      const sway = player.state.bobAmount * (1 - w.adsAmount);
      targetPos.x += Math.cos(player.state.bob * 0.5) * 0.02 * sway;
      targetPos.y += Math.sin(player.state.bob) * 0.016 * sway;
      if (player.state.sprinting) { targetPos.y -= 0.05; targetPos.z += 0.06; }
      view.position.lerp(targetPos, Math.min(1, 16 * dt));
      view.rotation.z = player.state.sprinting ? -0.35 : -0.04 * (1 - w.adsAmount);

      const targetFov = 75 - 16 * w.adsAmount;
      if (Math.abs(camera.fov - targetFov) > 0.05) {
        camera.fov += (targetFov - camera.fov) * Math.min(1, 12 * dt);
        camera.updateProjectionMatrix();
      }

      muzzle *= Math.max(0, 1 - 22 * dt);
      flashMat.opacity *= Math.max(0, 1 - 26 * dt);
      // the muzzle light rides just in front of the barrel, in world space
      camera.getWorldPosition(muzzleWorld);
      camera.getWorldDirection(muzzleDir);
      muzzleWorld.addScaledVector(muzzleDir, 0.8);
      flashLight.set(muzzleWorld.x, muzzleWorld.y, muzzleWorld.z);
      flashLight.setIntensity(muzzle * 5.5);

      for (const t of tracers) {
        if (t.life <= 0) continue;
        t.life -= dt;
        if (t.life <= 0) { t.line.visible = false; continue; }
        t.line.material.opacity = (t.life / 0.06) * 0.9;
      }
      for (const s of sparks) {
        if (s.life <= 0) continue;
        s.life -= dt;
        if (s.life <= 0) { s.mesh.visible = false; continue; }
        s.mesh.position.addScaledVector(s.vel, dt);
        s.vel.y -= 9 * dt;
        s.mesh.scale.setScalar(s.life / 0.32);   // per-spark fade, material is shared
      }
    }

    /* Three compiles a shader and uploads geometry the first time an object
       is actually rendered. For pooled tracers and sparks that would land on
       the first shot, as a hitch. Show them all for one frame up front so the
       cost is paid during loading instead. */
    function prewarm(renderer, sceneRef, cameraRef) {
      const eye = player.eyePosition;
      for (const t of tracers) {
        const pos = t.line.geometry.attributes.position;
        pos.setXYZ(0, eye.x, eye.y - 0.2, eye.z);
        pos.setXYZ(1, eye.x + 0.01, eye.y - 0.2, eye.z + 0.01);
        pos.needsUpdate = true;
        t.line.material.opacity = 0.001;
        t.line.visible = true;
      }
      for (const s2 of sparks) {
        s2.mesh.position.set(eye.x, eye.y - 0.2, eye.z);
        s2.mesh.visible = true;
      }
      sparkMat.opacity = 0.001;
      renderer.compile(sceneRef, cameraRef);
      renderer.render(sceneRef, cameraRef);
      for (const t of tracers) { t.line.visible = false; t.life = 0; }
      for (const s2 of sparks) { s2.mesh.visible = false; s2.life = 0; }
    }

    return {
      state: w, spec, ability, view, prewarm,
      update, fire, reload, useAbility,
      setAds(v) { w.ads = v; },
      addReserve(n) { w.reserve = Math.min(spec.reserve, w.reserve + n); hud.refreshAmmo(w); }
    };
  }

  SF.weapons = { create, WEAPONS, ABILITIES };
})(window.SF);
