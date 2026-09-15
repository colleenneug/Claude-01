// Bank heist minigame: a moving marker, a green zone, 3 hits to crack the safe.
window.CityBank = (function () {
  const panel = document.getElementById('bank-heist');
  const zoneEl = document.getElementById('safe-zone');
  const markerEl = document.getElementById('safe-marker');
  const progressEl = document.getElementById('safe-progress');
  const resultEl = document.getElementById('bank-result');

  let active = false;
  let hits = 0;
  let markerPos = 0; // 0..1
  let markerSpeed = 0.55; // fraction per second
  let zoneStart = 0.4;
  let rafId = null;
  let lastT = 0;
  let onDone = null;

  function randomizeZone() {
    const width = 0.16;
    zoneStart = Math.random() * (1 - width);
    zoneEl.style.left = `${zoneStart * 100}%`;
    zoneEl.style.width = `${width * 100}%`;
  }

  function renderProgress() {
    progressEl.innerHTML = '';
    for (let i = 0; i < 3; i++) {
      const pip = document.createElement('div');
      pip.className = 'pip' + (i < hits ? ' hit' : '');
      progressEl.appendChild(pip);
    }
  }

  function loop(t) {
    if (!active) return;
    const dt = lastT ? (t - lastT) / 1000 : 0;
    lastT = t;
    markerPos += markerSpeed * dt;
    if (markerPos > 1 || markerPos < 0) {
      markerSpeed *= -1;
      markerPos = Math.max(0, Math.min(1, markerPos));
    }
    markerEl.style.left = `${markerPos * 100}%`;
    rafId = requestAnimationFrame(loop);
  }

  function finish(success) {
    active = false;
    if (rafId) cancelAnimationFrame(rafId);
    lastT = 0;
    setTimeout(() => {
      panel.classList.add('hidden');
      if (onDone) onDone(success);
    }, success ? 900 : 1400);
  }

  function attemptHit() {
    if (!active) return;
    const width = parseFloat(zoneEl.style.width) / 100;
    const inZone = markerPos >= zoneStart && markerPos <= zoneStart + width;
    if (inZone) {
      hits += 1;
      renderProgress();
      resultEl.textContent = 'Nice — tumbler set.';
      markerSpeed *= 1.18;
      if (hits >= 3) {
        resultEl.textContent = "Safe's open!";
        finish(true);
        return;
      }
      randomizeZone();
    } else {
      resultEl.textContent = 'Missed it — try again.';
    }
  }

  function open(done) {
    onDone = done;
    active = true;
    hits = 0;
    markerPos = 0;
    markerSpeed = 0.55;
    resultEl.textContent = '';
    randomizeZone();
    renderProgress();
    panel.classList.remove('hidden');
    rafId = requestAnimationFrame(loop);
  }

  function cooldownMessage(remainingMs) {
    const mins = Math.ceil(remainingMs / 60000);
    resultEl.textContent = `Bank's on lockdown — try again in about ${mins} minute${mins === 1 ? '' : 's'}.`;
    panel.classList.remove('hidden');
    setTimeout(() => panel.classList.add('hidden'), 2200);
  }

  document.addEventListener('keydown', (e) => {
    if (e.code === 'Space' && active) {
      e.preventDefault();
      attemptHit();
    }
  });

  return { open, isActive: () => active, cooldownMessage };
})();
