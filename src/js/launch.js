/* ============================================================
   Native launch bar — the title-screen plate that opens the C++
   desktop build in cpp/.

   A page can't start a process, so the button asks the local host
   (server/server.js) to spawn cpp/build/erebus_native for it. Three
   things can be true when it's clicked, and each has to say so
   plainly rather than silently doing nothing:

     served + built      -> spawn it
     served + not built  -> offer the build command
     no server at all    -> the bundled artifact or a file:// open;
                            offer both commands

   The status line under LAUNCH GAME reports which of those is the
   case before the click, so the state is visible, not discovered.
   ============================================================ */
(function (SF) {
  'use strict';
  const { $, toast, confirmDialog } = SF.util;

  const BUILD_CMD = 'cmake -S cpp -B cpp/build -DCMAKE_BUILD_TYPE=Release && cmake --build cpp/build -j';
  const RUN_CMD   = './cpp/build/erebus_native';
  const SERVE_CMD = 'node server/server.js';

  let status = null;      // last /api/native payload, or null when unreachable
  let busy = false;

  const bar = () => $('#btn-launch-native');

  function setStatus(text, kind) {
    const node = $('#launch-status');
    if (!node) return;
    node.textContent = text;
    node.className = 'lb-status' + (kind ? ' ' + kind : '');
  }

  /* Ask the host what it can do. A failure here is ordinary — the page
     is just as likely to be opened as a file or as the bundled artifact
     as it is to be served — so it downgrades the button rather than
     logging an error. */
  async function probe() {
    try {
      const res = await fetch('api/native', { cache: 'no-store' });
      if (!res.ok) throw new Error('HTTP ' + res.status);
      status = await res.json();
    } catch (e) {
      status = null;
    }
    render();
    return status;
  }

  function render() {
    if (busy) return;
    if (!status)            return setStatus('HOST OFFLINE — MANUAL LAUNCH', 'warn');
    if (status.running)     return setStatus('NATIVE BUILD RUNNING — PID ' + status.pid, 'ok');
    if (!status.built)      return setStatus('NATIVE BUILD NOT COMPILED', 'warn');
    return setStatus('NATIVE BUILD READY — C++ / OPENGL', 'ok');
  }

  /* The fallback: hand over the exact commands instead of a dead button.
     CONFIRM copies them, since the one thing wanted here is a paste into
     a terminal. */
  async function manual(headline, cmds) {
    const body = '<span class="lb-hint">' + headline + '</span>' +
      cmds.map((c) => '<code class="lb-cmd">' + c + '</code>').join('');
    const copy = await confirmDialog('NATIVE BUILD', body, 'COPY COMMANDS');
    if (!copy) return;
    try {
      await navigator.clipboard.writeText(cmds.join('\n'));
      toast('COMMANDS COPIED', 'good');
    } catch (e) {
      toast('CLIPBOARD BLOCKED — COPY BY HAND', 'warn');
    }
  }

  async function launch() {
    if (busy) return;
    const el = bar();

    // The status is only as fresh as the last probe; re-check, since the
    // build may have finished (or the game may have been closed) since.
    busy = true;
    el.classList.add('is-busy');
    setStatus('CONTACTING HOST', '');
    await probe();
    busy = false;
    el.classList.remove('is-busy');

    if (!status) {
      SF.audio.sfx.deny();
      render();
      return manual('This page isn’t being served by the project host, so it can’t start a process. Run the desktop build directly:',
                    [BUILD_CMD, RUN_CMD]);
    }
    if (!status.built) {
      SF.audio.sfx.deny();
      render();
      return manual('The native build hasn’t been compiled yet. Build it once, then this button launches it:',
                    [BUILD_CMD]);
    }
    if (status.running) {
      SF.audio.sfx.deny();
      toast('NATIVE BUILD ALREADY RUNNING — PID ' + status.pid, 'warn');
      return;
    }

    busy = true;
    el.classList.add('is-busy');
    el.disabled = true;
    setStatus('LAUNCHING', '');
    SF.audio.sfx.confirm();

    let payload = null;
    try {
      const res = await fetch('api/native/launch', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: '{}'
      });
      payload = await res.json();
    } catch (e) {
      payload = { ok: false, reason: 'UNREACHABLE', detail: 'The host stopped answering.' };
    }

    busy = false;
    el.classList.remove('is-busy');
    el.disabled = false;

    if (payload && payload.ok) {
      status = payload.status || status;
      setStatus('NATIVE BUILD RUNNING — PID ' + payload.pid, 'ok');
      toast('NATIVE BUILD LAUNCHED — CHECK YOUR DESKTOP', 'good');
      // The window opens on another surface entirely; poll until it exits
      // so the plate goes back to READY on its own.
      watch();
      return;
    }

    SF.audio.sfx.deny();
    render();
    const detail = (payload && payload.detail) || 'The host refused the launch.';
    if (payload && payload.reason === 'NO_DISPLAY') {
      return manual(detail, [RUN_CMD]);
    }
    if (payload && payload.reason === 'NOT_BUILT') {
      return manual(detail, [BUILD_CMD]);
    }
    toast('LAUNCH FAILED — ' + detail.toUpperCase(), 'bad');
  }

  let watcher = 0;
  function watch() {
    clearInterval(watcher);
    watcher = setInterval(async () => {
      await probe();
      if (!status || !status.running) clearInterval(watcher);
    }, 4000);
  }

  function init() {
    const el = bar();
    if (!el) return;
    el.addEventListener('click', launch);
    probe();
  }

  SF.launch = { init, probe, launch, SERVE_CMD };
})(window.SF);
