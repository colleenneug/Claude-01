#pragma once
// =============================================================================
//  playground/engine/Editor.h — the developer UI's panels (DebugUi.h shows
//  them; F1).
//
//    editor bar   File: Save (Ctrl+S) and Load (Ctrl+O) the scene file
//                 (engine/SceneSerializer.h); View: which panels are open;
//                 the last save or load's result.
//    Hierarchy    every entity in the ecs::World, by Name if it has one;
//                 click to select.
//    Inspector    the selected entity's components, editable live:
//                 Name, Transform, Transform3D (moving a physics body
//                 teleports it), Velocity, Sprite, PlayerTag, MeshRenderer,
//                 Material, Light, RigidBody, Collider and
//                 CharacterController (body settings apply with "Rebuild
//                 body": PhysicsSystem::rebuild), and a body's live state.
//    Profiler     frame time (ms, FPS, a graph of the last 4 s), the fixed
//                 steps and their time, update and render time, entities,
//                 draw calls, physics bodies, and for a network client its
//                 round trip, snapshots (deltas, out of order) and
//                 prediction corrections.
//
//  Call draw() from a DebugUi panel (only then is there an ImGui frame).
// =============================================================================

#include "playground/core/Result.h"
#include "playground/ecs/World.h"
#include "playground/engine/Application.h"
#include "playground/engine/Physics.h"
#include "playground/physics/PhysicsWorld.h"
#include "playground/render/Renderer.h"

#include <array>
#include <filesystem>
#include <optional>
#include <string>

namespace playground::engine {

// A network client's numbers, for the profiler.
struct NetProfile {
  std::string state;
  f64 roundTripMs = 0.0;  // ENet's estimate
  f64 ackDelayMs = 0.0;   // input sent -> acknowledged in a snapshot
  u64 snapshots = 0;
  u64 deltaSnapshots = 0;
  u64 outOfOrder = 0;
  u64 missingBaseline = 0;
  u64 corrections = 0;
  u32 inputSeq = 0;
  u32 ackedInputSeq = 0;
  usize knownEntities = 0;
};

struct ProfilerData {
  FrameProfile frame;
  render::Renderer::FrameStats render;
  usize entities = 0;
  std::optional<physics::PhysicsStats> physics;
  std::optional<NetProfile> net;
};

class Editor {
 public:
  explicit Editor(std::filesystem::path sceneFile) : sceneFile_(std::move(sceneFile)) {}

  // The editor bar and the open panels for `world`. Returns true if a scene
  // was loaded into `world` this frame (entities the caller held are stale).
  bool draw(ecs::World& world, const ProfilerData& data, PhysicsSystem* physics = nullptr);

  // Only the profiler (a scene with no ecs::World of its own).
  void drawProfilerOnly(const ProfilerData& data) {
    if (showProfiler_) drawProfiler(data);
  }

  // What the menu and the shortcuts do.
  Status save(ecs::World& world);
  Result<usize> load(ecs::World& world);

  [[nodiscard]] const std::filesystem::path& sceneFile() const noexcept { return sceneFile_; }
  [[nodiscard]] ecs::Entity selected() const noexcept { return selected_; }

 private:
  void drawHierarchy(ecs::World& world);
  void drawInspector(ecs::World& world, PhysicsSystem* physics);
  void drawProfiler(const ProfilerData& data);

  std::filesystem::path sceneFile_;
  ecs::Entity selected_;
  bool showHierarchy_ = true;
  bool showInspector_ = true;
  bool showProfiler_ = true;
  std::array<f32, 480> frameMs_{};  // a ring of the last frames' times
  usize frameCursor_ = 0;
  f64 smoothedFrameSeconds_ = 0.0;
  std::string status_;
};

}  // namespace playground::engine
