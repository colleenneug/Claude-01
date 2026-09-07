# Erebus Cradle — Unreal Engine port

A real Unreal Engine 5 C++ project scaffold for the same sci-fi story and
three-doctrine class system as the browser build (`../README.md`) and the
native OpenGL renderer (`../cpp/`), aimed at an actual UE5 game rather than
a from-scratch engine.

**Scope, honestly stated:** this environment cannot run the Unreal Editor —
it's a multi-gigabyte proprietary application with no headless CLI build
path available here, so nothing in `unreal/ErebusCradle/` has been compiled
or launched. What exists is a real C++ gameplay module — project file,
build rules, and a doctrine/weapon/ability/AI/save-game architecture with
numbers ported exactly from `src/js/classes.js` and `src/js/fps/weapons.js`
— written the way it would be written with the editor open, and it needs
your machine's Unreal install to actually build, populate with content, and
play. Treat compiler errors on first open as normal for hand-written UE
code that has never been run through UBT; this hasn't been machine-verified
the way the browser build and `cpp/` have.

## What's here

```
ErebusCradle.uproject                 opens in the Editor
Source/ErebusCradle.Target.cs         game target
Source/ErebusCradleEditor.Target.cs   editor target
Source/ErebusCradle/                  the one C++ module
  Public/ECTypes.h                    EECDoctrine enum + doctrine/weapon data structs
  Public|Private/Characters/          ECCharacterBase (vitals/damage/energy),
                                       ECPlayerCharacter (doctrine-driven pawn, Enhanced Input)
  Public|Private/Weapons/             ECWeaponBase — one data-driven hitscan weapon class
                                       for all three doctrines' guns
  Public|Private/Abilities/           ECFieldAbilityComponent base + Aegis Barrier /
                                       Systems Breach / Phase Step
  Public|Private/AI/                  ECHostileCharacter + ECHostileAIController
                                       (AIPerception sight, no Behavior Tree asset needed)
  Public|Private/Game/                ECGameMode, ECGameInstance (doctrine table),
                                       ECSaveGame, ECProgressionLibrary (XP/level curve)
Config/DefaultEngine.ini              GameInstance/GameMode class, Enhanced Input as default
Config/DefaultGame.ini                project name/version
```

There is no `Content/` folder. Meshes, animations, materials, the Input
Mapping Context and Input Actions Enhanced Input needs, and any Blueprint
subclasses are binary editor assets — they cannot be authored as text files
outside the Editor, so they aren't faked here. The C++ classes expose the
hooks a content pass plugs into (see *Next steps in the Editor* below).

## The class system

`EECDoctrine` (bulwark / oracle / wraith) and `FECDoctrineDefinition` in
`ECTypes.h` are the whole class system: one struct holding a doctrine's
vitals, attributes, passive, weapon stats and ability class. `UECGameInstance::
BuildDefaultDoctrineTable()` populates three of them with the exact numbers
from the browser build —

| | Bulwark | Oracle | Wraith |
|---|---|---|---|
| HP / Energy | 124 / 10 | 94 / 14 | 100 / 12 |
| Weapon | MAUL-12 shotgun, 17×8 dmg, 75 rpm | ARC LANCE rifle, 26 dmg, 320 rpm, pierces | WHISPER carbine, 15 dmg, 640 rpm, ×3 headshot |
| Passive | Bulkhead Plating — 22% flat mitigation | Ghost in the Wire (deepest mag, flattest recoil — modeled directly in the weapon stats) | Blindside (headshot multiplier) |
| Field ability | Aegis Barrier — 60 overshield | Systems Breach — EMP stun | Phase Step — blink + primed shot |

Adding a fourth doctrine is a new `FECDoctrineDefinition` entry, not a new
character class — `AECPlayerCharacter::InitializeDoctrine()` reads the
struct and configures itself, the same "one character record, a doctrine
id" shape as `classes.js`.

`ECProgressionLibrary::XPRequiredForLevel()` is `40 + (level-1)*55`,
matching `classes.js`'s `xpForLevel` exactly, so a shared web/desktop
leaderboard would agree on when either version ranks up.

## Building it — on your machine, not here

1. Install **Unreal Engine 5.4** (Epic Games Launcher, or build from source)
   and a C++ toolchain (Visual Studio 2022 with the "Game development with
   C++" workload on Windows; Xcode on macOS; clang on Linux).
2. Right-click `ErebusCradle.uproject` → **Generate Visual Studio project
   files** (or run `GenerateProjectFiles` from the Engine's `Build/BatchFiles`
   on macOS/Linux).
3. Open the generated solution, build the `ErebusCradleEditor` target, or
   just double-click the `.uproject` and let the Editor prompt to compile.
4. The Editor will open with **no map** — there's no Content to open into.
   Create one (`File → New Level`) and save it as the project's startup map
   under Project Settings → Maps & Modes.

## Next steps in the Editor

This is a code skeleton, not a playable level. To get something on screen:

1. Create Input Actions (`IA_Move`, `IA_Look`, `IA_Jump`, `IA_Sprint`,
   `IA_Crouch`, `IA_Fire`, `IA_Aim`, `IA_Reload`, `IA_FieldAbility`) and one
   Input Mapping Context binding WASD/mouse/gamepad to them, then assign all
   of them on a Blueprint child of `ECPlayerCharacter` (`BP_PlayerCharacter`).
2. Give that Blueprint a mesh and a camera-ready skeleton (or keep it a pure
   capsule for a first-person-only prototype — `ECPlayerCharacter` already
   sets up a first-person camera on a zero-length spring arm).
3. Create three weapon Blueprints (`BP_Weapon_Maul`, `BP_Weapon_Lance`,
   `BP_Weapon_Whisper`) off `ECWeaponBase` with a mesh each, and three
   ability components already exist as C++ classes
   (`UECAegisBarrierComponent`, `UECSystemsBreachComponent`,
   `UECPhaseStepComponent`) — no Blueprint needed for those unless you want
   to add VFX/sound hooks.
4. Make a Blueprint child of `UECGameInstance`, point `WeaponClass` and
   `FieldAbilityClass` on each doctrine table entry at the Blueprints from
   steps 2–3, and set it as the project's Game Instance class in Project
   Settings (or leave `Config/DefaultEngine.ini`'s reference to the base
   C++ class and override the table there directly).
5. Give `AECHostileCharacter` a mesh via a Blueprint child; it already
   AI-possesses itself with `ECHostileAIController` and will chase/attack
   anything it can see with no further setup.
6. Build a level, place a `PlayerStart` and a few hostiles, hit Play.

## What didn't come across from the other two builds

No mission/campaign loader, no HUD, no co-op netcode, no gear/rarity/salvage
system, no orbital patrol zones, and no character-creator preview — those
are substantial systems in their own right (see `../README.md`) and porting
them is future work, not implied by this scaffold. What's here is the
foundation those would sit on: vitals, a data-driven weapon, three doctrines
with real passives and abilities, hostile AI, and a save-game format for
career progression.
