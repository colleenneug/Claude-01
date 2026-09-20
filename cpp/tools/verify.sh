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
  # Every check starts from a fresh save, which now means a record with no
  # doctrine — and a record with no doctrine stops on the creation screen and
  # waits, which no scripted run can answer. EREBUS_CLASS settles it. A check
  # that wants a different doctrine just passes its own, later on the command
  # line, which wins.
  xvfb-run -a env LIBGL_ALWAYS_SOFTWARE=1 EREBUS_FIXED_DT=0.016 EREBUS_CLASS=wraith \
    EREBUS_SKIP_TUTORIAL=1 \
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

# A doctrine is the body it gives you, and the three are not
# interchangeable: a Bulwark walks into the same fight with thirty more
# points of integrity than an Oracle and takes 22% off everything that
# reaches it. These two runs assert that the doctrine actually reached the
# player, on a record that has not been anywhere near an armoury.
#
# They deliberately do *not* assert the doctrine's weapon any more. A new
# record owns a service sidearm and nothing else — that weapon is on the
# armoury bench in Block D — so what is in your hands here is the pistol,
# whichever doctrine you picked. The claim that the bench hands a Bulwark a
# shotgun is proved where it is now true: "you start unarmed and end up
# issued", further down, which walks the block and comes out with a MAUL-12.
run "$OUT/bulwark.json" EREBUS_SKIP_HUB=1 EREBUS_CLASS=bulwark EREBUS_FORCE_FIRE=1 \
    EREBUS_DEBUG_AUTOAIM=1 EREBUS_MAX_FRAMES=600 -- --mission breach
check "a bulwark takes the field as a bulwark" "$OUT/bulwark.json" \
      "s['class'] == 'bulwark' and abs(s['maxHp'] - 124) < 0.01 and s['magSize'] == 12"

run "$OUT/oracle.json" EREBUS_SKIP_HUB=1 EREBUS_CLASS=oracle EREBUS_MAX_FRAMES=60 \
    -- --mission breach
check "an oracle takes the field as an oracle" "$OUT/oracle.json" \
      "s['class'] == 'oracle' and abs(s['maxHp'] - 94) < 0.01 and s['magSize'] == 12"

# A record with no doctrine has not been created yet and must stop and ask,
# however it was reached — otherwise a campaign starts with no weapon, no
# ability and no perk.
run "$OUT/create.json" EREBUS_MAX_FRAMES=40 EREBUS_CLASS=
check "a record with no doctrine stops to ask" "$OUT/create.json" \
      "s['appState'] == 'create'"

# Docking walks you into the Cradle rather than opening a menu, and the ship
# starts parked at it, so the scripted engage lands you inside on frame one.
run "$OUT/dockin.json" EREBUS_SPACE_AUTOPILOT=cradle EREBUS_FORCE_ENGAGE=1 \
    EREBUS_MAX_FRAMES=60
check "docking walks you into the Cradle" "$OUT/dockin.json" \
      "s['appState'] == 'station'"

# The concourse is walkable end to end. Built with a solid bow wall the
# arrivals tube is sealed off and you stop dead twelve metres short of the
# room, which is exactly what happened the first time.
run "$OUT/spine.json" EREBUS_SPACE_AUTOPILOT=cradle EREBUS_FORCE_ENGAGE=1 \
    EREBUS_FORCE_FORWARD=1 EREBUS_MAX_FRAMES=900
check "the spine is walkable end to end" "$OUT/spine.json" \
      "s['appState'] == 'station' and s['pos'][2] > 10"

# ...and the stairs are stairs. This is the whole multi-deck collision model
# in one check: a step is low enough to be support rather than a wall, and
# the well cut in the deck above means your head does not hit the slab two
# thirds of the way up.
run "$OUT/stairs.json" EREBUS_SPACE_AUTOPILOT=cradle EREBUS_FORCE_ENGAGE=1 \
    EREBUS_FORCE_FORWARD=1 EREBUS_STATION_AT=-19,0,-16 EREBUS_STATION_YAW=90 \
    EREBUS_MAX_FRAMES=520
check "the stairs climb to deck B" "$OUT/stairs.json" \
      "s['appState'] == 'station' and s['pos'][1] > 6.5"

# The posts are people now, not kiosks. Walking up to Kaur and hearing her
# out opens the route; Voss opens the armoury. The scripted key is pulsed, so
# this also covers the conversation advancing a line at a time.
run "$OUT/kaur.json" EREBUS_SPACE_AUTOPILOT=cradle EREBUS_FORCE_ENGAGE=1 \
    EREBUS_STATION_AT=0,0,31 EREBUS_STATION_YAW=90 EREBUS_MAX_FRAMES=160
check "the flight officer opens the route" "$OUT/kaur.json" \
      "s['appState'] == 'hub'"

run "$OUT/voss.json" EREBUS_SPACE_AUTOPILOT=cradle EREBUS_FORCE_ENGAGE=1 \
    EREBUS_STATION_AT=-34.5,7,2 EREBUS_STATION_YAW=180 EREBUS_MAX_FRAMES=160
check "the quartermaster opens the armoury" "$OUT/voss.json" \
      "s['appState'] == 'hub'"

# ...and the ones with nothing to sell are a conversation and nothing else.
# The Rook has no shop, so talking all the way through leaves you standing in
# the concourse rather than opening a screen that would do nothing.
run "$OUT/rook.json" EREBUS_SPACE_AUTOPILOT=cradle EREBUS_FORCE_ENGAGE=1 \
    EREBUS_STATION_AT=-8,0,-13 EREBUS_STATION_YAW=-90 EREBUS_MAX_FRAMES=160
check "a post with no shop is only a conversation" "$OUT/rook.json" \
      "s['appState'] == 'station'"

# A brand-new record starts on Earth, at the ground site, and is walked
# through its kit. A record that has cleared anything at all goes straight up.
run "$OUT/newrec.json" EREBUS_MAX_FRAMES=40 EREBUS_SKIP_TUTORIAL=
check "a new record starts on the ground site" "$OUT/newrec.json" \
      "s['appState'] == 'mission'"

# ...and the block plays end to end: you wake up with nothing, the sidearm is
# in the footlocker, the issued weapon is on the armoury bench, and the route
# runs bunks -> corridor -> armoury -> muster hall -> the pad. The block is
# laid out along +Z on purpose, so holding W walks the whole thing: this is
# the check that every doorway, lintel, bench and table leaves a lane
# through, which is exactly what kept going wrong while building it.
run "$OUT/tutorial.json" EREBUS_SKIP_HUB=1 EREBUS_CLASS=bulwark EREBUS_FORCE_FORWARD=1 \
    EREBUS_FORCE_FIRE=1 EREBUS_DEBUG_AUTOAIM=1 EREBUS_MAX_FRAMES=5600 \
    -- --mission tutorial_earth
check "the block plays end to end" "$OUT/tutorial.json" \
      "s['missionState'] == 'complete' and s['chits'] > 100"

# You start with empty hands and end holding what your doctrine was issued —
# which means the sidearm was found in the bunks and swapped at the bench.
check "you start unarmed and end up issued" "$OUT/tutorial.json" \
      "s['weapon'] == 'MAUL-12' and s['magSize'] == 6"

# Nothing is armed before you are: forty frames in you are still empty-handed
# and the opening cutscene is running.
run "$OUT/wake.json" EREBUS_SKIP_HUB=1 EREBUS_CLASS=bulwark EREBUS_MAX_FRAMES=40 \
    -- --mission tutorial_earth
check "you wake up with nothing" "$OUT/wake.json" \
      "s['weapon'] == '' and s['ammoInMag'] == 0 and s['reserveAmmo'] == 0"

# ...and the opening cutscene is still on screen, which is the whole of the
# bug this check exists for: the skip used to be "any key" with its edge
# seeded false, so the Enter still held down from the doctrine screen read as
# a fresh press and skipped the scene on frame one, every single time. Nobody
# ever saw a cutscene. Forty frames is 0.64 seconds into a 9.6-second scene.
check "a cutscene is not skipped before you see it" "$OUT/wake.json" \
      "s['inCutscene'] is True"

# A fresh record owns a sidearm and nothing else — the doctrine's weapon is
# found on the armoury bench, not issued at a desk — so dropping straight
# into any other mission puts a pistol in your hands, not a MAUL-12.
run "$OUT/pistol.json" EREBUS_SKIP_HUB=1 EREBUS_CLASS=bulwark EREBUS_MAX_FRAMES=40 \
    -- --mission patrol_dust_shelf
check "a new record starts with the pistol and nothing else" "$OUT/pistol.json" \
      "s['weapon'] == 'Service Sidearm' and s['magSize'] == 12"

# The trauma harness: walk into the block without firing a shot and the
# drones put you down. That is not the end of the mission three times over —
# it is three seconds on the floor and then back up on the spot, which is
# what this proves: still in progress, still alive, harness spent.
run "$OUT/harness.json" EREBUS_SKIP_HUB=1 EREBUS_CLASS=bulwark EREBUS_FORCE_FORWARD=1 \
    EREBUS_MAX_FRAMES=2200 -- --mission tutorial_earth
check "the harness stands you back up where you fell" "$OUT/harness.json" \
      "s['harness'] < 3 and s['playerHp'] > 0 and s['missionState'] == 'in_progress'"

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
