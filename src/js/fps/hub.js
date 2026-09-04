/* ============================================================
   Living in the station: the lifts and the terminals.

   A lift car is a real floor with a collider that moves in Y only, so
   the spatial hash it was bucketed into at build time stays correct.
   Stand on one and hold the use key: it goes to the next deck up and
   wraps back to the bottom from the top. While it moves it carries you
   directly rather than leaving that to gravity, which at a metre a
   frame would drop you through the floor.

   A terminal is the reason you came: hold the use key at one and the
   station hands you back to the menu it stands for.
   ============================================================ */
(function (SF) {
  'use strict';

  const CALL_TIME = 0.45;         // how long you hold the key before it moves
  const LIFT_SPEED = 4.2;         // metres a second
  const USE_RANGE = 2.9;          // how close you stand to a terminal
  const USE_TIME = 0.5;

  function create(ctx) {
    const { level, player, hud } = ctx;
    const lifts = level.lifts || [];
    const terminals = level.terminals || [];
    const stops = [level.decks.a, level.decks.b, level.decks.c];
    const NAMES = ['A', 'B', 'C'];

    let prompting = false;
    let useT = 0;
    let riding = null;

    function onCar(l) {
      return Math.hypot(player.position.x - l.x, player.position.z - l.z) < l.r - 0.2 &&
             Math.abs(player.position.y - l.y) < 1.2;
    }

    function send(l) {
      l.deck = (l.deck + 1) % stops.length;
      l.target = stops[l.deck];
      l.moving = true;
      riding = l;
      hud.pickup('DECK ' + NAMES[l.deck]);
      SF.audio.sfx.open();
    }

    function update(dt, holding) {
      /* ---- lifts in motion ---- */
      for (const l of lifts) {
        if (!l.moving) continue;
        const dir = Math.sign(l.target - l.y);
        l.y += dir * LIFT_SPEED * dt;
        if ((dir > 0 && l.y >= l.target) || (dir < 0 && l.y <= l.target)) {
          l.y = l.target;
          l.moving = false;
          if (riding === l) riding = null;
          SF.audio.sfx.objective();
        }
        l.car.position.y = l.y;
        l.col.top = l.y;
        l.col.bottom = l.y - 0.3;
        l.ring.material.emissiveIntensity = 2.4 + Math.sin(performance.now() / 90) * 1.2;
        // carry whoever is standing on it
        if (riding === l && onCar(l)) {
          player.state.pos.y = l.y;
          player.state.vel.y = 0;
          player.state.onGround = true;
        }
      }

      /* ---- what is in reach ---- */
      let liftHere = null;
      for (const l of lifts) if (!l.moving && onCar(l)) { liftHere = l; break; }

      let term = null, td = USE_RANGE;
      if (!liftHere) {
        for (const t of terminals) {
          const d = Math.hypot(player.position.x - t.x, player.position.z - t.z);
          if (d < td && Math.abs(player.position.y - t.y) < 2.4) { td = d; term = t; }
        }
      }

      if (!liftHere && !term) {
        if (prompting) { hud.pickup(''); prompting = false; }
        useT = 0;
        return;
      }
      prompting = true;

      if (liftHere) {
        const next = NAMES[(liftHere.deck + 1) % stops.length];
        if (holding) {
          useT += dt;
          hud.pickup('LIFT TO DECK ' + next + ' ' + Math.max(0, CALL_TIME - useT).toFixed(1) + 's');
          if (useT >= CALL_TIME) { useT = 0; send(liftHere); }
        } else {
          useT = 0;
          hud.pickup('HOLD F — LIFT TO DECK ' + next);
        }
        return;
      }

      if (holding) {
        useT += dt;
        hud.pickup('OPENING ' + term.name + ' ' + Math.max(0, USE_TIME - useT).toFixed(1) + 's');
        if (useT >= USE_TIME) {
          useT = 0;
          hud.pickup('');
          SF.audio.sfx.confirm();
          if (ctx.onTerminal) ctx.onTerminal(term.id);
        }
      } else {
        useT = 0;
        hud.pickup('HOLD F — ' + term.name + ' · ' + term.line);
      }
    }

    /* Which deck the player is standing on, for the readout. */
    function deckOf() {
      const y = player.position.y;
      let best = 0;
      for (let i = 0; i < stops.length; i++) if (y >= stops[i] - 1.5) best = i;
      return NAMES[best];
    }

    function destroy() { hud.pickup(''); }

    return { update, destroy, deckOf, get riding() { return !!riding; } };
  }

  SF.hub = { create };
})(window.SF);
