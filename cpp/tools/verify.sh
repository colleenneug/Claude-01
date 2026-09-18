#!/usr/bin/env bash
# Headless regression suite. Runs the real binary under Xvfb + llvmpipe with a
# fixed timestep (so results don't depend on host load), reads back the state
# each run logged, and checks it.
#
#   tools/verify.sh [build-dir]
set -u
BUILD="${1:-$(cd "$(dirname "$0")/../build" && pwd)}"
BIN="$BUILD/erebus_native"
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT
PASS=0; FAIL=0

run() {  # run <state-file> <env assignments...> [-- <binary args>]
  local log="$1"; shift
  # A save file per check. Sharing one made the checks order-dependent: a
  # later run inherited whatever chits and gear the earlier ones had spent,
  # so the same assertion passed or failed depending on what ran before it.
  local save="$OUT/save-$(basename "$log" .json).txt"
  local env_args=() bin_args=()
  while [ $# -gt 0 ]; do
    if [ "$1" = "--" ]; then shift; bin_args=("$@"); break; fi
    env_args+=("$1"); shift
  done
  xvfb-run -a env LIBGL_ALWAYS_SOFTWARE=1 EREBUS_FIXED_DT=0.016 \
    EREBUS_SAVE_PATH="$save" EREBUS_LOG_STATE="$log" "${env_args[@]}" \
    "$BIN" "${bin_args[@]}" >/dev/null 2>&1
}

check() {  # check <name> <state-file> <python expression over `s`>
  local name="$1" log="$2" expr="$3"
  if python3 -c "
import json, sys
s = json.load(open('$log'))
sys.exit(0 if ($expr) else 1)
" 2>/dev/null; then
    echo "  PASS $name"; PASS=$((PASS+1))
  else
    echo "  FAIL $name  ($(cat "$log" 2>/dev/null || echo 'no state written'))"
    FAIL=$((FAIL+1))
  fi
}

# Flying to a world and landing on it drops you into that world's mission.
# Keep the frame budget just past the landing: run it long enough and the
# mission itself ends (the scripted pilot doesn't shoot back) and drops you
# out to space again, which reads as a failure to land.
for world in glacius karrash; do
  run "$OUT/$world.json" EREBUS_SLOT=1 EREBUS_SPACE_AUTOPILOT=$world \
      EREBUS_FORCE_FORWARD=1 EREBUS_FORCE_ENGAGE=1 EREBUS_MAX_FRAMES=600
  check "land on $world" "$OUT/$world.json" "s['appState'] == 'mission'"
done

# The Cradle's dock prompt has to be reachable *after* leaving it: the ship is
# held at the station's standoff distance, so a prompt range smaller than that
# standoff makes docking impossible — which is exactly what it used to be.
run "$OUT/dock.json" EREBUS_SLOT=1 EREBUS_SPACE_AUTOPILOT=cradle \
    EREBUS_FORCE_FORWARD=1 EREBUS_MAX_FRAMES=1400
check "dock prompt reachable at the Cradle" "$OUT/dock.json" \
      "s['appState'] == 'space' and s['nearest'] == 'The Cradle' and s['inRange']"

# An existing mission still plays through end to end.
run "$OUT/patrol.json" EREBUS_SKIP_HUB=1 EREBUS_FORCE_FIRE=1 \
    EREBUS_DEBUG_AUTOAIM=1 EREBUS_MAX_FRAMES=5200 -- --mission patrol_dust_shelf
check "patrol mission completes" "$OUT/patrol.json" \
      "s['appState'] == 'mission' and s['missionState'] == 'complete'"

# The campaign's first sector plays end to end, pays its reward once, and
# runs its comms thread.
run "$OUT/breach.json" EREBUS_SKIP_HUB=1 EREBUS_FORCE_FIRE=1 \
    EREBUS_DEBUG_AUTOAIM=1 EREBUS_MAX_FRAMES=3000 -- --mission breach
check "campaign sector 1 completes" "$OUT/breach.json" \
      "s['missionState'] == 'complete' and s['chits'] > 140"

# The route opens one sector at a time. From a fresh profile every campaign
# mission but the first is locked, so cycling the hub's list has to step over
# all of them and land on side content — never on sector 2.
run "$OUT/lock.json" EREBUS_SLOT=1 EREBUS_SKIP_SPACE=1 EREBUS_HUB_SCRIPT=mission \
    EREBUS_MAX_FRAMES=40
check "route stays locked ahead of your progress" "$OUT/lock.json" \
      "s['appState'] == 'hub' and s['selectedMission'] != 'spine'"

# Space renders without blowing up at either end of the quality ladder. The
# proof is that the run got where it was flying: the tier switches shadow
# cascades, bloom and DoF on and off, and a tier that fails to build its
# targets takes the whole frame down rather than degrading.
for tier in 0 3; do
  run "$OUT/tier$tier.json" EREBUS_SLOT=1 EREBUS_QUALITY_TIER=$tier \
      EREBUS_SPACE_AUTOPILOT=glacius EREBUS_FORCE_FORWARD=1 EREBUS_MAX_FRAMES=400
  check "space tier $tier clean" "$OUT/tier$tier.json" \
        "s['appState'] == 'space' and s['nearest'].startswith('Glacius') and s['frame'] == 400"
done

echo
echo "PASS=$PASS FAIL=$FAIL"
[ "$FAIL" -eq 0 ]
