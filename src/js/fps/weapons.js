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
    const { scene, camera, player, ai, hud } = ctx;
    const spec = WEAPONS[ctx.classId];
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

    /* muzzle flash: a light plus a billboard, both flicked on for a frame or two */
    const flashLight = new THREE.PointLight(spec.colour, 0, 9, 2);
    flashLight.position.set(0, 0.02, -0.66);
    view.add(flashLight);
    const flashMat = new THREE.SpriteMaterial({
      color: spec.colour, transparent: true, opacity: 0,
      blending: THREE.AdditiveBlending, depthWrite: false
    });
    const flash = new THREE.Sprite(flashMat);
    flash.scale.set(0.5, 0.5, 0.5);
    flash.position.set(0, 0.02, -0.68);
    view.add(flash);

    /* ---------- transient effects ---------- */
    const tracers = [];
    const impacts = [];
    const tracerMat = new THREE.LineBasicMaterial({
      color: spec.tracer, transparent: true, opacity: 0.9, blending: THREE.AdditiveBlending
    });
    const sparkGeo = new THREE.SphereGeometry(0.035, 6, 6);

    function spawnTracer(from, to) {
      const geo = new THREE.BufferGeometry().setFromPoints([from, to]);
      const line = new THREE.Line(geo, tracerMat.clone());
      scene.add(line);
      tracers.push({ line, life: 0.06 });
    }

    function spawnImpact(point, normal, colour) {
      const mat = new THREE.MeshBasicMaterial({
        color: colour || 0xffd9a0, transparent: true, blending: THREE.AdditiveBlending
      });
      for (let i = 0; i < 5; i++) {
        const s = new THREE.Mesh(sparkGeo, mat);
        s.position.copy(point);
        scene.add(s);
        impacts.push({
          mesh: s, life: 0.32,
          vel: new THREE.Vector3(
            normal.x + (Math.random() - 0.5) * 1.6,
            normal.y + Math.random() * 1.4,
            normal.z + (Math.random() - 0.5) * 1.6
          ).multiplyScalar(2.4)
        });
      }
      const l = new THREE.PointLight(colour || 0xffb066, 1.6, 4, 2);
      l.position.copy(point);
      scene.add(l);
      impacts.push({ mesh: l, life: 0.1, vel: new THREE.Vector3(), isLight: true });
    }

    /* ---------- firing ---------- */
    const raycaster = new THREE.Raycaster();
    raycaster.far = spec.range;

    function fire() {
      if (w.reloading || w.cool > 0) return;
      if (w.ammo <= 0) { SF.audio.sfx.dryfire(); w.cool = 0.25; return; }

      w.ammo--;
      w.cool = 60 / spec.rpm;
      w.shots++;

      const spread = (w.ads ? spec.adsSpread : spec.spread) * (player.state.sprinting ? 2.1 : 1);
      const origin = player.eyePosition;
      const forward = new THREE.Vector3();
      camera.getWorldDirection(forward);

      let anyHit = false, anyHead = false;
      for (let p = 0; p < spec.pellets; p++) {
        const dir = forward.clone();
        dir.x += (Math.random() - 0.5) * spread * 2;
        dir.y += (Math.random() - 0.5) * spread * 2;
        dir.z += (Math.random() - 0.5) * spread * 2;
        dir.normalize();

        const shot = ai.raycast(origin, dir, spec.range, ctx.level.colliders, spec.pierce);
        const end = shot.point || origin.clone().add(dir.multiplyScalar(spec.range));
        spawnTracer(origin.clone().add(forward.clone().multiplyScalar(0.6)), end);

        if (shot.enemies && shot.enemies.length) {
          for (const h of shot.enemies) {
            let dmg = spec.damage * (h.head ? spec.headMult : 1);
            if (w.primed) { dmg *= 2; w.primed = false; }
            const dead = ai.damage(h.enemy, dmg, dir);
            anyHit = true;
            if (h.head) anyHead = true;
            if (dead) { w.kills++; hud.killFeed(h.enemy.spec.name); }
            spawnImpact(h.point, dir.clone().negate(), 0xff5a6a);
          }
        } else if (shot.point) {
          spawnImpact(shot.point, shot.normal || dir.clone().negate(), 0xffc07a);
        }
      }

      if (anyHit) { w.hits++; hud.hitMarker(anyHead); if (anyHead) w.headshots++; }

      player.addRecoil(spec.recoil.pitch * (w.ads ? 0.6 : 1),
                       (Math.random() - 0.5) * spec.recoil.yaw * 2);
      player.state.shake = Math.max(player.state.shake, spec.shake);

      flashLight.intensity = 4.5;
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
        const forward = new THREE.Vector3();
        camera.getWorldDirection(forward);
        forward.y = 0; forward.normalize();
        const dest = player.position.clone().add(forward.multiplyScalar(6.5));
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

      flashLight.intensity *= Math.max(0, 1 - 22 * dt);
      flashMat.opacity *= Math.max(0, 1 - 26 * dt);

      for (let i = tracers.length - 1; i >= 0; i--) {
        const t = tracers[i];
        t.life -= dt;
        t.line.material.opacity = Math.max(0, t.life / 0.06) * 0.9;
        if (t.life <= 0) { scene.remove(t.line); t.line.geometry.dispose(); tracers.splice(i, 1); }
      }
      for (let i = impacts.length - 1; i >= 0; i--) {
        const s = impacts[i];
        s.life -= dt;
        if (!s.isLight) {
          s.mesh.position.addScaledVector(s.vel, dt);
          s.vel.y -= 9 * dt;
          s.mesh.material.opacity = Math.max(0, s.life / 0.32);
        } else {
          s.mesh.intensity = Math.max(0, s.life / 0.1) * 1.6;
        }
        if (s.life <= 0) { scene.remove(s.mesh); impacts.splice(i, 1); }
      }
    }

    return {
      state: w, spec, ability, view,
      update, fire, reload, useAbility,
      setAds(v) { w.ads = v; },
      addReserve(n) { w.reserve += n; hud.refreshAmmo(w); }
    };
  }

  SF.weapons = { create, WEAPONS, ABILITIES };
})(window.SF);
