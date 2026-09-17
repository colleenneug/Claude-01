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
const { spawn } = require('child_process');
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

/* ---------------- native build launcher ---------------- */
/*
   The browser build's title screen carries a LAUNCH GAME plate for the
   C++ desktop build in cpp/. A page can't start a process, so it asks
   here and this spawns cpp/build/erebus_native.

   Deliberately narrow: no argument comes off the wire except a mission
   id, and that is matched against the .cfg files actually present under
   cpp/content/missions rather than passed through — the request can
   pick which mission, never what to run. Loopback only, since a machine
   serving this on a LAN shouldn't hand every client on it a process.
*/

const NATIVE_DIR = path.join(ROOT, 'cpp', 'build');
const NATIVE_BIN = path.join(NATIVE_DIR, process.platform === 'win32' ? 'erebus_native.exe' : 'erebus_native');
const MISSION_DIR = path.join(ROOT, 'cpp', 'content', 'missions');

const BUILD_CMD = 'cmake -S cpp -B cpp/build -DCMAKE_BUILD_TYPE=Release && cmake --build cpp/build -j';

let native = null;                  // the one live child, or null
let install = null;                 // the one live install, or the last one

const loopback = (req) => {
  const a = req.socket.remoteAddress || '';
  return a === '127.0.0.1' || a === '::1' || a === '::ffff:127.0.0.1';
};

/* A display is needed to open a window; without one the process starts
   and dies immediately, which reads as "the button did nothing". Say so
   instead. macOS and Windows always have one. */
const headless = () =>
  process.platform === 'linux' && !process.env.DISPLAY && !process.env.WAYLAND_DISPLAY;

/* Is the compiled binary older than the code it was built from? That is
   the whole of "this needs updating": the sources that actually go into
   the build, newest mtime, against the binary's own. Cheap enough to
   answer on every poll, cached for a couple of seconds because the page
   polls while an install runs. Content and shaders count — they are
   copied next to the binary at build time, so a changed mission or
   shader is just as stale as changed C++. */

const SOURCE_DIRS = ['src', 'shaders', 'content'];
let staleCache = { at: 0, value: null };

function newestSource() {
  let newest = 0;
  let file = null;
  const visit = (dir) => {
    let entries;
    try { entries = fs.readdirSync(dir, { withFileTypes: true }); } catch (e) { return; }
    for (const entry of entries) {
      const full = path.join(dir, entry.name);
      if (entry.isDirectory()) { visit(full); continue; }
      let st;
      try { st = fs.statSync(full); } catch (e) { continue; }
      if (st.mtimeMs > newest) { newest = st.mtimeMs; file = full; }
    }
  };
  for (const d of SOURCE_DIRS) visit(path.join(ROOT, 'cpp', d));
  try {
    const st = fs.statSync(path.join(ROOT, 'cpp', 'CMakeLists.txt'));
    if (st.mtimeMs > newest) { newest = st.mtimeMs; file = path.join(ROOT, 'cpp', 'CMakeLists.txt'); }
  } catch (e) { /* no CMakeLists: nothing to compare against */ }
  return { newest, file };
}

function staleness() {
  if (Date.now() - staleCache.at < 2000) return staleCache.value;
  let value = { stale: false, staleFile: null, builtAt: null };
  try {
    const bin = fs.statSync(NATIVE_BIN);
    const src = newestSource();
    value = {
      stale: src.newest > bin.mtimeMs,
      staleFile: src.newest > bin.mtimeMs ? path.relative(ROOT, src.file) : null,
      builtAt: new Date(bin.mtimeMs).toISOString()
    };
  } catch (e) { /* not built yet: not stale, just absent */ }
  staleCache = { at: Date.now(), value };
  return value;
}

function nativeStatus() {
  const age = staleness();
  return {
    built: fs.existsSync(NATIVE_BIN),
    running: !!native,
    pid: native ? native.pid : null,
    display: !headless(),
    installing: !!(install && install.running),
    installExit: install && !install.running ? install.code : null,
    stale: age.stale,
    staleFile: age.staleFile,
    builtAt: age.builtAt,
    binary: path.relative(ROOT, NATIVE_BIN),
    buildCmd: BUILD_CMD
  };
}

/* The launcher page can also be a published artifact on another origin,
   so these two endpoints answer cross-origin requests. What guards them
   is not the origin but the socket: every state-changing path checks
   loopback() first, so a request only ever starts a process on the
   machine it came from, and the one process it can start is this
   project's own game binary. Chrome also asks permission before a public
   page may reach a private address at all, which is the private-network
   header below. */
function cors(req, res) {
  const origin = req.headers.origin;
  if (origin) {
    res.setHeader('Access-Control-Allow-Origin', origin);
    res.setHeader('Vary', 'Origin');
  }
  res.setHeader('Access-Control-Allow-Private-Network', 'true');
}

function sendJson(res, code, body) {
  const data = JSON.stringify(body);
  res.writeHead(code, { 'Content-Type': 'application/json', 'Cache-Control': 'no-store' });
  res.end(data);
}

/* A mission id is only ever a filename we already have on disk. */
function missionArgs(id) {
  if (!id) return [];
  if (!/^[a-z0-9_-]{1,40}$/.test(String(id))) return null;
  if (!fs.existsSync(path.join(MISSION_DIR, id + '.cfg'))) return null;
  return ['--mission', String(id)];
}

function launchNative(req, res, body) {
  if (!loopback(req)) {
    return sendJson(res, 403, { ok: false, reason: 'REMOTE',
      detail: 'Only the machine running this server can launch the desktop build.' });
  }
  if (native) {
    return sendJson(res, 409, { ok: false, reason: 'RUNNING', pid: native.pid,
      detail: 'The native build is already running (pid ' + native.pid + ').', status: nativeStatus() });
  }
  if (!fs.existsSync(NATIVE_BIN)) {
    return sendJson(res, 404, { ok: false, reason: 'NOT_BUILT',
      detail: 'The native build has not been compiled yet. Build it once, then this button launches it.',
      buildCmd: BUILD_CMD, status: nativeStatus() });
  }
  if (headless()) {
    return sendJson(res, 409, { ok: false, reason: 'NO_DISPLAY',
      detail: 'This host has no display attached, so the game window has nowhere to open. Run it from a desktop session.',
      status: nativeStatus() });
  }

  const args = missionArgs(body && body.mission);
  if (args === null) {
    return sendJson(res, 400, { ok: false, reason: 'BAD_MISSION',
      detail: 'No such mission under cpp/content/missions.', status: nativeStatus() });
  }

  let child;
  try {
    // cwd is the build directory because CMake copies content/ and
    // shaders/ next to the binary, and the game resolves both relative
    // to where it is run from.
    child = spawn(NATIVE_BIN, args, { cwd: NATIVE_DIR, detached: true, stdio: 'ignore' });
  } catch (err) {
    return sendJson(res, 500, { ok: false, reason: 'SPAWN_FAILED', detail: err.message, status: nativeStatus() });
  }

  // Unref'd so quitting the server doesn't take the game down with it;
  // exit still reports back here while the server is up, which is what
  // lets the page's status line fall back to READY on its own.
  child.unref();
  native = child;
  log('native build launched (pid ' + child.pid + ')' + (args.length ? ' mission ' + args[1] : ''));

  child.on('error', (err) => {
    log('native build failed to start: ' + err.message);
    if (native === child) native = null;
  });
  child.on('exit', (code, signal) => {
    log('native build exited (' + (signal || code) + ')');
    if (native === child) native = null;
  });

  sendJson(res, 200, { ok: true, pid: child.pid, status: nativeStatus() });
}

/* ---------------- installing it ---------------- */
/*
   The launcher's one button installs the desktop build and then opens
   it, so the install has to be something a page can start and watch:
   this runs tools/install-native.sh (build only, never the game) and
   keeps its output in a ring buffer the page polls.

   Nothing from the request reaches the command line — there is no
   argument to pass. The script is the same one a person runs by hand,
   which is deliberate: one install path, not a second one that only
   exists in here and drifts.
*/

const INSTALL_SCRIPT = path.join(ROOT, 'tools', 'install-native.sh');
const LOG_CAP = 400;                // lines kept for the page to catch up on

function pushLine(line) {
  install.total++;
  install.lines.push(line);
  if (install.lines.length > LOG_CAP) install.lines.shift();
}

function collect(stream) {
  let buf = '';
  stream.setEncoding('utf8');
  stream.on('data', (chunk) => {
    buf += chunk;
    const parts = buf.split('\n');
    buf = parts.pop();
    for (const line of parts) pushLine(line.replace(/\u001b\[[0-9;]*m/g, ''));
  });
  stream.on('end', () => { if (buf.trim()) pushLine(buf.replace(/\u001b\[[0-9;]*m/g, '')); });
}

function startInstall(req, res) {
  if (!loopback(req)) {
    return sendJson(res, 403, { ok: false, reason: 'REMOTE',
      detail: 'Only the machine running this server can install the desktop build.' });
  }
  if (install && install.running) {
    return sendJson(res, 409, { ok: false, reason: 'INSTALLING',
      detail: 'An install is already running.', status: nativeStatus() });
  }
  if (!fs.existsSync(INSTALL_SCRIPT)) {
    return sendJson(res, 404, { ok: false, reason: 'NO_SCRIPT',
      detail: 'tools/install-native.sh is missing from this checkout.' });
  }

  const child = spawn('bash', [INSTALL_SCRIPT, '--no-run'], { cwd: ROOT, stdio: ['ignore', 'pipe', 'pipe'] });
  install = { running: true, code: null, lines: [], total: 0, pid: child.pid, startedAt: Date.now() };
  log('install started (pid ' + child.pid + ')');

  collect(child.stdout);
  collect(child.stderr);

  child.on('error', (err) => {
    pushLine('install failed to start: ' + err.message);
    install.running = false;
    install.code = -1;
  });
  child.on('close', (code) => {
    install.running = false;
    install.code = code;
    staleCache = { at: 0, value: null };          // the binary just moved
    // Exit 3 is the script's own "the packages need a password" signal;
    // the page turns that into the one command to paste, not an error.
    pushLine(code === 0 ? 'INSTALL COMPLETE' : (code === 3 ? 'DEPENDENCIES NEED A PASSWORD' : 'INSTALL FAILED (exit ' + code + ')'));
    log('install finished (exit ' + code + ')');
  });

  sendJson(res, 200, { ok: true, pid: child.pid, status: nativeStatus() });
}

/* Whatever the page hasn't seen yet, plus where the install got to. */
function installLog(req, res, query) {
  if (!install) {
    return sendJson(res, 200, { running: false, code: null, total: 0, from: 0, lines: [], status: nativeStatus() });
  }
  const since = Math.max(0, Number(query.get('since')) || 0);
  const first = install.total - install.lines.length;     // absolute index of lines[0]
  const start = Math.max(0, since - first);
  sendJson(res, 200, {
    running: install.running,
    code: install.code,
    total: install.total,
    from: first + start,
    lines: install.lines.slice(start),
    status: nativeStatus()
  });
}

/* ---------------- routing ---------------- */

function api(req, res) {
  const route = req.url.split('?')[0];
  cors(req, res);

  if (req.method === 'OPTIONS') {
    res.writeHead(204, {
      'Access-Control-Allow-Methods': 'GET, POST, OPTIONS',
      'Access-Control-Allow-Headers': 'Content-Type',
      'Access-Control-Max-Age': '600'
    });
    res.end();
    return;
  }

  if (route === '/api/native' && req.method === 'GET') {
    return sendJson(res, 200, nativeStatus());
  }
  if (route === '/api/native/install' && req.method === 'POST') {
    req.resume();                                  // nothing in the body to read
    return startInstall(req, res);
  }
  if (route === '/api/native/install/log' && req.method === 'GET') {
    return installLog(req, res, new URLSearchParams(req.url.split('?')[1] || ''));
  }
  if (route === '/api/native/launch' && req.method === 'POST') {
    let raw = '';
    req.on('data', (chunk) => {
      raw += chunk;
      if (raw.length > 1024) { raw = ''; req.destroy(); }   // nothing legitimate is this big
    });
    req.on('end', () => {
      let body = {};
      try { body = raw ? JSON.parse(raw) : {}; } catch (e) { body = {}; }
      launchNative(req, res, body);
    });
    return;
  }
  sendJson(res, 404, { ok: false, reason: 'NO_ROUTE' });
}

function handle(req, res) {
  if (req.url.split('?')[0].startsWith('/api/')) return api(req, res);
  serve(req, res);
}

const server = http.createServer(handle);

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

/* Started by the one-line bootstrap, there is nobody watching the
   terminal for a URL to click, so open the launcher itself. Every
   platform has its own opener and none of them take a shell string. */
function openBrowser(url) {
  const cmd = process.platform === 'darwin' ? 'open'
            : process.platform === 'win32' ? 'cmd'
            : 'xdg-open';
  const args = process.platform === 'win32' ? ['/c', 'start', '', url] : [url];
  try {
    const child = spawn(cmd, args, { stdio: 'ignore', detached: true });
    child.on('error', () => log('could not open a browser; visit the URL above'));
    child.unref();
  } catch (e) {
    log('could not open a browser; visit the URL above');
  }
}

server.listen(PORT, () => {
  log(`Erebus Cradle server on http://localhost:${PORT}`);
  log(`  game:  http://localhost:${PORT}/`);
  log(`  co-op: ws://localhost:${PORT}/ws`);
  log(`  launcher: http://localhost:${PORT}/launcher.html`);
  log(`  native: ${nativeStatus().built ? 'built — the launcher can start it' : 'not built — the launcher can install it'}`);
  if (process.argv.includes('--open') || process.env.EREBUS_OPEN) {
    openBrowser(`http://localhost:${PORT}/launcher.html`);
  }
});

module.exports = { server, rooms };
