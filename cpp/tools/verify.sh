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

checkfile() {  # checkfile <name> <file> <grep -E pattern>
  local name="$1" file="$2" pattern="$3"
  if [ -f "$file" ] && grep -Eq "$pattern" "$file"; then
    echo "  PASS $name"; PASS=$((PASS+1))
  else
    echo "  FAIL $name  (no '$pattern' in $(basename "$file"))"
    FAIL=$((FAIL+1))
  fi
}

checkabsent() {  # checkabsent <name> <file> <grep -E pattern>
  local name="$1" file="$2" pattern="$3"
  if [ -f "$file" ] && ! grep -Eq "$pattern" "$file"; then
    echo "  PASS $name"; PASS=$((PASS+1))
  else
    echo "  FAIL $name  ('$pattern' found in $(basename "$file"))"
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

# The campaign's first sector is an expedition, and the route through it
# works: the scripted player clears the first area, walks the corridor
# (DEBUG_AUTOAIM walks the route when there is nothing to shoot) and gets
# into the next fight. Run at a coarser timestep, because a map this size
# takes a while to cross. Playing one to the end with a scripted pistol is
# not the claim: a player who stands still to shoot loses the long fights.
run "$OUT/breach.json" EREBUS_SKIP_HUB=1 EREBUS_FORCE_FIRE=1 EREBUS_FIXED_DT=0.033 \
    EREBUS_DEBUG_AUTOAIM=1 EREBUS_MAX_FRAMES=1500 -- --mission breach
check "a campaign sector is an expedition, and its route advances" "$OUT/breach.json" \
      "s['expedition'] is True and s['objectives'] >= 5 and (s['objective'] >= 2 or s['missionState'] == 'complete')"

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

# ...and everyone else has a desk of their own. The Rook's is appraisal: a
# fresh record owns nothing worth buying back, and the screen says so rather
# than offering to buy your service sidearm.
run "$OUT/rook.json" EREBUS_SPACE_AUTOPILOT=cradle EREBUS_FORCE_ENGAGE=1 \
    EREBUS_STATION_AT=-8,0,-13 EREBUS_STATION_YAW=-90 EREBUS_MAX_FRAMES=160
check "the appraiser's desk opens after the conversation" "$OUT/rook.json" \
      "s['appState'] == 'post' and s['host'] == 'rook' and s['tab'] == 'service' and s['chits'] == 100"

# Each counter carries its own stock. Voss's rack is not Ondo's, and Kaur's
# route is the ark and nothing else.
check "the orbital quartermaster carries his own rack" "$OUT/voss.json" \
      "s['host'] == 'VOSS' and 'anvil' in s['weapons'] and 'kestrel' not in s['weapons']"
check "the flight officer flies the ark and only the ark" "$OUT/kaur.json" \
      "s['host'] == 'KAUR' and 'breach' in s['missions'] and 'earth_range' not in s['missions'] \
       and 'patrol_dust_shelf' not in s['missions']"

# THE ROUTE IS FLOWN IN ORDER. A fresh record standing at Kaur's counter can
# see HARD DOCK and cannot launch it: the Earth programme comes first.
run "$OUT/kaurlock.json" EREBUS_SPACE_AUTOPILOT=cradle EREBUS_FORCE_ENGAGE=1 \
    EREBUS_STATION_AT=0,0,31 EREBUS_STATION_YAW=90 EREBUS_HUB_SCRIPT=launch,launch,launch \
    EREBUS_MAX_FRAMES=170
check "a locked sector cannot be launched" "$OUT/kaurlock.json" \
      "s['appState'] == 'hub' and s['selectedMission'] == 'breach'"

# BOUNTIES. Taken at the desk of whoever posted them...
run "$OUT/odil.json" EREBUS_SPACE_AUTOPILOT=cradle EREBUS_FORCE_ENGAGE=1 \
    EREBUS_STATION_AT=34.5,7,2 EREBUS_STATION_YAW=0 EREBUS_POST_SCRIPT=right,use \
    EREBUS_MAX_FRAMES=200
checkfile "a bounty is taken at its giver's desk" "$OUT/save-odil.txt" "^bounty odil_use_your_kit 0$"

# ...counted across a mission...
cat > "$OUT/save-tally.txt" <<'SEED'
name = Operative
chits = 100
xp = 600
rank_paid = 1
class = bulwark
equipped_weapon = maul_12
equipped_sidearm = sidearm
equipped_armor = patrol_vest
equipped_cosmetic = default
owned_weapon sidearm
owned_weapon maul_12
owned_armor patrol_vest
owned_cosmetic default
completed tutorial_earth
bounty teal_busy_week 0
bounty voss_parts_run 0
SEED
run "$OUT/tally.json" EREBUS_SKIP_HUB=1 EREBUS_CLASS=bulwark EREBUS_FORCE_FIRE=1 \
    EREBUS_FORCE_ENGAGE=1 EREBUS_DEBUG_AUTOAIM=1 EREBUS_MAX_FRAMES=900 -- --mission debug_single
checkfile "a mission's kills count towards a bounty" "$OUT/save-tally.txt" "^bounty teal_busy_week [1-9][0-9]*$"
checkfile "...and only the kills it asks for" "$OUT/save-tally.txt" "^bounty voss_parts_run 0$"

# ...and paid by the person who posted it. A ready bounty opens their desk on
# the board instead of their service, and the scripted E turns it in.
cat > "$OUT/save-turnin.txt" <<'SEED'
name = Operative
chits = 100
xp = 600
rank_paid = 1
class = wraith
equipped_sidearm = sidearm
equipped_armor = patrol_vest
equipped_cosmetic = default
owned_weapon sidearm
owned_armor patrol_vest
owned_cosmetic default
bounty odil_use_your_kit 15
SEED
run "$OUT/turnin.json" EREBUS_SPACE_AUTOPILOT=cradle EREBUS_FORCE_ENGAGE=1 \
    EREBUS_STATION_AT=34.5,7,2 EREBUS_STATION_YAW=0 EREBUS_MAX_FRAMES=200
check "a finished bounty is paid at its giver's desk" "$OUT/turnin.json" \
      "s['appState'] == 'post' and s['tab'] == 'bounties' and s['chits'] == 250 and s['xp'] == 820"
checkfile "...once" "$OUT/save-turnin.txt" "^bounty_done odil_use_your_kit$"

# Their own thing. Odil sells muster training, bought off the record's chits.
cat > "$OUT/save-train.txt" <<'SEED'
name = Operative
chits = 1000
xp = 600
rank_paid = 1
class = wraith
equipped_sidearm = sidearm
equipped_armor = patrol_vest
equipped_cosmetic = default
owned_weapon sidearm
owned_armor patrol_vest
owned_cosmetic default
SEED
run "$OUT/train.json" EREBUS_SPACE_AUTOPILOT=cradle EREBUS_FORCE_ENGAGE=1 \
    EREBUS_STATION_AT=34.5,7,2 EREBUS_STATION_YAW=0 EREBUS_POST_SCRIPT=use \
    EREBUS_MAX_FRAMES=200
checkfile "an upgrade is bought at its giver's desk" "$OUT/save-train.txt" "^upgrade odil_ability_1$"
checkfile "...for its price" "$OUT/save-train.txt" "^chits = 600$"

# ...and an upgrade does what it says in the field: CONDITIONING adds health.
cat > "$OUT/save-hp.txt" <<'SEED'
name = Operative
chits = 100
class = bulwark
equipped_sidearm = sidearm
equipped_armor = patrol_vest
equipped_cosmetic = default
owned_weapon sidearm
owned_armor patrol_vest
owned_cosmetic default
upgrade odil_hp_1
SEED
cp "$OUT/save-hp.txt" "$OUT/save-nohp.txt"
sed -i '/^upgrade/d' "$OUT/save-nohp.txt"
run "$OUT/hp.json" EREBUS_SKIP_HUB=1 EREBUS_CLASS=bulwark EREBUS_MAX_FRAMES=30 -- --mission patrol_dust_shelf
run "$OUT/nohp.json" EREBUS_SKIP_HUB=1 EREBUS_CLASS=bulwark EREBUS_MAX_FRAMES=30 -- --mission patrol_dust_shelf
if python3 -c "
import json; a=json.load(open('$OUT/hp.json')); b=json.load(open('$OUT/nohp.json'))
raise SystemExit(0 if abs(a['maxHp'] - b['maxHp'] - 15) < 0.01 else 1)"; then
  echo "  PASS an upgrade applies in the field"; PASS=$((PASS+1))
else
  echo "  FAIL an upgrade applies in the field"; FAIL=$((FAIL+1))
fi

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

# Rank. Every enemy archetype has carried an `xp` figure in its content file
# since they were written and nothing read it; now clearing the block pays
# what its hostiles are worth plus the mission's own clearance bonus, and the
# ladder resolves that into a rung. Asserted on the run that already walked
# the whole building rather than on one of its own.
check "the block pays experience and a rank" "$OUT/tutorial.json" \
      "s['xp'] >= 300 and s['careerXp'] >= 300 and s['rank'] in ('PROBATIONARY STRIDER', 'STRIDER THIRD CLASS')"

# ...and a promotion pays its stipend, once. Seeded just under the second
# rung — run() points at a save file per check and does not delete it, so
# writing one first is how a check starts from a record with a history
# instead of from a fresh one.
cat > "$OUT/save-promo.txt" <<'SEED'
name = Operative
chits = 100
xp = 380
rank_paid = 0
class = bulwark
equipped_weapon = sidearm
equipped_armor = patrol_vest
equipped_cosmetic = default
owned_weapon sidearm
owned_armor patrol_vest
owned_cosmetic default
SEED
run "$OUT/promo.json" EREBUS_SKIP_HUB=1 EREBUS_CLASS=bulwark EREBUS_FORCE_FIRE=1 \
    EREBUS_FORCE_ENGAGE=1 EREBUS_DEBUG_AUTOAIM=1 EREBUS_MAX_FRAMES=900 -- --mission debug_single
checkfile "a promotion is recorded once" "$OUT/save-promo.txt" "^rank_paid = 1$"
checkfile "a promotion pays its stipend" "$OUT/save-promo.txt" "^chits = 2[0-9][0-9]$"

# The Strider programme. A record that has cleared Block D but not the rest of
# Earth is on the ground: it resumes at Kourou, not in a ship it has not been
# issued and not in orbit.
cat > "$OUT/save-ground.txt" <<'SEED'
name = Operative
chits = 300
xp = 480
rank_paid = 1
class = bulwark
equipped_weapon = maul_12
equipped_sidearm = sidearm
equipped_armor = patrol_vest
equipped_cosmetic = default
owned_weapon sidearm
owned_weapon maul_12
owned_armor patrol_vest
owned_cosmetic default
completed tutorial_earth
SEED
run "$OUT/ground.json" EREBUS_MAX_FRAMES=60 EREBUS_SKIP_TUTORIAL=
check "an unrated record resumes on the ground at Kourou" "$OUT/ground.json" \
      "s['appState'] == 'station' and s['layout'] == 'kourou'"

# Two holsters, each with its own magazine. The swap is proved by running the
# same fixture twice and only swapping in one of them: a check that only
# looked at the end state could not tell a swap from a different loadout.
cp "$OUT/save-ground.txt" "$OUT/save-holster.txt"
run "$OUT/holster.json" EREBUS_SKIP_HUB=1 EREBUS_CLASS=bulwark EREBUS_MAX_FRAMES=120 \
    -- --mission earth_range
check "a primary and a sidearm are both carried" "$OUT/holster.json" \
      "s['slot'] == 0 and s['primary'] == 'MAUL-12' and s['sidearm'] == 'Service Sidearm' and s['magSize'] == 6"

cp "$OUT/save-ground.txt" "$OUT/save-swap.txt"
run "$OUT/swap.json" EREBUS_SKIP_HUB=1 EREBUS_CLASS=bulwark EREBUS_SWAP_AT=40 \
    EREBUS_MAX_FRAMES=120 -- --mission earth_range
check "switching holsters brings up the other weapon" "$OUT/swap.json" \
      "s['slot'] == 1 and s['weapon'] == 'Service Sidearm' and s['magSize'] == 12 and s['primary'] == 'MAUL-12'"

# ...and so is an Earth qualification: three areas in, the route is still
# carrying the run forward.
run "$OUT/range.json" EREBUS_SKIP_HUB=1 EREBUS_CLASS=bulwark EREBUS_FORCE_FIRE=1 \
    EREBUS_FIXED_DT=0.033 EREBUS_DEBUG_AUTOAIM=1 EREBUS_MAX_FRAMES=4500 -- --mission earth_range
check "the range is an expedition, and the run gets three areas in" "$OUT/range.json" \
      "s['expedition'] is True and s['objective'] >= 4"

# Side contracts stay single arenas.
check "a side contract is still an arena" "$OUT/patrol.json" "s['expedition'] is False"

# UPGRADING AN EXISTING SAVE. A record that was already flying the ark before
# the Earth programme was written must not be sent back to school for it: it
# keeps its ship, and its route stays open. Without the grandfather clause in
# trackCleared this save woke up on the ground at Kourou with the whole ark
# locked, which is the worst thing an update can do to somebody's record.
cat > "$OUT/save-legacy.txt" <<'SEED'
name = Operative
chits = 900
class = bulwark
equipped_weapon = maul_12
equipped_armor = patrol_vest
equipped_cosmetic = default
owned_weapon maul_12
owned_armor patrol_vest
owned_cosmetic default
completed breach
completed spine
completed junction
SEED
run "$OUT/legacy.json" EREBUS_MAX_FRAMES=40 EREBUS_SKIP_TUTORIAL=
# ...but it does not keep a ship it was never issued. Ships come at the end
# of the campaign now, so a record halfway down the ark resumes on foot at
# the Cradle, with Kaur's route still open where it left off.
check "a record halfway down the ark is on foot at the Cradle" "$OUT/legacy.json" \
      "s['appState'] == 'station' and s['layout'] == 'cradle'"

# ...and its route list opens where it actually is, not on the tutorial it
# never played.
cp "$OUT/save-legacy.txt" "$OUT/save-legacyhub.txt"
run "$OUT/legacyhub.json" EREBUS_SKIP_SPACE=1 EREBUS_MAX_FRAMES=30 EREBUS_SKIP_TUTORIAL=
check "an existing record opens its route where it left off" "$OUT/legacyhub.json" \
      "s['selectedMission'] == 'hold'"

# THE KIT SCREEN. G opens it over a mission, and the whole point is that it
# writes to the record from there: a menu you can only reach from a counter is
# the hub, which already existed.
cat > "$OUT/save-kit.txt" <<'SEED'
name = Operative
chits = 520
xp = 3000
rank_paid = 3
class = bulwark
equipped_weapon = maul_12
equipped_sidearm = sidearm
equipped_armor = patrol_vest
equipped_cosmetic = default
owned_weapon sidearm
owned_weapon maul_12
owned_armor patrol_vest
owned_cosmetic default
completed tutorial_earth
SEED
run "$OUT/kit.json" EREBUS_SKIP_HUB=1 EREBUS_CLASS=bulwark EREBUS_KIT_AT=40 \
    EREBUS_MAX_FRAMES=90 -- --mission earth_hangar
check "the kit screen opens over a running mission" "$OUT/kit.json" \
      "s['appState'] == 'kit' and s['primary'] == 'maul_12'"

# ...and buying from it charges the chits, records the weapon as owned, and
# the mission underneath is carrying it before the screen has even closed a
# second time. The primary keeps its magazine, which is the half of
# Game::applyLoadout worth checking: changing your sidearm must not reload
# the gun in your hands.
cp "$OUT/save-kit.txt" "$OUT/save-kitbuy.txt"
run "$OUT/kitbuy.json" EREBUS_SKIP_HUB=1 EREBUS_CLASS=bulwark EREBUS_KIT_AT=40 \
    EREBUS_KIT_SCRIPT=right,up,equip,close EREBUS_MAX_FRAMES=200 -- --mission earth_hangar
check "buying from the kit screen equips it into the live mission" "$OUT/kitbuy.json" \
      "s['appState'] == 'mission' and s['sidearm'] == 'KESTREL' and s['chits'] == 350 \
       and s['primary'] == 'MAUL-12' and s['ammoInMag'] == 6 and s['reserveAmmo'] == 48"
checkfile "the kit screen writes the purchase to the record" "$OUT/save-kitbuy.txt" \
      "^owned_weapon kestrel$"

# The gates hold from the kit screen too. A candidate cannot buy an ANVIL off
# it however many chits they are holding: the row is shown, and refused.
cat > "$OUT/save-kitrank.txt" <<'SEED'
name = Operative
chits = 5000
xp = 0
rank_paid = 0
class = bulwark
equipped_weapon = maul_12
equipped_sidearm = sidearm
equipped_armor = patrol_vest
equipped_cosmetic = default
owned_weapon sidearm
owned_weapon maul_12
owned_armor patrol_vest
owned_cosmetic default
completed tutorial_earth
SEED
run "$OUT/kitrank.json" EREBUS_SKIP_HUB=1 EREBUS_CLASS=bulwark EREBUS_KIT_AT=40 \
    EREBUS_KIT_SCRIPT=up,up,up,up,equip,close EREBUS_MAX_FRAMES=220 -- --mission earth_hangar
check "the kit screen will not sell above your rank" "$OUT/kitrank.json" \
      "s['chits'] == 5000 and s['primary'] == 'MAUL-12'"

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

# THE END OF THE CAMPAIGN. Until it is done, experience stops one point short
# of STRIDER SECOND CLASS however much a mission pays...
cat > "$OUT/save-cap.txt" <<'SEED'
name = Operative
chits = 100
xp = 4490
rank_paid = 4
class = bulwark
equipped_weapon = maul_12
equipped_sidearm = sidearm
equipped_armor = patrol_vest
equipped_cosmetic = default
owned_weapon sidearm
owned_weapon maul_12
owned_armor patrol_vest
owned_cosmetic default
completed tutorial_earth
SEED
run "$OUT/cap.json" EREBUS_SKIP_HUB=1 EREBUS_CLASS=bulwark EREBUS_FORCE_FIRE=1 \
    EREBUS_FORCE_ENGAGE=1 EREBUS_DEBUG_AUTOAIM=1 EREBUS_MAX_FRAMES=900 -- --mission debug_single
checkfile "experience holds below second class until the campaign is done" "$OUT/save-cap.txt" "^xp = 4499$"

# ...and finishing it, Block D to THE CONDUCTOR, makes the record a Strider
# Second Class and issues its first ship, which puts it in space.
{
  sed -n '1,/^owned_cosmetic/p' "$OUT/save-cap.txt" | sed 's/^xp = .*/xp = 3200/'
  for m in tutorial_earth earth_range earth_hangar earth_assembly earth_perimeter earth_launch \
           breach spine junction counter aft promenade sunset greenhouse medbay ward reactor \
           coolant choir deeper threshold conductor; do echo "completed $m"; done
} > "$OUT/save-done.txt"
# One sector short of the end, taken before the run below writes a ship into
# the finished record.
grep -v '^completed conductor$' "$OUT/save-done.txt" > "$OUT/save-almost.txt"
run "$OUT/done.json" EREBUS_MAX_FRAMES=40 EREBUS_SKIP_TUTORIAL=
check "the end of the campaign issues a ship" "$OUT/done.json" "s['appState'] == 'space'"
checkfile "...the KITE" "$OUT/save-done.txt" "^owned_ship kite$"
checkfile "...and second class" "$OUT/save-done.txt" "^xp = 4500$"

# A record one sector short of the end has no ship, and the airlock is sealed.
run "$OUT/almost.json" EREBUS_MAX_FRAMES=40 EREBUS_SKIP_TUTORIAL=
check "one sector short of the end is still on foot" "$OUT/almost.json" \
      "s['appState'] == 'station' and s['layout'] == 'cradle'"
checkabsent "...with no ship" "$OUT/save-almost.txt" "^owned_ship"

# More ships, from the shipwright. A KITE cannot reach Karrash...
sed -e 's/^xp = .*/xp = 4500/' -e 's/^chits = .*/chits = 1000/' "$OUT/save-done.txt" > "$OUT/save-kite.txt"
echo "owned_ship kite" >> "$OUT/save-kite.txt"
echo "equipped_ship = kite" >> "$OUT/save-kite.txt"
cp "$OUT/save-kite.txt" "$OUT/save-kitefar.txt"
run "$OUT/kitefar.json" EREBUS_SPACE_AUTOPILOT=karrash EREBUS_FORCE_FORWARD=1 \
    EREBUS_FORCE_ENGAGE=1 EREBUS_MAX_FRAMES=600 EREBUS_SKIP_TUTORIAL=
check "a KITE cannot make the drop on Karrash" "$OUT/kitefar.json" \
      "s['appState'] == 'space' and s['nearest'].startswith('Karrash') and s['inRange']"
cp "$OUT/save-kite.txt" "$OUT/save-kitenear.txt"
run "$OUT/kitenear.json" EREBUS_SPACE_AUTOPILOT=glacius EREBUS_FORCE_FORWARD=1 \
    EREBUS_FORCE_ENGAGE=1 EREBUS_MAX_FRAMES=600 EREBUS_SKIP_TUTORIAL=
check "...but can make it on Glacius" "$OUT/kitenear.json" "s['appState'] == 'mission'"

# ...and Mercer sells the next one up.
cp "$OUT/save-kite.txt" "$OUT/save-yard.txt"
run "$OUT/yard.json" EREBUS_SPACE_AUTOPILOT=cradle EREBUS_FORCE_ENGAGE=1 \
    EREBUS_STATION_AT=35.5,0,2 EREBUS_STATION_YAW=0 EREBUS_POST_SCRIPT=down,use \
    EREBUS_MAX_FRAMES=200 EREBUS_SKIP_TUTORIAL=
checkfile "the shipwright sells a second ship" "$OUT/save-yard.txt" "^owned_ship heron$"
checkfile "...and you fly it" "$OUT/save-yard.txt" "^equipped_ship = heron$"
checkfile "...for its price" "$OUT/save-yard.txt" "^chits = 100$"

# THE CAMPAIGN IS THE ONLY THING TO FLY UNTIL IT IS DONE. Shaw's board is
# shut to a record that has not finished it, and says why...
run "$OUT/shawshut.json" EREBUS_SPACE_AUTOPILOT=cradle EREBUS_FORCE_ENGAGE=1 \
    EREBUS_STATION_AT=9,0,-13 EREBUS_STATION_YAW=-90 EREBUS_MAX_FRAMES=200
check "the contract board is shut until the campaign is done" "$OUT/shawshut.json" \
      "s['appState'] == 'post' and s['host'] == 'shaw' and s['notice'] == 'AFTER THE CAMPAIGN'"

# ...and a side mission cannot even be picked: asking for one by name opens
# the hub on the route instead.
run "$OUT/sidelock.json" EREBUS_SKIP_SPACE=1 EREBUS_MAX_FRAMES=30 -- --mission patrol_dust_shelf
check "a side contract cannot be launched before the campaign" "$OUT/sidelock.json" \
      "s['appState'] == 'hub' and s['selectedMission'] != 'patrol_dust_shelf'"

# The arrow knows where you are going: the pad at Kourou, Kaur on the ark.
check "at Kourou the arrow points at the pad" "$OUT/ground.json" \
      "s['next'].startswith('NEXT: THE RANGE') and 'PAD' in s['next']"
check "on the ark it points at Kaur" "$OUT/legacy.json" \
      "s['next'].startswith('NEXT: THE HOLD') and 'KAUR' in s['next']"

# Once the campaign is done, Earth is a flight away: landing on it puts you on
# foot at Kourou.
cp "$OUT/save-kite.txt" "$OUT/save-earth.txt"
run "$OUT/earth.json" EREBUS_SPACE_AUTOPILOT=earth EREBUS_FORCE_FORWARD=1 \
    EREBUS_FORCE_ENGAGE=1 EREBUS_MAX_FRAMES=600 EREBUS_SKIP_TUTORIAL=
check "a ship can take you back down to Kourou" "$OUT/earth.json" \
      "s['appState'] == 'station' and s['layout'] == 'kourou'"

# LEAVING A MISSION. Escape, then Y: back on foot where you launched from,
# the mission not cleared and still there to fly.
cp "$OUT/save-ground.txt" "$OUT/save-leave.txt"
run "$OUT/leave.json" EREBUS_SKIP_HUB=1 EREBUS_CLASS=bulwark EREBUS_LEAVE_AT=400 \
    EREBUS_MAX_FRAMES=480 -- --mission earth_perimeter
check "a mission can be left part way through" "$OUT/leave.json" \
      "s['appState'] == 'station' and s['layout'] == 'kourou'"
checkabsent "...and is not cleared by it" "$OUT/save-leave.txt" "^completed earth_perimeter$"

# A HUNDRED WEAPONS, AND THE WAYS THEY COME. A first clear can hand one over...
checkfile "a first clear hands over a weapon" "$OUT/save-promo.txt" "^owned_weapon hal_wren$"
# ...so can a promotion...
checkfile "a promotion hands over a weapon" "$OUT/save-promo.txt" "^owned_weapon ves_finch$"
# ...a kill can drop one (a boss always does; the switch here makes every
# kill drop, since a scripted player standing still cannot win a boss fight),
# and whatever is on the floor when the mission clears comes with you...
cat > "$OUT/save-bossloot.txt" <<'SEED'
name = Operative
chits = 100
xp = 3000
rank_paid = 4
class = bulwark
equipped_weapon = maul_12
equipped_sidearm = sidearm
equipped_armor = patrol_vest
equipped_cosmetic = default
owned_weapon sidearm
owned_weapon maul_12
owned_armor patrol_vest
owned_cosmetic default
completed tutorial_earth
SEED
run "$OUT/bossloot.json" EREBUS_SKIP_HUB=1 EREBUS_CLASS=bulwark EREBUS_FORCE_FIRE=1 \
    EREBUS_DEBUG_AUTOAIM=1 EREBUS_LOOT_ALWAYS=1 EREBUS_MAX_FRAMES=900 -- --mission debug_single
check "a kill drops a weapon, and it comes with you when the mission clears" "$OUT/bossloot.json" \
      "s['missionState'] == 'complete' and s['loot'] >= 1 and s['owned'] >= 3"
# ...and the Rook sells what nobody else will.
cat > "$OUT/save-market.txt" <<'SEED'
name = Operative
chits = 2000
xp = 3200
rank_paid = 4
class = wraith
equipped_sidearm = sidearm
equipped_armor = patrol_vest
equipped_cosmetic = default
owned_weapon sidearm
owned_armor patrol_vest
owned_cosmetic default
SEED
run "$OUT/market.json" EREBUS_SPACE_AUTOPILOT=cradle EREBUS_FORCE_ENGAGE=1 \
    EREBUS_STATION_AT=-8,0,-13 EREBUS_STATION_YAW=-90 EREBUS_POST_SCRIPT=use \
    EREBUS_MAX_FRAMES=200
checkfile "the Rook's black market sells a weapon" "$OUT/save-market.txt" "^owned_weapon hal_sparrow$"
checkfile "...for its price" "$OUT/save-market.txt" "^chits = 1410$"

# No GL errors anywhere — in particular after a mission, back on the
# concourse. Ending a mission used to free the meshes the station's crew are
# built from, and the stations drew motes they do not have; both were a
# GL_INVALID_OPERATION every frame, which some drivers print to the console
# thousands of times and some drivers draw nothing for.
cp "$OUT/save-tally.txt" "$OUT/save-glclean.txt"
xvfb-run -a env LIBGL_ALWAYS_SOFTWARE=1 EREBUS_FIXED_DT=0.016 EREBUS_CLASS=bulwark \
  EREBUS_SKIP_HUB=1 EREBUS_FORCE_FIRE=1 EREBUS_FORCE_ENGAGE=1 EREBUS_DEBUG_AUTOAIM=1 \
  EREBUS_SAVE_PATH="$OUT/save-glclean.txt" EREBUS_LOG_STATE="$OUT/glclean.json" \
  EREBUS_MAX_FRAMES=1100 "$BIN" --mission debug_single > "$OUT/glclean.log" 2>&1
check "back on the concourse after a mission" "$OUT/glclean.json" "s['appState'] == 'station'"
checkabsent "...with no GL errors on the way" "$OUT/glclean.log" "GL ERROR"

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
