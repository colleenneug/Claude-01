/* ============================================================
   Erebus Cradle — co-op server.

   Serves the game and runs the multiplayer relay on one port, so a
   bare checkout plus `node server/server.js` is a playable co-op
   host. No dependencies.

   Authority model: the first player in a room is the host. The host
   simulates hostiles and waves and broadcasts snapshots; everyone
   else renders those snapshots and reports the damage they deal back
   to the host, which applies it. Player movement is owned by each
   client. If the host leaves, the next player is promoted.
   ============================================================ */
'use strict';

const http = require('http');
const fs = require('fs');
const path = require('path');
const ws = require('./ws');

const PORT = process.env.PORT ? Number(process.env.PORT) : 8080;
const ROOT = path.resolve(__dirname, '..');
const TICK_MS = 66;                 // ~15 Hz outbound player states
const ROOM_LIMIT = 4;

/* ---------------- static file serving ---------------- */

const TYPES = {
  '.html': 'text/html; charset=utf-8', '.js': 'text/javascript; charset=utf-8',
  '.css': 'text/css; charset=utf-8', '.json': 'application/json',
  '.webp': 'image/webp', '.png': 'image/png', '.jpg': 'image/jpeg',
  '.svg': 'image/svg+xml', '.ico': 'image/x-icon', '.woff2': 'font/woff2'
};

function serve(req, res) {
  let rel = decodeURIComponent(req.url.split('?')[0]);
  if (rel === '/') rel = '/index.html';

  const file = path.join(ROOT, rel);
  // never serve outside the checkout
  if (!file.startsWith(ROOT)) { res.writeHead(403); res.end('forbidden'); return; }

  fs.readFile(file, (err, data) => {
    if (err) { res.writeHead(404); res.end('not found'); return; }
    res.writeHead(200, {
      'Content-Type': TYPES[path.extname(file).toLowerCase()] || 'application/octet-stream',
      'Cache-Control': 'no-cache'
    });
    res.end(data);
  });
}

const server = http.createServer(serve);

/* ---------------- rooms ---------------- */

const rooms = new Map();            // code -> { code, players: Map, hostId, mission }
let nextId = 1;

const roomCode = () => {
  const letters = 'ABCDEFGHJKLMNPQRSTUVWXYZ23456789';
  let code;
  do {
    code = Array.from({ length: 4 }, () => letters[Math.floor(Math.random() * letters.length)]).join('');
  } while (rooms.has(code));
  return code;
};

function getRoom(code) {
  if (!rooms.has(code)) rooms.set(code, { code, players: new Map(), hostId: null, mission: null });
  return rooms.get(code);
}

const publicPlayer = (p) => ({
  id: p.id, name: p.name, cls: p.cls, look: p.look, power: p.power,
  host: p.room && p.room.hostId === p.id
});

function broadcast(room, msg, exceptId) {
  for (const p of room.players.values()) {
    if (p.id !== exceptId && p.sock.open) p.sock.send(msg);
  }
}

function promoteHost(room) {
  const first = room.players.values().next().value;
  room.hostId = first ? first.id : null;
  if (first) {
    broadcast(room, { t: 'host', id: room.hostId });
    log(`room ${room.code}: host is now ${first.name} (${first.id})`);
  }
}

function leave(p) {
  const room = p.room;
  if (!room) return;
  room.players.delete(p.id);
  broadcast(room, { t: 'leave', id: p.id });
  log(`room ${room.code}: ${p.name} left (${room.players.size} remaining)`);

  if (room.players.size === 0) { rooms.delete(room.code); log(`room ${room.code}: closed`); }
  else if (room.hostId === p.id) promoteHost(room);
  p.room = null;
}

const log = (m) => console.log(`[${new Date().toISOString().slice(11, 19)}] ${m}`);

/* ---------------- connections ---------------- */

ws.attach(server, '/ws', (sock) => {
  const player = {
    id: 'p' + (nextId++), sock: sock, room: null,
    name: 'OPERATIVE', cls: 'bulwark', look: null, power: 0,
    state: null, lastSeen: Date.now()
  };

  sock.on('message', (msg) => {
    player.lastSeen = Date.now();
    if (!msg || typeof msg.t !== 'string') return;

    switch (msg.t) {
      /* ---- join or create a room ---- */
      case 'hello': {
        player.name = String(msg.name || 'OPERATIVE').slice(0, 18);
        player.cls = String(msg.cls || 'bulwark').slice(0, 16);
        player.look = msg.look || null;
        player.power = Number(msg.power) || 0;

        const code = msg.room ? String(msg.room).toUpperCase().slice(0, 6) : roomCode();
        const room = getRoom(code);
        if (room.players.size >= ROOM_LIMIT) {
          sock.send({ t: 'error', reason: 'ROOM FULL' });
          return;
        }
        player.room = room;
        room.players.set(player.id, player);
        if (!room.hostId) room.hostId = player.id;

        sock.send({
          t: 'welcome', id: player.id, room: code, hostId: room.hostId,
          mission: room.mission,
          players: Array.from(room.players.values()).filter((q) => q.id !== player.id).map(publicPlayer)
        });
        broadcast(room, { t: 'join', player: publicPlayer(player) }, player.id);
        log(`room ${code}: ${player.name} joined (${room.players.size}/${ROOM_LIMIT})`);
        break;
      }

      /* ---- per-frame player state, relayed on the room tick ---- */
      case 'state':
        player.state = msg.s;
        break;

      /* ---- host-owned world state ---- */
      case 'enemies':
      case 'wave':
      case 'objective':
        if (player.room && player.room.hostId === player.id) {
          broadcast(player.room, { t: msg.t, d: msg.d }, player.id);
        }
        break;

      /* ---- the host decides which mission everyone is in ---- */
      case 'mission':
        if (player.room && player.room.hostId === player.id) {
          player.room.mission = msg.d;
          broadcast(player.room, { t: 'mission', d: msg.d }, player.id);
          log(`room ${player.room.code}: mission ${JSON.stringify(msg.d)}`);
        }
        break;

      /* ---- damage a client dealt, forwarded to the host to apply ---- */
      case 'hit': {
        if (!player.room) break;
        const host = player.room.players.get(player.room.hostId);
        if (host && host.sock.open) host.sock.send({ t: 'hit', from: player.id, d: msg.d });
        break;
      }

      /* ---- anything else everyone should see (shots, kills, downs) ---- */
      case 'event':
        if (player.room) broadcast(player.room, { t: 'event', from: player.id, d: msg.d }, player.id);
        break;

      case 'ping':
        sock.send({ t: 'pong', at: msg.at });
        break;
      default:
        break;
    }
  });

  sock.on('close', () => leave(player));
});

/* ---------------- room tick ---------------- */

setInterval(() => {
  for (const room of rooms.values()) {
    const states = {};
    let any = false;
    for (const p of room.players.values()) {
      if (!p.state) continue;
      states[p.id] = p.state;
      any = true;
    }
    if (!any) continue;
    for (const p of room.players.values()) {
      if (!p.sock.open) continue;
      const others = {};
      for (const id of Object.keys(states)) if (id !== p.id) others[id] = states[id];
      if (Object.keys(others).length) p.sock.send({ t: 'states', d: others });
    }
  }
}, TICK_MS);

/* drop connections that have gone quiet */
setInterval(() => {
  for (const room of rooms.values()) {
    for (const p of Array.from(room.players.values())) {
      if (Date.now() - p.lastSeen > 30000) { log(`timeout: ${p.name}`); p.sock.close(); }
      else if (p.sock.open) p.sock.ping();
    }
  }
}, 10000);

server.listen(PORT, () => {
  log(`Erebus Cradle server on http://localhost:${PORT}`);
  log(`  game:  http://localhost:${PORT}/`);
  log(`  co-op: ws://localhost:${PORT}/ws`);
});

module.exports = { server, rooms };
