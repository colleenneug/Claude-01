/* Baseball Masters — screens: menu, exhibition setup, franchise, derby,
   collection. main.js wires the live-game screen; this file handles
   everything around it. */
(function (BM) {
  'use strict';
  const D = BM.data, PK = BM.packs;

  function $(sel, root) { return (root || document).querySelector(sel); }
  function $all(sel, root) { return Array.prototype.slice.call((root || document).querySelectorAll(sel)); }
  function el(tag, cls, html) {
    const e = document.createElement(tag);
    if (cls) e.className = cls;
    if (html !== undefined) e.innerHTML = html;
    return e;
  }

  function UI(app) {
    this.app = app;
    this.save = PK.load();
  }

  UI.prototype.persist = function () { PK.save(this.save); };

  UI.prototype.show = function (id) {
    $all('.screen').forEach(s => s.classList.remove('active'));
    $('#' + id).classList.add('active');
  };

  UI.prototype.updateStubsBadges = function () {
    $all('.stubs-count').forEach(e => { e.textContent = this.save.stubs.toLocaleString(); });
  };

  /* ------------------------------------------------------------ main menu */

  UI.prototype.initMenu = function () {
    $('#nav-exhibition').addEventListener('click', () => this.openExhibitionSetup());
    $('#nav-derby').addEventListener('click', () => this.openDerbySetup());
    $('#nav-franchise').addEventListener('click', () => this.openFranchise());
    $('#nav-collection').addEventListener('click', () => this.openCollection());
    $all('.btn-back').forEach(b => b.addEventListener('click', () => this.show('screen-menu')));
    this.updateStubsBadges();
  };

  /* ------------------------------------------------------- exhibition setup */

  UI.prototype.teamOptionsHtml = function (selected) {
    let html = '';
    D.TEAM_DEFS.forEach(t => {
      html += '<option value="' + t.id + '"' + (t.id === selected ? ' selected' : '') + '>' + t.city + ' ' + t.nick + '</option>';
    });
    if (this.save.collection.length >= 6) {
      html += '<option value="YOU">Your Legends (My Team)</option>';
    }
    return html;
  };

  UI.prototype.openExhibitionSetup = function () {
    $('#ex-away').innerHTML = this.teamOptionsHtml('BOL');
    $('#ex-home').innerHTML = this.teamOptionsHtml('RDW');
    $('#ex-stadium').innerHTML = D.STADIUMS.map(s => '<option value="' + s.id + '">' + s.name + '</option>').join('');
    $('#ex-difficulty').value = this.save.settings.difficulty;
    $('#ex-innings').value = String(this.save.settings.innings);
    this.show('screen-exhibition-setup');
  };

  UI.prototype.resolveTeam = function (id) {
    if (id === 'YOU') return PK.myTeam(this.save);
    return D.buildTeam(D.TEAM_DEFS.find(t => t.id === id));
  };

  UI.prototype.wireExhibitionStart = function (onStart) {
    $('#ex-start').addEventListener('click', () => {
      const awayId = $('#ex-away').value, homeId = $('#ex-home').value;
      if (awayId === homeId) { alert('Pick two different teams.'); return; }
      const away = this.resolveTeam(awayId);
      const home = this.resolveTeam(homeId);
      const stadium = D.STADIUMS.find(s => s.id === $('#ex-stadium').value);
      const difficulty = $('#ex-difficulty').value;
      const innings = parseInt($('#ex-innings').value, 10);
      const userSide = $('input[name="ex-side"]:checked').value;
      this.save.settings.difficulty = difficulty;
      this.save.settings.innings = innings;
      this.persist();
      onStart({ away: away, home: home, stadium: stadium, difficulty: difficulty, innings: innings, userSide: userSide });
    });
  };

  /* -------------------------------------------------------------- derby */

  UI.prototype.openDerbySetup = function () {
    const sel = $('#derby-team');
    sel.innerHTML = this.teamOptionsHtml('BOL');
    this.populateDerbyBatters();
    sel.onchange = () => this.populateDerbyBatters();
    this.show('screen-derby-setup');
  };
  UI.prototype.populateDerbyBatters = function () {
    const team = this.resolveTeam($('#derby-team').value);
    const opts = team.lineup.concat(team.bench)
      .sort((a, b) => b.power - a.power)
      .map((p, i) => '<option value="' + i + '">' + p.name + ' (' + p.pos + ', PWR ' + p.power + ')</option>')
      .join('');
    $('#derby-batter').innerHTML = opts;
    this._derbyTeam = team;
    this._derbyPool = team.lineup.concat(team.bench);
  };
  UI.prototype.wireDerbyStart = function (onStart) {
    $('#derby-start').addEventListener('click', () => {
      const batter = this._derbyPool[parseInt($('#derby-batter').value, 10)];
      onStart(batter, this._derbyTeam);
    });
  };

  /* ---------------------------------------------------------- collection */

  UI.prototype.openCollection = function () {
    this.renderCollection();
    this.show('screen-collection');
  };

  UI.prototype.renderCollection = function () {
    this.updateStubsBadges();
    const list = $('#collection-list');
    list.innerHTML = '';
    const coll = this.save.collection.slice().sort((a, b) => b.ovr - a.ovr);
    if (!coll.length) {
      list.appendChild(el('p', 'muted', 'No cards yet — crack a pack below.'));
    }
    coll.forEach(c => {
      const card = el('div', 'card tier-' + c.tier.name.toLowerCase());
      card.innerHTML =
        '<div class="card-tier">' + c.tier.name + '</div>' +
        '<div class="card-name">' + c.name + '</div>' +
        '<div class="card-pos">' + c.pos + (c.kind === 'pitcher' ? ' · ' + c.throws + 'HP' : ' · ' + c.bats + 'HB') + '</div>' +
        '<div class="card-ovr">' + c.ovr + '</div>';
      list.appendChild(card);
    });

    const packWrap = $('#pack-list');
    packWrap.innerHTML = '';
    PK.PACKS.forEach(p => {
      const row = el('div', 'pack-row');
      row.innerHTML =
        '<div class="pack-info"><strong>' + p.name + '</strong><span>' + p.cards + ' cards</span></div>' +
        '<button class="btn btn-buy" data-pack="' + p.id + '">' + p.cost + ' Stubs</button>';
      packWrap.appendChild(row);
    });
    $all('.btn-buy', packWrap).forEach(btn => {
      btn.addEventListener('click', () => this.buyPack(btn.getAttribute('data-pack')));
    });
  };

  UI.prototype.buyPack = function (packId) {
    const pack = PK.PACKS.find(p => p.id === packId);
    if (this.save.stubs < pack.cost) { this.flashReveal(['<p>Not enough Stubs.</p>']); return; }
    const pulled = PK.openPack(this.save, packId);
    this.persist();
    this.renderCollection();
    const cardsHtml = pulled.map(c =>
      '<div class="card reveal tier-' + c.tier.name.toLowerCase() + '">' +
      '<div class="card-tier">' + c.tier.name + '</div>' +
      '<div class="card-name">' + c.name + '</div>' +
      '<div class="card-pos">' + c.pos + '</div>' +
      '<div class="card-ovr">' + c.ovr + '</div></div>'
    );
    this.flashReveal(cardsHtml);
  };

  UI.prototype.flashReveal = function (cardsHtml) {
    const box = $('#pack-reveal');
    box.innerHTML = cardsHtml.join('');
    box.classList.add('show');
    clearTimeout(this._revealTimer);
    this._revealTimer = setTimeout(() => box.classList.remove('show'), 3800);
  };

  UI.prototype.awardStubs = function (n, reason) {
    this.save.stubs += n;
    this.persist();
    this.updateStubsBadges();
  };

  /* --------------------------------------------------------------- franchise */

  UI.prototype.openFranchise = function () {
    if (this.save.franchise) this.renderFranchise();
    else this.renderFranchiseSetup();
    this.show('screen-franchise');
  };

  UI.prototype.renderFranchiseSetup = function () {
    const wrap = $('#franchise-body');
    wrap.innerHTML = '';
    const pick = el('div', 'panel');
    pick.innerHTML = '<h3>Start a Season</h3><p class="muted">Pick your club. Two games against each of the other seven — 14 in all — then a one-game Pennant Series against the best of the rest.</p>' +
      '<select id="fr-pick-team">' + this.teamOptionsHtml('BOL') + '</select>' +
      '<button id="fr-begin" class="btn btn-primary">Start Season</button>';
    wrap.appendChild(pick);
    $('#fr-begin').addEventListener('click', () => {
      this.beginFranchise($('#fr-pick-team').value);
    });
  };

  UI.prototype.beginFranchise = function (teamId) {
    const opponents = D.TEAM_DEFS.filter(t => t.id !== teamId || teamId === 'YOU').map(t => t.id);
    const oppIds = teamId === 'YOU' ? D.TEAM_DEFS.map(t => t.id) : D.TEAM_DEFS.filter(t => t.id !== teamId).map(t => t.id);
    const schedule = [];
    oppIds.forEach((oppId, i) => {
      schedule.push({ opp: oppId, home: i % 2 === 0, played: false });
      schedule.push({ opp: oppId, home: i % 2 !== 0, played: false });
    });
    this.save.franchise = {
      teamId: teamId, schedule: schedule, results: [],
      standings: this.freshStandings(teamId),
      stage: 'season', idx: 0
    };
    this.persist();
    this.renderFranchise();
  };

  UI.prototype.freshStandings = function (teamId) {
    const ids = teamId === 'YOU' ? ['YOU'].concat(D.TEAM_DEFS.map(t => t.id)) : D.TEAM_DEFS.map(t => t.id);
    const s = {};
    ids.forEach(id => { s[id] = { w: 0, l: 0, rf: 0, ra: 0 }; });
    return s;
  };

  UI.prototype.teamLabel = function (id) {
    if (id === 'YOU') return 'Your Legends';
    const t = D.TEAM_DEFS.find(x => x.id === id);
    return t ? t.city + ' ' + t.nick : id;
  };

  UI.prototype.renderFranchise = function () {
    const fr = this.save.franchise;
    const wrap = $('#franchise-body');
    wrap.innerHTML = '';

    const standingsPanel = el('div', 'panel');
    let rows = Object.keys(fr.standings).map(id => Object.assign({ id: id }, fr.standings[id]));
    rows.sort((a, b) => (b.w - b.l) - (a.w - a.l));
    let sHtml = '<h3>Standings</h3><table class="tbl"><tr><th>Team</th><th>W</th><th>L</th><th>RD</th></tr>';
    rows.forEach(r => {
      sHtml += '<tr' + (r.id === fr.teamId ? ' class="me"' : '') + '><td>' + this.teamLabel(r.id) + '</td><td>' + r.w + '</td><td>' + r.l + '</td><td>' + (r.rf - r.ra >= 0 ? '+' : '') + (r.rf - r.ra) + '</td></tr>';
    });
    sHtml += '</table>';
    standingsPanel.innerHTML = sHtml;
    wrap.appendChild(standingsPanel);

    if (fr.stage === 'season') {
      const next = fr.schedule[fr.idx];
      const gamesPanel = el('div', 'panel');
      if (next) {
        gamesPanel.innerHTML = '<h3>Game ' + (fr.idx + 1) + ' of ' + fr.schedule.length + '</h3>' +
          '<p>' + (next.home ? this.teamLabel(fr.teamId) + ' vs ' + this.teamLabel(next.opp) + ' (Home)' :
                                this.teamLabel(fr.teamId) + ' @ ' + this.teamLabel(next.opp) + ' (Away)') + '</p>' +
          '<button id="fr-play" class="btn btn-primary">Play Game</button>' +
          '<button id="fr-sim" class="btn">Sim Game</button>' +
          '<button id="fr-sim-rest" class="btn">Sim Rest of Season</button>';
        wrap.appendChild(gamesPanel);
        $('#fr-play', gamesPanel).addEventListener('click', () => this.playFranchiseGame(false));
        $('#fr-sim', gamesPanel).addEventListener('click', () => this.playFranchiseGame(true));
        $('#fr-sim-rest', gamesPanel).addEventListener('click', () => this.simRestOfSeason());
      } else {
        this.advanceToPennant();
      }
    } else if (fr.stage === 'pennant') {
      const p = fr.pennant;
      const panel = el('div', 'panel');
      panel.innerHTML = '<h3>Pennant Series</h3><p>' + this.teamLabel(fr.teamId) + ' vs ' + this.teamLabel(p.opp) + ' — winner takes the flag.</p>' +
        '<button id="fr-play-pennant" class="btn btn-primary">Play Pennant Game</button>' +
        '<button id="fr-sim-pennant" class="btn">Sim Pennant Game</button>';
      wrap.appendChild(panel);
      $('#fr-play-pennant', panel).addEventListener('click', () => this.playPennant(false));
      $('#fr-sim-pennant', panel).addEventListener('click', () => this.playPennant(true));
    } else if (fr.stage === 'done') {
      const panel = el('div', 'panel');
      panel.innerHTML = '<h3>' + (fr.champion === fr.teamId ? 'Champions!' : 'Season Over') + '</h3>' +
        '<p>' + this.teamLabel(fr.champion) + ' win the pennant.</p>' +
        '<button id="fr-new" class="btn btn-primary">Start New Season</button>';
      wrap.appendChild(panel);
      $('#fr-new', panel).addEventListener('click', () => { this.save.franchise = null; this.persist(); this.renderFranchiseSetup(); });
    }

    const log = el('div', 'panel');
    log.innerHTML = '<h3>Recent Results</h3>' + (fr.results.slice(-6).reverse().map(r => '<div class="result-line">' + r + '</div>').join('') || '<p class="muted">None yet.</p>');
    wrap.appendChild(log);
  };

  UI.prototype.recordResult = function (winnerId, loserId, wr, lr) {
    const fr = this.save.franchise;
    if (fr.standings[winnerId]) { fr.standings[winnerId].w++; fr.standings[winnerId].rf += wr; fr.standings[winnerId].ra += lr; }
    if (fr.standings[loserId]) { fr.standings[loserId].l++; fr.standings[loserId].rf += lr; fr.standings[loserId].ra += wr; }
  };

  UI.prototype.simOneFranchiseGame = function (game) {
    const away = game.away, home = game.home;
    game.simGame();
    const winnerTeam = game.score.home > game.score.away ? home : away;
    const loserTeam = winnerTeam === home ? away : home;
    const winnerId = winnerTeam.id, loserId = loserTeam.id;
    this.recordResult(winnerId, loserId, Math.max(game.score.home, game.score.away), Math.min(game.score.home, game.score.away));
    this.save.franchise.results.push(away.name + ' ' + game.score.away + ' — ' + home.name + ' ' + game.score.home);
    return { winnerId: winnerId, game: game };
  };

  UI.prototype.playFranchiseGame = function (simOnly) {
    const fr = this.save.franchise;
    const next = fr.schedule[fr.idx];
    const me = this.resolveTeam(fr.teamId);
    const opp = this.resolveTeam(next.opp);
    const away = next.home ? opp : me;
    const home = next.home ? me : opp;
    const game = new BM.Game({
      away: away, home: home, stadium: D.STADIUMS[Math.floor(Math.random() * D.STADIUMS.length)],
      innings: 9, difficulty: this.save.settings.difficulty,
      userSide: simOnly ? null : (next.home ? 'home' : 'away')
    });
    fr.idx++;
    if (simOnly) {
      this.simOneFranchiseGame(game);
      this.persist();
      this.renderFranchise();
    } else {
      this.persist();
      this.app.launchLiveGame(game, () => {
        const winnerTeam = game.score.home > game.score.away ? home : away;
        const loserTeam = winnerTeam === home ? away : home;
        this.recordResult(winnerTeam.id, loserTeam.id, Math.max(game.score.home, game.score.away), Math.min(game.score.home, game.score.away));
        fr.results.push(away.name + ' ' + game.score.away + ' — ' + home.name + ' ' + game.score.home);
        const won = winnerTeam.id === fr.teamId;
        this.awardStubs(won ? 150 : 60, 'franchise game');
        this.persist();
        this.show('screen-franchise');
        this.renderFranchise();
      });
    }
  };

  UI.prototype.simRestOfSeason = function () {
    const fr = this.save.franchise;
    while (fr.idx < fr.schedule.length) {
      const next = fr.schedule[fr.idx];
      const me = this.resolveTeam(fr.teamId);
      const opp = this.resolveTeam(next.opp);
      const away = next.home ? opp : me;
      const home = next.home ? me : opp;
      const game = new BM.Game({ away: away, home: home, stadium: D.STADIUMS[0], innings: 9, difficulty: this.save.settings.difficulty });
      this.simOneFranchiseGame(game);
      fr.idx++;
    }
    this.persist();
    this.renderFranchise();
  };

  UI.prototype.advanceToPennant = function () {
    const fr = this.save.franchise;
    const rows = Object.keys(fr.standings).filter(id => id !== fr.teamId)
      .map(id => Object.assign({ id: id }, fr.standings[id]))
      .sort((a, b) => (b.w - b.l) - (a.w - a.l));
    fr.pennant = { opp: rows[0].id };
    fr.stage = 'pennant';
    this.persist();
    this.renderFranchise();
  };

  UI.prototype.playPennant = function (simOnly) {
    const fr = this.save.franchise;
    const me = this.resolveTeam(fr.teamId);
    const opp = this.resolveTeam(fr.pennant.opp);
    const game = new BM.Game({
      away: opp, home: me, stadium: D.STADIUMS[Math.floor(Math.random() * D.STADIUMS.length)],
      innings: 9, difficulty: this.save.settings.difficulty, userSide: simOnly ? null : 'home'
    });
    const conclude = () => {
      const champ = game.score.home > game.score.away ? me.id : opp.id;
      fr.champion = champ; fr.stage = 'done';
      fr.results.push('PENNANT: ' + opp.name + ' ' + game.score.away + ' — ' + me.name + ' ' + game.score.home);
      this.awardStubs(champ === fr.teamId ? 800 : 200, 'pennant');
      this.persist();
      this.show('screen-franchise');
      this.renderFranchise();
    };
    if (simOnly) { game.simGame(); conclude(); }
    else this.app.launchLiveGame(game, conclude);
  };

  BM.UI = UI;
})(window.BM = window.BM || {});
