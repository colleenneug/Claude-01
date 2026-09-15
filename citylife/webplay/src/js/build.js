// Build mode: click to drop a colored block, right-click one of your own blocks to remove it.
window.CityBuild = (function () {
  const COLORS = [0xef4444, 0xf97316, 0xfacc15, 0x22c55e, 0x3b82f6, 0x8b5cf6, 0xec4899, 0xffffff];
  const swatchesEl = document.getElementById('build-swatches');
  let selectedColor = COLORS[0];
  let scene = null;
  let raycaster = new THREE.Raycaster();
  let placed = [];
  let enabled = false;

  function renderSwatches() {
    swatchesEl.innerHTML = '';
    COLORS.forEach((color, i) => {
      const el = document.createElement('div');
      el.className = 'swatch' + (i === 0 ? ' active' : '');
      el.style.background = `#${color.toString(16).padStart(6, '0')}`;
      el.addEventListener('click', () => {
        selectedColor = color;
        swatchesEl.querySelectorAll('.swatch').forEach((s) => s.classList.remove('active'));
        el.classList.add('active');
      });
      swatchesEl.appendChild(el);
    });
  }

  function init(threeScene, camera, canvas) {
    scene = threeScene;
    renderSwatches();

    canvas.addEventListener('pointerdown', (e) => {
      if (!enabled || e.button !== 0 && e.button !== 2) return;
      const rect = canvas.getBoundingClientRect();
      const ndc = new THREE.Vector2(
        ((e.clientX - rect.left) / rect.width) * 2 - 1,
        -((e.clientY - rect.top) / rect.height) * 2 + 1
      );
      raycaster.setFromCamera(ndc, camera);

      if (e.button === 2) {
        const hits = raycaster.intersectObjects(placed.map((p) => p.mesh));
        if (hits.length) {
          const mesh = hits[0].object;
          scene.remove(mesh);
          placed = placed.filter((p) => p.mesh !== mesh);
        }
        return;
      }

      const targets = scene.children.filter((c) => c.userData.solid || c.userData.ground || c.userData.placedBlock);
      const hits = raycaster.intersectObjects(targets);
      if (!hits.length) return;
      const point = hits[0].point.clone();
      const normal = hits[0].face ? hits[0].face.normal.clone().transformDirection(hits[0].object.matrixWorld) : new THREE.Vector3(0, 1, 0);
      point.addScaledVector(normal, 0.5);

      const geo = new THREE.BoxGeometry(1, 1, 1);
      const mat = new THREE.MeshStandardMaterial({ color: selectedColor });
      const mesh = new THREE.Mesh(geo, mat);
      mesh.position.set(Math.round(point.x), Math.max(0.5, Math.round(point.y)), Math.round(point.z));
      mesh.userData.placedBlock = true;
      scene.add(mesh);
      placed.push({ mesh });
    });

    canvas.addEventListener('contextmenu', (e) => { if (enabled) e.preventDefault(); });
  }

  function setEnabled(v) { enabled = v; }

  return { init, setEnabled };
})();
