#!/bin/sh
# =============================================================================
#  End-to-end camera test: real X11 key events drive the 3D scene's camera.
#
#  Starts the default (3D) scene, presses Tab (orbit -> fly), holds W for one
#  second (fly forward at 3 units/s), then presses Escape, and checks what
#  the scene logged at exit:
#    - the camera ended in fly mode,
#    - it moved about 3 units (2 to 4.5: key timing is not exact),
#    - every model of the scene had loaded (4 of 4),
#    - Escape ended the game cleanly.
#
#  Needs a display and xdotool; without either it exits 77 (CTest: skipped).
#  Usage: camera.sh <playground_app> <log file>
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
# Wait until the loop runs and the models are in (they load in the background).
for t in $(seq 1 160); do grep -q 'spheres ready' "$LOG" 2>/dev/null && break; sleep 0.25; done
grep -q 'spheres ready' "$LOG" || { echo "FAIL: the 3D scene did not load"; tail -20 "$LOG"; kill $pid; exit 1; }

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
sleep 0.3
xdotool keydown w; sleep 1.0; xdotool keyup w
sleep 0.3
xdotool key Escape
for t in $(seq 1 40); do kill -0 $pid 2>/dev/null || break; sleep 0.25; done
wait $pid; code=$?

line=$(grep 'camera ended in' "$LOG" | sed 's/.*camera ended in //')
echo "game exit code: $code; camera ended in $line"
mode=$(echo "$line" | sed -n 's/^\([a-z]*\) mode.*/\1/p')
moved=$(echo "$line" | sed -n 's/^[a-z]* mode, \([0-9.]*\) units.*/\1/p')

[ "$code" -eq 0 ] || { echo "FAIL: exit code $code; the game's own output follows"; tail -40 "$LOG.out"; exit 1; }
grep -q 'camera: fly' "$LOG" || { echo "FAIL: Tab did not switch to fly mode"; exit 1; }
[ "$mode" = "fly" ] || { echo "FAIL: camera ended in '$mode' mode, expected fly"; exit 1; }
awk -v m="$moved" 'BEGIN { exit !(m >= 2.0 && m <= 4.5) }' || { echo "FAIL: camera moved $moved units, expected about 3"; exit 1; }
echo "$line" | grep -q '4 of 4 object(s) ready' || { echo "FAIL: not every model loaded"; exit 1; }
echo "PASS"
