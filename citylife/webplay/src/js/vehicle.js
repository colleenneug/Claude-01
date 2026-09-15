// Drivable cars: a handful of simple box vehicles parked around the spawn plaza.
window.CityVehicle = (function () {
  const COLORS = [0xa855f7, 0x67e8f9, 0xf97316, 0x22d3ee];
  const ACCEL = 14;
  const BRAKE = 22;
  const MAX_SPEED = 26;
  const TURN_RATE = 2.4;
  const FRICTION = 8;

  function buildCarMesh(color) {
    const group = new THREE.Group();
    const bodyMat = new THREE.MeshStandardMaterial({ color });
    const glassMat = new THREE.MeshStandardMaterial({ color: 0x1e293b });
    const wheelMat = new THREE.MeshStandardMaterial({ color: 0x111318 });

    const base = new THREE.Mesh(new THREE.BoxGeometry(2.1, 0.6, 4.2), bodyMat);
    base.position.y = 0.6;
    group.add(base);

    const cabin = new THREE.Mesh(new THREE.BoxGeometry(1.8, 0.6, 2.2), glassMat);
    cabin.position.set(0, 1.1, -0.2);
    group.add(cabin);

    const wheelGeo = new THREE.CylinderGeometry(0.4, 0.4, 0.4, 12);
    [[-1, 0.3, 1.4], [1, 0.3, 1.4], [-1, 0.3, -1.4], [1, 0.3, -1.4]].forEach(([x, y, z]) => {
      const wheel = new THREE.Mesh(wheelGeo, wheelMat);
      wheel.rotation.z = Math.PI / 2;
      wheel.position.set(x, y, z);
      group.add(wheel);
    });

    return group;
  }

  function spawnCars(scene, spots) {
    return spots.map((spot, i) => {
      const mesh = buildCarMesh(COLORS[i % COLORS.length]);
      mesh.position.copy(spot);
      mesh.rotation.y = Math.random() * Math.PI * 2;
      scene.add(mesh);
      return {
        mesh,
        speed: 0,
        heading: mesh.rotation.y,
        occupied: false,
      };
    });
  }

  function update(car, dt, input, collideAt) {
    if (input.forward) car.speed += ACCEL * dt;
    else if (input.back) car.speed -= BRAKE * dt;
    else car.speed -= Math.sign(car.speed) * FRICTION * dt;

    car.speed = Math.max(-MAX_SPEED * 0.5, Math.min(MAX_SPEED, car.speed));
    if (Math.abs(car.speed) < 0.05) car.speed = 0;

    const turnDir = (input.left ? 1 : 0) - (input.right ? 1 : 0);
    if (car.speed !== 0) {
      car.heading += turnDir * TURN_RATE * dt * Math.sign(car.speed);
    }

    const forward = new THREE.Vector3(Math.sin(car.heading), 0, Math.cos(car.heading));
    const next = car.mesh.position.clone().addScaledVector(forward, car.speed * dt);

    if (collideAt && collideAt(next, 1.6)) {
      car.speed = 0;
    } else {
      car.mesh.position.copy(next);
    }
    car.mesh.rotation.y = car.heading;
  }

  function nearest(cars, point, maxDist) {
    let best = null, bestDist = maxDist;
    cars.forEach((car) => {
      if (car.occupied) return;
      const d = car.mesh.position.distanceTo(point);
      if (d < bestDist) { best = car; bestDist = d; }
    });
    return best;
  }

  return { spawnCars, update, nearest };
})();
