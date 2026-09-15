// Auth screen: tab switching, login/signup submission, role picker for new accounts.
window.CityAuth = (function () {
  const authScreen = document.getElementById('auth-screen');
  const roleScreen = document.getElementById('role-screen');
  const gameScreen = document.getElementById('game-screen');
  const errorEl = document.getElementById('auth-error');
  const loginForm = document.getElementById('login-form');
  const signupForm = document.getElementById('signup-form');

  function showError(msg) {
    errorEl.textContent = msg;
    errorEl.classList.remove('hidden');
  }
  function clearError() {
    errorEl.classList.add('hidden');
  }

  function showScreen(el) {
    [authScreen, roleScreen, gameScreen].forEach((s) => s.classList.add('hidden'));
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

  function enterGame(player) {
    showScreen(gameScreen);
    window.CityGame.start(player);
  }

  function afterAuth(player, isNewAccount) {
    if (isNewAccount) {
      showScreen(roleScreen);
      roleScreen.dataset.pending = '1';
      roleScreen.__player = player;
    } else {
      enterGame(player);
    }
  }

  loginForm.addEventListener('submit', async (e) => {
    e.preventDefault();
    clearError();
    const username = document.getElementById('login-username').value.trim();
    const password = document.getElementById('login-password').value;
    try {
      const { player } = await window.CityAPI.login(username, password);
      afterAuth(player, false);
    } catch (err) {
      showError(err.message);
    }
  });

  signupForm.addEventListener('submit', async (e) => {
    e.preventDefault();
    clearError();
    const username = document.getElementById('signup-username').value.trim();
    const password = document.getElementById('signup-password').value;
    try {
      const { player } = await window.CityAPI.register(username, password);
      afterAuth(player, true);
    } catch (err) {
      showError(err.message);
    }
  });

  document.querySelectorAll('.role-card').forEach((card) => {
    card.addEventListener('click', async () => {
      const role = card.dataset.role;
      const player = roleScreen.__player;
      player.role = role;
      try {
        await window.CityAPI.save({ role });
      } catch (e) { /* non-fatal, keep local role */ }
      enterGame(player);
    });
  });

  async function tryResumeSession() {
    try {
      const { player } = await window.CityAPI.me();
      enterGame(player);
    } catch (e) {
      showScreen(authScreen);
    }
  }

  return { init: tryResumeSession };
})();

document.addEventListener('DOMContentLoaded', () => window.CityAuth.init());
