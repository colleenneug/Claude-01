/* ============================================================
   Screen flow and character/save state. Pure DOM — the 3D runtime
   (renderer, mission loop, pointer lock, input) lives in main.js and
   is reached only through the small DB.runtime contract this module
   calls into (runtime.buildMission / engage / pause / resume /
   abandon). Keeping the split means a UI change never risks the
   render loop and vice versa.
   ============================================================ */
(function (DB) {
  'use strict';

  function el(id) { return document.getElementById(id); }
  const SCREENS = ['boot', 'title', 'creation', 'hub', 'briefing', 'engage', 'pause', 'debrief'];

  function showScreen(id) {
    SCREENS.forEach(function (s) { el('screen-' + s).classList.toggle('show', s === id); });
  }

  let character = null;
  const creationState = { speciesId: null, classId: null };
  let pendingMissionId = null;
  let lastResult = null;

  const MISSIONS = {
    tutorial: {
      id: 'tutorial', name: 'First Communion',
      spec: { theme: 'tutorial', half: 22, crates: 6, waves: [{ typeId: 'ember', count: 3, radius: 14 }] }
    },
    throne: {
      id: 'throne', name: 'The Sunken Throne',
      spec: {
        theme: 'throne', half: 42, crates: 16,
        waves: [
          { typeId: 'voltaic', count: 4, radius: 26 },
          { typeId: 'null', count: 3, radius: 30 },
          { typeId: 'ember', count: 3, radius: 20 }
        ],
        boss: { typeId: 'vanguard', hpMult: 1 }
      }
    }
  };

  function hex(n) { return '#' + n.toString(16).padStart(6, '0'); }

  function newCharacter(speciesId, classId) {
    const klass = DB.classes.klass(classId);
    return {
      speciesId: speciesId, classId: classId, power: 0,
      disciplines: [klass.discipline],
      tutorialDone: false, throneCleared: false, disciplineBonus: 0
    };
  }

  function saveCharacter() { DB.util.storage.save(character); }

  /* ---------- boot ---------- */
  function initBoot() {
    setTimeout(function () {
      character = DB.util.storage.load();
      el('btn-title-continue').disabled = !(character && character.speciesId);
      showScreen('title');
    }, 1300);
  }

  /* ---------- creation ---------- */
  function renderCreationCards() {
    const speciesWrap = el('species-cards');
    speciesWrap.innerHTML = '';
    DB.classes.SPECIES.forEach(function (s) {
      const card = document.createElement('div');
      card.className = 'pick-card';
      card.innerHTML = '<div class="swatch" style="background:' + hex(s.colour) + '"></div>'
        + '<div class="tag">' + s.tag + '</div><h3>' + s.name + '</h3><p>' + s.blurb + '</p>'
        + '<div class="passive">' + s.passive + ' — ' + s.passiveBlurb + '</div>';
      card.addEventListener('mouseenter', function () { DB.audio.sfx.hover(); });
      card.addEventListener('click', function () {
        creationState.speciesId = s.id;
        Array.prototype.forEach.call(speciesWrap.children, function (c) { c.classList.remove('selected'); });
        card.classList.add('selected');
        el('btn-species-next').disabled = false;
        DB.audio.sfx.select();
      });
      speciesWrap.appendChild(card);
    });

    const classWrap = el('class-cards');
    classWrap.innerHTML = '';
    DB.classes.CLASSES.forEach(function (c) {
      const disc = DB.classes.discipline(c.discipline);
      const card = document.createElement('div');
      card.className = 'pick-card';
      card.innerHTML = '<div class="swatch" style="background:' + hex(c.colour) + '"></div>'
        + '<div class="tag">' + c.tag + '</div><h3>' + c.icon + ' ' + c.name + '</h3><p>' + c.blurb + '</p>'
        + '<div class="passive">' + c.passive + ' — ' + c.passiveBlurb + '<br>Native discipline: ' + disc.name + '</div>';
      card.addEventListener('mouseenter', function () { DB.audio.sfx.hover(); });
      card.addEventListener('click', function () {
        creationState.classId = c.id;
        Array.prototype.forEach.call(classWrap.children, function (x) { x.classList.remove('selected'); });
        card.classList.add('selected');
        el('btn-class-next').disabled = false;
        DB.audio.sfx.select();
      });
      classWrap.appendChild(card);
    });
  }

  function showCreationStep(step) {
    ['species', 'class', 'confirm'].forEach(function (s) { el('step-' + s).classList.toggle('show', s === step); });
  }

  function buildConfirmSummary() {
    const s = DB.classes.species(creationState.speciesId);
    const c = DB.classes.klass(creationState.classId);
    const disc = DB.classes.discipline(c.discipline);
    el('confirm-summary').textContent =
      s.name + ', ' + c.name + '. Bound first to ' + disc.name + ' — ' + disc.blurb + ' '
      + 'Weapon: ' + c.weapon.name + '. Ability: ' + c.ability.name + '.';
  }

  /* ---------- hub ---------- */
  function missionRow(name, desc, live, onClick) {
    const row = document.createElement('div');
    row.className = 'mission-row' + (live ? '' : ' locked');
    row.innerHTML = '<div><div class="m-name">' + name + '</div><div class="m-desc">' + desc + '</div></div>'
      + '<div class="m-status' + (live ? '' : ' planned') + '">' + (live ? 'Live' : 'Locked') + '</div>';
    if (onClick && live) row.addEventListener('click', onClick);
    return row;
  }

  function renderHub() {
    el('hub-character-line').textContent =
      DB.classes.species(character.speciesId).name + ' · ' + DB.classes.klass(character.classId).name;
    el('hub-power-value').textContent = character.power;

    const list = el('hub-mission-list');
    list.innerHTML = '';
    list.appendChild(missionRow('Tutorial — First Communion',
      'Learn the Hollow, and what it lets you do with a trigger.',
      true, function () { startBriefing('tutorial'); }));
    list.appendChild(missionRow('The Sunken Throne',
      character.tutorialDone
        ? 'The Unseen sends you after Lumen Wardens guarding the Coilqueen’s realm.'
        : 'Complete the tutorial first.',
      character.tutorialDone, function () { startBriefing('throne'); }));

    const chips = el('discipline-chips');
    chips.innerHTML = '';
    Object.keys(DB.classes.DISCIPLINES).forEach(function (id) {
      const d = DB.classes.DISCIPLINES[id];
      const unlocked = character.disciplines.indexOf(id) >= 0;
      const chip = document.createElement('div');
      chip.className = 'discipline-chip' + (unlocked ? ' unlocked' : '');
      chip.textContent = d.name + (unlocked ? '' : ' — quest locked');
      chips.appendChild(chip);
    });
  }

  document.addEventListener('DOMContentLoaded', function () {
    Array.prototype.forEach.call(document.querySelectorAll('.hub-tab'), function (btn) {
      btn.addEventListener('click', function () {
        DB.audio.sfx.select();
        Array.prototype.forEach.call(document.querySelectorAll('.hub-tab'), function (b) { b.classList.remove('active'); });
        btn.classList.add('active');
        Array.prototype.forEach.call(document.querySelectorAll('.hub-pane'), function (p) { p.classList.remove('show'); });
        el(btn.dataset.pane).classList.add('show');
      });
    });
  });

  /* ---------- briefing ---------- */
  function startBriefing(missionId) {
    pendingMissionId = missionId;
    const brief = missionId === 'tutorial' ? DB.story.TUTORIAL_BRIEFING : DB.story.THRONE_BRIEFING;
    el('brief-title').textContent = brief.title;
    el('brief-lines').innerHTML = brief.lines.map(function (l) { return '<p>' + l + '</p>'; }).join('');
    showScreen('briefing');
  }

  function launchMission() {
    const m = MISSIONS[pendingMissionId];
    DB.runtime.buildMission(m, character, { onComplete: onMissionComplete, onFail: onMissionFail, onBossIntro: onBossIntro });
    showScreen('engage');
  }

  function onBossIntro(name) { DB.util.toast(name.toUpperCase() + ' HAS AWAKENED'); DB.audio.sfx.bossRoar(); }

  /* ---------- debrief ---------- */
  function onMissionComplete(result) {
    DB.audio.sfx.victory();
    const isTutorial = pendingMissionId === 'tutorial';
    if (isTutorial) character.tutorialDone = true;
    else character.throneCleared = true;

    const gear = DB.game.rollGear();
    character.power += gear.power;
    saveCharacter();

    el('debrief-kicker').textContent = 'MISSION COMPLETE';
    el('debrief-title').textContent = isTutorial ? 'First Communion' : 'The Sunken Throne';
    el('debrief-line').textContent = DB.util.pick(DB.story.DEBRIEF_WIN);
    el('debrief-stats').innerHTML =
      stat(result.kills, 'Wardens Erased') + stat('+' + gear.power, 'Power');
    el('debrief-gear').innerHTML = '<div class="gear-card" style="border-color:' + gear.colour + '">'
      + '<div class="rarity" style="color:' + gear.colour + '">' + gear.label + ' salvage recovered</div></div>';
    showScreen('debrief');
  }

  function onMissionFail(result) {
    DB.audio.sfx.defeat();
    el('debrief-kicker').textContent = 'MISSION FAILED';
    el('debrief-title').textContent = 'The Hollow Pulled You Back';
    el('debrief-line').textContent = DB.util.pick(DB.story.DEBRIEF_LOSE);
    el('debrief-stats').innerHTML = stat(result.kills, 'Wardens Erased');
    el('debrief-gear').innerHTML = '';
    showScreen('debrief');
  }

  function stat(v, label) {
    return '<div class="debrief-stat"><div class="v">' + v + '</div><div class="l">' + label + '</div></div>';
  }

  /* ---------- wiring ---------- */
  function wire() {
    el('btn-title-begin').addEventListener('click', function () {
      DB.audio.sfx.confirm();
      creationState.speciesId = null; creationState.classId = null;
      renderCreationCards();
      showCreationStep('species');
      showScreen('creation');
    });
    el('btn-title-continue').addEventListener('click', function () {
      if (!character) return;
      DB.audio.sfx.confirm();
      renderHub();
      showScreen('hub');
    });

    el('btn-species-next').addEventListener('click', function () { DB.audio.sfx.select(); showCreationStep('class'); });
    el('btn-class-back').addEventListener('click', function () { DB.audio.sfx.back(); showCreationStep('species'); });
    el('btn-class-next').addEventListener('click', function () { DB.audio.sfx.select(); buildConfirmSummary(); showCreationStep('confirm'); });
    el('btn-confirm-back').addEventListener('click', function () { DB.audio.sfx.back(); showCreationStep('class'); });
    el('btn-confirm-go').addEventListener('click', function () {
      DB.audio.sfx.confirm();
      character = newCharacter(creationState.speciesId, creationState.classId);
      saveCharacter();
      renderHub();
      showScreen('hub');
    });

    el('btn-brief-back').addEventListener('click', function () { DB.audio.sfx.back(); renderHub(); showScreen('hub'); });
    el('btn-brief-launch').addEventListener('click', function () { DB.audio.sfx.confirm(); launchMission(); });

    el('screen-engage').addEventListener('click', function () { DB.runtime.engage(); });

    el('btn-pause-resume').addEventListener('click', function () { DB.runtime.resume(); });
    el('btn-pause-abandon').addEventListener('click', function () {
      DB.audio.sfx.back();
      DB.runtime.abandon();
      renderHub();
      showScreen('hub');
    });

    el('btn-debrief-continue').addEventListener('click', function () {
      DB.audio.sfx.select();
      renderHub();
      showScreen('hub');
    });
  }

  document.addEventListener('DOMContentLoaded', wire);

  DB.ui = {
    showScreen: showScreen, initBoot: initBoot,
    showPause: function () { showScreen('pause'); },
    showMissionHUD: function () { el('mission-hud').classList.add('show'); },
    hideMissionHUD: function () { el('mission-hud').classList.remove('show'); },
    hideEngage: function () { el('screen-engage').classList.remove('show'); },
    get character() { return character; },
    get pendingMission() { return MISSIONS[pendingMissionId]; }
  };
})(window.DB || (window.DB = {}));
