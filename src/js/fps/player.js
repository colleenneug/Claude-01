/* ============================================================
   First-person controller: pointer-lock look, accelerated ground
   movement with friction, gravity, AABB collision against the level,
   crouch, sprint, and the head motion that sells all of it.
   ============================================================ */
(function (SF) {
  'use strict';

  const EYE_STAND = 1.68;
  const EYE_CROUCH = 0.95;
  const RADIUS = 0.36;
  const GRAVITY = 22;
  const JUMP_V = 7.2;

  const SPEED = { walk: 4.6, sprint: 7.4, crouch: 2.3, air: 1.6 };

  function create(camera, level) {
    const state = {
      pos: level.playerStart.clone(),
      vel: new THREE.Vector3(),
      /* Face down the ship (+Z) rather than three's default -Z, which at the
         docking collar is the back wall. */
      yaw: Math.PI, pitch: 0,
      onGround: true,
      crouching: false,
      sprinting: false,
      eye: EYE_STAND,
      bob: 0, bobAmount: 0,
      recoil: new THREE.Vector2(),     // camera kick, decays back to zero
      shake: 0, shakePhase: 0,
      landDip: 0,
      alive: true
    };

    const keys = Object.create(null);
    let sensitivity = 0.0022;
    let locked = false;

    /* ---------- input ---------- */
    const onKey = (e, down) => {
      const k = e.code;
      keys[k] = down;
      if (down && (k === 'Space' || k.startsWith('Arrow'))) e.preventDefault();
    };
    window.addEventListener('keydown', (e) => onKey(e, true));
    window.addEventListener('keyup', (e) => onKey(e, false));

    function onMouseMove(e) {
      if (!locked) return;
      state.yaw   -= e.movementX * sensitivity;
      state.pitch -= e.movementY * sensitivity;
      state.pitch = Math.max(-Math.PI / 2 + 0.02, Math.min(Math.PI / 2 - 0.02, state.pitch));
    }
    document.addEventListener('mousemove', onMouseMove);

    /* ---------- collision ---------- */
    /* Resolve the player's circle against every box, one axis at a time,
       so sliding along a wall feels right instead of sticking. */
    function collide(next) {
      const feet = state.pos.y;
      for (const c of level.colliders) {
        if (c.top <= feet + 0.35 || c.bottom >= feet + state.eye) continue;  // steppable / overhead
        const minX = c.min.x - RADIUS, maxX = c.max.x + RADIUS;
        const minZ = c.min.z - RADIUS, maxZ = c.max.z + RADIUS;
        if (next.x <= minX || next.x >= maxX || next.z <= minZ || next.z >= maxZ) continue;

        // push out along the shallowest axis
        const dxLeft = next.x - minX, dxRight = maxX - next.x;
        const dzUp = next.z - minZ, dzDown = maxZ - next.z;
        const m = Math.min(dxLeft, dxRight, dzUp, dzDown);
        if (m === dxLeft)       { next.x = minX; state.vel.x = 0; }
        else if (m === dxRight) { next.x = maxX; state.vel.x = 0; }
        else if (m === dzUp)    { next.z = minZ; state.vel.z = 0; }
        else                    { next.z = maxZ; state.vel.z = 0; }
      }
      const b = level.bounds;
      next.x = Math.max(b.minX + RADIUS, Math.min(b.maxX - RADIUS, next.x));
      next.z = Math.max(b.minZ + RADIUS, Math.min(b.maxZ - RADIUS, next.z));
    }

    /* Highest surface under the player, used as the floor. */
    function groundHeight(x, z) {
      let best = 0;
      for (const c of level.colliders) {
        if (x < c.min.x - RADIUS * 0.5 || x > c.max.x + RADIUS * 0.5) continue;
        if (z < c.min.z - RADIUS * 0.5 || z > c.max.z + RADIUS * 0.5) continue;
        if (c.top <= state.pos.y + 0.55 && c.top > best) best = c.top;
      }
      return best;
    }

    /* ---------- step ---------- */
    function update(dt) {
      if (!state.alive) { applyCamera(dt); return; }

      state.crouching = !!(keys.ControlLeft || keys.KeyC);
      const wantSprint = !!keys.ShiftLeft && !state.crouching;

      // desired direction in world space
      const fx = -Math.sin(state.yaw), fz = -Math.cos(state.yaw);
      const rx =  Math.cos(state.yaw), rz = -Math.sin(state.yaw);
      let dx = 0, dz = 0;
      if (keys.KeyW || keys.ArrowUp)    { dx += fx; dz += fz; }
      if (keys.KeyS || keys.ArrowDown)  { dx -= fx; dz -= fz; }
      if (keys.KeyD || keys.ArrowRight) { dx += rx; dz += rz; }
      if (keys.KeyA || keys.ArrowLeft)  { dx -= rx; dz -= rz; }
      const len = Math.hypot(dx, dz);
      if (len > 0) { dx /= len; dz /= len; }

      state.sprinting = wantSprint && len > 0 && (keys.KeyW || keys.ArrowUp);

      const target = state.crouching ? SPEED.crouch : state.sprinting ? SPEED.sprint : SPEED.walk;
      const accel = state.onGround ? 52 : 12;
      state.vel.x += (dx * target - state.vel.x) * Math.min(1, accel * dt);
      state.vel.z += (dz * target - state.vel.z) * Math.min(1, accel * dt);

      if (state.onGround && len === 0) {
        const friction = Math.max(0, 1 - 14 * dt);
        state.vel.x *= friction; state.vel.z *= friction;
      }

      if (keys.Space && state.onGround) {
        state.vel.y = JUMP_V;
        state.onGround = false;
        SF.audio.sfx.step();
      }

      state.vel.y -= GRAVITY * dt;

      const next = state.pos.clone();
      next.x += state.vel.x * dt;
      next.z += state.vel.z * dt;
      collide(next);
      next.y += state.vel.y * dt;

      const gh = groundHeight(next.x, next.z);
      if (next.y <= gh) {
        if (!state.onGround && state.vel.y < -6) {
          state.landDip = Math.min(0.34, -state.vel.y * 0.022);
          SF.audio.sfx.land();
        }
        next.y = gh;
        state.vel.y = 0;
        state.onGround = true;
      } else {
        state.onGround = false;
      }
      state.pos.copy(next);

      // head bob follows actual ground speed
      const speed = Math.hypot(state.vel.x, state.vel.z);
      const moving = state.onGround && speed > 0.4;
      state.bobAmount += ((moving ? (state.sprinting ? 1.35 : 1) : 0) - state.bobAmount) * Math.min(1, 8 * dt);
      if (moving) {
        const prev = state.bob;
        state.bob += dt * (state.sprinting ? 13 : 9.5);
        // footstep on each bottom of the cycle
        if (Math.floor(prev / Math.PI) !== Math.floor(state.bob / Math.PI)) SF.audio.sfx.step();
      }

      applyCamera(dt);
    }

    function applyCamera(dt) {
      const targetEye = state.crouching ? EYE_CROUCH : EYE_STAND;
      state.eye += (targetEye - state.eye) * Math.min(1, 12 * dt);
      state.landDip *= Math.max(0, 1 - 7 * dt);
      state.shake *= Math.max(0, 1 - 7 * dt);
      state.recoil.multiplyScalar(Math.max(0, 1 - 9 * dt));

      const bobY = Math.sin(state.bob) * 0.055 * state.bobAmount;
      const bobX = Math.cos(state.bob * 0.5) * 0.04 * state.bobAmount;
      /* Smooth oscillation rather than per-frame noise: random offsets every
         frame read as a rendering glitch, not as weight. */
      state.shakePhase += dt * 34;
      const shakeX = Math.sin(state.shakePhase) * state.shake * 0.035;
      const shakeY = Math.cos(state.shakePhase * 1.7) * state.shake * 0.03;

      camera.position.set(
        state.pos.x + bobX + shakeX,
        state.pos.y + state.eye + bobY - state.landDip + shakeY,
        state.pos.z
      );
      camera.rotation.set(0, 0, 0);
      camera.rotateY(state.yaw);
      camera.rotateX(state.pitch + state.recoil.y);
      camera.rotateZ(Math.cos(state.bob * 0.5) * 0.012 * state.bobAmount + state.recoil.x * 0.35);
    }

    function addRecoil(pitch, yaw) {
      state.recoil.y += pitch;
      state.recoil.x += yaw;
      state.yaw += yaw * 0.12;         // a nudge, not a permanent walk off target
    }

    return {
      state, keys,
      update, addRecoil,
      get position() { return state.pos; },
      get eyePosition() { return new THREE.Vector3(state.pos.x, state.pos.y + state.eye, state.pos.z); },
      setLocked(v) { locked = v; },
      setSensitivity(v) { sensitivity = v; },
      reset(pos) {
        state.pos.copy(pos || level.playerStart);
        state.vel.set(0, 0, 0);
        state.yaw = Math.PI; state.pitch = 0;
        state.alive = true;
      }
    };
  }

  SF.player = { create, EYE_STAND };
})(window.SF);
