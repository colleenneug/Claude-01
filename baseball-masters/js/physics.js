/* Baseball Masters — batted-ball flight, fielder pursuit, and play resolution math. */
(function (BM) {
  'use strict';

  const G = 32.174;          // ft/s^2
  const MPH = 1.46667;       // mph -> ft/s
  const DRAG = 0.00160;      // lumped drag coefficient (1/ft), fit to Statcast carry
  const LIFT = 0.00060;      // backspin (Magnus) lift, applied perpendicular to velocity
  const DT = 1 / 240;

  /**
   * Integrate a batted ball.
   * @param {number} exitVelo mph
   * @param {number} launch   degrees above horizontal
   * @param {number} spray    degrees; negative pulls to left field, positive to right
   * @returns {{path:Array, hangTime:number, distance:number, apex:number, landing:{x:number,y:number}}}
   */
  function flight(exitVelo, launch, spray) {
    const v = exitVelo * MPH;
    const la = launch * Math.PI / 180;
    const sa = spray * Math.PI / 180;
    const horiz = v * Math.cos(la);
    let x = 0, y = 0, z = 3.0;                  // contact point ~3 ft off the ground
    let vx = horiz * Math.sin(sa);
    let vy = horiz * Math.cos(sa);
    let vz = v * Math.sin(la);

    const path = [{ x: x, y: y, z: z, t: 0 }];
    let t = 0, apex = z;
    // Cap the sim so a foul pop straight up can't spin forever.
    while (z > 0 && t < 12) {
      const speed = Math.sqrt(vx * vx + vy * vy + vz * vz) || 1e-6;
      const vh = Math.sqrt(vx * vx + vy * vy) || 1e-6;
      // Magnus force is perpendicular to velocity, so it has a horizontal
      // component too: it slows a climbing ball and pushes a falling one on.
      const ax = -DRAG * speed * vx - LIFT * speed * vz * (vx / vh);
      const ay = -DRAG * speed * vy - LIFT * speed * vz * (vy / vh);
      const az = -DRAG * speed * vz - G + LIFT * speed * vh;
      vx += ax * DT; vy += ay * DT; vz += az * DT;
      x += vx * DT; y += vy * DT; z += vz * DT;
      t += DT;
      if (z > apex) apex = z;
      if (path.length === 0 || t - path[path.length - 1].t > 0.02) {
        path.push({ x: x, y: y, z: Math.max(0, z), t: t });
      }
    }
    path.push({ x: x, y: y, z: 0, t: t });
    return {
      path: path,
      hangTime: t,
      distance: Math.sqrt(x * x + y * y),
      apex: apex,
      landing: { x: x, y: y }
    };
  }

  /** Distance the ball would carry if it never hit a wall — used for HR calls. */
  function carry(exitVelo, launch) {
    return flight(exitVelo, launch, 0).distance;
  }

  /** Where the ball is along a path at time t (linear interp between samples). */
  function sampleAt(path, t) {
    if (t <= 0) return path[0];
    for (let i = 1; i < path.length; i++) {
      if (path[i].t >= t) {
        const a = path[i - 1], b = path[i];
        const u = (t - a.t) / Math.max(1e-6, b.t - a.t);
        return { x: a.x + (b.x - a.x) * u, y: a.y + (b.y - a.y) * u, z: a.z + (b.z - a.z) * u, t: t };
      }
    }
    return path[path.length - 1];
  }

  /* --------------------------------------------------------------- fielders */
  // Standard alignment, in feet. Home plate is the origin, +y toward center.
  const FIELDERS = [
    { pos: 'P',  x: 0,    y: 60.5 },
    { pos: 'C',  x: 0,    y: -6 },
    { pos: '1B', x: 62,   y: 88 },
    { pos: '2B', x: 38,   y: 148 },
    { pos: 'SS', x: -40,  y: 148 },
    { pos: '3B', x: -62,  y: 88 },
    { pos: 'LF', x: -146, y: 288 },
    { pos: 'CF', x: 4,    y: 322 },
    { pos: 'RF', x: 150,  y: 286 }
  ];
  const BASES = [
    { name: '1B', x: 63.64, y: 63.64 },
    { name: '2B', x: 0,     y: 127.28 },
    { name: '3B', x: -63.64, y: 63.64 },
    { name: 'H',  x: 0,     y: 0 }
  ];

  function dist(ax, ay, bx, by) { return Math.hypot(ax - bx, ay - by); }

  /** Seconds for a fielder of the given rating to cover a distance. */
  function pursuitTime(feet, fieldingRating, reaction) {
    const speed = 20 + (fieldingRating / 99) * 9;   // 20 - 29 ft/s
    const react = reaction !== undefined ? reaction : 0.45 - (fieldingRating / 99) * 0.18;
    return react + feet / speed;
  }

  /** Seconds for a runner to cover 90 ft, from a speed rating. */
  function runTime(speedRating, bases) {
    const per = 4.65 - (speedRating / 99) * 0.95;    // 4.65s down to 3.70s to first
    return per * bases;
  }

  function isFoul(sprayDeg) { return Math.abs(sprayDeg) > 45; }

  BM.physics = {
    G: G, MPH: MPH, DT: DT,
    flight: flight, carry: carry, sampleAt: sampleAt,
    FIELDERS: FIELDERS, BASES: BASES,
    dist: dist, pursuitTime: pursuitTime, runTime: runTime, isFoul: isFoul
  };
})(window.BM = window.BM || {});
