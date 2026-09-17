/* Entry point: wire the subsystems together and boot the terminal. */
(function (SF) {
  'use strict';

  function start() {
    SF.cursor.init();
    SF.fx.initStars();
    SF.ui.bind();
    SF.launch.init();

    // Browsers gate audio until the first gesture; unlock on any of them.
    const unlock = () => SF.audio.unlock();
    document.addEventListener('pointerdown', unlock, { once: true });
    document.addEventListener('keydown', unlock, { once: true });

    // Skipping the POST sequence is the first thing the cursor can do.
    document.addEventListener('pointerdown', () => SF.ui.skipBoot());
    document.addEventListener('keydown', () => SF.ui.skipBoot());

    // Keyboard focus. A page embedded in a frame - a preview pane, the
    // launcher, anything that iframes the game - gets no key events at
    // all until that frame is focused, which is why "PRESS ANY KEY" can
    // look like it does nothing while the mouse still works. Ask for
    // focus on load and take it back on every pointer contact, and tell
    // the player to click if we still don't have it a moment later,
    // rather than leaving a dead keyboard unexplained.
    const grab = () => { try { window.focus(); } catch (e) {} };
    grab();
    document.addEventListener('pointerdown', grab);
    window.addEventListener('focus', () => document.body.classList.remove('needs-focus'));
    window.addEventListener('blur', () => {
      if (window.self !== window.top) document.body.classList.add('needs-focus');
    });
    if (window.self !== window.top) {
      setTimeout(() => {
        if (!document.hasFocus()) document.body.classList.add('needs-focus');
      }, 700);
    }

    SF.ui.runBoot();
  }

  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', start);
  } else {
    start();
  }
})(window.SF);
