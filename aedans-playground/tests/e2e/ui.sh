#!/bin/sh
# =============================================================================
#  End-to-end developer UI test: real X11 keys drive the ImGui overlay.
#
#  Starts the 3D scene with Vulkan validation on (debug builds), presses F1
#  (the UI appears, drawn over the frame), Ctrl+S (saves the scene to
#  --scene-file), Ctrl+O (loads it back, replacing every entity), F1 (hides
#  it), then Escape, and checks:
#    - the UI was shown and hidden, and its overlay was set up;
#    - the scene file exists, is a scene, and holds every entity;
#    - loading it brought all of them back (the walker too: the end summary
#      still reports it);
#    - the game exited cleanly: 0 Vulkan validation errors.
#
#  Needs a display and xdotool; without either it exits 77 (CTest: skipped).
#  Usage: ui.sh <playground_app> <log file>
# =============================================================================
APP="$1"; LOG="$2"
[ -n "$DISPLAY" ] || { echo "skipping: no display"; exit 77; }
command -v xdotool >/dev/null 2>&1 || { echo "skipping: xdotool not installed"; exit 77; }

rm -f "$LOG" "$LOG.scene.json"
if [ -n "$PLAYGROUND_APP_PRELOAD" ]; then
  LD_PRELOAD="$PLAYGROUND_APP_PRELOAD" "$APP" --scene-file "$LOG.scene.json" --log-file "$LOG" >"$LOG.out" 2>&1 &
else
  "$APP" --scene-file "$LOG.scene.json" --log-file "$LOG" >"$LOG.out" 2>&1 &
fi
pid=$!
# Wait until the loop runs and every model is in (they load in the background).
loaded() {
  total=$(sed -n 's/.*3D scene: \([0-9]*\) object(s) loading.*/\1/p' "$LOG" 2>/dev/null | head -1)
  [ -n "$total" ] && [ "$(grep -c ' ready: ' "$LOG")" -ge "$total" ]
}
for t in $(seq 1 160); do loaded && break; sleep 0.25; done
loaded || { echo "FAIL: the 3D scene did not load"; tail -20 "$LOG"; kill $pid; exit 1; }

win=$(xdotool search --name "Aedan" | head -1)
[ -n "$win" ] || { echo "FAIL: game window not found"; kill $pid; exit 1; }
for t in $(seq 1 20); do
  xdotool windowfocus "$win" 2>/dev/null
  [ "$(xdotool getwindowfocus 2>/dev/null)" = "$win" ] && break
  sleep 0.1
done
[ "$(xdotool getwindowfocus 2>/dev/null)" = "$win" ] || { echo "FAIL: could not focus the game window"; kill $pid; exit 1; }
sleep 0.3
xdotool key F1
sleep 0.8
# Chords as a person presses them: Ctrl held across several frames. (An
# instant `xdotool key ctrl+o` can fall inside one slow software-rendered
# frame, and ImGui then never sees Ctrl and O down together.)
chord() { xdotool keydown ctrl; sleep 0.3; xdotool key "$1"; sleep 0.3; xdotool keyup ctrl; }
chord s
sleep 0.8
chord o
sleep 1.0
xdotool key F1
sleep 0.5
xdotool key Escape
for t in $(seq 1 40); do kill -0 $pid 2>/dev/null || break; sleep 0.25; done
wait $pid; code=$?

echo "game exit code: $code"
[ "$code" -eq 0 ] || { echo "FAIL: exit code $code; the log follows"; tail -40 "$LOG"; exit 1; }
grep -q 'UI overlay ready' "$LOG" || { echo "FAIL: the overlay was not set up"; exit 1; }
grep -q 'debug UI shown' "$LOG" || { echo "FAIL: F1 did not show the UI"; exit 1; }
grep -q 'debug UI hidden' "$LOG" || { echo "FAIL: F1 did not hide the UI"; exit 1; }
saved=$(sed -n 's/.*scene: saved \([0-9]*\) entities.*/\1/p' "$LOG" | head -1)
loaded=$(sed -n 's/.*scene: loaded \([0-9]*\) entities.*/\1/p' "$LOG" | head -1)
echo "saved $saved entities, loaded $loaded"
[ -n "$saved" ] && [ "$saved" -ge 20 ] || { echo "FAIL: Ctrl+S did not save the scene"; exit 1; }
[ "$loaded" = "$saved" ] || { echo "FAIL: Ctrl+O loaded '$loaded' entities, saved $saved"; exit 1; }
grep -q '"format": "aedans-playground-scene"' "$LOG.scene.json" || { echo "FAIL: the scene file is not a scene"; exit 1; }
grep -q '"CharacterController"' "$LOG.scene.json" || { echo "FAIL: the walker was not saved"; exit 1; }
grep -q 'walker ended at' "$LOG" || { echo "FAIL: the walker did not survive the load"; exit 1; }
grep -q 'clean exit' "$LOG" || { echo "FAIL: no clean exit (validation errors?)"; exit 1; }
echo "PASS"
