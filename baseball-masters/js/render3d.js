/* Baseball Masters — 3D rendering (three.js r144, UMD, vendored).
   Replaces the old 2D canvas views with real WebGL scenes, but keeps the
   exact same public API (Field / PitchView, method names and signatures)
   that input.js and main.js already call, so nothing upstream changes.

   Coordinate convention: everything is in feet, matching physics.js.
     physics x (lateral, + = 1B/right-field side) -> scene X
     physics z (height above ground)               -> scene Y
     physics y (distance from home plate)           -> scene -Z
   i.e. the camera sits near home plate looking down -Z toward center field
   / the mound. One scene unit == one foot everywhere, so the two scenes
   (field + pitch tunnel) share constants freely. */
(function (BM) {
  'use strict';
  const T = window.THREE;
  const P = BM.physics, D = BM.data, ZONE = BM.ZONE;

  function disposeObject(obj) {
    obj.traverse((n) => {
      if (n.geometry) n.geometry.dispose();
      if (n.material) {
        if (Array.isArray(n.material)) n.material.forEach(m => m.dispose());
        else n.material.dispose();
      }
    });
  }

  function makeRenderer(canvas) {
    const r = new T.WebGLRenderer({ canvas: canvas, antialias: true, alpha: false });
    r.setPixelRatio(Math.min(window.devicePixelRatio || 1, 2));
    r.outputColorSpace = T.SRGBColorSpace || r.outputColorSpace;
    return r;
  }

  function addLights(scene, skyColor, groundColor) {
    const hemi = new T.HemisphereLight(skyColor, groundColor, 1.1);
    scene.add(hemi);
    const sun = new T.DirectionalLight(0xfff4d6, 1.15);
    sun.position.set(-120, 180, 80);
    scene.add(sun);
    const fill = new T.DirectionalLight(0x88aaff, 0.25);
    fill.position.set(100, 60, -100);
    scene.add(fill);
  }

  /* ===================================================================== */
  /* Field — the overhead/broadcast stadium view.                          */
  /* ===================================================================== */

  function Field(canvas) {
    this.canvas = canvas;
    this.renderer = makeRenderer(canvas);
    this.scene = new T.Scene();
    this.camera = new T.PerspectiveCamera(52, 1, 0.5, 900);
    this.camera.position.set(0, 46, 84);
    this.camera.lookAt(0, 8, -160);
    addLights(this.scene, 0x8fb8e8, 0x2a4a2a);
    this.scene.background = new T.Color(0x0a1420);
    this.scene.fog = new T.Fog(0x0a1420, 260, 620);

    this._builtStadiumId = null;
    this._fielderMeshes = {};
    this._runnerMeshes = [];
    this._ballShadow = null;
    this._ball = null;
    this._trailLine = null;

    this._buildBall();
    this._buildRunners();
    this.resize();
  }

  Field.prototype.resize = function () {
    const w = this.canvas.clientWidth || 1, h = this.canvas.clientHeight || 1;
    this.renderer.setSize(w, h, false);
    this.camera.aspect = w / h;
    this.camera.updateProjectionMatrix();
  };

  Field.prototype._render = function () { this.renderer.render(this.scene, this.camera); };

  Field.prototype._buildBall = function () {
    const geo = new T.SphereGeometry(0.65, 14, 10);
    const mat = new T.MeshLambertMaterial({ color: 0xf6f3e8 });
    this._ball = new T.Mesh(geo, mat);
    this._ball.visible = false;
    this.scene.add(this._ball);

    const shGeo = new T.CircleGeometry(1.4, 16);
    const shMat = new T.MeshBasicMaterial({ color: 0x000000, transparent: true, opacity: 0.35 });
    this._ballShadow = new T.Mesh(shGeo, shMat);
    this._ballShadow.rotation.x = -Math.PI / 2;
    this._ballShadow.visible = false;
    this.scene.add(this._ballShadow);
  };

  Field.prototype._buildRunners = function () {
    const geo = new T.CapsuleGeometry(1.1, 3.6, 4, 8);
    for (let i = 0; i < 3; i++) {
      const mat = new T.MeshLambertMaterial({ color: 0xff5a5a });
      const m = new T.Mesh(geo, mat);
      m.position.y = 2.9;
      m.visible = false;
      this.scene.add(m);
      this._runnerMeshes.push(m);
    }
  };

  Field.prototype._playerMesh = function (color) {
    const g = new T.Group();
    const body = new T.Mesh(new T.CapsuleGeometry(1.3, 4.2, 4, 8),
      new T.MeshLambertMaterial({ color: color }));
    body.position.y = 3.3;
    g.add(body);
    const cap = new T.Mesh(new T.SphereGeometry(0.9, 10, 8),
      new T.MeshLambertMaterial({ color: 0xf0d9b5 }));
    cap.position.y = 6.1;
    g.add(cap);
    return g;
  };

  Field.prototype._buildStadium = function (stadium) {
    if (this._stadiumGroup) { this.scene.remove(this._stadiumGroup); disposeObject(this._stadiumGroup); }
    const g = new T.Group();
    const turf = new T.Color(stadium.turf || '#2f7d3e');

    // Outfield grass — a fan shape out to the fence at every angle.
    const steps = 48;
    const fanShape = new T.Shape();
    fanShape.moveTo(0, 0);
    for (let i = 0; i <= steps; i++) {
      const ang = -45 + 90 * i / steps;
      const d = D.fenceAt(stadium, ang);
      const rad = ang * Math.PI / 180;
      const x = d * Math.sin(rad), y = d * Math.cos(rad);
      fanShape.lineTo(x, y);
    }
    fanShape.lineTo(0, 0);
    const grassGeo = new T.ShapeGeometry(fanShape);
    grassGeo.rotateX(-Math.PI / 2);
    const grass = new T.Mesh(grassGeo, new T.MeshLambertMaterial({ color: turf }));
    grass.position.y = 0;
    g.add(grass);

    // Infield dirt diamond + grass inset (classic skin shape, simplified).
    function shapeFromPts(pts) {
      const s = new T.Shape();
      pts.forEach((p, i) => { if (i === 0) s.moveTo(p[0], p[1]); else s.lineTo(p[0], p[1]); });
      s.closePath();
      return s;
    }
    const dirtGeo = new T.ShapeGeometry(shapeFromPts([[0, -8], [95, 95], [0, 190], [-95, 95]]));
    dirtGeo.rotateX(-Math.PI / 2);
    const dirt = new T.Mesh(dirtGeo, new T.MeshLambertMaterial({ color: 0xa5713f }));
    dirt.position.y = 0.03;
    g.add(dirt);

    const grassInGeo = new T.ShapeGeometry(shapeFromPts([[0, 18], [70, 88], [0, 158], [-70, 88]]));
    grassInGeo.rotateX(-Math.PI / 2);
    const grassIn = new T.Mesh(grassInGeo, new T.MeshLambertMaterial({ color: turf }));
    grassIn.position.y = 0.06;
    g.add(grassIn);

    // Mound.
    const mound = new T.Mesh(new T.CylinderGeometry(9, 10, 0.9, 20),
      new T.MeshLambertMaterial({ color: 0xa5713f }));
    mound.position.set(0, 0.45, -60.5);
    g.add(mound);

    // Bases + home plate.
    const baseMat = new T.MeshLambertMaterial({ color: 0xf2efe4 });
    P.BASES.forEach(b => {
      if (b.name === 'H') return;
      const base = new T.Mesh(new T.BoxGeometry(1.3, 0.25, 1.3), baseMat);
      base.position.set(b.x, 0.15, -b.y);
      g.add(base);
    });
    const home = new T.Mesh(new T.CylinderGeometry(0.9, 0.9, 0.2, 5), baseMat);
    home.position.set(0, 0.12, 0);
    g.add(home);

    // Foul lines.
    const lineMat = new T.LineBasicMaterial({ color: 0xe8e4d8 });
    [-45, 45].forEach(ang => {
      const d = D.fenceAt(stadium, ang);
      const rad = ang * Math.PI / 180;
      const pts = [new T.Vector3(0, 0.2, 0), new T.Vector3(d * Math.sin(rad), 0.2, -d * Math.cos(rad))];
      g.add(new T.Line(new T.BufferGeometry().setFromPoints(pts), lineMat));
    });

    // Outfield fence — a segmented wall following the true fence distances.
    const fenceMat = new T.MeshLambertMaterial({ color: 0x1c5a38 });
    const wallH = stadium.wall || 10;
    const fenceShape = [];
    for (let i = 0; i <= steps; i++) {
      const ang = -45 + 90 * i / steps;
      const d = D.fenceAt(stadium, ang);
      const rad = ang * Math.PI / 180;
      fenceShape.push([d * Math.sin(rad), d * Math.cos(rad)]);
    }
    for (let i = 0; i < fenceShape.length - 1; i++) {
      const a = fenceShape[i], b = fenceShape[i + 1];
      const len = Math.hypot(b[0] - a[0], b[1] - a[1]);
      const seg = new T.Mesh(new T.BoxGeometry(len + 0.4, wallH, 1.2), fenceMat);
      seg.position.set((a[0] + b[0]) / 2, wallH / 2, -(a[1] + b[1]) / 2);
      seg.rotation.y = -Math.atan2(b[1] - a[1], b[0] - a[0]) + Math.PI / 2;
      g.add(seg);
    }

    // Simple crowd suggestion: a low ring beyond the fence.
    const crowdGeo = new T.RingGeometry(D.fenceAt(stadium, 0) + 2, D.fenceAt(stadium, 0) + 40, 48, 1, 0, Math.PI);
    crowdGeo.rotateX(-Math.PI / 2);
    crowdGeo.rotateY(Math.PI);
    const crowd = new T.Mesh(crowdGeo, new T.MeshLambertMaterial({ color: 0x14202c }));
    crowd.position.y = 6;
    g.add(crowd);

    this._stadiumGroup = g;
    this.scene.add(g);
  };

  Field.prototype.drawStadium = function (stadium) {
    if (this._builtStadiumId !== stadium.id) {
      this._buildStadium(stadium);
      this._builtStadiumId = stadium.id;
    }
    this._render();
  };

  Field.prototype.drawFielders = function (team, highlightPos) {
    P.FIELDERS.forEach(f => {
      if (f.pos === 'C') return;
      let mesh = this._fielderMeshes[f.pos];
      if (!mesh) {
        mesh = this._playerMesh(team ? new T.Color(team.primary) : 0x5f7fff);
        mesh.position.set(f.x, 0, -f.y);
        this.scene.add(mesh);
        this._fielderMeshes[f.pos] = mesh;
      }
      const active = f.pos === highlightPos;
      mesh.children[0].material.color.set(active ? 0xffd24a : (team ? team.primary : 0x5f7fff));
      mesh.scale.setScalar(active ? 1.18 : 1);
    });
    this._render();
  };

  Field.prototype.drawRunners = function (bases) {
    const pts = [P.BASES[0], P.BASES[1], P.BASES[2]];
    bases.forEach((r, i) => {
      const m = this._runnerMeshes[i];
      if (r) {
        m.position.set(pts[i].x, 2.9, -pts[i].y);
        m.visible = true;
      } else {
        m.visible = false;
      }
    });
    this._render();
  };

  Field.prototype.drawBallAt = function (path, t) {
    const pt = P.sampleAt(path, t);
    this._ball.position.set(pt.x, Math.max(0.65, pt.z), -pt.y);
    this._ball.visible = true;
    this._ballShadow.position.set(pt.x, 0.05, -pt.y);
    // Shadow fades and shrinks a little the higher the ball gets.
    const k = Math.max(0.25, 1 - pt.z / 90);
    this._ballShadow.scale.setScalar(k);
    this._ballShadow.material.opacity = 0.38 * k;
    this._ballShadow.visible = true;
    this._render();
    return pt;
  };

  Field.prototype.drawTrail = function (path, uptoT) {
    if (this._trailLine) { this.scene.remove(this._trailLine); this._trailLine.geometry.dispose(); }
    const pts = [];
    for (const pt of path) {
      if (pt.t > uptoT) break;
      pts.push(new T.Vector3(pt.x, Math.max(0.05, pt.z), -pt.y));
    }
    if (pts.length > 1) {
      const geo = new T.BufferGeometry().setFromPoints(pts);
      const mat = new T.LineBasicMaterial({ color: 0xffffff, transparent: true, opacity: 0.6 });
      this._trailLine = new T.Line(geo, mat);
      this.scene.add(this._trailLine);
    } else {
      this._trailLine = null;
    }
    this._render();
  };

  /* ===================================================================== */
  /* PitchView — the batter's-eye pitch tunnel.                            */
  /* ===================================================================== */

  const RELEASE_DIST = 55; // feet from the plate the ball "appears" at, visually

  function PitchView(canvas) {
    this.canvas = canvas;
    this.renderer = makeRenderer(canvas);
    this.scene = new T.Scene();
    this.camera = new T.PerspectiveCamera(58, 1, 0.3, 260);
    // The zone is ~3ft away at ~2.5ft height; the release point is ~60ft
    // away at ~6ft height. A camera that lookAt()s the distant mound is
    // tilted only ~2° down — nowhere near enough to keep something that
    // close and that low inside the frame. Fix the tilt explicitly instead
    // of deriving it from a far-away target: aim along a boresight tilted a
    // fixed 14° below horizontal, which keeps the zone in the lower half of
    // the frame and the mound in the upper half at the same time.
    this.camera.position.set(0, 5.5, 6.0);
    const tilt = 14 * Math.PI / 180;
    const dir = new T.Vector3(0, -Math.sin(tilt), -Math.cos(tilt));
    const target = this.camera.position.clone().add(dir.multiplyScalar(100));
    this.camera.lookAt(target);
    this.scene.background = new T.Color(0x0a1420);
    this.scene.fog = new T.Fog(0x0a1420, 40, 130);
    addLights(this.scene, 0x6f90c8, 0x1a2a1a);

    this._buildScene();
    this._planeForRaycast = new T.Plane(new T.Vector3(0, 0, 1), 0); // z = 0, the plate
    this._raycaster = new T.Raycaster();
    this.resize();
  }

  PitchView.prototype.resize = function () {
    const w = this.canvas.clientWidth || 1, h = this.canvas.clientHeight || 1;
    this.renderer.setSize(w, h, false);
    this.camera.aspect = w / h;
    this.camera.updateProjectionMatrix();
  };

  PitchView.prototype._render = function () { this.renderer.render(this.scene, this.camera); };

  PitchView.prototype._buildScene = function () {
    // Ground strip down the pitcher-batter lane.
    const dirtGeo = new T.PlaneGeometry(24, RELEASE_DIST + 20);
    dirtGeo.rotateX(-Math.PI / 2);
    const dirt = new T.Mesh(dirtGeo, new T.MeshLambertMaterial({ color: 0x5a3f26 }));
    dirt.position.set(0, -0.05, -(RELEASE_DIST - 5) / 2);
    this.scene.add(dirt);
    const grassGeo = new T.PlaneGeometry(400, 400);
    grassGeo.rotateX(-Math.PI / 2);
    const grass = new T.Mesh(grassGeo, new T.MeshLambertMaterial({ color: 0x1c4a2a }));
    grass.position.set(0, -0.08, -80);
    this.scene.add(grass);

    // Mound + a simple pitcher silhouette, for depth reference.
    const mound = new T.Mesh(new T.CylinderGeometry(9, 10, 0.9, 20),
      new T.MeshLambertMaterial({ color: 0x5a3f26 }));
    mound.position.set(0, 0.45, -RELEASE_DIST - 5.5);
    this.scene.add(mound);
    const pitcher = new T.Group();
    const torso = new T.Mesh(new T.CapsuleGeometry(1.2, 3.6, 4, 8), new T.MeshLambertMaterial({ color: 0x2a3644 }));
    torso.position.y = 3.6;
    pitcher.add(torso);
    pitcher.position.set(0, 0.9, -RELEASE_DIST - 5.5);
    this.scene.add(pitcher);

    // Strike zone wireframe, fixed at the plate.
    const w = ZONE.halfW * 2, h = ZONE.top - ZONE.bot;
    const zoneGeo = new T.BoxGeometry(w, h, 0.1);
    const edges = new T.EdgesGeometry(zoneGeo);
    this._zone = new T.LineSegments(edges, new T.LineBasicMaterial({ color: 0xffffff, transparent: true, opacity: 0.85 }));
    this._zone.position.set(0, (ZONE.top + ZONE.bot) / 2, 0);
    this.scene.add(this._zone);
    // Inner grid (3x3), faint.
    const gridMat = new T.LineBasicMaterial({ color: 0xffffff, transparent: true, opacity: 0.22 });
    const gridPts = [];
    for (let i = 1; i < 3; i++) {
      const x = -ZONE.halfW + w * i / 3;
      gridPts.push(new T.Vector3(x, ZONE.bot, 0), new T.Vector3(x, ZONE.top, 0));
      const y = ZONE.bot + h * i / 3;
      gridPts.push(new T.Vector3(-ZONE.halfW, y, 0), new T.Vector3(ZONE.halfW, y, 0));
    }
    this.scene.add(new T.LineSegments(new T.BufferGeometry().setFromPoints(gridPts), gridMat));

    // Ball.
    this._ball = new T.Mesh(new T.SphereGeometry(0.42, 16, 12), new T.MeshLambertMaterial({ color: 0xffffff, emissive: 0x222222 }));
    this._ball.visible = false;
    this.scene.add(this._ball);

    // Aim reticle (pitching) and PCI reticle (batting) — flat rings at the plate.
    this._aim = this._makeReticle(0xffd24a);
    this._pci = this._makeReticle(0x5ad7ff, true);
    this.scene.add(this._aim, this._pci);
  };

  PitchView.prototype._makeReticle = function (color, filled) {
    const g = new T.Group();
    const ring = new T.Mesh(new T.RingGeometry(0.42, 0.5, 24),
      new T.MeshBasicMaterial({ color: color, side: T.DoubleSide, transparent: true, opacity: 0.95 }));
    g.add(ring);
    if (filled) {
      const disc = new T.Mesh(new T.CircleGeometry(0.42, 24),
        new T.MeshBasicMaterial({ color: color, side: T.DoubleSide, transparent: true, opacity: 0.16 }));
      g.add(disc);
    }
    // Simple crosshair via two thin boxes instead of planes, so it reads at any angle.
    const barMat = new T.MeshBasicMaterial({ color: color });
    const h1 = new T.Mesh(new T.BoxGeometry(0.9, 0.05, 0.05), barMat);
    const v1 = new T.Mesh(new T.BoxGeometry(0.05, 0.9, 0.05), barMat);
    g.add(h1, v1);
    g.visible = false;
    return g;
  };

  PitchView.prototype.drawBackground = function () { this._render(); };
  PitchView.prototype.drawZone = function () { this._render(); };

  PitchView.prototype.drawAimReticle = function (x, z, color) {
    this._pci.visible = false;
    this._aim.visible = true;
    this._aim.position.set(x, z, 0.02);
    this._render();
  };

  PitchView.prototype.drawPCI = function (x, z, radius) {
    this._aim.visible = false;
    this._pci.visible = true;
    this._pci.position.set(x, z, 0.02);
    const s = Math.max(0.35, radius / 0.46);
    this._pci.scale.set(s, s, 1);
    this._render();
  };

  PitchView.prototype.drawBall = function (game, pitch, u, color) {
    const pos = game.pitchPos(pitch, u);
    const sceneZ = -(RELEASE_DIST * (1 - u));
    this._ball.position.set(pos.x, pos.z, sceneZ);
    this._ball.material.color.set(color || 0xffffff);
    this._ball.visible = true;
    this._render();
    return pos;
  };

  /** Raycast the mouse/touch position onto the plate plane (z=0) and return
   *  physics-space {x, z} — replaces the old linear pixel<->feet mapping. */
  PitchView.prototype.screenToFeet = function (px, py) {
    const w = this.canvas.clientWidth || 1, h = this.canvas.clientHeight || 1;
    const ndc = new T.Vector2((px / w) * 2 - 1, -(py / h) * 2 + 1);
    this._raycaster.setFromCamera(ndc, this.camera);
    const out = new T.Vector3();
    const hit = this._raycaster.ray.intersectPlane(this._planeForRaycast, out);
    if (!hit) return { x: 0, z: (ZONE.top + ZONE.bot) / 2 };
    return { x: out.x, z: out.y };
  };

  BM.render = { Field: Field, PitchView: PitchView };
})(window.BM = window.BM || {});
