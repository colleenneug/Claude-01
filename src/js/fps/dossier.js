/* ============================================================
   The operative preview.

   A small three.js scene on the armoury screen showing the character
   you are actually building: skin tone, hair style and colour, and the
   armour you have equipped, plated in its rarity colour. First person
   means you never see yourself in the field, so this is where
   customisation has to read.
   ============================================================ */
(function (SF) {
  'use strict';

  function create(canvas) {
    const renderer = new THREE.WebGLRenderer({ canvas: canvas, antialias: true, alpha: true });
    renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
    renderer.outputEncoding = THREE.sRGBEncoding;
    renderer.toneMapping = THREE.ACESFilmicToneMapping;
    renderer.toneMappingExposure = 1.15;

    const scene = new THREE.Scene();
    const camera = new THREE.PerspectiveCamera(30, 1, 0.1, 40);
    camera.position.set(0, 1.45, 4.2);
    camera.lookAt(0, 1.35, 0);

    scene.add(new THREE.HemisphereLight(0x5a7a92, 0x0d1118, 1.1));
    const key = new THREE.DirectionalLight(0xdff0ff, 1.5);
    key.position.set(2.4, 4, 3);
    scene.add(key);
    const rim = new THREE.PointLight(0x5eeaff, 12, 9, 2);
    rim.position.set(-2.2, 2.4, -1.8);
    scene.add(rim);

    const root = new THREE.Group();
    scene.add(root);

    /* ---------- figure ---------- */
    const skinMat = new THREE.MeshStandardMaterial({ color: 0xe0b394, roughness: 0.72, metalness: 0.02 });
    const hairMat = new THREE.MeshStandardMaterial({ color: 0x2a1d14, roughness: 0.85, metalness: 0.05 });
    const suitMat = new THREE.MeshStandardMaterial({ color: 0x1d232c, roughness: 0.62, metalness: 0.35 });
    const plateMats = {
      head:  new THREE.MeshStandardMaterial({ color: 0x8894a4, roughness: 0.4, metalness: 0.85 }),
      chest: new THREE.MeshStandardMaterial({ color: 0x8894a4, roughness: 0.4, metalness: 0.85 }),
      legs:  new THREE.MeshStandardMaterial({ color: 0x8894a4, roughness: 0.4, metalness: 0.85 })
    };

    const head = new THREE.Mesh(new THREE.SphereGeometry(0.23, 24, 20), skinMat);
    head.position.y = 1.62;
    head.scale.set(1, 1.12, 0.95);

    const neck = new THREE.Mesh(new THREE.CylinderGeometry(0.09, 0.11, 0.14, 12), skinMat);
    neck.position.y = 1.44;

    const torso = new THREE.Mesh(new THREE.CapsuleGeometry(0.26, 0.44, 4, 16), suitMat);
    torso.position.y = 1.13;

    const hips = new THREE.Mesh(new THREE.CapsuleGeometry(0.22, 0.12, 3, 12), suitMat);
    hips.position.y = 0.82;

    const arms = [-1, 1].map((side) => {
      const a = new THREE.Mesh(new THREE.CapsuleGeometry(0.085, 0.5, 3, 10), suitMat);
      a.position.set(side * 0.36, 1.14, 0);
      a.rotation.z = side * 0.09;
      return a;
    });
    const hands = [-1, 1].map((side) => {
      const h = new THREE.Mesh(new THREE.SphereGeometry(0.085, 10, 8), skinMat);
      h.position.set(side * 0.4, 0.83, 0);
      return h;
    });
    const legs = [-1, 1].map((side) => {
      const l = new THREE.Mesh(new THREE.CapsuleGeometry(0.105, 0.56, 3, 10), suitMat);
      l.position.set(side * 0.13, 0.42, 0);
      return l;
    });

    root.add(head, neck, torso, hips, ...arms, ...hands, ...legs);

    /* ---------- armour plates, shown only when equipped ---------- */
    const helm = new THREE.Mesh(new THREE.SphereGeometry(0.26, 20, 16,
      0, Math.PI * 2, 0, Math.PI * 0.58), plateMats.head);
    helm.position.y = 1.63;
    const visor = new THREE.Mesh(new THREE.BoxGeometry(0.3, 0.07, 0.06),
      new THREE.MeshStandardMaterial({ color: 0x06131b, emissive: new THREE.Color(0x5eeaff),
                                       emissiveIntensity: 2.2, roughness: 0.3 }));
    visor.position.set(0, 1.605, 0.2);

    const chestPlate = new THREE.Mesh(new THREE.CapsuleGeometry(0.29, 0.36, 4, 16), plateMats.chest);
    chestPlate.position.y = 1.16;
    const pauldrons = [-1, 1].map((side) => {
      const m = new THREE.Mesh(new THREE.SphereGeometry(0.15, 12, 10,
        0, Math.PI * 2, 0, Math.PI * 0.6), plateMats.chest);
      m.position.set(side * 0.36, 1.32, 0);
      return m;
    });
    const greaves = [-1, 1].map((side) => {
      const m = new THREE.Mesh(new THREE.CapsuleGeometry(0.125, 0.34, 3, 10), plateMats.legs);
      m.position.set(side * 0.13, 0.5, 0);
      return m;
    });

    root.add(helm, visor, chestPlate, ...pauldrons, ...greaves);

    /* ---------- hair ---------- */
    const hairGroup = new THREE.Group();
    hairGroup.position.y = 1.62;
    root.add(hairGroup);

    function buildHair(styleId) {
      hairGroup.clear();
      const cap = () => {
        const m = new THREE.Mesh(new THREE.SphereGeometry(0.243, 20, 16,
          0, Math.PI * 2, 0, Math.PI * 0.52), hairMat);
        m.scale.set(1, 1.1, 0.97);
        return m;
      };
      switch (styleId) {
        case 'shaved':
          break;
        case 'crop':
          hairGroup.add(cap());
          break;
        case 'braids': {
          hairGroup.add(cap());
          for (let i = 0; i < 5; i++) {
            const b = new THREE.Mesh(new THREE.CapsuleGeometry(0.028, 0.3, 3, 6), hairMat);
            b.position.set(-0.16 + i * 0.08, -0.18, -0.14);
            hairGroup.add(b);
          }
          break;
        }
        case 'tail': {
          hairGroup.add(cap());
          const t = new THREE.Mesh(new THREE.CapsuleGeometry(0.06, 0.34, 4, 10), hairMat);
          t.position.set(0, -0.14, -0.24);
          t.rotation.x = 0.45;
          hairGroup.add(t);
          break;
        }
        case 'locs': {
          hairGroup.add(cap());
          for (let i = 0; i < 10; i++) {
            const ang = (i / 10) * Math.PI * 2;
            const l = new THREE.Mesh(new THREE.CapsuleGeometry(0.033, 0.36, 3, 6), hairMat);
            l.position.set(Math.cos(ang) * 0.19, -0.2, Math.sin(ang) * 0.17);
            hairGroup.add(l);
          }
          break;
        }
        case 'mohawk': {
          for (let i = 0; i < 7; i++) {
            const h = 0.1 + Math.sin((i / 6) * Math.PI) * 0.16;
            const m = new THREE.Mesh(new THREE.BoxGeometry(0.07, h, 0.075), hairMat);
            m.position.set(0, 0.2 + h * 0.35, 0.16 - i * 0.055);
            hairGroup.add(m);
          }
          break;
        }
        case 'long': {
          hairGroup.add(cap());
          const back = new THREE.Mesh(new THREE.CapsuleGeometry(0.17, 0.34, 4, 12), hairMat);
          back.position.set(0, -0.24, -0.09);
          back.scale.set(1, 1, 0.72);
          hairGroup.add(back);
          break;
        }
        default: hairGroup.add(cap());
      }
    }

    let current = null;

    function setLook(character) {
      const G = SF.gear;
      const look = character.look || G.defaultLook();
      skinMat.color.set(G.SKINS[look.skin % G.SKINS.length]);
      hairMat.color.set(G.HAIR_COLOURS[look.hairColour % G.HAIR_COLOURS.length]);

      const style = G.HAIR_STYLES[look.hair % G.HAIR_STYLES.length].id;
      if (style !== current) { buildHair(style); current = style; }

      const eq = character.equipped || {};
      helm.visible = visor.visible = !!eq.head;
      chestPlate.visible = !!eq.chest;
      for (const p of pauldrons) p.visible = !!eq.chest;
      for (const g of greaves) g.visible = !!eq.legs;
      // hair disappears under a helmet, as it would
      hairGroup.visible = !eq.head;

      for (const slot of ['head', 'chest', 'legs']) {
        if (eq[slot]) plateMats[slot].color.set(G.rarityOf(eq[slot].rarity).colour);
      }
      const cls = SF.classes.CLASSES[character.cls];
      const worn = SF.cosmetics && SF.cosmetics.suitColours(character);
      if (worn) {
        suitMat.color.set(worn.suit);
      } else {
        suitMat.color.set(cls ? cls.accent : '#1d232c');
        suitMat.color.multiplyScalar(0.32);
      }
    }

    let spin = 0.6, raf = 0, dragging = false, lastX = 0;

    canvas.addEventListener('pointerdown', (e) => { dragging = true; lastX = e.clientX; });
    window.addEventListener('pointerup', () => { dragging = false; });
    window.addEventListener('pointermove', (e) => {
      if (!dragging) return;
      spin += (e.clientX - lastX) * 0.01;
      lastX = e.clientX;
    });

    function resize() {
      const w = canvas.clientWidth || 320, h = canvas.clientHeight || 420;
      renderer.setSize(w, h, false);
      camera.aspect = w / h;
      camera.updateProjectionMatrix();
    }

    function frame() {
      raf = requestAnimationFrame(frame);
      if (!dragging) spin += 0.0035;
      root.rotation.y = spin;
      renderer.render(scene, camera);
    }

    function start() { resize(); if (!raf) frame(); }
    function stop() { cancelAnimationFrame(raf); raf = 0; }

    buildHair('crop');
    return { setLook, start, stop, resize };
  }

  SF.dossier = { create };
})(window.SF);
