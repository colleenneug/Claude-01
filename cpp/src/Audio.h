#pragma once

#include <glm/glm.hpp>

#include <string>

// Sound. Every effect is synthesised at start-up from a few oscillators,
// noise and envelopes (Audio.cpp) — there are no sound files to ship, lose
// or license — and mixed by hand in miniaudio's device callback.
//
// With no output device (a headless test run, a machine with sound off) or
// EREBUS_MUTE set, everything here still works and nothing is heard: calls
// are counted, so a test can prove the game asked for a sound, but never
// fail.
enum class Sfx {
  // weapons, one report per silhouette
  FirePistol, FireRifle, FireCarbine, FireSmg, FireShotgun, FireMarksman, FireInduction,
  DryFire, ReloadOut, ReloadIn, WeaponSwap,
  // your rounds landing
  Hit, Headshot, Kill, HitMetal,
  // hostiles
  DroneShot, ThrallShriek, WardenShot, MarauderShot, ScarabSkitter, ColossusSlam,
  ConductorChord, HostileWake, DeathOrganic, DeathMech, StructureDown, BossDeath,
  // you
  Footstep, Jump, Land, Slide, Hurt, Downed, Revive, Heal, ShieldBreak,
  AbilityBarrier, AbilityBreach, AbilityPhase, AbilityReady,
  // things on the floor
  PickupAmmo, PickupHealth, PickupWeapon, PickupCore, LootDrop,
  // the mission
  ObjectiveDone, HoldTick, MissionComplete, MissionFailed, BossSpawn, Promotion,
  Comms, Cutscene,
  // menus and people
  UiMove, UiSelect, UiBack, UiDeny, Buy, Sell, Talk, BountyDone,
  // ships and stations
  Boost, Dock, Undock, Door, Warp,
  Count
};

// Continuous sounds: started, steered and stopped rather than fired.
enum class Loop { Engine, Uplink, Heartbeat, MissionAmbience, StationHum, Count };

namespace audio {

// false when there is no device or sound is muted; the game carries on.
bool init();
void shutdown();
bool live();

// Where your ears are. Positional sounds pan and fall off from here.
void setListener(const glm::vec3& pos, const glm::vec3& forward);

void play(Sfx s, float volume = 1.0f, float pitch = 1.0f);
void playAt(Sfx s, const glm::vec3& at, float volume = 1.0f, float pitch = 1.0f);

// A loop at volume 0 is silent but keeps its phase; volume and pitch glide
// to the new values over a few milliseconds rather than stepping.
void setLoop(Loop l, float volume, float pitch = 1.0f);
void stopLoops();

// How many sounds the game has asked for since start-up, heard or not.
int playCount();
int playCount(Sfx s);
int soundCount();   // how many effects were synthesised
std::string deviceName();

}  // namespace audio
