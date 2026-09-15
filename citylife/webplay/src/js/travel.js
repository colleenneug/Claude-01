// Fast-travel menu: teleport the player to the bank, spawn, or an owned/visible business.
window.CityTravel = (function () {
  const listEl = document.getElementById('travel-list');
  const panel = document.getElementById('panel-travel');

  function open(landmarks, onTravel) {
    listEl.innerHTML = '';
    const spots = [{ name: 'Spawn Plaza', position: landmarks.spawn }];
    if (landmarks.bank) spots.push({ name: 'Bank', position: landmarks.bank.position });
    landmarks.businesses.forEach((b, i) => spots.push({ name: `Business #${i + 1}`, position: b.position }));

    spots.forEach((spot) => {
      const row = document.createElement('div');
      row.className = 'list-row';
      row.innerHTML = `<span class="name">${spot.name}</span>`;
      const btn = document.createElement('button');
      btn.className = 'go-btn';
      btn.textContent = 'Go';
      btn.addEventListener('click', () => {
        onTravel(spot.position);
        panel.classList.add('hidden');
      });
      row.appendChild(btn);
      listEl.appendChild(row);
    });

    panel.classList.remove('hidden');
  }

  return { open };
})();
