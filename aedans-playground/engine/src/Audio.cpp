#include "playground/engine/Audio.h"

#include "playground/core/Log.h"
#include "playground/engine/Components.h"

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>

namespace playground::engine {
namespace {

using log::Category;

// How fast something moved since the last frame; a teleport is not a speed.
glm::vec3 velocityOf(glm::vec3 now, glm::vec3 before, f32 dt) {
  if (!(dt > 0.0f)) return glm::vec3(0.0f);
  const glm::vec3 v = (now - before) / dt;
  return glm::length(v) > kTeleportSpeed ? glm::vec3(0.0f) : v;
}

}  // namespace

void audioSystem(ecs::World& world, audio::AudioEngine& engine, f32 dt) {
  // 1. The ears.
  bool heard = false;
  world.each<Transform3D, AudioListener>([&](ecs::Entity, Transform3D& t, AudioListener& listener) {
    if (heard || !listener.active) return;
    heard = true;
    const glm::vec3 velocity = listener.hasLast ? velocityOf(t.position, listener.lastPosition, dt) : glm::vec3(0.0f);
    engine.setListener({.position = t.position, .forward = t.rotation * glm::vec3(0.0f, 0.0f, -1.0f),
                        .up = t.rotation * glm::vec3(0.0f, 1.0f, 0.0f), .velocity = velocity});
    listener.lastPosition = t.position;
    listener.hasLast = true;
  });

  // 2. The sources.
  world.each<Transform3D, AudioSource>([&](ecs::Entity e, Transform3D& t, AudioSource& source) {
    const bool start = source.play || (source.playOnSpawn && !source.started);
    source.play = false;
    if (start && source.clip.valid()) {
      if (source.voice.valid()) engine.stop(source.voice);  // started again: from the top
      auto voice = engine.play(source.clip, {.volume = source.volume, .pitch = source.pitch, .loop = source.loop,
                                             .spatial = source.spatial, .position = t.position,
                                             .minDistance = source.minDistance, .maxDistance = source.maxDistance,
                                             .owner = ownerTag(e)});
      if (voice) {
        source.voice = *voice;
      } else {
        log::warn(Category::Engine, "audio: entity {}'s sound did not start: {}", e.index, voice.error().message);
      }
      source.started = true;
      source.lastPosition = t.position;
      return;
    }
    if (!source.voice.valid()) return;
    if (!engine.playing(source.voice)) {
      source.voice = {};  // a one-shot that finished
      return;
    }
    engine.setVoice(source.voice, t.position, velocityOf(t.position, source.lastPosition, dt));
    source.lastPosition = t.position;
  });

  // 3. Whatever belonged to an entity that is gone.
  engine.stopIf([&](u64 owner) { return owner != 0 && !world.alive(ownerEntity(owner)); });

  // 4. Finished one-shots.
  engine.update();
}

}  // namespace playground::engine
