// Builds the city: ground, roads, buildings, bank, business lots and landmark labels.
window.CityWorld = (function () {
  const BLOCK = 22;      // building footprint + gap, center to center
  const GRID = 7;        // GRID x GRID city blocks
  const HALF = ((GRID - 1) * BLOCK) / 2;

  function makeLabelSprite(text, bg) {
    const canvas = document.createElement('canvas');
    canvas.width = 256;
    canvas.height = 64;
    const ctx = canvas.getContext('2d');
    ctx.fillStyle = bg || '#1e293b';
    roundRect(ctx, 0, 0, 256, 64, 14);
    ctx.fill();
    ctx.fillStyle = '#facc15';
    ctx.font = 'bold 30px sans-serif';
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    ctx.fillText(text, 128, 34);
    const texture = new THREE.CanvasTexture(canvas);
    const material = new THREE.SpriteMaterial({ map: texture, depthTest: false });
    const sprite = new THREE.Sprite(material);
    sprite.scale.set(6, 1.5, 1);
    sprite.renderOrder = 10;
    return sprite;
  }

  function roundRect(ctx, x, y, w, h, r) {
    ctx.beginPath();
    ctx.moveTo(x + r, y);
    ctx.arcTo(x + w, y, x + w, y + h, r);
    ctx.arcTo(x + w, y + h, x, y + h, r);
    ctx.arcTo(x, y + h, x, y, r);
    ctx.arcTo(x, y, x + w, y, r);
    ctx.closePath();
  }

  function windowTexture(cols, rows, lit) {
    const c = document.createElement('canvas');
    c.width = 128; c.height = 128;
    const ctx = c.getContext('2d');
    ctx.fillStyle = '#8fa3c4';
    ctx.fillRect(0, 0, 128, 128);
    const cw = 128 / cols, rh = 128 / rows;
    for (let y = 0; y < rows; y++) {
      for (let x = 0; x < cols; x++) {
        ctx.fillStyle = Math.random() < lit ? '#fef08a' : '#334155';
        ctx.fillRect(x * cw + 2, y * rh + 2, cw - 4, rh - 4);
      }
    }
    const tex = new THREE.CanvasTexture(c);
    tex.wrapS = tex.wrapT = THREE.RepeatWrapping;
    return tex;
  }

  function build(scene) {
    const landmarks = { bank: null, businesses: [], npcSpots: [], carSpots: [], spawn: new THREE.Vector3(0, 1, HALF + BLOCK * 0.6) };

    // sky
    scene.background = new THREE.Color(0x8fc7ff);
    scene.fog = new THREE.Fog(0x8fc7ff, 60, 220);

    // lights
    const sun = new THREE.DirectionalLight(0xffffff, 1.4);
    sun.position.set(60, 100, 40);
    scene.add(sun);
    scene.add(new THREE.AmbientLight(0xbfd4ff, 0.7));

    // ground (grass, extends past the city grid)
    const groundGeo = new THREE.PlaneGeometry(HALF * 3, HALF * 3);
    const groundMat = new THREE.MeshStandardMaterial({ color: 0x4ba35a });
    const ground = new THREE.Mesh(groundGeo, groundMat);
    ground.rotation.x = -Math.PI / 2;
    ground.userData.ground = true;
    scene.add(ground);

    // road grid
    const roadMat = new THREE.MeshStandardMaterial({ color: 0x20242c });
    const roadWidth = BLOCK - 14;
    for (let i = 0; i < GRID; i++) {
      const pos = -HALF + i * BLOCK;
      const roadA = new THREE.Mesh(new THREE.PlaneGeometry(roadWidth, HALF * 2 + BLOCK), roadMat);
      roadA.rotation.x = -Math.PI / 2;
      roadA.position.set(pos, 0.01, 0);
      scene.add(roadA);
      const roadB = new THREE.Mesh(new THREE.PlaneGeometry(HALF * 2 + BLOCK, roadWidth), roadMat);
      roadB.rotation.x = -Math.PI / 2;
      roadB.position.set(0, 0.01, pos);
      scene.add(roadB);
    }

    // business lot indices (relative to grid center), and the bank
    const businessCells = [[1, 1], [-2, 2], [2, -2]];
    const bankCell = [0, 0];

    const buildingColors = [0xd6dee8, 0xc8d2e0, 0xe2e8f0, 0xb9c5d6];

    for (let gx = 0; gx < GRID; gx++) {
      for (let gz = 0; gz < GRID; gz++) {
        const cx = -HALF + gx * BLOCK;
        const cz = -HALF + gz * BLOCK;
        const relX = gx - (GRID - 1) / 2;
        const relZ = gz - (GRID - 1) / 2;

        // leave the outer ring open as parkland, matching the reference footage
        const isEdge = gx === 0 || gz === 0 || gx === GRID - 1 || gz === GRID - 1;
        const isBank = relX === bankCell[0] && relZ === bankCell[1];
        const isBusiness = businessCells.some(([bx, bz]) => bx === relX && bz === relZ);

        if (isEdge && !isBank && !isBusiness) continue;

        const footprint = BLOCK - roadWidth - 2;
        const height = isBank ? 14 : 20 + Math.random() * 40;
        const geo = new THREE.BoxGeometry(footprint, height, footprint);
        const tex = windowTexture(6, Math.round(height / 3), isBank ? 0.6 : 0.3);
        const mat = new THREE.MeshStandardMaterial({
          map: tex,
          color: isBank ? 0xfacc15 : buildingColors[(gx + gz) % buildingColors.length],
        });
        const building = new THREE.Mesh(geo, mat);
        building.position.set(cx, height / 2, cz);
        building.userData.solid = true;
        building.userData.halfExtents = new THREE.Vector3(footprint / 2, height / 2, footprint / 2);
        scene.add(building);

        if (isBank) {
          const label = makeLabelSprite('BANK', '#78350f');
          label.position.set(cx, height + 2, cz);
          scene.add(label);
          landmarks.bank = { position: new THREE.Vector3(cx, 1, cz + footprint / 2 + 2), buildingPos: new THREE.Vector3(cx, 0, cz) };
        } else if (isBusiness) {
          const label = makeLabelSprite('$ BUSINESS', '#14532d');
          label.position.set(cx, height + 2, cz);
          scene.add(label);
          landmarks.businesses.push({
            id: `biz_${gx}_${gz}`,
            position: new THREE.Vector3(cx, 1, cz + footprint / 2 + 2),
            price: 4000 + Math.floor(Math.random() * 6000),
            income: 6 + Math.floor(Math.random() * 10),
          });
        }
      }
    }

    // NPC spots and car spots dotted around the spawn plaza
    landmarks.npcSpots.push(new THREE.Vector3(6, 0, HALF + BLOCK * 0.3));
    landmarks.npcSpots.push(new THREE.Vector3(-8, 0, BLOCK * 1.5));
    landmarks.carSpots.push(new THREE.Vector3(4, 0, HALF + BLOCK * 0.5));
    landmarks.carSpots.push(new THREE.Vector3(-14, 0, HALF * 0.5));
    landmarks.carSpots.push(new THREE.Vector3(10, 0, -BLOCK));

    return landmarks;
  }

  // Simple AABB collision check against everything flagged solid=true.
  function collides(scene, point, radius) {
    let hit = null;
    scene.traverse((obj) => {
      if (hit || !obj.userData || !obj.userData.solid) return;
      const he = obj.userData.halfExtents;
      const dx = point.x - obj.position.x;
      const dz = point.z - obj.position.z;
      if (Math.abs(dx) < he.x + radius && Math.abs(dz) < he.z + radius) {
        hit = obj;
      }
    });
    return hit;
  }

  return { build, collides, BLOCK, GRID, HALF, makeLabelSprite };
})();
