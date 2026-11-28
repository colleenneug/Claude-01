#pragma once
// =============================================================================
//  playground/engine/Audio.h — sound for ECS entities.
//
//  Components (plain data, like the rest):
//    AudioSource    a clip this entity plays (from audio::AudioEngine::
//                   addClip): volume, pitch, looping, how far it carries,
//                   whether it is placed in 3D, and whether it starts by
//                   itself as soon as the entity exists (playOnSpawn) or
//                   waits for `play` to be set;
//    AudioListener  the ears: on the camera, or on the player.
//  Both need a Transform3D: that is where the sound is, and where it is
//  heard from (its rotation's -z is "forward", +y "up").
//
//  audioSystem(world, engine, dt), once a frame:
//    1. the listener follows the first active AudioListener (position,
//       facing, and velocity from how far it moved, for Doppler);
//    2. each AudioSource starts if it should, and its voice follows the
//       entity (position and velocity);
//    3. voices of destroyed entities stop (a loop would otherwise play on);
//    4. the engine frees finished one-shots.
//  A jump of more than kTeleportSpeed counts as a teleport, not a velocity
//  (no Doppler shriek when something is moved across the map).
// =============================================================================

#include "playground/audio/AudioEngine.h"
#include "playground/core/Types.h"
#include "playground/ecs/World.h"

#include <glm/vec3.hpp>

namespace playground::engine {

inline constexpr f32 kTeleportSpeed = 150.0f;  // m/s: faster than this is not motion

struct AudioSource {
  audio::ClipId clip;
  f32 volume = 1.0f;
  f32 pitch = 1.0f;
  bool loop = false;
  bool spatial = true;
  f32 minDistance = 1.0f;
  f32 maxDistance = 60.0f;
  bool playOnSpawn = true;  // start as soon as audioSystem sees it
  bool play = false;        // set to start it (again) now; audioSystem clears it

  // Set by audioSystem:
  audio::VoiceId voice;
  bool started = false;
  glm::vec3 lastPosition{0.0f};
};

struct AudioListener {
  bool active = true;
  // Set by audioSystem:
  bool hasLast = false;
  glm::vec3 lastPosition{0.0f};
};

// An entity as a voice's owner tag, and back (audio::PlayDesc::owner).
[[nodiscard]] constexpr u64 ownerTag(ecs::Entity e) noexcept {
  return (u64{e.generation} << 32 | e.index) + 1;  // + 1: 0 means "no owner"
}
[[nodiscard]] constexpr ecs::Entity ownerEntity(u64 tag) noexcept {
  const u64 value = tag - 1;
  return {.index = static_cast<u32>(value & 0xFFFFFFFFu), .generation = static_cast<u32>(value >> 32)};
}

void audioSystem(ecs::World& world, audio::AudioEngine& engine, f32 deltaSeconds);

}  // namespace playground::engine
