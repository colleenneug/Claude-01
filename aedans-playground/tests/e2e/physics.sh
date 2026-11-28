#!/bin/sh
# =============================================================================
#  End-to-end physics test: real X11 input walks a character and shoots.
#
#  Starts the default (3D) scene, presses Tab twice (orbit -> fly -> walk),
#  holds W for a second (walking at 3.5 m/s, looking where the orbit camera
#  looked: down and to the left), clicks the left button (a hitscan shot
#  from the eye), presses Escape, and checks what the scene logged at exit:
#    - walk mode was entered and the game ended cleanly;
#    - the walker moved (more than 1.5 m) and ended on the ground;
#    - the shot was fired and hit something (looking down, at least the
#      ground), and was logged with what it hit;
#    - the physics stepped, with the scene's bodies in it;
#    - sounds played: the shot's laser and impact, and crates landing.
#
#  Needs a display and xdotool; without either it exits 77 (CTest: skipped).
#  Usage: physics.sh <playground_app> <log file>
#  PLAYGROUND_APP_PRELOAD: as in keyboard.sh (sanitizer builds).
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
xdotool key Tab
sleep 0.2
xdotool key Tab
sleep 0.3
xdotool keydown w; sleep 1.0; xdotool keyup w
sleep 0.3
xdotool click 1
sleep 0.4
xdotool key Escape
for t in $(seq 1 40); do kill -0 $pid 2>/dev/null || break; sleep 0.25; done
wait $pid; code=$?

line=$(grep 'walker ended at' "$LOG" | sed 's/.*walker ended at //')
echo "game exit code: $code; walker ended at $line"
[ "$code" -eq 0 ] || { echo "FAIL: exit code $code; the game's own output follows"; tail -40 "$LOG.out"; exit 1; }
grep -q 'camera: walk' "$LOG" || { echo "FAIL: Tab Tab did not reach walk mode"; exit 1; }
grep -q 'camera ended in walk mode' "$LOG" || { echo "FAIL: not in walk mode at the end"; exit 1; }
moved=$(echo "$line" | sed -n 's/.*), \([0-9.]*\) m from the start.*/\1/p')
awk -v m="$moved" 'BEGIN { exit !(m >= 1.5) }' || { echo "FAIL: the walker moved $moved m, expected over 1.5"; exit 1; }
echo "$line" | grep -q 'on the ground;' || { echo "FAIL: the walker did not end on the ground: $line"; exit 1; }
shots=$(echo "$line" | sed -n 's/.*; \([0-9]*\) shot(s), \([0-9]*\) hit(s).*/\1 \2/p')
set -- $shots
[ -n "$1" ] && [ "$1" -ge 1 ] && [ "$2" -ge 1 ] || { echo "FAIL: shots and hits: '$shots'"; exit 1; }
grep -q 'shot 1: hit ' "$LOG" || { echo "FAIL: the shot was not logged with what it hit"; exit 1; }
steps=$(echo "$line" | sed -n 's/.*physics: \([0-9]*\) steps, \([0-9]*\) bodies.*/\1 \2/p')
set -- $steps
[ -n "$1" ] && [ "$1" -ge 120 ] && [ "$2" -ge 20 ] || { echo "FAIL: physics steps and bodies: '$steps'"; exit 1; }
# Sound (on the null device in tests): the shot's laser and its impact at
# least, plus the stack's crates knocking as they land.
sounds=$(sed -n 's/.*audio: \([0-9]*\) sound(s) played.*/\1/p' "$LOG" | head -1)
[ -n "$sounds" ] && [ "$sounds" -ge 2 ] || { echo "FAIL: '$sounds' sounds played, expected at least 2"; exit 1; }
echo "$sounds sounds played"
echo "PASS"
