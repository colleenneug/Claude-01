#!/bin/sh
# =============================================================================
#  End-to-end solo cutscene test: real X11 keys start and skip one.
#
#  In the 3D scene: C starts the cutscene (assets/cutscenes/intro.json, 11 s);
#  after a couple of seconds, Space is held until it skips (0.75 s of the
#  game's time); then Escape quits (Escape, like Space, only skips while a
#  cutscene plays). Checks:
#    - it started, and was skipped (not played to the end);
#    - the event already due (the sun dims at 1 s) fired while playing;
#    - skipping fired the state-changing events still to come (the sun
#      back up, the crate that drops in) but not the effects (the explosion
#      sound, the blast);
#    - the game then quit cleanly (0 Vulkan validation errors).
#
#  Needs a display and xdotool; without either it exits 77 (CTest: skipped).
#  Usage: cutscene.sh <playground_app> <log file>
# =============================================================================
APP="$1"; LOG="$2"
[ -n "$DISPLAY" ] || { echo "skipping: no display"; exit 77; }
command -v xdotool >/dev/null 2>&1 || { echo "skipping: xdotool not installed"; exit 77; }

rm -f "$LOG"
if [ -n "$PLAYGROUND_APP_PRELOAD" ]; then
  LD_PRELOAD="$PLAYGROUND_APP_PRELOAD" "$APP" --log-file "$LOG" >"$LOG.out" 2>&1 &
else
  "$APP" --log-file "$LOG" >"$LOG.out" 2>&1 &
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
xdotool key c
sleep 2.5
xdotool keydown space; sleep 2.0; xdotool keyup space
sleep 1.5
xdotool key Escape
for t in $(seq 1 40); do kill -0 $pid 2>/dev/null || break; sleep 0.25; done
wait $pid; code=$?

echo "game exit code: $code"
grep -E 'cutscene' "$LOG" | sed 's/^\[[^]]*\] //'
[ "$code" -eq 0 ] || { echo "FAIL: exit code $code"; tail -30 "$LOG"; exit 1; }
grep -q 'cutscene "intro" (.*) starts on tick' "$LOG" || { echo "FAIL: C did not start the cutscene"; exit 1; }
grep -q 'cutscene "intro" skipped on tick' "$LOG" || { echo "FAIL: holding Space did not skip it"; exit 1; }
grep -q 'cutscenes: 1 played (1 skipped)' "$LOG" || { echo "FAIL: the summary does not say played once, skipped once"; exit 1; }
grep -q 'cutscene event at 1.00 s: light' "$LOG" || { echo "FAIL: the event due at 1 s did not fire"; exit 1; }
grep -q 'cutscene event at 4.00 s: light' "$LOG" || { echo "FAIL: skipping did not restore the sun"; exit 1; }
grep -q 'cutscene event at 5.50 s: spawn cutscene crate' "$LOG" || { echo "FAIL: skipping did not spawn the crate"; exit 1; }
if grep -q 'cutscene event at 7.00 s' "$LOG"; then echo "FAIL: skipping fired an effect it should not have"; exit 1; fi
grep -q 'clean exit' "$LOG" || { echo "FAIL: no clean exit"; exit 1; }
echo "PASS"
