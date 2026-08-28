/* ============================================================
   Other operatives, drawn in your world.

   Snapshots arrive at about 15 Hz, so every remote body is rendered
   at an interpolated position a fraction of a second behind the wire
   — chasing raw packets directly reads as teleporting. Each carries a
   nameplate that turns to face you.
   ============================================================ */
(function (SF) {
  'use strict';

  const LERP = 12;          // position smoothing
  const NAME_SCALE = 0.0042;

  function nameplate(text, colour) {
    const cv = document.createElement('canvas');
    cv.width = 512; cv.height = 128;
    const g = cv.getContext('2d');
    g.font = '600 54px "Chakra Petch", system-ui, sans-serif';
    g.textAlign = 'center';
    g.textBaseline = 'middle';
    g.lineWidth = 8;
    g.strokeStyle = 'rgba(0,0,0,.85)';
    g.strokeText(text, 256, 64);
    g.fillStyle = colour;
    g.fillText(text, 256, 64);

    const tex = new THREE.CanvasTexture(cv);
    tex.encoding = THREE.sRGBEncoding;
    const sprite = new THREE.Sprite(new THREE.SpriteMaterial({
      map: tex, transparent: true, depthTest: false, depthWrite: false
    }));
    sprite.scale.set(512 * NAME_SCALE, 128 * NAME_SCALE, 1);
    return sprite;
  }

  function create(ctx) {
    const { scene, lights } = ctx;
    const bodies = new Map();          // playerId -> body

    function build(info) {
      const cls = SF.classes.CLASSES[info.cls] || SF.classes.CLASSES.bulwark;
      const G = SF.gear;
      const look = info.look || G.defaultLook();

      const group = new THREE.Group();
      const suit = new THREE.MeshStandardMaterial({
        color: new THREE.Color(cls.accent).multiplyScalar(0.34),
        roughness: 0.6, metalness: 0.4
      });
      const skin = new THREE.MeshStandardMaterial({
        color: G.SKINS[look.skin % G.SKINS.length], roughness: 0.75
      });
      const hair = new THREE.MeshStandardMaterial({
        color: G.HAIR_COLOURS[look.hairColour % G.HAIR_COLOURS.length], roughness: 0.85
      });

      const torso = new THREE.Mesh(new THREE.CapsuleGeometry(0.26, 0.44, 4, 12), suit);
      torso.position.y = 1.13;
      const head = new THREE.Mesh(new THREE.SphereGeometry(0.22, 16, 14), skin);
      head.position.y = 1.6;
      const cap = new THREE.Mesh(new THREE.SphereGeometry(0.232, 16, 12,
        0, Math.PI * 2, 0, Math.PI * 0.5), hair);
      cap.position.y = 1.6;
      const hips = new THREE.Mesh(new THREE.CapsuleGeometry(0.21, 0.1, 3, 10), suit);
      hips.position.y = 0.82;

      const limbs = [];
      for (const side of [-1, 1]) {
        const arm = new THREE.Mesh(new THREE.CapsuleGeometry(0.08, 0.46, 3, 8), suit);
        arm.position.set(side * 0.34, 1.14, 0);
        const leg = new THREE.Mesh(new THREE.CapsuleGeometry(0.1, 0.5, 3, 8), suit);
        leg.position.set(side * 0.13, 0.44, 0);
        group.add(arm, leg);
        limbs.push({ arm, leg, side });
      }

      // a stubby rifle so it is obvious which way they are facing
      const gun = new THREE.Mesh(new THREE.BoxGeometry(0.08, 0.1, 0.5),
        new THREE.MeshStandardMaterial({ color: 0x20242c, metalness: 0.85, roughness: 0.4 }));
      gun.position.set(0.22, 1.16, -0.34);

      group.add(torso, head, cap, hips, gun);
      for (const c of group.children) { c.castShadow = true; }

      const plate = nameplate(info.name + (info.isHost ? '  ★' : ''), cls.accent);
      plate.position.y = 2.12;
      group.add(plate);

      scene.add(group);
      const halo = lights.attach(new THREE.Color(cls.accent).getHex(), 0.7, 6, { dynamic: false });

      return {
        group, limbs, gun, halo, plate,
        pos: new THREE.Vector3(), target: new THREE.Vector3(),
        yaw: 0, targetYaw: 0, bob: 0, firing: 0, hp: 100
      };
    }

    /* Sync the set of bodies with the roster. */
    function setRoster(list) {
      const seen = new Set();
      for (const info of list) {
        seen.add(info.id);
        if (!bodies.has(info.id)) bodies.set(info.id, build(info));
      }
      for (const [id, b] of Array.from(bodies.entries())) {
        if (seen.has(id)) continue;
        b.halo.release();
        scene.remove(b.group);
        bodies.delete(id);
      }
    }

    function update(dt, players, cameraPos) {
      for (const [id, b] of bodies) {
        const info = players.get(id);
        const s = info && info.state;
        if (s) {
          b.target.set(s.p[0], s.p[1], s.p[2]);
          b.targetYaw = s.y;
          b.hp = s.h;
          if (s.f) b.firing = 0.12;
        }

        b.pos.lerp(b.target, Math.min(1, LERP * dt));
        b.group.position.copy(b.pos);

        // shortest-way-round yaw
        let d = b.targetYaw - b.yaw;
        while (d > Math.PI) d -= Math.PI * 2;
        while (d < -Math.PI) d += Math.PI * 2;
        b.yaw += d * Math.min(1, 12 * dt);
        b.group.rotation.y = b.yaw;

        const speed = b.pos.distanceTo(b.target);
        b.bob += dt * (2 + speed * 26);
        const swing = Math.min(0.7, speed * 8);
        for (const l of b.limbs) {
          l.arm.rotation.x = Math.sin(b.bob) * swing * l.side;
          l.leg.rotation.x = -Math.sin(b.bob) * swing * l.side;
        }

        b.firing = Math.max(0, b.firing - dt);
        b.gun.position.z = -0.34 + b.firing * 0.3;

        b.halo.set(b.pos.x, b.pos.y + 1.2, b.pos.z);
        b.group.visible = b.hp > 0;
        if (cameraPos) b.plate.lookAt(cameraPos);
      }
    }

    function clear() {
      for (const b of bodies.values()) { b.halo.release(); scene.remove(b.group); }
      bodies.clear();
    }

    return { setRoster, update, clear, bodies };
  }

  SF.remote = { create };
})(window.SF);
