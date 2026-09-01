/* ============================================================
   Your own operative, seen from outside.

   Third person on foot: the camera pulls back and this stands where
   you stand. It walks when you walk, crouches when you crouch, and
   turns with your look — but only in yaw, because a body that pitches
   with the camera looks like it is falling over.

   The shot still leaves through the crosshair: see shotOrigin, which
   returns a point on the camera's centre line rather than the eye.
   ============================================================ */
(function (SF) {
  'use strict';

  const CHASE_DIST = 3.1;
  const CHASE_LIFT = 0.45;

  function create(ctx) {
    const { scene, camera, player, character } = ctx;

    const built = SF.avatar.build({ cls: character.cls, look: character.look,
                                    livery: SF.cosmetics.suitColours(character) });
    const group = built.group;
    group.visible = false;
    scene.add(group);

    let on = false;
    let phase = 0;
    let amount = 0;

    function setEnabled(v) {
      on = !!v;
      group.visible = on;
      player.setChase(on ? CHASE_DIST : 0, on ? CHASE_LIFT : 0);
      if (ctx.onToggle) ctx.onToggle(on);
    }

    /* A muzzle on the camera's centre line, so third person aims where the
       crosshair points instead of where the eye happens to be. */
    const originV = new THREE.Vector3();
    const fwdV = new THREE.Vector3();
    function shotOrigin() {
      if (!on) return null;
      camera.getWorldPosition(originV);
      camera.getWorldDirection(fwdV);
      return originV.addScaledVector(fwdV, player.chaseDistance + 0.7);
    }

    function update(dt) {
      if (!on) return;
      const s = player.state;
      group.position.set(s.pos.x, s.pos.y, s.pos.z);
      group.rotation.y = s.yaw;

      const moving = Math.hypot(s.vel.x, s.vel.z);
      amount += (Math.min(1, moving / 5) - amount) * Math.min(1, 9 * dt);
      phase += dt * (4 + moving * 0.9);
      SF.avatar.stride(built.limbs, phase, amount * 0.7);

      // crouching folds the whole body down rather than bending the knees
      const squat = s.crouching ? 0.66 : 1;
      group.scale.y += (squat - group.scale.y) * Math.min(1, 12 * dt);
      // and a small forward lean under speed
      group.rotation.x = -Math.min(0.12, moving * 0.008);
    }

    function destroy() {
      player.setChase(0, 0);
      scene.remove(group);
      group.traverse((o) => {
        if (o.geometry) o.geometry.dispose();
        if (o.material) o.material.dispose();
      });
    }

    return {
      update, destroy, setEnabled, shotOrigin,
      toggle() { setEnabled(!on); return on; },
      get enabled() { return on; }
    };
  }

  SF.body = { create };
})(window.SF);
