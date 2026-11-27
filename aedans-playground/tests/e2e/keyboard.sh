#!/bin/sh
# =============================================================================
#  End-to-end keyboard test: real X11 key events -> GLFW -> Window.cpp's
#  callbacks -> InputCollector -> InputLatch -> inputSystem / Fire.
#
#  Holds D for one second, taps Space twice, then presses Escape (the game's
#  quit key), and checks what the game logged at exit:
#    - the player moved right about 360 units (360 units/s for ~1 s),
#    - exactly two bullets were fired (one per tap, however many fixed
#      steps each frame ran),
#    - Escape ended the game cleanly.
#
#  Needs a display and xdotool; without either it exits 77 (CTest: skipped).
#  Usage: keyboard.sh <playground_app> <log file>
#
#  PLAYGROUND_APP_PRELOAD, if set, becomes LD_PRELOAD for the game only (not
#  for xdotool or this shell): sanitizer builds need it, see tests/CMakeLists.txt.
# =============================================================================
APP="$1"; LOG="$2"
[ -n "$DISPLAY" ] || { echo "skipping: no display"; exit 77; }
command -v xdotool >/dev/null 2>&1 || { echo "skipping: xdotool not installed"; exit 77; }

rm -f "$LOG"
if [ -n "$PLAYGROUND_APP_PRELOAD" ]; then
  LD_PRELOAD="$PLAYGROUND_APP_PRELOAD" "$APP" --log-file "$LOG" --scene sprites --sprites 10 >"$LOG.out" 2>&1 &
else
  "$APP" --log-file "$LOG" --scene sprites --sprites 10 >"$LOG.out" 2>&1 &
fi
pid=$!
for t in $(seq 1 120); do grep -q 'entering main loop' "$LOG" 2>/dev/null && break; sleep 0.25; done
grep -q 'entering main loop' "$LOG" || { echo "FAIL: the game did not start"; kill $pid; exit 1; }

win=$(xdotool search --name "Aedan" | head -1)
[ -n "$win" ] || { echo "FAIL: game window not found"; kill $pid; exit 1; }
# Keys go to whichever window has focus. With no window manager (Xvfb), take
# it explicitly and confirm, so no keystroke is sent to nowhere.
for t in $(seq 1 20); do
  xdotool windowfocus "$win" 2>/dev/null
  [ "$(xdotool getwindowfocus 2>/dev/null)" = "$win" ] && break
  sleep 0.1
done
[ "$(xdotool getwindowfocus 2>/dev/null)" = "$win" ] || { echo "FAIL: could not focus the game window"; kill $pid; exit 1; }
sleep 0.3
xdotool keydown d; sleep 1.0; xdotool keyup d
sleep 0.2
xdotool key space; sleep 0.2; xdotool key space
sleep 0.2
xdotool key Escape
for t in $(seq 1 40); do kill -0 $pid 2>/dev/null || break; sleep 0.25; done
wait $pid; code=$?

line=$(grep 'player ended at' "$LOG" | sed 's/.*player ended at //')
echo "game exit code: $code; player ended at $line"
x=$(echo "$line" | sed -n 's/^x=\([-0-9]*\), y=\([-0-9]*\);.*/\1/p')
y=$(echo "$line" | sed -n 's/^x=\([-0-9]*\), y=\([-0-9]*\);.*/\2/p')
bullets=$(echo "$line" | sed -n 's/.*; \([0-9]*\) bullets fired.*/\1/p')

[ "$code" -eq 0 ] || { echo "FAIL: exit code $code; the game's own output follows"; tail -40 "$LOG.out"; exit 1; }
grep -q 'quit requested' "$LOG" || { echo "FAIL: Escape did not quit"; exit 1; }
[ -n "$x" ] && [ "$x" -ge 250 ] && [ "$x" -le 500 ] || { echo "FAIL: player x=$x, expected about 360"; exit 1; }
[ -n "$y" ] && [ "$y" -ge -20 ] && [ "$y" -le 20 ] || { echo "FAIL: player y=$y, expected about 0"; exit 1; }
[ "$bullets" = "2" ] || { echo "FAIL: $bullets bullets fired, expected 2"; exit 1; }
echo "PASS"
