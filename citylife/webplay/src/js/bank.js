// Bank heist minigame: turn a dial toward a hidden number, watch the feedback
// bar for hot/cold, lock it in. Three tumblers, one countdown.
window.CityBank = (function () {
  const TOTAL_TIME = 30;
  const TOLERANCE = 6;
  const FAIL_PENALTY = 4;
  const TURN_STEP = 2;

  const panel = document.getElementById('bank-heist');
  const tumblersEl = document.getElementById('heist-tumblers');
  const timerEl = document.getElementById('heist-timer-value');
  const needleEl = document.getElementById('safe-needle');
  const readoutEl = document.getElementById('safe-readout');
  const feedbackFillEl = document.getElementById('feedback-fill');
  const resultEl = document.getElementById('bank-result');
  const closeBtn = document.getElementById('heist-close');
  const lockBtn = document.getElementById('heist-lock');
  const turnLeftBtn = document.getElementById('heist-turn-left');
  const turnRightBtn = document.getElementById('heist-turn-right');

  let active = false;
  let solved = 0;
  let value = 0;
  let target = 0;
  let timeLeft = TOTAL_TIME;
  let intervalId = null;
  let onDone = null;

  function renderTumblers() {
    tumblersEl.innerHTML = '';
    for (let i = 0; i < 3; i++) {
      const pip = document.createElement('div');
      pip.className = 'tumbler-pip' + (i < solved ? ' solved' : i === solved ? ' current' : '');
      pip.textContent = i < solved ? '✓' : String(i + 1);
      tumblersEl.appendChild(pip);
    }
  }

  function renderDial() {
    const angle = -120 + (value / 99) * 240;
    needleEl.style.transform = `rotate(${angle}deg)`;
    readoutEl.textContent = String(value).padStart(2, '0');
    const distance = Math.abs(value - target);
    const pct = Math.min(100, (distance / 50) * 100);
    feedbackFillEl.style.width = `${100 - pct}%`;
  }

  function newTarget() {
    target = Math.floor(Math.random() * 100);
    value = 0;
  }

  function turn(delta) {
    if (!active) return;
    value = Math.max(0, Math.min(99, value + delta));
    renderDial();
  }

  function lock() {
    if (!active) return;
    const distance = Math.abs(value - target);
    if (distance <= TOLERANCE) {
      solved += 1;
      renderTumblers();
      if (solved >= 3) {
        resultEl.textContent = "Safe's open!";
        finish(true);
        return;
      }
      resultEl.textContent = 'Tumbler set. Keep going.';
      newTarget();
      renderDial();
    } else {
      resultEl.textContent = 'Not quite — watch the feedback bar.';
      timeLeft = Math.max(0, timeLeft - FAIL_PENALTY);
      renderTimer();
    }
  }

  function renderTimer() {
    timerEl.textContent = Math.ceil(timeLeft);
  }

  function tick() {
    if (!active) return;
    timeLeft -= 1;
    renderTimer();
    if (timeLeft <= 0) {
      resultEl.textContent = "Time's up — the vault reset.";
      finish(false);
    }
  }

  function finish(success) {
    active = false;
    clearInterval(intervalId);
    setTimeout(() => {
      panel.classList.add('hidden');
      if (onDone) onDone(success);
    }, success ? 900 : 1400);
  }

  function open(done) {
    onDone = done;
    active = true;
    solved = 0;
    timeLeft = TOTAL_TIME;
    resultEl.textContent = '';
    newTarget();
    renderTumblers();
    renderDial();
    renderTimer();
    panel.classList.remove('hidden');
    clearInterval(intervalId);
    intervalId = setInterval(tick, 1000);
  }

  function cooldownMessage(remainingMs) {
    const mins = Math.ceil(remainingMs / 60000);
    resultEl.textContent = `Bank's on lockdown — try again in about ${mins} minute${mins === 1 ? '' : 's'}.`;
    panel.classList.remove('hidden');
    tumblersEl.innerHTML = '';
    timerEl.textContent = '--';
    setTimeout(() => panel.classList.add('hidden'), 2400);
  }

  turnLeftBtn.addEventListener('click', () => turn(-TURN_STEP));
  turnRightBtn.addEventListener('click', () => turn(TURN_STEP));
  lockBtn.addEventListener('click', lock);
  closeBtn.addEventListener('click', () => finish(false));

  document.addEventListener('keydown', (e) => {
    if (!active) return;
    if (e.code === 'ArrowLeft') { e.preventDefault(); turn(-TURN_STEP); }
    if (e.code === 'ArrowRight') { e.preventDefault(); turn(TURN_STEP); }
    if (e.code === 'Space' || e.code === 'Enter') { e.preventDefault(); lock(); }
  });

  return { open, isActive: () => active, cooldownMessage };
})();
