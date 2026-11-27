#!/bin/sh
# =============================================================================
#  End-to-end test of the WINDOWED network client.
#
#  1. starts a dedicated server (`--server --port 0`, no display);
#  2. runs the windowed game against it (`--connect 127.0.0.1:PORT
#     --frames 240`), which opens a window, joins, and draws the server's
#     world for 240 frames, with Vulkan validation on in debug builds;
#  3. stops the server with Ctrl+C;
#  then checks: the game exited 0 with a clean exit (no validation errors),
#  it was welcomed and was still playing at the end, it knew the 64 balls
#  and its own player, and the server saw it join and leave.
#
#  Needs a display: exit code 77 (skipped) without one.
#  Usage: netclient.sh <playground_app> <log prefix>
# =============================================================================
APP="$1"; LOG="$2"
[ -n "$DISPLAY" ] || { echo "no display: skipped"; exit 77; }
rm -f "$LOG.server.log" "$LOG.client.log"

env -u DISPLAY -u WAYLAND_DISPLAY "$APP" --server --port 0 --log-file "$LOG.server.log" >"$LOG.server.out" 2>&1 &
server=$!
for t in $(seq 1 80); do grep -q 'entering server loop' "$LOG.server.log" 2>/dev/null && break; sleep 0.25; done
grep -q 'entering server loop' "$LOG.server.log" || { echo "FAIL: the server did not start"; cat "$LOG.server.out"; kill $server; exit 1; }
port=$(sed -n 's/.*listening on UDP port \([0-9]*\).*/\1/p' "$LOG.server.log" | head -1)

${PLAYGROUND_APP_PRELOAD:+env LD_PRELOAD=$PLAYGROUND_APP_PRELOAD} "$APP" --connect "127.0.0.1:$port" --frames 240 \
  --log-file "$LOG.client.log" >"$LOG.client.out" 2>&1
client=$?
kill -INT $server
wait $server; serverCode=$?

if grep -qE 'DISPLAY environment variable is missing|Failed to open display' "$LOG.client.log"; then
  echo "no usable display: skipped"; exit 77
fi
summary=$(grep 'network client summary' "$LOG.client.log" | sed 's/.*network client summary: //')
echo "client exit $client; server exit $serverCode; client: $summary"
[ "$client" -eq 0 ] || { echo "FAIL: the game exited $client"; tail -20 "$LOG.client.log"; exit 1; }
[ "$serverCode" -eq 0 ] || { echo "FAIL: the server exited $serverCode"; tail -20 "$LOG.server.log"; exit 1; }
grep -q 'client: welcomed' "$LOG.client.log" || { echo "FAIL: no welcome"; exit 1; }
echo "$summary" | grep -q '^playing, player entity 64' || { echo "FAIL: not playing at the end: $summary"; exit 1; }
echo "$summary" | grep -q '65 entities known' || { echo "FAIL: did not know every entity: $summary"; exit 1; }
grep -q 'clean exit' "$LOG.client.log" || { echo "FAIL: no clean exit"; exit 1; }
grep -q 'joined as player entity 64' "$LOG.server.log" || { echo "FAIL: the server did not see it join"; exit 1; }
grep -q 'player entity 64 (windowed player) despawned' "$LOG.server.log" || { echo "FAIL: the server did not see it leave"; exit 1; }
echo "PASS"
