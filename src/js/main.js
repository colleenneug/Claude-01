/* Entry point: wire the subsystems together and boot the terminal. */
(function (SF) {
  'use strict';

  function start() {
    SF.cursor.init();
    SF.fx.initStars();
    SF.ui.bind();
    SF.combat.bind();

    // Browsers gate audio until the first gesture; unlock on any of them.
    const unlock = () => SF.audio.unlock();
    document.addEventListener('pointerdown', unlock, { once: true });
    document.addEventListener('keydown', unlock, { once: true });

    // Skipping the POST sequence is the first thing the cursor can do.
    document.addEventListener('pointerdown', () => SF.ui.skipBoot());
    document.addEventListener('keydown', () => SF.ui.skipBoot());

    SF.ui.runBoot();
  }

  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', start);
  } else {
    start();
  }
})(window.SF);
