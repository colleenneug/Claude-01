// Role income rates, business ownership, and per-role "Powers".
window.CityEconomy = (function () {
  const ROLE_META = {
    criminal: { label: 'Criminal', baseIncome: 14, powers: ['Bigger bank-robbery payouts', 'Sprint (Shift)'] },
    superhero: { label: 'Superhero', baseIncome: 9, powers: ['Glide — hold Space while falling', 'Sprint (Shift)'] },
    villain: { label: 'Super Villain', baseIncome: 11, powers: ['Dash — press Q for a burst of speed', 'Sprint (Shift)'] },
  };

  function incomePerSecond(player, businessCatalog) {
    const meta = ROLE_META[player.role] || ROLE_META.criminal;
    let total = meta.baseIncome;
    (player.businesses || []).forEach((id) => {
      const biz = businessCatalog.find((b) => b.id === id);
      if (biz) total += biz.income;
    });
    return total;
  }

  function tick(player, dt, businessCatalog) {
    player.money += incomePerSecond(player, businessCatalog) * dt;
  }

  function canAfford(player, price) {
    return player.money >= price;
  }

  function buy(player, biz) {
    if (!canAfford(player, biz.price)) return false;
    if (player.businesses.includes(biz.id)) return false;
    player.money -= biz.price;
    player.businesses.push(biz.id);
    return true;
  }

  return { ROLE_META, incomePerSecond, tick, canAfford, buy };
})();
