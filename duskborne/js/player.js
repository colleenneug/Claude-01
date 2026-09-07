/* ============================================================
   First-person controller: pointer-lock look, accelerated ground
   movement with friction, gravity, circle-vs-AABB collision, jump,
   sprint, crouch and the head bob that sells it. Deliberately simpler
   than the browser build's Erebus Cradle controller (no slide, no wall
   run) — this is a different game built from scratch, not a reskin.
   ============================================================ */
(function (DB) {
  'use strict';

  const EYE_STAND = 1.65;
  const EYE_CROUCH = 1.0;
  const RADIUS = 0.35;
  const GRAVITY = 22;
  const JUMP_V = 7.0;
  const SPEED = { walk: 4.4, sprint: 8.4, crouch: 2.3 };

  function create(camera, level) {
    const state = {
      pos: level.playerStart.clone(),
      vel: new THREE.Vector3(),
      yaw: Math.PI, pitch: 0,
      onGround: true, crouching: false, sprinting: false,
      eye: EYE_STAND, bob: 0, bobAmount: 0,
      recoil: new THREE.Vector2(),
      landDip: 0, alive: true, moveInput: 0
    };

    const keys = Object.create(null);
    let mode = 'off';
    const sensitivity = 0.0022;

    window.addEventListener('keydown', function (e) {
      keys[e.code] = true;
      if (e.code === 'Space') e.preventDefault();
    });
    window.addEventListener('keyup', function (e) { keys[e.code] = false; });

    function onMouseMove(e) {
      if (mode !== 'locked') return;
      state.yaw -= e.movementX * sensitivity;
      state.pitch -= e.movementY * sensitivity;
      state.pitch = DB.util.clamp(state.pitch, -Math.PI / 2 + 0.02, Math.PI / 2 - 0.02);
    }
    document.addEventListener('mousemove', onMouseMove);

    function collide(next) {
      const feet = state.pos.y;
      for (const c of level.colliders) {
        if (c.top <= feet + 0.35 || c.bottom >= feet + state.eye) continue;
        const minX = c.min.x - RADIUS, maxX = c.max.x + RADIUS;
        const minZ = c.min.z - RADIUS, maxZ = c.max.z + RADIUS;
        if (next.x <= minX || next.x >= maxX || next.z <= minZ || next.z >= maxZ) continue;
        const dxLeft = next.x - minX, dxRight = maxX - next.x;
        const dzUp = next.z - minZ, dzDown = maxZ - next.z;
        const m = Math.min(dxLeft, dxRight, dzUp, dzDown);
        if (m === dxLeft) { next.x = minX; state.vel.x = 0; }
        else if (m === dxRight) { next.x = maxX; state.vel.x = 0; }
        else if (m === dzUp) { next.z = minZ; state.vel.z = 0; }
        else { next.z = maxZ; state.vel.z = 0; }
      }
      const h = level.half - RADIUS;
      next.x = DB.util.clamp(next.x, -h, h);
      next.z = DB.util.clamp(next.z, -h, h);
    }

    function groundHeight(x, z) {
      let best = 0;
      for (const c of level.colliders) {
        if (x < c.min.x - RADIUS * 0.5 || x > c.max.x + RADIUS * 0.5) continue;
        if (z < c.min.z - RADIUS * 0.5 || z > c.max.z + RADIUS * 0.5) continue;
        if (c.top <= state.pos.y + 0.55 && c.top > best) best = c.top;
      }
      return best;
    }

    function update(dt) {
      if (!state.alive) { applyCamera(dt); return; }

      const crouchHeld = !!(keys.ControlLeft || keys.KeyC);
      const wantSprint = !!keys.ShiftLeft && !crouchHeld;

      const fx = -Math.sin(state.yaw), fz = -Math.cos(state.yaw);
      const rx = Math.cos(state.yaw), rz = -Math.sin(state.yaw);
      let dx = 0, dz = 0;
      if (keys.KeyW) { dx += fx; dz += fz; }
      if (keys.KeyS) { dx -= fx; dz -= fz; }
      if (keys.KeyD) { dx += rx; dz += rz; }
      if (keys.KeyA) { dx -= rx; dz -= rz; }
      const len = Math.hypot(dx, dz);
      if (len > 0) { dx /= len; dz /= len; }

      state.moveInput = len;
      state.crouching = crouchHeld;
      state.sprinting = wantSprint && len > 0 && keys.KeyW;

      const target = (state.crouching ? SPEED.crouch : state.sprinting ? SPEED.sprint : SPEED.walk);
      const accel = state.onGround ? 50 : 4;
      state.vel.x += (dx * target - state.vel.x) * Math.min(1, accel * dt);
      state.vel.z += (dz * target - state.vel.z) * Math.min(1, accel * dt);
      if (state.onGround && len === 0) {
        const friction = Math.max(0, 1 - 14 * dt);
        state.vel.x *= friction; state.vel.z *= friction;
      }

      if (keys.Space && state.onGround) {
        state.vel.y = JUMP_V;
        state.onGround = false;
      }

      state.vel.y -= GRAVITY * dt;

      const next = state.pos.clone();
      const travel = Math.hypot(state.vel.x, state.vel.z) * dt;
      const steps = Math.min(6, Math.max(1, Math.ceil(travel / RADIUS)));
      for (let i = 0; i < steps; i++) {
        next.x += (state.vel.x * dt) / steps;
        next.z += (state.vel.z * dt) / steps;
        collide(next);
      }
      next.y += state.vel.y * dt;

      const gh = groundHeight(next.x, next.z);
      if (next.y <= gh) {
        if (!state.onGround && state.vel.y < -6) state.landDip = Math.min(0.3, -state.vel.y * 0.02);
        next.y = gh;
        state.vel.y = 0;
        state.onGround = true;
      } else {
        state.onGround = false;
      }
      state.pos.copy(next);

      const speed = Math.hypot(state.vel.x, state.vel.z);
      const moving = state.onGround && speed > 0.4;
      const bobWant = moving ? (state.sprinting ? 1.3 : 1) : 0;
      state.bobAmount += (bobWant - state.bobAmount) * Math.min(1, 8 * dt);
      if (moving) state.bob += dt * (state.sprinting ? 12.5 : 9);

      applyCamera(dt);
    }

    function applyCamera(dt) {
      const targetEye = state.crouching ? EYE_CROUCH : EYE_STAND;
      state.eye += (targetEye - state.eye) * Math.min(1, 12 * dt);
      state.landDip *= Math.max(0, 1 - 7 * dt);
      state.recoil.multiplyScalar(Math.max(0, 1 - 9 * dt));

      const bobY = Math.sin(state.bob) * 0.05 * state.bobAmount;
      const bobX = Math.cos(state.bob * 0.5) * 0.035 * state.bobAmount;

      camera.position.set(state.pos.x + bobX, state.pos.y + state.eye + bobY - state.landDip, state.pos.z);
      camera.rotation.set(0, 0, 0);
      camera.rotateY(state.yaw);
      camera.rotateX(state.pitch + state.recoil.y);
      camera.rotateZ(state.recoil.x * 0.3);
    }

    function addRecoil(pitch, yaw) {
      state.recoil.y += pitch;
      state.recoil.x += yaw;
      state.yaw += yaw * 0.1;
    }

    return {
      state: state, keys: keys,
      update: update, addRecoil: addRecoil,
      get position() { return state.pos; },
      get eyePosition() { return new THREE.Vector3(state.pos.x, state.pos.y + state.eye, state.pos.z); },
      setMode: function (v) { mode = v; },
      get mode() { return mode; },
      reset: function (pos) {
        state.pos.copy(pos || level.playerStart);
        state.vel.set(0, 0, 0);
        state.yaw = Math.PI; state.pitch = 0;
        state.alive = true;
      }
    };
  }

  DB.player = { create: create, EYE_STAND: EYE_STAND };
})(window.DB || (window.DB = {}));
