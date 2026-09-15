// NPCs you can walk up to and talk to (E) or rob/arrest (R).
window.CityNPC = (function () {
  const NAMES = ['Yuki', 'Marco', 'Dana', 'Theo', 'Priya', 'Idris', 'Sven'];
  const LINES = {
    mayor: [
      "Thanks for keeping the lights on, Mayor.",
      "When's that new business lot going up?",
      "The city looks better every week, honestly.",
    ],
    cop: [
      "Evening, officer. Nice and quiet tonight.",
      "Glad someone's watching these streets.",
      "You didn't see anything over there, right?",
    ],
    criminal: [
      "Heard the vault's got a new lock. Good luck with that.",
      "Cops don't come round here much after dark.",
      "You didn't hear it from me, but the bank's short-staffed on weekends.",
    ],
    superhero: [
      "Thanks for keeping the streets safe out here.",
      "Saw you stop a runaway car yesterday. Respect.",
      "The city could use more people like you.",
    ],
    citizen: [
      "Nice night for a walk, isn't it?",
      "Have you tried the new place on 5th?",
      "Just heading home. Take care out there.",
    ],
  };

  function buildNPCMesh(color) {
    const group = new THREE.Group();
    const mat = new THREE.MeshStandardMaterial({ color });
    const torso = new THREE.Mesh(new THREE.BoxGeometry(1.1, 1.3, 0.6), mat);
    torso.position.y = 1.6;
    group.add(torso);
    const head = new THREE.Mesh(new THREE.BoxGeometry(0.8, 0.8, 0.8), new THREE.MeshStandardMaterial({ color: 0xf2c9a0 }));
    head.position.y = 2.7;
    group.add(head);
    const legs = new THREE.Mesh(new THREE.BoxGeometry(0.8, 1.3, 0.5), new THREE.MeshStandardMaterial({ color: 0x1e293b }));
    legs.position.y = 0.65;
    group.add(legs);
    return group;
  }

  function spawn(scene, spots) {
    const colors = [0xf97316, 0x14b8a6, 0xe11d48, 0x6366f1, 0xa855f7];
    return spots.map((pos, i) => {
      const mesh = buildNPCMesh(colors[i % colors.length]);
      mesh.position.copy(pos);
      mesh.rotation.y = Math.random() * Math.PI * 2;
      scene.add(mesh);
      return { mesh, name: NAMES[i % NAMES.length], lastRobbedAt: 0 };
    });
  }

  function nearest(npcs, point, maxDist) {
    let best = null, bestDist = maxDist;
    npcs.forEach((npc) => {
      const d = npc.mesh.position.distanceTo(point);
      if (d < bestDist) { best = npc; bestDist = d; }
    });
    return best;
  }

  function lineFor(role) {
    const pool = LINES[role] || LINES.citizen;
    return pool[Math.floor(Math.random() * pool.length)];
  }

  return { spawn, nearest, lineFor };
})();
