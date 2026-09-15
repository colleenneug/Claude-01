// Role definitions: starting cash, passive income, health decay, and powers.
window.CityEconomy = (function () {
  const ROLE_META = {
    mayor: {
      label: 'Mayor', icon: '\u{1F3E2}', startMoney: 2000, baseIncome: 10, healthDecay: 2.2,
      desc: 'Build and manage the city. Buy properties, earn passive income, and shape the streets block by block.',
      powers: ['Business prices are 15% cheaper for you', 'Sprint (Shift)'],
    },
    cop: {
      label: 'Cop', icon: '\u{1F6E1}\u{FE0F}', startMoney: 1500, baseIncome: 8, healthDecay: 2.2,
      desc: 'Fight crime on the streets. Press R near a pedestrian to arrest them for a reward.',
      powers: ['Arrest pedestrians for a reward — press R near them', 'Sprint (Shift)'],
    },
    criminal: {
      label: 'Criminal', icon: '\u{1F480}', startMoney: 1000, baseIncome: 12, healthDecay: 2.2,
      desc: 'Rob the city blind. Shake down pedestrians and crack the bank vault.',
      powers: ['Rob pedestrians and the bank — press R', 'Bigger bank-heist payouts', 'Sprint (Shift)'],
    },
    superhero: {
      label: 'Superhero', icon: '\u{1F4AB}', startMoney: 1200, baseIncome: 9, healthDecay: 2.2,
      desc: 'Superpowers on tap: fly, sling a web-line between rooftops, or move cars with your mind.',
      powers: [
        'Fly — hold Space in mid-air (costs Energy)',
        'Telekinesis — press 1 near a car (costs Energy)',
        'Web Swing — press 2 near a building, hold W to climb (costs Energy)',
      ],
    },
    citizen: {
      label: 'Citizen', icon: '\u{1F9CD}', startMoney: 800, baseIncome: 6, healthDecay: 1.0,
      desc: 'A peaceful resident. No weapons, no powers — just a quiet life that drains health slower than everyone else\'s.',
      powers: ['Health drains at half the normal rate', 'Sprint (Shift)'],
    },
  };

  const ROLE_ORDER = ['mayor', 'cop', 'criminal', 'superhero', 'citizen'];

  function incomePerSecond(player, businessCatalog) {
    const meta = ROLE_META[player.role] || ROLE_META.citizen;
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

  function businessPrice(player, price) {
    return player.role === 'mayor' ? Math.round(price * 0.85) : price;
  }

  function canAfford(player, price) {
    return player.money >= businessPrice(player, price);
  }

  function buy(player, biz) {
    const price = businessPrice(player, biz.price);
    if (player.money < price) return false;
    if (player.businesses.includes(biz.id)) return false;
    player.money -= price;
    player.businesses.push(biz.id);
    return true;
  }

  return { ROLE_META, ROLE_ORDER, incomePerSecond, tick, canAfford, buy, businessPrice };
})();
