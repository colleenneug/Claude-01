// Main orchestrator: scene setup, input, camera, interactions, powers, save loop.
window.CityGame = (function () {
  let scene, camera, renderer;
  let player, cars, npcs, landmarks;
  let firstPerson = false;
  let yaw = 0, pitch = 0.5;
  let dragging = false;
  let lastMouse = { x: 0, y: 0 };

  const keys = {};
  let jumpQueued = false;

  let inVehicle = false;
  let currentCar = null;

  let wasOnGround = true;
  let fallStartY = 0;

  let playerState = null;
  let kidsMode = false;
  let saveTimer = 0;

  let telekinesisOn = false;
  let telekinesisCar = null;
  let webAttached = false;
  let webTarget = null;
  let webLine = null;
  let invisible = false;

  const canvas = document.getElementById('game-canvas');
  const tmpVec = new THREE.Vector3();
  const raycaster = new THREE.Raycaster();

  const ENERGY_DRAIN = 16;
  const ENERGY_REGEN = 10;
  const TELEKINESIS_RANGE = 12;
  const WEBSWING_RANGE = 24;
  const NPC_ROB_COOLDOWN = 15000;

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

  function isSuperhero() { return playerState.role === 'superhero'; }

  function bindInput() {
    window.addEventListener('keydown', (e) => {
      if (isTypingIntoForm()) return;
      keys[e.code] = true;
      if (e.code === 'Space') { jumpQueued = true; e.preventDefault(); }
      if (e.code === 'KeyV') toggleVehicle();
      if (e.code === 'KeyT') tryTravel();
      if (e.code === 'Digit1' && isSuperhero()) toggleTelekinesis();
      if (e.code === 'Digit2' && isSuperhero()) toggleWebSwing();
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

  function tryTravel() {
    if (inVehicle || !landmarks.airport) return;
    if (player.position.distanceTo(landmarks.airport.position) < 5) {
      window.CityTravel.open(landmarks, teleportTo);
    } else {
      window.CityHUD.showDialog('Fast travel is only available at the Airport.');
    }
  }

  function teleportTo(position) {
    if (inVehicle) return;
    player.teleport(new THREE.Vector3(position.x, 0, position.z));
  }

  function toggleVehicle() {
    if (inVehicle) {
      currentCar.occupied = false;
      const exitPos = currentCar.mesh.position.clone();
      exitPos.x += 2.5;
      player.teleport(new THREE.Vector3(exitPos.x, 0, exitPos.z));
      player.root.visible = !invisible;
      inVehicle = false;
      currentCar = null;
      return;
    }
    const car = window.CityVehicle.nearest(cars, player.position, 4);
    if (car) {
      if (car === telekinesisCar) { car.mesh.position.y = 0; stopTelekinesis(); }
      car.occupied = true;
      currentCar = car;
      inVehicle = true;
      player.root.visible = false;
    }
  }

  function toggleTelekinesis() {
    if (!telekinesisOn) {
      const car = window.CityVehicle.nearest(cars, player.position, TELEKINESIS_RANGE);
      if (!car || playerState.energy <= 0) return;
      telekinesisOn = true;
      telekinesisCar = car;
      window.CityHUD.showDialog('Telekinesis engaged — focus on the car.');
    } else {
      stopTelekinesis();
    }
  }
  function stopTelekinesis() {
    telekinesisOn = false;
    telekinesisCar = null;
  }

  function findWebAnchor() {
    const origin = tmpVec.clone();
    player.getEyePosition(origin);
    const dir = new THREE.Vector3(-Math.sin(yaw), -0.15, -Math.cos(yaw)).normalize();
    raycaster.set(origin, dir);
    raycaster.far = WEBSWING_RANGE;
    const targets = scene.children.filter((c) => c.userData.solid);
    const hits = raycaster.intersectObjects(targets);
    return hits.length ? hits[0].point.clone() : null;
  }

  function toggleWebSwing() {
    if (webAttached) { detachWeb(); return; }
    if (playerState.energy <= 0) return;
    const anchor = findWebAnchor();
    if (!anchor) { window.CityHUD.showDialog('No building in range to sling a web at.'); return; }
    webAttached = true;
    webTarget = anchor;
    const material = new THREE.LineBasicMaterial({ color: 0xffffff });
    const geometry = new THREE.BufferGeometry().setFromPoints([player.position.clone(), webTarget]);
    webLine = new THREE.Line(geometry, material);
    scene.add(webLine);
    window.CityHUD.showDialog('Web attached — hold W to climb.');
  }
  function detachWeb() {
    webAttached = false;
    webTarget = null;
    if (webLine) { scene.remove(webLine); webLine = null; }
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
      camera.position.set(target.x + Math.sin(yaw) * horiz, target.y + dist * Math.sin(pitch) + 1, target.z + Math.cos(yaw) * horiz);
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
    camera.position.set(target.x + Math.sin(yaw) * horiz, target.y + dist * Math.sin(pitch) + 1, target.z + Math.cos(yaw) * horiz);
    camera.lookAt(target);
  }

  function updateInteractions() {
    if (inVehicle) { window.CityHUD.hidePrompt(); return; }
    const pos = player.position;

    const nearBank = landmarks.bank && pos.distanceTo(landmarks.bank.position) < 3.5;
    const nearAirport = landmarks.airport && pos.distanceTo(landmarks.airport.position) < 5;
    const npc = window.CityNPC.nearest(npcs, pos, 3.5);
    const business = landmarks.businesses.find((b) => pos.distanceTo(b.position) < 3.5 && !playerState.businesses.includes(b.id));
    const car = window.CityVehicle.nearest(cars, pos, 4);

    if (nearBank && !kidsMode) {
      window.CityHUD.showPrompt('Rob the Bank', 'R');
      if (keys.KeyR) { keys.KeyR = false; startHeist(); }
    } else if (npc) {
      const label = playerState.role === 'cop' ? `Arrest ${npc.name}` : `Rob ${npc.name}`;
      window.CityHUD.showPrompt(kidsMode ? `Talk to ${npc.name} <kbd>E</kbd>` : `Talk to ${npc.name} <kbd>E</kbd> &nbsp;|&nbsp; ${label} <kbd>R</kbd>`);
      if (keys.KeyE) { keys.KeyE = false; window.CityHUD.showDialog(`${npc.name}: "${window.CityNPC.lineFor(playerState.role)}"`); }
      if (!kidsMode && keys.KeyR) { keys.KeyR = false; robNPC(npc); }
    } else if (business) {
      const price = window.CityEconomy.businessPrice(playerState, business.price);
      window.CityHUD.showPrompt(`Buy this business — $${price.toLocaleString()}`, 'F');
      if (keys.KeyF) {
        keys.KeyF = false;
        if (window.CityEconomy.buy(playerState, business)) {
          window.CityHUD.showDialog('Business acquired. Passive income increased.');
          persistState();
        } else {
          window.CityHUD.showDialog("You can't afford that yet.");
        }
      }
    } else if (car) {
      window.CityHUD.showPrompt('Enter vehicle', 'V');
    } else if (nearAirport) {
      window.CityHUD.showPrompt('Fast Travel', 'T');
    } else {
      window.CityHUD.hidePrompt();
    }
  }

  function robNPC(npc) {
    const now = Date.now();
    if (now - npc.lastRobbedAt < NPC_ROB_COOLDOWN) {
      window.CityHUD.showDialog('Nothing left to take there right now.');
      return;
    }
    npc.lastRobbedAt = now;
    const amount = 150 + Math.floor(Math.random() * 450);
    playerState.money += amount;
    const flavor = playerState.role === 'cop'
      ? `You arrested ${npc.name} and collected a $${amount.toLocaleString()} bounty.`
      : `You lifted $${amount.toLocaleString()} off ${npc.name}.`;
    window.CityHUD.showDialog(flavor);
    persistState();
  }

  function startHeist() {
    const onCooldown = playerState.lastRobberyAt && Date.now() - playerState.lastRobberyAt < 5 * 60 * 1000;
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
        if (err.data && err.data.remainingMs) window.CityBank.cooldownMessage(err.data.remainingMs);
      }
    });
  }

  function renderBusinessPanel() {
    const listEl = document.getElementById('business-list');
    listEl.innerHTML = '';
    landmarks.businesses.forEach((biz, i) => {
      const owned = playerState.businesses.includes(biz.id);
      const price = window.CityEconomy.businessPrice(playerState, biz.price);
      const row = document.createElement('div');
      row.className = 'list-row';
      row.innerHTML = `<span><span class="name">Business #${i + 1}</span><br><span class="sub">${owned ? 'Owned' : `$${price.toLocaleString()}`} &middot; +$${biz.income}/s</span></span>`;
      if (!owned) {
        const btn = document.createElement('button');
        btn.className = 'buy-btn';
        btn.textContent = 'Buy';
        btn.disabled = !window.CityEconomy.canAfford(playerState, biz.price);
        btn.addEventListener('click', () => {
          if (window.CityEconomy.buy(playerState, biz)) { persistState(); renderBusinessPanel(); }
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
    if (isSuperhero()) {
      const btn = document.createElement('button');
      btn.className = 'power-toggle-btn' + (invisible ? ' on' : '');
      btn.textContent = invisible ? 'Invisible: ON — click to reveal yourself' : 'Turn Invisible';
      btn.addEventListener('click', () => {
        invisible = !invisible;
        player.setInvisible(invisible);
        btn.classList.toggle('on', invisible);
        btn.textContent = invisible ? 'Invisible: ON — click to reveal yourself' : 'Turn Invisible';
      });
      listEl.appendChild(btn);
    }
  }

  function persistState() {
    window.CityAPI.save({
      money: playerState.money,
      health: playerState.health,
      energy: playerState.energy,
      role: playerState.role,
      businesses: playerState.businesses,
    }).catch(() => {});
  }

  function applyFallDamage() {
    if (!wasOnGround && player.onGround) {
      const fallDist = fallStartY - player.position.y;
      if (fallDist > 6) {
        const damage = Math.min(35, (fallDist - 6) * 3);
        playerState.health = Math.max(5, playerState.health - damage);
      }
    }
    if (wasOnGround && !player.onGround) fallStartY = player.position.y;
    wasOnGround = player.onGround;
  }

  function respawnIfDown() {
    if (playerState.health > 0) return;
    if (!kidsMode) playerState.money = Math.floor(playerState.money * 0.95);
    playerState.health = 100;
    player.teleport(landmarks.spawn);
    window.CityHUD.showDialog(kidsMode ? 'Time for a rest! Back to full health.' : "You collapsed and had to be taken home. (-5% cash)");
  }

  let lastT = performance.now();
  function loop() {
    const now = performance.now();
    const dt = Math.min(0.05, (now - lastT) / 1000);
    lastT = now;

    const paused = window.CityHUD.anyPanelOpen() || window.CityBank.isActive();

    if (!paused) {
      if (inVehicle) {
        window.CityVehicle.update(currentCar, dt, { forward: keys.KeyW, back: keys.KeyS, left: keys.KeyA, right: keys.KeyD }, collideAt);
      } else if (webAttached) {
        const dir = webTarget.clone().sub(player.position);
        const dist = dir.length();
        if (dist < 2 || playerState.energy <= 0) { detachWeb(); }
        else {
          dir.normalize();
          const next = player.position.clone().addScaledVector(dir, 14 * dt);
          player.teleport(next);
          playerState.energy = Math.max(0, playerState.energy - ENERGY_DRAIN * dt);
          webLine.geometry.setFromPoints([player.position.clone(), webTarget]);
        }
      } else {
        const moveDir = moveDirFromCamera();
        const sprinting = !!keys.ShiftLeft || !!keys.ShiftRight;
        const wallAhead = wallAheadOf(player.position, moveDir);
        const canFly = isSuperhero() && playerState.energy > 0;
        const flyHeld = canFly && !!keys.Space && !player.onGround;
        player.update(dt, moveDir, { sprinting, jumpPressed: jumpQueued, climbHeld: !!keys.Space, wallAhead, canFly: canFly && !player.onGround, flyHeld, collideAt });
        if (canFly && flyHeld) playerState.energy = Math.max(0, playerState.energy - ENERGY_DRAIN * 0.6 * dt);
        applyFallDamage();
      }

      if (telekinesisOn) {
        if (!telekinesisCar || playerState.energy <= 0 || player.position.distanceTo(telekinesisCar.mesh.position) > TELEKINESIS_RANGE + 4) {
          stopTelekinesis();
        } else {
          telekinesisCar.mesh.position.y = 2.5 + Math.sin(now / 300) * 0.4;
          playerState.energy = Math.max(0, playerState.energy - ENERGY_DRAIN * dt);
        }
      } else if (telekinesisCar) {
        telekinesisCar.mesh.position.y += (0 - telekinesisCar.mesh.position.y) * Math.min(1, dt * 4);
        if (Math.abs(telekinesisCar.mesh.position.y) < 0.05) { telekinesisCar.mesh.position.y = 0; telekinesisCar = null; }
      }

      if (!telekinesisOn && !webAttached && !(isSuperhero() && !player.onGround && keys.Space)) {
        playerState.energy = Math.min(100, playerState.energy + ENERGY_REGEN * dt);
      }

      updateInteractions();

      const meta = window.CityEconomy.ROLE_META[playerState.role];
      playerState.health = Math.max(0, playerState.health - dt * meta.healthDecay);
      respawnIfDown();
    } else {
      window.CityHUD.hidePrompt();
    }
    jumpQueued = false;

    window.CityEconomy.tick(playerState, dt, landmarks.businesses);
    updateCamera();

    window.CityHUD.setMoney(playerState.money, window.CityEconomy.incomePerSecond(playerState, landmarks.businesses));
    window.CityHUD.setHealth(playerState.health);
    if (isSuperhero()) window.CityHUD.setEnergy(playerState.energy);

    saveTimer += dt;
    if (saveTimer > 6) { saveTimer = 0; persistState(); }

    renderer.render(scene, camera);
    requestAnimationFrame(loop);
  }

  function start(player0, options) {
    kidsMode = !!(options && options.kids);
    playerState = Object.assign({ businesses: [], health: 100, energy: 100, money: 0, role: 'citizen', lastRobberyAt: 0 }, player0);
    if (playerState.money <= 0) playerState.money = window.CityAPI.STARTING[playerState.role] || 1000;
    setupScene();
    bindInput();

    const meta = window.CityEconomy.ROLE_META[playerState.role];
    window.CityHUD.setRole(playerState.role, meta.label);
    window.CityHUD.setHudVisibility({
      weapon: !kidsMode && (playerState.role === 'cop' || playerState.role === 'criminal'),
      energy: isSuperhero(),
      powers: isSuperhero(),
    });
    document.getElementById('controls-rob').classList.toggle('hidden', kidsMode);

    window.CityHUD.onOpen('business', renderBusinessPanel);
    window.CityHUD.onOpen('powers', renderPowersPanel);
    window.CityHUD.onOpen('leaderboard', window.CityLeaderboard.open);
    window.CityHUD.onOpen('build', () => window.CityBuild.setEnabled(true));
    document.getElementById('panel-build').querySelector('.btn-close').addEventListener('click', () => window.CityBuild.setEnabled(false));

    window.addEventListener('beforeunload', () => persistState());

    requestAnimationFrame(loop);
  }

  return { start };
})();
