// Leaderboard panel: local high-scores across every profile saved on this device/browser.
window.CityLeaderboard = (function () {
  const listEl = document.getElementById('leaderboard-list');

  async function open() {
    listEl.innerHTML = '<li>Loading&hellip;</li>';
    try {
      const { leaderboard } = await window.CityAPI.leaderboard();
      listEl.innerHTML = '';
      if (!leaderboard.length) {
        listEl.innerHTML = '<li>Nobody on the board yet.</li>';
        return;
      }
      leaderboard.forEach((row) => {
        const li = document.createElement('li');
        li.innerHTML = `${row.username} <span class="lb-money">$${Math.floor(row.money).toLocaleString()}</span>`;
        listEl.appendChild(li);
      });
    } catch (e) {
      listEl.innerHTML = '<li>Could not load the leaderboard.</li>';
    }
  }

  return { open };
})();
