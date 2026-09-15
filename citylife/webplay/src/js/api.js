// Local-device "accounts": no server. Profiles live in this browser's localStorage.
// Not real security — just enough of a login feel to keep separate saves apart
// on a shared machine. Nothing here ever leaves the device.
window.CityAPI = (function () {
  const DB_KEY = 'citylife_db_v1';
  const SESSION_KEY = 'citylife_session_v1';
  const STARTING = {
    mayor: 2000, cop: 1500, criminal: 1000, superhero: 1200, citizen: 800,
  };

  function loadDB() {
    try { return JSON.parse(localStorage.getItem(DB_KEY)) || { users: {} }; }
    catch (e) { return { users: {} }; }
  }
  function saveDB(db) { localStorage.setItem(DB_KEY, JSON.stringify(db)); }

  async function hash(password, salt) {
    const enc = new TextEncoder().encode(salt + ':' + password);
    const digest = await crypto.subtle.digest('SHA-256', enc);
    return Array.from(new Uint8Array(digest)).map((b) => b.toString(16).padStart(2, '0')).join('');
  }
  function randomSalt() {
    const bytes = crypto.getRandomValues(new Uint8Array(12));
    return Array.from(bytes).map((b) => b.toString(16).padStart(2, '0')).join('');
  }

  function publicState(user) {
    return {
      username: user.username,
      role: user.role,
      kidsMode: !!user.kidsMode,
      money: user.money,
      health: user.health,
      energy: user.energy,
      businesses: user.businesses,
      lastRobberyAt: user.lastRobberyAt || 0,
      hasChosenRole: !!user.role,
    };
  }

  function getSession() {
    const key = localStorage.getItem(SESSION_KEY);
    if (!key) return null;
    const db = loadDB();
    return db.users[key] || null;
  }

  const USERNAME_RE = /^[a-zA-Z0-9_]{3,20}$/;

  async function register(username, password) {
    username = String(username).trim();
    if (!USERNAME_RE.test(username)) throw new Error('Username must be 3-20 letters, numbers or underscores.');
    if (String(password).length < 4) throw new Error('Password must be at least 4 characters.');
    const db = loadDB();
    const key = username.toLowerCase();
    if (db.users[key]) throw new Error('That name already has a save on this device.');
    const salt = randomSalt();
    const passHash = await hash(password, salt);
    const user = {
      username, salt, hash: passHash,
      role: null, kidsMode: false,
      money: 0, health: 100, energy: 100,
      businesses: [], lastRobberyAt: 0, createdAt: Date.now(),
    };
    db.users[key] = user;
    saveDB(db);
    localStorage.setItem(SESSION_KEY, key);
    return { player: publicState(user) };
  }

  async function login(username, password) {
    const db = loadDB();
    const key = String(username).trim().toLowerCase();
    const user = db.users[key];
    if (!user || (await hash(password, user.salt)) !== user.hash) {
      throw new Error('Wrong name or password for this device.');
    }
    localStorage.setItem(SESSION_KEY, key);
    return { player: publicState(user) };
  }

  function logout() {
    localStorage.removeItem(SESSION_KEY);
    return Promise.resolve({ ok: true });
  }

  async function me() {
    const user = getSession();
    if (!user) { const e = new Error('Not logged in.'); e.status = 401; throw e; }
    return { player: publicState(user) };
  }

  async function save(partial) {
    const db = loadDB();
    const key = localStorage.getItem(SESSION_KEY);
    const user = db.users[key];
    if (!user) { const e = new Error('Not logged in.'); e.status = 401; throw e; }
    if (typeof partial.money === 'number' && isFinite(partial.money)) user.money = Math.max(0, partial.money);
    if (typeof partial.health === 'number' && isFinite(partial.health)) user.health = Math.max(0, Math.min(100, partial.health));
    if (typeof partial.energy === 'number' && isFinite(partial.energy)) user.energy = Math.max(0, Math.min(100, partial.energy));
    if (typeof partial.role === 'string') user.role = partial.role;
    if (typeof partial.kidsMode === 'boolean') user.kidsMode = partial.kidsMode;
    if (Array.isArray(partial.businesses)) user.businesses = partial.businesses.slice(0, 50);
    saveDB(db);
    return { player: publicState(user) };
  }

  const BANK_ROB_COOLDOWN_MS = 5 * 60 * 1000;
  const BANK_ROB_MIN = 8000, BANK_ROB_MAX = 14000;

  async function robBank() {
    const db = loadDB();
    const key = localStorage.getItem(SESSION_KEY);
    const user = db.users[key];
    if (!user) { const e = new Error('Not logged in.'); e.status = 401; throw e; }
    const now = Date.now();
    const remaining = (user.lastRobberyAt || 0) + BANK_ROB_COOLDOWN_MS - now;
    if (remaining > 0) {
      const e = new Error('Bank is still on cooldown.');
      e.data = { remainingMs: remaining };
      throw e;
    }
    const take = Math.floor(BANK_ROB_MIN + Math.random() * (BANK_ROB_MAX - BANK_ROB_MIN));
    user.money += take;
    user.lastRobberyAt = now;
    saveDB(db);
    return { take, player: publicState(user) };
  }

  function leaderboard() {
    const db = loadDB();
    const rows = Object.values(db.users)
      .map((u) => ({ username: u.username, money: u.money, role: u.role }))
      .sort((a, b) => b.money - a.money)
      .slice(0, 20);
    return Promise.resolve({ leaderboard: rows });
  }

  return { register, login, logout, me, save, robBank, leaderboard, STARTING };
})();
