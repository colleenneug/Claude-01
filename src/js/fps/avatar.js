/* ============================================================
   The operative's body.

   One build, used everywhere a person has to be visible: a squadmate
   across the room, the rider on a runner, and your own operative when
   you drop into third person. Class colour on the suit, the look you
   chose on the skin and hair.

   Limbs come back in a list so a caller can animate them; the build
   itself is static.
   ============================================================ */
(function (SF) {
  'use strict';

  function build(info) {
    const G = SF.gear;
    const cls = SF.classes.CLASSES[info.cls] || SF.classes.CLASSES.bulwark;
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

    group.add(torso, head, cap, hips);

    /* A stubby rifle, so it is obvious which way a body is facing. Optional:
       a rider has both hands on the frame. */
    let gun = null;
    if (info.gun !== false) {
      gun = new THREE.Mesh(new THREE.BoxGeometry(0.08, 0.1, 0.5),
        new THREE.MeshStandardMaterial({ color: 0x20242c, metalness: 0.85, roughness: 0.4 }));
      gun.position.set(0.22, 1.16, -0.34);
      group.add(gun);
    }

    for (const c of group.children) c.castShadow = true;
    return { group, limbs, gun, torso, head, cap, hips, materials: [suit, skin, hair] };
  }

  /* The walk cycle every body shares: legs and arms counter-swinging at a
     rate set by how fast the body is actually moving. */
  function stride(limbs, phase, amount) {
    for (const l of limbs) {
      const swing = Math.sin(phase + (l.side > 0 ? Math.PI : 0)) * amount;
      l.leg.rotation.x = swing;
      l.arm.rotation.x = -swing * 0.7;
    }
  }

  SF.avatar = { build, stride };
})(window.SF);
