#!/bin/sh
# =============================================================================
#  End-to-end co-op cutscene test, with no display: a dedicated server and
#  two headless bots, as separate processes talking UDP.
#
#  The server (`--cutscene-after 0.5`) starts the co-op cutscene
#  (assets/cutscenes/coop.json, 8 s) half a second after the first player
#  joins; both bots (`--skip-cutscenes`) hold skip as soon as it plays. Checks:
#    - both bots were told the same start tick and the same end tick;
#    - it ended early, skipped by everyone (8 s would outlast the bots);
#    - the server saw every player hold skip;
#    - all three exited cleanly, and the server touched no GPU.
#
#  Usage: coop_cutscene.sh <playground_app> <log prefix>
# =============================================================================
APP="$1"; LOG="$2"
unset DISPLAY WAYLAND_DISPLAY
rm -f "$LOG".*.log

"$APP" --server --port 0 --cutscene-after 0.5 --log-file "$LOG.server.log" >"$LOG.server.out" 2>&1 &
server=$!
for t in $(seq 1 80); do grep -q 'entering server loop' "$LOG.server.log" 2>/dev/null && break; sleep 0.25; done
grep -q 'entering server loop' "$LOG.server.log" || { echo "FAIL: the server did not start"; kill $server; exit 1; }
port=$(sed -n 's/.*listening on UDP port \([0-9]*\).*/\1/p' "$LOG.server.log" | head -1)

"$APP" --connect "127.0.0.1:$port" --headless --frames 600 --skip-cutscenes --log-file "$LOG.bot1.log" >"$LOG.bot1.out" 2>&1 &
bot1=$!
"$APP" --connect "127.0.0.1:$port" --headless --frames 600 --skip-cutscenes --log-file "$LOG.bot2.log" >"$LOG.bot2.out" 2>&1 &
bot2=$!
wait $bot1; code1=$?
wait $bot2; code2=$?
kill -INT $server
wait $server; serverCode=$?

one=$(grep 'client cutscene:' "$LOG.bot1.log" | sed 's/.*client cutscene: //')
two=$(grep 'client cutscene:' "$LOG.bot2.log" | sed 's/.*client cutscene: //')
echo "bot 1: $one"
echo "bot 2: $two"
[ "$code1" -eq 0 ] && [ "$code2" -eq 0 ] || { echo "FAIL: bots exited $code1 and $code2"; tail -5 "$LOG.bot1.log" "$LOG.bot2.log"; exit 1; }
[ "$serverCode" -eq 0 ] || { echo "FAIL: the server exited $serverCode"; exit 1; }
[ -n "$one" ] || { echo "FAIL: bot 1 never heard of a cutscene"; exit 1; }
[ "$one" = "$two" ] || { echo "FAIL: the bots disagree about the cutscene's ticks"; exit 1; }
echo "$one" | grep -q 'skipped by everyone' || { echo "FAIL: it was not skipped"; exit 1; }
grep -q 'cutscene 1 starts for' "$LOG.server.log" || { echo "FAIL: the server did not start it"; exit 1; }
grep -q 'all 2 player(s) held skip' "$LOG.server.log" || { echo "FAIL: the server did not see both skip"; exit 1; }
if grep -qiE 'GLFW|Vulkan|\[gpu' "$LOG.server.log"; then echo "FAIL: the server touched GLFW or Vulkan"; exit 1; fi
echo "PASS"
