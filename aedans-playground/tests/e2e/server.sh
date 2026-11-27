#!/bin/sh
# =============================================================================
#  End-to-end dedicated server test, with NO display at all.
#
#  1. starts `playground_app --server --port 0` (any free port; read back
#     from its log) with DISPLAY and WAYLAND_DISPLAY removed;
#  2. runs the headless client (`--connect 127.0.0.1:PORT --frames 240`:
#     2 s) against it;
#  3. sends the server Ctrl+C (SIGINT) and waits for it;
#  then checks:
#    - both exited 0, and the server stopped because it was interrupted;
#    - the client was welcomed and received at least 20 snapshots (20 a
#      second), 64 entities each, none malformed;
#    - the server saw the client connect and say goodbye;
#    - the server never started GLFW or Vulkan.
#
#  Usage: server.sh <playground_app> <log prefix>
# =============================================================================
APP="$1"; LOG="$2"
unset DISPLAY WAYLAND_DISPLAY
rm -f "$LOG.server.log" "$LOG.client.log"

"$APP" --server --port 0 --log-file "$LOG.server.log" >"$LOG.server.out" 2>&1 &
server=$!
for t in $(seq 1 80); do grep -q 'entering server loop' "$LOG.server.log" 2>/dev/null && break; sleep 0.25; done
grep -q 'entering server loop' "$LOG.server.log" || { echo "FAIL: the server did not start"; cat "$LOG.server.out"; kill $server; exit 1; }
port=$(sed -n 's/.*listening on UDP port \([0-9]*\).*/\1/p' "$LOG.server.log" | head -1)
echo "server listening on port $port"

"$APP" --connect "127.0.0.1:$port" --frames 240 --log-file "$LOG.client.log" >"$LOG.client.out" 2>&1
client=$?

kill -INT $server
wait $server; serverCode=$?

summary=$(grep 'client summary' "$LOG.client.log" | sed 's/.*client summary: //')
echo "client exit $client; server exit $serverCode; client: $summary"
snapshots=$(echo "$summary" | sed -n 's/.*, \([0-9]*\) snapshots.*/\1/p')

[ "$client" -eq 0 ] || { echo "FAIL: the client exited $client"; tail -20 "$LOG.client.log"; exit 1; }
[ "$serverCode" -eq 0 ] || { echo "FAIL: the server exited $serverCode"; tail -20 "$LOG.server.log"; exit 1; }
echo "$summary" | grep -q 'welcome received' || { echo "FAIL: no welcome"; exit 1; }
[ -n "$snapshots" ] && [ "$snapshots" -ge 20 ] || { echo "FAIL: $snapshots snapshots, expected at least 20"; exit 1; }
echo "$summary" | grep -q '64 entities per snapshot, 0 malformed packets' || { echo "FAIL: bad snapshots: $summary"; exit 1; }
grep -q 'client 1 connected' "$LOG.server.log" || { echo "FAIL: the server did not see the client connect"; exit 1; }
grep -q 'client 1 (.*) disconnected' "$LOG.server.log" || { echo "FAIL: the server did not see the client leave"; exit 1; }
grep -q 'server stopping: interrupted' "$LOG.server.log" || { echo "FAIL: Ctrl+C did not stop the server"; exit 1; }
if grep -qiE 'GLFW|Vulkan|\[gpu' "$LOG.server.log"; then echo "FAIL: the server touched GLFW or Vulkan"; exit 1; fi
echo "PASS"
