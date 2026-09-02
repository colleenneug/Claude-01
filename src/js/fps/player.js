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

  /* Base speeds. An open zone multiplies these — see setSpeedScale — because
     a kilometre-wide world at corridor pace is a walking simulator. */
  const SPEED = { walk: 4.6, sprint: 9.4, crouch: 2.3, air: 1.6 };

  /* ---------- the slide ----------
     Crouch while sprinting and you go down on the plates. The slide takes
     whatever speed you arrived with and adds to it, then bleeds off; you
     steer a little but not much, and you can jump out of it, which is the
     whole reason to do it. The floor under the kick is your entry speed, so
     sliding from a standing crouch does nothing. */
  /* ---------- the wall run ----------
     Leave the ground at speed with a wall beside you and you run along it.
     Gravity is not switched off, only turned down, so a wall run is always a
     descent — it buys you distance, not flight. Jumping off pushes away from
     the wall as well as up, and you cannot re-attach to the same wall until
     you have touched something else. */
  const WALL = {
    /* The probe runs from the player's centre, so this is added to their
       radius: 0.75 lets you catch a wall from about a metre off it, which is
       the difference between a move you can aim and one you have to scrape. */
    reach: 0.75,        // how far past the player's own radius to look
    minSpeed: 5.2,      // slower than this and you just fall
    gravity: 4.5,       // instead of 22, while attached
    stick: 7,           // pull toward the wall, so you do not drift off it
    along: 1.04,        // a little speed gain each second along the wall
    time: 1.8,          // longest you can hold one
    jumpUp: 7.4,        // and how you get off it
    jumpOut: 6.6,
    tilt: 0.19,         // camera roll, toward the wall
    cooldown: 0.25
  };

  const SLIDE = {
    kick: 1.7,          // multiplier on the speed you came in with
    floor: 10,          // ...but never slower than this
    ceiling: 17,        // ...and never faster than this
    /* Drag is what decides how long a slide lasts, and it is exponential:
       from a 14 m/s entry, 1.5 takes about three quarters of a second to
       reach the exit speed. Anything much above that and the slide is over
       before the camera has finished dropping. */
    drag: 1.5,          // how fast it bleeds off, per second
    steer: 3.4,         // how much authority you keep, against 52 on foot
    time: 1.15,         // longest a slide can last
    exit: 4.6,          // ...or until it has slowed to this
    cooldown: 0.45,     // before another one is allowed
    eye: 0.72,          // how low the camera rides
    roll: 0.09          // and how far it leans
  };

  function create(camera, level) {
    let speedScale = 1;
    let turnScale = 1;
    /* Third person, for when you are riding something. `want` is what the
       ride asks for; `cur` is what the world will actually allow, eased so
       the camera does not snap in and out as scenery passes behind you. */
    const chase = { want: 0, height: 0, cur: 0 };
    const CB_R = new THREE.Vector3(), CB_U = new THREE.Vector3(), CB_BACK = new THREE.Vector3();
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
      shake: 0, shakePhase: 0, turnRate: 0,
      moveInput: 0,                    // how hard the player is asking to move, 0..1
      sliding: false, slideT: 0, slideCool: 0, slideRoll: 0,
      wallRunning: false, wallSide: 0, wallT: 0, wallCool: 0, wallRoll: 0,
      wallNormal: { x: 0, z: 0 }, wallLast: null,
      landDip: 0,
      alive: true
    };

    const keys = Object.create(null);
    let sensitivity = 0.0022;
    let crouchWasHeld = false;
    let slideAllowed = true;          // a runner takes it away
    let wallRunAllowed = true;
    const clampNum = (v, lo, hi) => Math.max(lo, Math.min(hi, v));

    /* Is there a wall at this point, at this height? Returns the collider,
       or null. The same AABBs the walking collision uses. */
    function wallAt(x, z, feet) {
      const candidates = level.space ? level.space.near(x, z, 1) : level.colliders;
      for (const c of candidates) {
        if (c.top < feet + 0.9 || c.bottom > feet + 1.4) continue;   // too low, or overhead
        if (x > c.min.x && x < c.max.x && z > c.min.z && z < c.max.z) return c;
      }
      return null;
    }

    /* The outward normal of the face the probe went through: whichever face
       of the box the point is nearest. */
    function faceNormal(c, x, z) {
      const dxMin = x - c.min.x, dxMax = c.max.x - x;
      const dzMin = z - c.min.z, dzMax = c.max.z - z;
      const m = Math.min(dxMin, dxMax, dzMin, dzMax);
      if (m === dxMin) return { x: -1, z: 0 };
      if (m === dxMax) return { x: 1, z: 0 };
      if (m === dzMin) return { x: 0, z: -1 };
      return { x: 0, z: 1 };
    }

    function endWall(why) {
      if (!state.wallRunning) return;
      state.wallRunning = false;
      state.wallSide = 0;
      state.wallT = 0;
      state.wallCool = WALL.cooldown;
      if (why !== 'jump') state.wallLast = null;
    }

    function endSlide(keepSpeed) {
      if (!state.sliding) return;
      state.sliding = false;
      state.slideT = 0;
      state.slideCool = SLIDE.cooldown;
      if (!keepSpeed) {
        // stand up into a sprint rather than a dead stop
        const now = Math.hypot(state.vel.x, state.vel.z);
        const cap = SPEED.sprint * speedScale;
        if (now > cap) { state.vel.x *= cap / now; state.vel.z *= cap / now; }
      }
    }

    /* Look modes:
         'locked' — real pointer lock; raw mouse deltas, infinite travel.
         'free'   — pointer lock unavailable (an embedded frame that denies
                    it). Deltas still arrive while the cursor is over the
                    view, but the cursor runs out of screen, so nearing an
                    edge steers continuously. That is what makes a full turn
                    possible without lock.
         'off'    — paused or not yet engaged. */
    let mode = 'off';
    const pointer = { x: 0.5, y: 0.5, inside: false };
    const TURN_RATE = 2.4;          // radians per second, keyboard and edge steer
    const EDGE = 0.16;              // fraction of the viewport that steers

    /* ---------- input ---------- */
    const onKey = (e, down) => {
      const k = e.code;
      keys[k] = down;
      if (down && (k === 'Space' || k.startsWith('Arrow'))) e.preventDefault();
    };
    window.addEventListener('keydown', (e) => onKey(e, true));
    window.addEventListener('keyup', (e) => onKey(e, false));

    function onMouseMove(e) {
      pointer.x = e.clientX / window.innerWidth;
      pointer.y = e.clientY / window.innerHeight;
      pointer.inside = true;
      if (mode === 'off') return;
      const dYaw = e.movementX * sensitivity * turnScale;
      state.yaw   -= dYaw;
      state.turnRate = dYaw;
      state.pitch -= e.movementY * sensitivity;
      clampPitch();
    }
    document.addEventListener('mousemove', onMouseMove);
    document.addEventListener('mouseleave', () => { pointer.inside = false; });
    document.addEventListener('mouseenter', () => { pointer.inside = true; });

    function clampPitch() {
      state.pitch = Math.max(-Math.PI / 2 + 0.02, Math.min(Math.PI / 2 - 0.02, state.pitch));
    }

    /* Continuous turn from the keyboard, and — when the cursor cannot travel
       any further — from holding it against the edge of the view. */
    function steer(dt) {
      let turn = 0;
      if (keys.ArrowLeft)  turn += 1;
      if (keys.ArrowRight) turn -= 1;

      if (mode === 'free' && pointer.inside) {
        if (pointer.x < EDGE)          turn += (1 - pointer.x / EDGE);
        else if (pointer.x > 1 - EDGE) turn -= (1 - (1 - pointer.x) / EDGE);
      }
      if (turn !== 0) {
        const d = turn * TURN_RATE * turnScale * dt;
        state.yaw += d;
        state.turnRate = -d;
      } else {
        state.turnRate *= Math.max(0, 1 - 8 * dt);
      }

      // keep yaw in [-PI, PI] so it never drifts into large-float territory
      if (state.yaw > Math.PI) state.yaw -= Math.PI * 2;
      else if (state.yaw < -Math.PI) state.yaw += Math.PI * 2;
    }

    /* ---------- collision ---------- */
    /* Resolve the player's circle against every box, one axis at a time,
       so sliding along a wall feels right instead of sticking. */
    function collide(next) {
      const feet = state.pos.y;
      const candidates = level.space ? level.space.near(next.x, next.z, RADIUS + 1)
                                     : level.colliders;
      for (const c of candidates) {
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

    /* How far back the camera can sit before it is inside something. Walk
       the line out in steps and stop at the first blocked one — a box test
       per step is cheap and a camera that stops short reads better than one
       that clips through a rock. */
    const CAM_R = 0.34;
    function chaseClearance(ox, oy, oz, dx, dy, dz, want) {
      const candidates = level.space ? level.space.near(ox + dx * want * 0.5,
                                                        oz + dz * want * 0.5, want + CAM_R)
                                     : level.colliders;
      const steps = 8;
      for (let i = 1; i <= steps; i++) {
        const t = (want * i) / steps;
        const x = ox + dx * t, y = oy + dy * t, z = oz + dz * t;
        if (y < 0.5) return Math.max(0, t - want / steps);
        for (const c of candidates) {
          if (y > c.top + CAM_R || y < c.bottom - CAM_R) continue;
          if (x > c.min.x - CAM_R && x < c.max.x + CAM_R &&
              z > c.min.z - CAM_R && z < c.max.z + CAM_R) {
            return Math.max(0, t - want / steps);
          }
        }
      }
      return want;
    }

    /* Highest surface under the player, used as the floor. */
    function groundHeight(x, z) {
      let best = 0;
      const candidates = level.space ? level.space.near(x, z, RADIUS) : level.colliders;
      for (const c of candidates) {
        if (x < c.min.x - RADIUS * 0.5 || x > c.max.x + RADIUS * 0.5) continue;
        if (z < c.min.z - RADIUS * 0.5 || z > c.max.z + RADIUS * 0.5) continue;
        if (c.top <= state.pos.y + 0.55 && c.top > best) best = c.top;
      }
      return best;
    }

    /* ---------- step ---------- */
    function update(dt) {
      steer(dt);
      if (!state.alive) { applyCamera(dt); return; }

      const crouchHeld = !!(keys.ControlLeft || keys.KeyC);
      const wantSprint = !!keys.ShiftLeft && !crouchHeld;

      // desired direction in world space
      const fx = -Math.sin(state.yaw), fz = -Math.cos(state.yaw);
      const rx =  Math.cos(state.yaw), rz = -Math.sin(state.yaw);
      let dx = 0, dz = 0;
      if (keys.KeyW || keys.ArrowUp)    { dx += fx; dz += fz; }
      if (keys.KeyS || keys.ArrowDown)  { dx -= fx; dz -= fz; }
      if (keys.KeyD) { dx += rx; dz += rz; }
      if (keys.KeyA) { dx -= rx; dz -= rz; }
      const len = Math.hypot(dx, dz);
      if (len > 0) { dx /= len; dz /= len; }

      state.moveInput = len;
      state.sprinting = wantSprint && len > 0 && (keys.KeyW || keys.ArrowUp) && !state.sliding;

      /* ---- starting a slide ----
         Crouch, at speed, on the ground, with the cooldown clear. Anything
         else and crouch is just crouch. */
      const speedNow = Math.hypot(state.vel.x, state.vel.z);
      state.slideCool = Math.max(0, state.slideCool - dt);
      if (!state.sliding && crouchHeld && !crouchWasHeld && state.onGround &&
          state.slideCool <= 0 && speedNow > SPEED.walk * speedScale * 0.85 && slideAllowed) {
        state.sliding = true;
        state.slideT = SLIDE.time;
        const boosted = clampNum(speedNow * SLIDE.kick,
                                 SLIDE.floor * speedScale, SLIDE.ceiling * speedScale);
        const s0 = speedNow > 0.01 ? boosted / speedNow : 0;
        state.vel.x *= s0;
        state.vel.z *= s0;
        state.slideRoll = 0;
        SF.audio.sfx.slide();
      }
      crouchWasHeld = crouchHeld;

      if (state.sliding) {
        state.slideT -= dt;
        // bleed off, steer a little, and end when it is over
        const drag = Math.max(0, 1 - SLIDE.drag * dt);
        state.vel.x *= drag;
        state.vel.z *= drag;
        if (len > 0) {
          state.vel.x += dx * SLIDE.steer * dt * speedScale;
          state.vel.z += dz * SLIDE.steer * dt * speedScale;
        }
        const now = Math.hypot(state.vel.x, state.vel.z);
        if (state.slideT <= 0 || now < SLIDE.exit * speedScale ||
            !state.onGround || !crouchHeld || !slideAllowed) {
          endSlide();
        }
      }

      state.crouching = crouchHeld || state.sliding;

      if (!state.sliding) {
        /* Crouch only slows you on the ground — ducking in mid-air should not
           brake you — and air control is a nudge, not a steering wheel. It
           used to close 60% of the gap to the target every frame, which threw
           away whatever speed you jumped with: a slide hop landed at walking
           pace, and every jump was a full stop in the air. */
        const target = (state.crouching && state.onGround ? SPEED.crouch
                      : state.sprinting ? SPEED.sprint : SPEED.walk) * speedScale;
        const accel = state.onGround ? 52 : 3.5;
        state.vel.x += (dx * target - state.vel.x) * Math.min(1, accel * dt);
        state.vel.z += (dz * target - state.vel.z) * Math.min(1, accel * dt);

        if (state.onGround && len === 0) {
          const friction = Math.max(0, 1 - 14 * dt);
          state.vel.x *= friction; state.vel.z *= friction;
        }
      }

      if (keys.Space && state.onGround) {
        // jumping out of a slide keeps the speed you built — that is the trick
        if (state.sliding) endSlide(true);
        state.vel.y = JUMP_V;
        state.onGround = false;
        SF.audio.sfx.step();
      }

      /* ---- the wall run ----
         Off the ground, moving, with something solid beside you. Probe both
         sides at shoulder height and take whichever is there. */
      state.wallCool = Math.max(0, state.wallCool - dt);
      if (state.onGround) { state.wallLast = null; if (state.wallRunning) endWall('ground'); }

      const flat = Math.hypot(state.vel.x, state.vel.z);
      if (!state.onGround && wallRunAllowed && state.wallCool <= 0 && flat > WALL.minSpeed) {
        const rxu = Math.cos(state.yaw), rzu = -Math.sin(state.yaw);   // player's right
        const out = RADIUS + WALL.reach;
        for (const side of state.wallRunning ? [state.wallSide] : [1, -1]) {
          const px = state.pos.x + rxu * out * side;
          const pz = state.pos.z + rzu * out * side;
          const hit = wallAt(px, pz, state.pos.y);
          if (!hit || (!state.wallRunning && hit === state.wallLast)) continue;
          if (!state.wallRunning) {
            state.wallRunning = true;
            state.wallT = WALL.time;
            state.wallSide = side;
            state.wallLast = hit;
            state.vel.y = Math.max(state.vel.y, 0);   // catch the fall on contact
            SF.audio.sfx.slide();
          }
          state.wallNormal = faceNormal(hit, px, pz);
          break;
        }
        if (state.wallRunning && !wallAt(state.pos.x + rxu * out * state.wallSide,
                                        state.pos.z + rzu * out * state.wallSide,
                                        state.pos.y)) endWall('gone');
      } else if (state.wallRunning) {
        endWall('slow');
      }

      if (state.wallRunning) {
        state.wallT -= dt;
        if (state.wallT <= 0) endWall('time');
      }

      if (state.wallRunning) {
        const n = state.wallNormal;
        // kill the component going into the wall, keep the one along it
        const into = state.vel.x * n.x + state.vel.z * n.z;
        if (into < 0) { state.vel.x -= n.x * into; state.vel.z -= n.z * into; }
        // and lean on it, so you track the surface instead of drifting off
        state.vel.x -= n.x * WALL.stick * dt;
        state.vel.z -= n.z * WALL.stick * dt;
        const along = Math.hypot(state.vel.x, state.vel.z);
        if (along > 0.01) {
          const want = along * (1 + (WALL.along - 1) * dt);
          state.vel.x *= want / along;
          state.vel.z *= want / along;
        }
        // kicking off: away from the wall and up
        if (keys.Space) {
          state.vel.x += n.x * WALL.jumpOut;
          state.vel.z += n.z * WALL.jumpOut;
          state.vel.y = WALL.jumpUp;
          endWall('jump');
          SF.audio.sfx.step();
        }
      }

      state.vel.y -= (state.wallRunning ? WALL.gravity : GRAVITY) * dt;

      /* Resolve the horizontal step in pieces no longer than the player's
         own radius. At walking pace this is always one piece; on a boosting
         runner a frame can carry you several metres, and a single test would
         step clean through anything thinner than that. */
      const next = state.pos.clone();
      const travel = Math.hypot(state.vel.x, state.vel.z) * dt;
      const steps = Math.min(8, Math.max(1, Math.ceil(travel / RADIUS)));
      for (let i = 0; i < steps; i++) {
        next.x += (state.vel.x * dt) / steps;
        next.z += (state.vel.z * dt) / steps;
        collide(next);
      }
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
      const bobWant = state.sliding ? 0 : moving ? (state.sprinting ? 1.35 : 1) : 0;
      state.bobAmount += (bobWant - state.bobAmount) * Math.min(1, 8 * dt);
      if (moving) {
        const prev = state.bob;
        state.bob += dt * (state.sprinting ? 13 : 9.5);
        // footstep on each bottom of the cycle
        if (Math.floor(prev / Math.PI) !== Math.floor(state.bob / Math.PI)) SF.audio.sfx.step();
      }

      applyCamera(dt);
    }

    function applyCamera(dt) {
      const targetEye = state.sliding ? SLIDE.eye
                      : state.crouching ? EYE_CROUCH : EYE_STAND;
      // lean into the slide, and come back out of it smoothly
      const wantRoll = state.sliding ? SLIDE.roll : 0;
      state.slideRoll += (wantRoll - state.slideRoll) * Math.min(1, 9 * dt);
      // and lean into a wall you are running along, away from the surface
      const wallWant = state.wallRunning ? -state.wallSide * WALL.tilt : 0;
      state.wallRoll += (wallWant - state.wallRoll) * Math.min(1, 8 * dt);
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
      camera.rotateZ(Math.cos(state.bob * 0.5) * 0.012 * state.bobAmount +
                     state.recoil.x * 0.35 + state.slideRoll + state.wallRoll);

      /* Pull the eye straight backwards along the view axis — not off to a
         shoulder, and not a fixed horizontal offset either. Backing up along
         the true axis keeps the eye point on the camera's centre line, so a
         shot fired from the rider still leaves through the crosshair at any
         pitch, and looking down shortens the pull instead of burying the
         camera in the ground. */
      if (chase.want > 0 || chase.cur > 0.001) {
        camera.updateMatrixWorld();
        camera.matrixWorld.extractBasis(CB_R, CB_U, CB_BACK);   // third column is +Z: backwards
        const px = camera.position.x, py = camera.position.y, pz = camera.position.z;
        const allowed = chase.want > 0
          ? chaseClearance(px, py, pz, CB_BACK.x, CB_BACK.y, CB_BACK.z, chase.want)
          : 0;
        // snap in fast when something crowds the camera, ease out gently
        chase.cur += (allowed - chase.cur) * Math.min(1, (allowed < chase.cur ? 26 : 9) * dt);
        if (chase.want === 0 && chase.cur < 0.02) chase.cur = 0;
        const lift = chase.height * (chase.want > 0 ? Math.min(1, chase.cur / chase.want) : 0);
        camera.position.set(px + CB_BACK.x * chase.cur,
                            py + CB_BACK.y * chase.cur + lift,
                            pz + CB_BACK.z * chase.cur);
      }
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
      setSpeedScale(v) { speedScale = v || 1; },
      /* Ask for a chase camera. dist 0 puts it back on the eye. */
      setChase(dist, height) { chase.want = dist || 0; chase.height = height || 0; },
      /* A frame carries you; you do not slide off one. */
      setSlideAllowed(v) {
        slideAllowed = !!v;
        wallRunAllowed = !!v;
        if (!v) { endSlide(); endWall('mounted'); }
      },
      get sliding() { return state.sliding; },
      get wallRunning() { return state.wallRunning; },
      get chaseDistance() { return chase.cur; },
      setTurnScale(v) { turnScale = v || 1; },
      setMode(v) { mode = v; },
      get mode() { return mode; },
      /* Look state, for diagnostics: which mode is live and where the
         cursor sits as a fraction of the viewport. */
      get look() { return { mode: mode, x: pointer.x, y: pointer.y, inside: pointer.inside }; },
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
