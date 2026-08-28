/* ============================================================
   Co-op client.

   Talks to server/server.js. The first player in a room is the host:
   it simulates hostiles and broadcasts snapshots, and everyone else
   renders those and reports damage back. Movement is always owned by
   the client that is doing the moving.

   The whole module is optional. With no server reachable the game runs
   exactly as it always has — `active` stays false and nothing else in
   the codebase changes behaviour.
   ============================================================ */
(function (SF) {
  'use strict';

  const SEND_HZ = 15;

  function defaultUrl() {
    // the server hosts the game, so co-op lives on the same origin
    if (location.protocol === 'file:') return null;
    const proto = location.protocol === 'https:' ? 'wss:' : 'ws:';
    return proto + '//' + location.host + '/ws';
  }

  const state = {
    sock: null, active: false, id: null, room: null, hostId: null,
    players: new Map(),          // id -> { id, name, cls, look, power, state }
    lastSend: 0, status: 'offline', latency: 0
  };

  const handlers = {
    players: [], enemies: [], mission: [], hit: [], event: [], status: []
  };
  const on = (evt, fn) => { (handlers[evt] || []).push(fn); };
  const fire = (evt, arg) => { for (const fn of handlers[evt] || []) fn(arg); };

  const isHost = () => state.active && state.id && state.id === state.hostId;

  function setStatus(s) { state.status = s; fire('status', s); }

  function connect(character, roomCode, url) {
    return new Promise((resolve, reject) => {
      const target = url || defaultUrl();
      if (!target) { reject(new Error('No server reachable from file:// — run server/server.js')); return; }

      let sock;
      try { sock = new WebSocket(target); }
      catch (err) { reject(err); return; }

      const failTimer = setTimeout(() => {
        try { sock.close(); } catch (e) { /* already closing */ }
        reject(new Error('Connection timed out'));
      }, 6000);

      setStatus('connecting');

      sock.onopen = () => {
        sock.send(JSON.stringify({
          t: 'hello', name: character.name, cls: character.cls,
          look: character.look, power: SF.gear.powerOfCharacter(character),
          room: roomCode || null
        }));
      };

      sock.onmessage = (ev) => {
        let msg;
        try { msg = JSON.parse(ev.data); } catch (err) { return; }

        switch (msg.t) {
          case 'welcome':
            clearTimeout(failTimer);
            state.sock = sock;
            state.active = true;
            state.id = msg.id;
            state.room = msg.room;
            state.hostId = msg.hostId;
            state.players.clear();
            for (const p of msg.players) state.players.set(p.id, p);
            setStatus('connected');
            fire('players', roster());
            resolve({ room: msg.room, id: msg.id, host: isHost(), mission: msg.mission });
            break;

          case 'join':
            state.players.set(msg.player.id, msg.player);
            fire('players', roster());
            break;

          case 'leave':
            state.players.delete(msg.id);
            fire('players', roster());
            break;

          case 'host':
            state.hostId = msg.id;
            fire('players', roster());
            break;

          case 'states':
            for (const id of Object.keys(msg.d)) {
              const p = state.players.get(id);
              if (p) p.state = msg.d[id];
            }
            break;

          case 'enemies':  fire('enemies', msg.d); break;
          case 'mission':  fire('mission', msg.d); break;
          case 'hit':      fire('hit', { from: msg.from, d: msg.d }); break;
          case 'event':    fire('event', { from: msg.from, d: msg.d }); break;
          case 'pong':     state.latency = Date.now() - msg.at; break;
          case 'error':
            clearTimeout(failTimer);
            reject(new Error(msg.reason || 'refused'));
            break;
          default: break;
        }
      };

      sock.onclose = () => {
        clearTimeout(failTimer);
        if (state.active) {
          state.active = false;
          state.players.clear();
          setStatus('disconnected');
          fire('players', roster());
        } else {
          setStatus('offline');
          reject(new Error('Could not reach the server'));
        }
      };

      sock.onerror = () => { /* onclose reports it */ };
    });
  }

  function disconnect() {
    if (state.sock) { try { state.sock.close(); } catch (err) { /* ignore */ } }
    state.sock = null;
    state.active = false;
    state.players.clear();
    setStatus('offline');
  }

  function send(obj) {
    if (!state.active || !state.sock || state.sock.readyState !== 1) return;
    state.sock.send(JSON.stringify(obj));
  }

  /* Rate-limited player state; everything else goes out immediately. */
  function sendState(s) {
    const now = performance.now();
    if (now - state.lastSend < 1000 / SEND_HZ) return;
    state.lastSend = now;
    send({ t: 'state', s: s });
  }

  const roster = () => Array.from(state.players.values()).map((p) =>
    Object.assign({}, p, { isHost: p.id === state.hostId }));

  setInterval(() => { if (state.active) send({ t: 'ping', at: Date.now() }); }, 5000);

  SF.net = {
    connect, disconnect, send, sendState, on,
    get active() { return state.active; },
    get isHost() { return isHost(); },
    get id() { return state.id; },
    get room() { return state.room; },
    get status() { return state.status; },
    get latency() { return state.latency; },
    get players() { return state.players; },
    roster, defaultUrl
  };
})(window.SF);
