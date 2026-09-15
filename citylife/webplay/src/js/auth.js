// Screen flow: Log In / Sign Up -> Choose Your Role (new profiles) -> Enter the City -> Game.
window.CityAuth = (function () {
  const authScreen = document.getElementById('auth-screen');
  const roleScreen = document.getElementById('role-screen');
  const confirmScreen = document.getElementById('confirm-screen');
  const gameScreen = document.getElementById('game-screen');
  const errorEl = document.getElementById('auth-error');
  const loginForm = document.getElementById('login-form');
  const signupForm = document.getElementById('signup-form');
  const roleGrid = document.getElementById('role-grid');

  const confirmRoleIcon = document.getElementById('confirm-role-icon');
  const confirmRoleName = document.getElementById('confirm-role-name');
  const confirmDesc = document.getElementById('confirm-desc');
  const confirmPowers = document.getElementById('confirm-powers');

  let currentPlayer = null;

  function showError(msg) {
    errorEl.textContent = msg;
    errorEl.classList.remove('hidden');
  }
  function clearError() { errorEl.classList.add('hidden'); }

  function showScreen(el) {
    [authScreen, roleScreen, confirmScreen, gameScreen].forEach((s) => s.classList.add('hidden'));
    el.classList.remove('hidden');
  }

  document.querySelectorAll('.auth-tab').forEach((tab) => {
    tab.addEventListener('click', () => {
      document.querySelectorAll('.auth-tab').forEach((t) => t.classList.remove('active'));
      tab.classList.add('active');
      const isLogin = tab.dataset.tab === 'login';
      loginForm.classList.toggle('hidden', !isLogin);
      signupForm.classList.toggle('hidden', isLogin);
      clearError();
    });
  });

  function renderRoleGrid() {
    roleGrid.innerHTML = '';
    window.CityEconomy.ROLE_ORDER.forEach((role) => {
      const meta = window.CityEconomy.ROLE_META[role];
      const card = document.createElement('button');
      card.type = 'button';
      card.className = 'role-card';
      card.dataset.role = role;
      card.innerHTML = `
        <span class="role-icon">${meta.icon}</span>
        <span>
          <span class="role-name">${meta.label} <span class="role-start">Start: $${meta.startMoney.toLocaleString()}</span></span>
          <span class="role-desc">${meta.desc}</span>
        </span>`;
      card.addEventListener('click', () => chooseRole(role));
      roleGrid.appendChild(card);
    });
  }

  function renderConfirmScreen() {
    const meta = window.CityEconomy.ROLE_META[currentPlayer.role];
    confirmRoleIcon.textContent = meta.icon;
    confirmRoleName.textContent = meta.label;
    confirmDesc.textContent = meta.desc;
    confirmPowers.innerHTML = meta.powers.map((p) => `<li>${p}</li>`).join('');
  }

  async function chooseRole(role) {
    currentPlayer.role = role;
    if (currentPlayer.money <= 0) currentPlayer.money = window.CityAPI.STARTING[role];
    try { await window.CityAPI.save({ role, money: currentPlayer.money }); } catch (e) { /* keep local */ }
    renderConfirmScreen();
    showScreen(confirmScreen);
  }

  function afterAuth(player) {
    currentPlayer = player;
    if (!player.hasChosenRole) {
      renderRoleGrid();
      showScreen(roleScreen);
    } else {
      renderConfirmScreen();
      showScreen(confirmScreen);
    }
  }

  loginForm.addEventListener('submit', async (e) => {
    e.preventDefault();
    clearError();
    const username = document.getElementById('login-username').value.trim();
    const password = document.getElementById('login-password').value;
    try {
      const { player } = await window.CityAPI.login(username, password);
      afterAuth(player);
    } catch (err) { showError(err.message); }
  });

  signupForm.addEventListener('submit', async (e) => {
    e.preventDefault();
    clearError();
    const username = document.getElementById('signup-username').value.trim();
    const password = document.getElementById('signup-password').value;
    try {
      const { player } = await window.CityAPI.register(username, password);
      afterAuth(player);
    } catch (err) { showError(err.message); }
  });

  document.getElementById('btn-change-role').addEventListener('click', () => {
    renderRoleGrid();
    showScreen(roleScreen);
  });

  document.getElementById('btn-enter-city').addEventListener('click', () => {
    showScreen(gameScreen);
    window.CityGame.start(currentPlayer, { kids: false });
  });

  document.getElementById('btn-kids-city').addEventListener('click', async () => {
    currentPlayer.kidsMode = true;
    try { await window.CityAPI.save({ kidsMode: true }); } catch (e) { /* ignore */ }
    showScreen(gameScreen);
    window.CityGame.start(currentPlayer, { kids: true });
  });

  async function tryResumeSession() {
    try {
      const { player } = await window.CityAPI.me();
      afterAuth(player);
    } catch (e) {
      showScreen(authScreen);
    }
  }

  return { init: tryResumeSession };
})();

document.addEventListener('DOMContentLoaded', () => window.CityAuth.init());
