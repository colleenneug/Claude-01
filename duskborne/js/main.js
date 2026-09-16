/* ============================================================
   Entry point: the three.js renderer, the render loop, input capture,
   pointer lock and pause handling — the one place that owns the 3D
   runtime. Screen navigation lives in ui.js; this module only exposes
   the small DB.runtime contract ui.js calls into.
   ============================================================ */
(function (DB) {
  'use strict';

  const canvas = document.getElementById('game-canvas');
  const renderer = new THREE.WebGLRenderer({ canvas: canvas, antialias: true, powerPreference: 'high-performance' });
  renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, 2));
  renderer.outputEncoding = THREE.sRGBEncoding;
  renderer.toneMapping = THREE.ACESFilmicToneMapping;
  renderer.toneMappingExposure = 1.05;
  renderer.shadowMap.enabled = true;
  renderer.shadowMap.type = THREE.PCFSoftShadowMap;

  const scene = new THREE.Scene();
  const camera = new THREE.PerspectiveCamera(75, window.innerWidth / window.innerHeight, 0.05, 500);
  const threeCtx = { renderer: renderer, scene: scene, camera: camera };

  function resize() {
    const w = window.innerWidth, h = window.innerHeight;
    renderer.setSize(w, h, false);
    camera.aspect = w / h;
    camera.updateProjectionMatrix();
  }
  window.addEventListener('resize', resize);
  resize();

  let lightRig = [];
  function buildLights(theme) {
    lightRig.forEach(function (l) { scene.remove(l); });
    lightRig = [];
    const tutorial = theme === 'tutorial';
    const hemi = new THREE.HemisphereLight(tutorial ? 0x9fb8d8 : 0x4a2a6a, 0x0a0a0f, 0.6);
    scene.add(hemi); lightRig.push(hemi);
    const sun = new THREE.DirectionalLight(tutorial ? 0xdfe8ff : 0xcfa0ff, tutorial ? 1.2 : 0.95);
    sun.position.set(-12, 20, 9);
    sun.castShadow = true;
    sun.shadow.mapSize.set(1536, 1536);
    sun.shadow.camera.left = -46; sun.shadow.camera.right = 46;
    sun.shadow.camera.top = 46; sun.shadow.camera.bottom = -46;
    sun.shadow.camera.near = 1; sun.shadow.camera.far = 110;
    scene.add(sun); lightRig.push(sun);

    scene.fog = new THREE.FogExp2(tutorial ? 0x121722 : 0x120a1a, tutorial ? 0.016 : 0.022);
    scene.background = new THREE.Color(tutorial ? 0x0a0d14 : 0x07040c);
  }

  const hud = DB.hud.create();
  let mission = null;
  let running = false;
  let paused = false;
  const input = { firePressed: false, reloadHeld: false, abilityPressed: false };

  canvas.addEventListener('mousedown', function (e) {
    if (e.button === 0 && running && !paused) input.firePressed = true;
  });
  window.addEventListener('mouseup', function (e) { if (e.button === 0) input.firePressed = false; });
  window.addEventListener('keydown', function (e) {
    if (e.code === 'KeyR') input.reloadHeld = true;
    if (e.code === 'KeyE' || e.code === 'KeyQ') input.abilityPressed = true;
    /* This is the primary way Escape pauses — it does not wait on the
       browser's own "Escape releases pointer lock" behaviour to round-trip
       through a pointerlockchange event first. That native release is real
       and will still fire its own pointerlockchange shortly after, but
       relying on it alone left pause unreachable in at least one real
       environment (automated/embedded browsing contexts can suppress or
       delay it), which is not a risk worth taking on the player's only way
       out of a mission. pauseNow() is idempotent, so the later, redundant
       pointerlockchange event below is harmless. */
    if (e.code === 'Escape' && running && !paused) pauseNow();
  });
  window.addEventListener('keyup', function (e) {
    if (e.code === 'KeyR') input.reloadHeld = false;
  });

  document.addEventListener('pointerlockchange', function () {
    const locked = document.pointerLockElement === canvas;
    if (mission) mission.player.setMode(locked ? 'locked' : 'off');
    if (running && !locked && !paused) pauseNow();
  });
  document.addEventListener('pointerlockerror', function () {
    DB.util.toast('Pointer lock unavailable here — try opening the game in its own tab.');
  });

  function pauseNow() {
    paused = true;
    if (document.exitPointerLock) document.exitPointerLock();
    DB.ui.showPause();
  }

  function disposeMission() {
    if (mission) { mission.dispose(); mission = null; }
  }

  function handleMissionEnd(cb, result) {
    running = false;
    if (document.exitPointerLock) document.exitPointerLock();
    DB.ui.hideMissionHUD();
    cb(result);
  }

  function buildMission(missionDef, character, callbacks) {
    disposeMission();
    buildLights(missionDef.spec.theme);
    const wrapped = {
      onComplete: function (r) { handleMissionEnd(callbacks.onComplete, r); },
      onFail: function (r) { handleMissionEnd(callbacks.onFail, r); },
      onBossIntro: callbacks.onBossIntro
    };
    mission = DB.game.createMission(threeCtx, character, missionDef.spec, hud, wrapped);
    hud.setClassLabel(DB.classes.klass(character.classId).name.toUpperCase() + ' · ' + missionDef.name.toUpperCase());
    hud.setObjective(missionDef.spec.boss ? 'ELIMINATE THE LUMEN WARDENS' : 'CLEAR THE CHAMBER');
    hud.setBoss(false, 0, '');
    running = false;
    paused = false;
  }

  function engage() {
    if (!mission) return;
    canvas.requestPointerLock = canvas.requestPointerLock || canvas.mozRequestPointerLock;
    canvas.requestPointerLock();
    DB.ui.hideEngage();
    DB.ui.showMissionHUD();
    running = true;
    paused = false;
    lastTime = performance.now();
  }

  function resume() {
    if (!mission) return;
    paused = false;
    DB.ui.showScreen(null);
    canvas.requestPointerLock();
    lastTime = performance.now();
  }

  function abandon() {
    running = false;
    paused = false;
    if (document.exitPointerLock) document.exitPointerLock();
    disposeMission();
    DB.ui.hideMissionHUD();
  }

  DB.runtime = { buildMission: buildMission, engage: engage, resume: resume, abandon: abandon };

  let lastTime = performance.now();
  function frame(t) {
    requestAnimationFrame(frame);
    const dt = Math.min(0.05, (t - lastTime) / 1000);
    lastTime = t;
    if (running && !paused && mission) {
      mission.update(dt, input);
      input.abilityPressed = false;
    }
    renderer.render(scene, camera);
  }
  requestAnimationFrame(frame);

  DB.ui.initBoot();
})(window.DB || (window.DB = {}));
