// Blocky Roblox-style avatar + movement controller (walk, jump, climb, fly).
window.CityPlayer = (function () {
  const GRAVITY = -28;
  const JUMP_SPEED = 9.5;
  const WALK_SPEED = 6.5;
  const SPRINT_MULT = 1.7;
  const RADIUS = 0.9;
  const EYE_HEIGHT = 2.7;
  const FLY_SPEED = 7;

  const ROLE_COLORS = {
    mayor: { shirt: 0x0f766e, pants: 0x1e293b },
    cop: { shirt: 0x1d4ed8, pants: 0x0f172a },
    criminal: { shirt: 0x525252, pants: 0x0a0a0a },
    superhero: { shirt: 0xef4444, pants: 0x1b2440 },
    citizen: { shirt: 0x84a98c, pants: 0x3f3f46 },
  };

  function buildAvatar(role) {
    const colors = ROLE_COLORS[role] || ROLE_COLORS.citizen;
    const group = new THREE.Group();

    const skin = new THREE.MeshStandardMaterial({ color: 0xf2c9a0 });
    const shirt = new THREE.MeshStandardMaterial({ color: colors.shirt });
    const pants = new THREE.MeshStandardMaterial({ color: colors.pants });
    const hair = new THREE.MeshStandardMaterial({ color: 0x2b1c12 });

    const torso = new THREE.Mesh(new THREE.BoxGeometry(1.2, 1.4, 0.7), shirt);
    torso.position.y = 1.7;
    group.add(torso);

    const head = new THREE.Mesh(new THREE.BoxGeometry(0.9, 0.9, 0.9), skin);
    head.position.y = 2.85;
    group.add(head);

    const hairCap = new THREE.Mesh(new THREE.BoxGeometry(0.95, 0.35, 0.95), hair);
    hairCap.position.y = 3.25;
    group.add(hairCap);

    const armL = new THREE.Mesh(new THREE.BoxGeometry(0.35, 1.3, 0.35), shirt);
    armL.position.set(-0.78, 1.7, 0);
    group.add(armL);
    const armR = armL.clone();
    armR.position.x = 0.78;
    group.add(armR);

    const legL = new THREE.Mesh(new THREE.BoxGeometry(0.45, 1.4, 0.45), pants);
    legL.position.set(-0.35, 0.7, 0);
    group.add(legL);
    const legR = legL.clone();
    legR.position.x = 0.35;
    group.add(legR);

    group.userData.limbs = { armL, armR, legL, legR };
    group.userData.allMeshes = [torso, head, hairCap, armL, armR, legL, legR];
    return group;
  }

  function create(scene, role, spawn) {
    const root = buildAvatar(role);
    root.position.copy(spawn);
    scene.add(root);

    const state = {
      root,
      velocity: new THREE.Vector3(),
      onGround: true,
      climbing: false,
      climbTimer: 0,
      walkPhase: 0,
      role,
    };

    function setInvisible(invisible) {
      state.root.userData.allMeshes.forEach((m) => {
        m.visible = !invisible;
      });
    }

    function update(dt, moveDir, opts) {
      const { root, velocity } = state;
      const speed = WALK_SPEED * (opts.sprinting ? SPRINT_MULT : 1);

      if (opts.canFly) {
        velocity.x = moveDir.x * speed;
        velocity.z = moveDir.z * speed;
        velocity.y = opts.flyHeld ? FLY_SPEED : Math.max(velocity.y - 10 * dt, -6);
        state.onGround = false;
      } else if (state.climbing) {
        state.climbTimer -= dt;
        velocity.set(0, 4.2, 0);
        if (state.climbTimer <= 0 || !opts.wallAhead) state.climbing = false;
      } else {
        velocity.x = moveDir.x * speed;
        velocity.z = moveDir.z * speed;

        if (opts.wallAhead && opts.climbHeld && moveDir.lengthSq() > 0.01) {
          state.climbing = true;
          state.climbTimer = 0.9;
        } else {
          velocity.y += GRAVITY * dt;
          if (state.onGround && opts.jumpPressed) {
            velocity.y = JUMP_SPEED;
            state.onGround = false;
          }
        }
      }

      const next = root.position.clone();
      next.x += velocity.x * dt;
      next.z += velocity.z * dt;

      if (opts.collideAt) {
        const blockedX = opts.collideAt(new THREE.Vector3(next.x, next.y, root.position.z), RADIUS);
        if (blockedX) next.x = root.position.x;
        const blockedZ = opts.collideAt(new THREE.Vector3(next.x, next.y, next.z), RADIUS);
        if (blockedZ) next.z = root.position.z;
      }

      next.y += velocity.y * dt;
      if (next.y <= 0) {
        next.y = 0;
        velocity.y = 0;
        state.onGround = true;
      }

      root.position.copy(next);

      if (moveDir.lengthSq() > 0.01 && !state.climbing) {
        const targetAngle = Math.atan2(moveDir.x, moveDir.z);
        root.rotation.y = targetAngle;
        state.walkPhase += dt * (opts.sprinting ? 14 : 9);
        const swing = Math.sin(state.walkPhase) * 0.6;
        root.userData.limbs.legL.rotation.x = swing;
        root.userData.limbs.legR.rotation.x = -swing;
        root.userData.limbs.armL.rotation.x = -swing;
        root.userData.limbs.armR.rotation.x = swing;
      } else {
        root.userData.limbs.legL.rotation.x *= 0.8;
        root.userData.limbs.legR.rotation.x *= 0.8;
        root.userData.limbs.armL.rotation.x *= 0.8;
        root.userData.limbs.armR.rotation.x *= 0.8;
      }
    }

    function getEyePosition(out) {
      out.copy(state.root.position);
      out.y += EYE_HEIGHT;
      return out;
    }

    function teleport(vec) {
      state.root.position.copy(vec);
      state.velocity.set(0, 0, 0);
    }

    return {
      get root() { return state.root; },
      get position() { return state.root.position; },
      get rotationY() { return state.root.rotation.y; },
      get onGround() { return state.onGround; },
      update,
      getEyePosition,
      setInvisible,
      teleport,
      radius: RADIUS,
    };
  }

  return { create, RADIUS, EYE_HEIGHT };
})();
