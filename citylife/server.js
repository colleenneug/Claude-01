// City Life -- standalone server. No dependencies: Node's http/fs/crypto only.
// Serves the game and a tiny JSON API for accounts (signup/login) and save data.
//
//   node citylife/server.js          # http://localhost:8090
//   PORT=9000 node citylife/server.js
'use strict';

const http = require('http');
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');

const ROOT = __dirname;
const REPO_ROOT = path.join(ROOT, '..');
const DATA_DIR = path.join(ROOT, 'data');
const DB_FILE = path.join(DATA_DIR, 'db.json');
const PORT = Number(process.env.PORT) || 8090;

const SESSION_TTL_MS = 30 * 24 * 60 * 60 * 1000; // 30 days
const STARTING_MONEY = 5000;
const BANK_ROB_COOLDOWN_MS = 5 * 60 * 1000;
const BANK_ROB_MIN = 800;
const BANK_ROB_MAX = 2600;

// ---------- tiny JSON "database" ----------

function loadDB() {
  try {
    return JSON.parse(fs.readFileSync(DB_FILE, 'utf8'));
  } catch (err) {
    return { users: {}, sessions: {} };
  }
}

let db = loadDB();
let saveTimer = null;
function persist() {
  if (saveTimer) return;
  saveTimer = setTimeout(() => {
    saveTimer = null;
    fs.mkdirSync(DATA_DIR, { recursive: true });
    fs.writeFileSync(DB_FILE, JSON.stringify(db, null, 2));
  }, 150);
}

function hashPassword(password, salt) {
  salt = salt || crypto.randomBytes(16).toString('hex');
  const hash = crypto.scryptSync(password, salt, 64).toString('hex');
  return { salt, hash };
}

function verifyPassword(password, salt, hash) {
  const check = crypto.scryptSync(password, salt, 64).toString('hex');
  return crypto.timingSafeEqual(Buffer.from(check, 'hex'), Buffer.from(hash, 'hex'));
}

function newSessionToken() {
  return crypto.randomBytes(24).toString('hex');
}

function publicState(user) {
  return {
    username: user.username,
    role: user.role,
    money: user.money,
    health: user.health,
    businesses: user.businesses,
    lastRobberyAt: user.lastRobberyAt || 0,
    createdAt: user.createdAt,
  };
}

// ---------- request helpers ----------

function readBody(req) {
  return new Promise((resolve, reject) => {
    let data = '';
    let size = 0;
    req.on('data', (chunk) => {
      size += chunk.length;
      if (size > 1e6) { reject(new Error('body too large')); req.destroy(); return; }
      data += chunk;
    });
    req.on('end', () => {
      if (!data) return resolve({});
      try { resolve(JSON.parse(data)); } catch (e) { reject(e); }
    });
    req.on('error', reject);
  });
}

function sendJSON(res, status, obj, extraHeaders) {
  const body = JSON.stringify(obj);
  res.writeHead(status, Object.assign({
    'Content-Type': 'application/json; charset=utf-8',
    'Content-Length': Buffer.byteLength(body),
  }, extraHeaders || {}));
  res.end(body);
}

function parseCookies(req) {
  const header = req.headers.cookie;
  const out = {};
  if (!header) return out;
  header.split(';').forEach((pair) => {
    const idx = pair.indexOf('=');
    if (idx === -1) return;
    out[pair.slice(0, idx).trim()] = decodeURIComponent(pair.slice(idx + 1).trim());
  });
  return out;
}

function sessionCookie(token) {
  const maxAge = Math.floor(SESSION_TTL_MS / 1000);
  return `citylife_session=${token}; Path=/; HttpOnly; SameSite=Lax; Max-Age=${maxAge}`;
}

function currentUser(req) {
  const cookies = parseCookies(req);
  const token = cookies.citylife_session;
  if (!token) return null;
  const session = db.sessions[token];
  if (!session) return null;
  if (session.expiresAt < Date.now()) { delete db.sessions[token]; persist(); return null; }
  const user = db.users[session.username];
  return user || null;
}

// ---------- API handlers ----------

const USERNAME_RE = /^[a-zA-Z0-9_]{3,20}$/;
const VALID_ROLES = new Set(['criminal', 'superhero', 'villain']);

async function handleRegister(req, res) {
  const body = await readBody(req);
  const username = String(body.username || '').trim();
  const password = String(body.password || '');
  if (!USERNAME_RE.test(username)) {
    return sendJSON(res, 400, { error: 'Username must be 3-20 letters, numbers or underscores.' });
  }
  if (password.length < 6) {
    return sendJSON(res, 400, { error: 'Password must be at least 6 characters.' });
  }
  const key = username.toLowerCase();
  if (db.users[key]) {
    return sendJSON(res, 409, { error: 'That username is already taken.' });
  }
  const { salt, hash } = hashPassword(password);
  const user = {
    username,
    salt,
    hash,
    role: 'criminal',
    money: STARTING_MONEY,
    health: 100,
    businesses: [],
    lastRobberyAt: 0,
    createdAt: Date.now(),
  };
  db.users[key] = user;
  const token = newSessionToken();
  db.sessions[token] = { username: key, expiresAt: Date.now() + SESSION_TTL_MS };
  persist();
  sendJSON(res, 200, { player: publicState(user) }, { 'Set-Cookie': sessionCookie(token) });
}

async function handleLogin(req, res) {
  const body = await readBody(req);
  const username = String(body.username || '').trim();
  const password = String(body.password || '');
  const key = username.toLowerCase();
  const user = db.users[key];
  if (!user || !verifyPassword(password, user.salt, user.hash)) {
    return sendJSON(res, 401, { error: 'Wrong username or password.' });
  }
  const token = newSessionToken();
  db.sessions[token] = { username: key, expiresAt: Date.now() + SESSION_TTL_MS };
  persist();
  sendJSON(res, 200, { player: publicState(user) }, { 'Set-Cookie': sessionCookie(token) });
}

function handleLogout(req, res) {
  const cookies = parseCookies(req);
  const token = cookies.citylife_session;
  if (token) { delete db.sessions[token]; persist(); }
  sendJSON(res, 200, { ok: true }, { 'Set-Cookie': 'citylife_session=; Path=/; HttpOnly; Max-Age=0' });
}

function handleMe(req, res) {
  const user = currentUser(req);
  if (!user) return sendJSON(res, 401, { error: 'Not logged in.' });
  sendJSON(res, 200, { player: publicState(user) });
}

async function handleSave(req, res) {
  const user = currentUser(req);
  if (!user) return sendJSON(res, 401, { error: 'Not logged in.' });
  const body = await readBody(req);
  if (typeof body.money === 'number' && isFinite(body.money) && body.money >= 0) {
    user.money = Math.min(body.money, 1e12);
  }
  if (typeof body.health === 'number' && isFinite(body.health)) {
    user.health = Math.max(0, Math.min(100, body.health));
  }
  if (typeof body.role === 'string' && VALID_ROLES.has(body.role)) {
    user.role = body.role;
  }
  if (Array.isArray(body.businesses)) {
    user.businesses = body.businesses.filter((b) => typeof b === 'string').slice(0, 50);
  }
  persist();
  sendJSON(res, 200, { player: publicState(user) });
}

async function handleRob(req, res) {
  const user = currentUser(req);
  if (!user) return sendJSON(res, 401, { error: 'Not logged in.' });
  const now = Date.now();
  const remaining = (user.lastRobberyAt || 0) + BANK_ROB_COOLDOWN_MS - now;
  if (remaining > 0) {
    return sendJSON(res, 429, { error: 'Bank is still on cooldown.', remainingMs: remaining });
  }
  const take = Math.floor(BANK_ROB_MIN + Math.random() * (BANK_ROB_MAX - BANK_ROB_MIN));
  user.money += take;
  user.lastRobberyAt = now;
  persist();
  sendJSON(res, 200, { take, player: publicState(user) });
}

function handleLeaderboard(req, res) {
  const rows = Object.values(db.users)
    .map((u) => ({ username: u.username, money: u.money, role: u.role }))
    .sort((a, b) => b.money - a.money)
    .slice(0, 20);
  sendJSON(res, 200, { leaderboard: rows });
}

// ---------- static file serving ----------

const MIME = {
  '.html': 'text/html; charset=utf-8',
  '.js': 'text/javascript; charset=utf-8',
  '.css': 'text/css; charset=utf-8',
  '.json': 'application/json; charset=utf-8',
  '.png': 'image/png',
  '.jpg': 'image/jpeg',
  '.svg': 'image/svg+xml',
};

function serveStatic(req, res, urlPath) {
  let rel = urlPath === '/' ? '/index.html' : urlPath;
  let base = ROOT;
  if (rel.startsWith('/vendor/')) {
    base = REPO_ROOT; // reuse the three.js build vendored for the FPS game
  }
  const filePath = path.normalize(path.join(base, rel));
  if (!filePath.startsWith(base)) { res.writeHead(403); return res.end('Forbidden'); }
  fs.readFile(filePath, (err, data) => {
    if (err) { res.writeHead(404); return res.end('Not found'); }
    const ext = path.extname(filePath).toLowerCase();
    res.writeHead(200, { 'Content-Type': MIME[ext] || 'application/octet-stream' });
    res.end(data);
  });
}

// ---------- router ----------

const server = http.createServer((req, res) => {
  const url = new URL(req.url, `http://${req.headers.host}`);
  const p = url.pathname;
  try {
    if (p === '/api/register' && req.method === 'POST') return void handleRegister(req, res);
    if (p === '/api/login' && req.method === 'POST') return void handleLogin(req, res);
    if (p === '/api/logout' && req.method === 'POST') return void handleLogout(req, res);
    if (p === '/api/me' && req.method === 'GET') return void handleMe(req, res);
    if (p === '/api/save' && req.method === 'POST') return void handleSave(req, res);
    if (p === '/api/bank/rob' && req.method === 'POST') return void handleRob(req, res);
    if (p === '/api/leaderboard' && req.method === 'GET') return void handleLeaderboard(req, res);
    if (p.startsWith('/api/')) return sendJSON(res, 404, { error: 'Unknown endpoint.' });
    return serveStatic(req, res, p);
  } catch (err) {
    sendJSON(res, 500, { error: 'Server error.' });
  }
});

server.listen(PORT, () => {
  console.log(`City Life running at http://localhost:${PORT}`);
});
