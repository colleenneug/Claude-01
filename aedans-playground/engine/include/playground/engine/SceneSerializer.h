#pragma once
// =============================================================================
//  playground/engine/SceneSerializer.h — saving an ecs::World as JSON, and
//  loading it back.
//
//  A scene file is a JSON object:
//
//    { "format": "aedans-playground-scene", "version": 1,
//      "entities": [
//        { "Name": "crate",
//          "Transform3D": { "position": [-1.4, 0.5, 0.6], "rotation": [1, 0, 0, 0], "scale": [1, 1, 1] },
//          "MeshRenderer": { "model": "models/crate.gltf", "visible": true, "castsShadow": true },
//          "RigidBody": { "motion": "kinematic", "mass": 0, "friction": 0.5, ... },
//          "Collider": { "shape": "box", "halfExtents": [0.5, 0.5, 0.5] } },
//        ... ] }
//
//  One object per entity, one key per component it has, written by name so
//  a person can read and edit the file. The components saved: Name,
//  Transform, Transform3D, Velocity, Sprite, PlayerTag, MeshRenderer,
//  Material, Light, RigidBody, Collider, CharacterController. What the
//  running engine fills in (a body's id, a character's ground state,
//  previous transforms) is not saved: it is rebuilt when the scene runs.
//  Rotations are quaternions [w, x, y, z]. Floats go out with as many digits
//  as it takes to read back the same float exactly.
//
//  LOADING replaces everything in the world with the file's entities (in
//  the file's order; entity ids are new). It is all or nothing: the whole
//  file is parsed and checked first, and a file with an error (not JSON,
//  another format, a wrong type, an unknown shape) leaves the world as it
//  was. A component key this version does not know is skipped, with a
//  warning in the log (a newer file still loads what it can).
// =============================================================================

#include "playground/core/Result.h"
#include "playground/core/Types.h"
#include "playground/ecs/World.h"

#include <filesystem>
#include <string>
#include <string_view>

namespace playground::engine {

class SceneSerializer {
 public:
  static constexpr int kVersion = 1;

  [[nodiscard]] static std::string toJson(ecs::World& world);
  // The number of entities loaded.
  [[nodiscard]] static Result<usize> fromJson(ecs::World& world, std::string_view json);

  [[nodiscard]] static Status saveToFile(ecs::World& world, const std::filesystem::path& path);
  [[nodiscard]] static Result<usize> loadFromFile(ecs::World& world, const std::filesystem::path& path);
};

}  // namespace playground::engine
