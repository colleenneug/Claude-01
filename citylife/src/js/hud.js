// HUD: money/health/role readouts, prompts, dialog bubble, and side-panel plumbing.
window.CityHUD = (function () {
  const roleEl = document.getElementById('hud-role');
  const roleNameEl = document.getElementById('hud-role-name');
  const moneyEl = document.getElementById('hud-money');
  const incomeEl = document.getElementById('hud-income');
  const healthFillEl = document.getElementById('hud-health-fill');
  const healthValueEl = document.getElementById('hud-health-value');
  const onlineEl = document.getElementById('hud-online');
  const promptEl = document.getElementById('prompt');
  const dialogEl = document.getElementById('dialog-box');

  const panelOpenHandlers = {};

  function setRole(role, label) {
    roleEl.className = 'hud-row hud-role role-' + role;
    roleNameEl.textContent = label;
  }

  function setMoney(amount, incomePerSec) {
    moneyEl.textContent = Math.floor(amount).toLocaleString();
    incomeEl.textContent = incomePerSec ? `+$${incomePerSec.toFixed(1)}/s` : '';
  }

  function setHealth(v) {
    const pct = Math.max(0, Math.min(100, v));
    healthFillEl.style.width = `${pct}%`;
    healthValueEl.textContent = Math.round(pct);
  }

  function setOnline(n) {
    onlineEl.textContent = n;
  }

  function showPrompt(text, key) {
    promptEl.innerHTML = key ? `${text} <kbd>${key}</kbd>` : text;
    promptEl.classList.remove('hidden');
  }
  function hidePrompt() {
    promptEl.classList.add('hidden');
  }

  let dialogTimer = null;
  function showDialog(text) {
    dialogEl.textContent = text;
    dialogEl.classList.remove('hidden');
    clearTimeout(dialogTimer);
    dialogTimer = setTimeout(() => dialogEl.classList.add('hidden'), 4000);
  }

  function openPanel(name) {
    document.getElementById(`panel-${name}`).classList.remove('hidden');
    if (panelOpenHandlers[name]) panelOpenHandlers[name]();
  }
  function closePanel(name) {
    document.getElementById(`panel-${name}`).classList.add('hidden');
  }
  function onOpen(name, handler) {
    panelOpenHandlers[name] = handler;
  }

  document.querySelectorAll('.menu-btn[data-panel]').forEach((btn) => {
    btn.addEventListener('click', () => openPanel(btn.dataset.panel));
  });
  document.querySelectorAll('.btn-close[data-close]').forEach((btn) => {
    btn.addEventListener('click', () => closePanel(btn.dataset.close));
  });

  function anyPanelOpen() {
    return ['business', 'build', 'powers', 'leaderboard', 'travel'].some(
      (name) => !document.getElementById(`panel-${name}`).classList.contains('hidden')
    );
  }

  return {
    setRole, setMoney, setHealth, setOnline,
    showPrompt, hidePrompt, showDialog,
    openPanel, closePanel, onOpen, anyPanelOpen,
  };
})();
