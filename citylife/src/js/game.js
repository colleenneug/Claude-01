// Main orchestrator: scene setup, input, camera, interactions, save loop.
window.CityGame = (function () {
  let scene, camera, renderer;
  let player, cars, npcs, landmarks;
  let firstPerson = false;
  let yaw = 0, pitch = 0.5;
  let dragging = false;
  let lastMouse = { x: 0, y: 0 };

  const keys = {};
  let jumpQueued = false;
  let dashQueued = false;

  let inVehicle = false;
  let currentCar = null;

  let wasOnGround = true;
  let fallStartY = 0;

  let playerState = null; // { username, role, money, health, businesses, lastRobberyAt }
  let saveTimer = 0;

  const canvas = document.getElementById('game-canvas');
  const tmpVec = new THREE.Vector3();

  function setupScene() {
    scene = new THREE.Scene();
    camera = new THREE.PerspectiveCamera(65, window.innerWidth / window.innerHeight, 0.1, 500);
    renderer = new THREE.WebGLRenderer({ canvas, antialias: true });
    renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
    renderer.setSize(window.innerWidth, window.innerHeight);

    landmarks = window.CityWorld.build(scene);
    player = window.CityPlayer.create(scene, playerState.role, landmarks.spawn);
    cars = window.CityVehicle.spawnCars(scene, landmarks.carSpots);
    npcs = window.CityNPC.spawn(scene, landmarks.npcSpots);

    window.CityBuild.init(scene, camera, canvas);

    window.addEventListener('resize', () => {
      camera.aspect = window.innerWidth / window.innerHeight;
      camera.updateProjectionMatrix();
      renderer.setSize(window.innerWidth, window.innerHeight);
    });
  }

  function collideAt(point, radius) {
    return !!window.CityWorld.collides(scene, point, radius);
  }

  function isTypingIntoForm() {
    const t = document.activeElement && document.activeElement.tagName;
    return t === 'INPUT' || t === 'TEXTAREA';
  }

  function bindInput() {
    window.addEventListener('keydown', (e) => {
      if (isTypingIntoForm()) return;
      keys[e.code] = true;
      if (e.code === 'Space') { jumpQueued = true; e.preventDefault(); }
      if (e.code === 'KeyQ') dashQueued = true;
      if (e.code === 'KeyV') toggleVehicle();
      if (e.code === 'KeyT' && !inVehicle) window.CityTravel.open(landmarks, teleportTo);
    });
    window.addEventListener('keyup', (e) => { keys[e.code] = false; });

    canvas.addEventListener('pointerdown', (e) => {
      if (e.button !== 0 || window.CityHUD.anyPanelOpen()) return;
      dragging = true;
      lastMouse = { x: e.clientX, y: e.clientY };
    });
    window.addEventListener('pointerup', () => { dragging = false; });
    window.addEventListener('pointermove', (e) => {
      if (!dragging) return;
      const dx = e.clientX - lastMouse.x;
      const dy = e.clientY - lastMouse.y;
      lastMouse = { x: e.clientX, y: e.clientY };
      yaw -= dx * 0.006;
      pitch = Math.max(0.08, Math.min(1.2, pitch + dy * 0.004));
    });

    document.getElementById('btn-camera').addEventListener('click', () => {
      firstPerson = !firstPerson;
      document.getElementById('btn-camera').textContent = firstPerson ? '\u{1F441} 3rd Person' : '\u{1F441} 1st Person';
    });
  }

  function teleportTo(position) {
    if (inVehicle) return;
    player.position.set(position.x, 0, position.z);
  }

  function toggleVehicle() {
    if (inVehicle) {
      currentCar.occupied = false;
      const exitPos = currentCar.mesh.position.clone();
      exitPos.x += 2.5;
      player.position.set(exitPos.x, 0, exitPos.z);
      player.root.visible = true;
      inVehicle = false;
      currentCar = null;
      return;
    }
    const car = window.CityVehicle.nearest(cars, player.position, 4);
    if (car) {
      car.occupied = true;
      currentCar = car;
      inVehicle = true;
      player.root.visible = false;
    }
  }

  function moveDirFromCamera() {
    const forward = new THREE.Vector3(-Math.sin(yaw), 0, -Math.cos(yaw));
    const right = new THREE.Vector3(forward.z, 0, -forward.x);
    const dir = new THREE.Vector3();
    if (keys.KeyW) dir.add(forward);
    if (keys.KeyS) dir.sub(forward);
    if (keys.KeyD) dir.add(right);
    if (keys.KeyA) dir.sub(right);
    if (dir.lengthSq() > 0) dir.normalize();
    return dir;
  }

  function wallAheadOf(point, dir) {
    if (dir.lengthSq() < 0.01) return false;
    const probe = point.clone().addScaledVector(dir, window.CityPlayer.RADIUS + 0.5);
    return collideAt(probe, 0.2);
  }

  function updateCamera() {
    if (inVehicle) {
      const target = currentCar.mesh.position.clone();
      target.y += 1.2;
      const dist = 8;
      const horiz = dist * Math.cos(pitch);
      camera.position.set(
        target.x + Math.sin(yaw) * horiz,
        target.y + dist * Math.sin(pitch) + 1,
        target.z + Math.cos(yaw) * horiz
      );
      camera.lookAt(target);
      return;
    }
    if (firstPerson) {
      player.getEyePosition(tmpVec);
      camera.position.copy(tmpVec);
      const lookTarget = tmpVec.clone().add(new THREE.Vector3(-Math.sin(yaw), 0, -Math.cos(yaw)));
      camera.lookAt(lookTarget);
      return;
    }
    const target = player.position.clone();
    target.y += 1.6;
    const dist = 9;
    const horiz = dist * Math.cos(pitch);
    camera.position.set(
      target.x + Math.sin(yaw) * horiz,
      target.y + dist * Math.sin(pitch) + 1,
      target.z + Math.cos(yaw) * horiz
    );
    camera.lookAt(target);
  }

  function updateInteractions(dt) {
    if (inVehicle) { window.CityHUD.hidePrompt(); return; }
    const pos = player.position;

    const npc = window.CityNPC.nearest(npcs, pos, 3.5);
    const nearBank = landmarks.bank && pos.distanceTo(landmarks.bank.position) < 3.5;
    const business = landmarks.businesses.find((b) => pos.distanceTo(b.position) < 3.5 && !playerState.businesses.includes(b.id));
    const car = window.CityVehicle.nearest(cars, pos, 4);

    if (nearBank) {
      window.CityHUD.showPrompt('Rob the bank', 'E');
      if (keys.KeyE) { keys.KeyE = false; startHeist(); }
    } else if (npc) {
      window.CityHUD.showPrompt(`Talk to ${npc.name}`, 'E');
      if (keys.KeyE) { keys.KeyE = false; window.CityHUD.showDialog(`${npc.name}: "${window.CityNPC.lineFor(playerState.role)}"`); }
    } else if (business) {
      window.CityHUD.showPrompt(`Buy this business — $${business.price.toLocaleString()}`, 'F');
      if (keys.KeyF) {
        keys.KeyF = false;
        if (window.CityEconomy.buy(playerState, business)) {
          window.CityHUD.showDialog('Business acquired. Passive income increased.');
          persistState(true);
        } else {
          window.CityHUD.showDialog("You can't afford that yet.");
        }
      }
    } else if (car) {
      window.CityHUD.showPrompt('Enter vehicle', 'V');
    } else {
      window.CityHUD.hidePrompt();
    }
  }

  function startHeist() {
    const onCooldown = landmarks.bank && playerState.lastRobberyAt &&
      Date.now() - playerState.lastRobberyAt < 5 * 60 * 1000;
    if (onCooldown) {
      window.CityBank.cooldownMessage(5 * 60 * 1000 - (Date.now() - playerState.lastRobberyAt));
      return;
    }
    window.CityBank.open(async (success) => {
      if (!success) return;
      try {
        const { take, player: updated } = await window.CityAPI.robBank();
        playerState.money = updated.money;
        playerState.lastRobberyAt = updated.lastRobberyAt;
        window.CityHUD.showDialog(`You got away with $${take.toLocaleString()}!`);
      } catch (err) {
        if (err.data && err.data.remainingMs) {
          window.CityBank.cooldownMessage(err.data.remainingMs);
        }
      }
    });
  }

  function renderBusinessPanel() {
    const listEl = document.getElementById('business-list');
    listEl.innerHTML = '';
    landmarks.businesses.forEach((biz, i) => {
      const owned = playerState.businesses.includes(biz.id);
      const row = document.createElement('div');
      row.className = 'list-row';
      row.innerHTML = `<span><span class="name">Business #${i + 1}</span><br><span class="sub">${owned ? 'Owned' : `$${biz.price.toLocaleString()}`} &middot; +$${biz.income}/s</span></span>`;
      if (!owned) {
        const btn = document.createElement('button');
        btn.className = 'buy-btn';
        btn.textContent = 'Buy';
        btn.disabled = !window.CityEconomy.canAfford(playerState, biz.price);
        btn.addEventListener('click', () => {
          if (window.CityEconomy.buy(playerState, biz)) {
            persistState(true);
            renderBusinessPanel();
          }
        });
        row.appendChild(btn);
      }
      listEl.appendChild(row);
    });
  }

  function renderPowersPanel() {
    const listEl = document.getElementById('powers-list');
    const meta = window.CityEconomy.ROLE_META[playerState.role];
    listEl.innerHTML = meta.powers.map((p) => `<div class="list-row"><span>${p}</span></div>`).join('');
  }

  function persistState(immediate) {
    const doSave = () => window.CityAPI.save({
      money: playerState.money,
      health: playerState.health,
      role: playerState.role,
      businesses: playerState.businesses,
    }).catch(() => {});
    if (immediate) doSave();
  }

  function applyFallDamage() {
    if (!wasOnGround && player.onGround) {
      const fallDist = fallStartY - player.position.y;
      if (fallDist > 6) {
        const damage = Math.min(35, (fallDist - 6) * 3);
        playerState.health = Math.max(15, playerState.health - damage);
      }
    }
    if (wasOnGround && !player.onGround) {
      fallStartY = player.position.y;
    }
    wasOnGround = player.onGround;
  }

  let lastT = performance.now();
  function loop() {
    const now = performance.now();
    const dt = Math.min(0.05, (now - lastT) / 1000);
    lastT = now;

    if (!window.CityHUD.anyPanelOpen() && !window.CityBank.isActive()) {
      if (inVehicle) {
        window.CityVehicle.update(currentCar, dt, {
          forward: keys.KeyW, back: keys.KeyS, left: keys.KeyA, right: keys.KeyD,
        }, collideAt);
      } else {
        const moveDir = moveDirFromCamera();
        const sprinting = !!keys.ShiftLeft || !!keys.ShiftRight;
        const wallAhead = wallAheadOf(player.position, moveDir);
        const glide = playerState.role === 'superhero' && !player.onGround && !!keys.Space;
        player.update(dt, moveDir, {
          sprinting,
          jumpPressed: jumpQueued,
          climbHeld: !!keys.Space,
          wallAhead,
          glide,
          collideAt,
        });
        if (dashQueued && playerState.role === 'villain') {
          const dashDir = moveDir.lengthSq() > 0.01
            ? moveDir
            : new THREE.Vector3(Math.sin(player.rotationY), 0, Math.cos(player.rotationY));
          const target = player.position.clone().addScaledVector(dashDir, 6);
          if (!collideAt(target, window.CityPlayer.RADIUS)) player.position.copy(target);
        }
        applyFallDamage();
      }
      updateInteractions(dt);
    } else {
      window.CityHUD.hidePrompt();
    }
    jumpQueued = false;
    dashQueued = false;

    window.CityEconomy.tick(playerState, dt, landmarks.businesses);
    playerState.health = Math.min(100, playerState.health + dt * 1.5);

    updateCamera();

    window.CityHUD.setMoney(playerState.money, window.CityEconomy.incomePerSecond(playerState, landmarks.businesses));
    window.CityHUD.setHealth(playerState.health);

    saveTimer += dt;
    if (saveTimer > 6) { saveTimer = 0; persistState(true); }

    renderer.render(scene, camera);
    requestAnimationFrame(loop);
  }

  function start(player0) {
    playerState = Object.assign({ businesses: [], health: 100, money: 0, role: 'criminal', lastRobberyAt: 0 }, player0);
    setupScene();
    bindInput();

    const meta = window.CityEconomy.ROLE_META[playerState.role];
    window.CityHUD.setRole(playerState.role, meta.label);
    window.CityHUD.setOnline(1);
    window.CityHUD.onOpen('business', renderBusinessPanel);
    window.CityHUD.onOpen('powers', renderPowersPanel);
    window.CityHUD.onOpen('leaderboard', window.CityLeaderboard.open);
    window.CityHUD.onOpen('build', () => window.CityBuild.setEnabled(true));
    document.getElementById('panel-build').querySelector('.btn-close').addEventListener('click', () => window.CityBuild.setEnabled(false));

    window.addEventListener('beforeunload', () => persistState(true));

    requestAnimationFrame(loop);
  }

  return { start };
})();
