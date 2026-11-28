#include "playground/engine/Editor.h"

#include "playground/core/Log.h"
#include "playground/engine/Components.h"
#include "playground/engine/SceneSerializer.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <format>

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>

namespace playground::engine {
namespace {

using log::Category;

std::string labelOf(ecs::World& world, ecs::Entity e) {
  if (const Name* name = world.tryGet<Name>(e); name != nullptr && !name->value.empty()) {
    return std::format("{}##{}", name->value, e.index);
  }
  return std::format("entity {}##{}", e.index, e.index);
}

// A std::string in an ImGui text field.
bool editText(const char* label, std::string& value) {
  std::array<char, 256> buffer{};
  std::strncpy(buffer.data(), value.c_str(), buffer.size() - 1);
  if (!ImGui::InputText(label, buffer.data(), buffer.size())) return false;
  value = buffer.data();
  return true;
}

const char* describe(physics::GroundState ground) {
  switch (ground) {
    case physics::GroundState::OnGround: return "on the ground";
    case physics::GroundState::OnSteepGround: return "on steep ground";
    case physics::GroundState::NotSupported: return "not supported";
    case physics::GroundState::InAir: return "in the air";
  }
  return "?";
}

}  // namespace

Status Editor::save(ecs::World& world) {
  Status s = SceneSerializer::saveToFile(world, sceneFile_);
  status_ = s ? std::format("saved {} entities to {}", world.entityCount(), sceneFile_.string())
              : std::format("save failed: {}", s.error().message);
  return s;
}

Result<usize> Editor::load(ecs::World& world) {
  auto loaded = SceneSerializer::loadFromFile(world, sceneFile_);
  if (loaded) {
    status_ = std::format("loaded {} entities from {}", *loaded, sceneFile_.string());
    selected_ = {};
  } else {
    status_ = std::format("load failed: {}", loaded.error().message);
    log::warn(Category::Engine, "{}", status_);
  }
  return loaded;
}

bool Editor::draw(ecs::World& world, const ProfilerData& data, PhysicsSystem* physics) {
  bool loaded = false;
  bool wantSave = ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S);
  bool wantLoad = ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_O);
  if (ImGui::BeginMainMenuBar()) {
    if (ImGui::BeginMenu("File")) {
      wantSave |= ImGui::MenuItem("Save scene", "Ctrl+S");
      wantLoad |= ImGui::MenuItem("Load scene", "Ctrl+O");
      ImGui::Separator();
      ImGui::TextDisabled("%s", sceneFile_.string().c_str());
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
      ImGui::MenuItem("Hierarchy", nullptr, &showHierarchy_);
      ImGui::MenuItem("Inspector", nullptr, &showInspector_);
      ImGui::MenuItem("Profiler", nullptr, &showProfiler_);
      ImGui::EndMenu();
    }
    ImGui::TextDisabled("F1 hides  |  %s", status_.empty() ? "unsaved" : status_.c_str());
    ImGui::EndMainMenuBar();
  }
  if (wantSave) (void)save(world);
  if (wantLoad) loaded = load(world).has_value();

  if (!world.alive(selected_)) selected_ = {};
  if (showHierarchy_) drawHierarchy(world);
  if (showInspector_) drawInspector(world, physics);
  if (showProfiler_) drawProfiler(data);
  return loaded;
}

void Editor::drawHierarchy(ecs::World& world) {
  ImGui::SetNextWindowPos({10, 30}, ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize({250, 380}, ImGuiCond_FirstUseEver);
  if (ImGui::Begin("Hierarchy", &showHierarchy_)) {
    const std::vector<ecs::Entity> entities = world.entities();
    ImGui::TextDisabled("%zu entities", entities.size());
    ImGuiListClipper clipper;  // only the rows on screen: fine for 25,000 sprites too
    clipper.Begin(static_cast<int>(entities.size()));
    while (clipper.Step()) {
      for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
        const ecs::Entity e = entities[static_cast<usize>(i)];
        if (ImGui::Selectable(labelOf(world, e).c_str(), e == selected_)) selected_ = e;
      }
    }
  }
  ImGui::End();
}

void Editor::drawInspector(ecs::World& world, PhysicsSystem* physics) {
  ImGui::SetNextWindowPos({10, 420}, ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize({330, 420}, ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Inspector", &showInspector_)) {
    ImGui::End();
    return;
  }
  const ecs::Entity e = selected_;
  if (!world.alive(e)) {
    ImGui::TextDisabled("Select an entity in the Hierarchy.");
    ImGui::End();
    return;
  }
  ImGui::Text("entity %u (generation %u)", e.index, e.generation);
  bool rebuild = false;

  if (Name* c = world.tryGet<Name>(e)) editText("Name", c->value);
  if (Transform3D* c = world.tryGet<Transform3D>(e); c && ImGui::CollapsingHeader("Transform3D", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::DragFloat3("position", glm::value_ptr(c->position), 0.01f);
    glm::vec3 euler = glm::degrees(glm::eulerAngles(c->rotation));
    if (ImGui::DragFloat3("rotation (deg)", glm::value_ptr(euler), 0.5f)) c->rotation = glm::quat(glm::radians(euler));
    ImGui::DragFloat3("scale", glm::value_ptr(c->scale), 0.01f, 0.001f, 1000.0f);
  }
  if (Transform* c = world.tryGet<Transform>(e); c && ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::DragFloat2("position", glm::value_ptr(c->position), 1.0f);
    f32 degrees = glm::degrees(c->rotation);
    if (ImGui::DragFloat("rotation (deg)", &degrees, 0.5f)) c->rotation = glm::radians(degrees);
    ImGui::DragFloat2("scale", glm::value_ptr(c->scale), 0.01f, 0.001f, 1000.0f);
  }
  if (Velocity* c = world.tryGet<Velocity>(e); c && ImGui::CollapsingHeader("Velocity")) {
    ImGui::DragFloat2("linear", glm::value_ptr(c->linear), 1.0f);
    ImGui::DragFloat("angular", &c->angular, 0.05f);
  }
  if (Sprite* c = world.tryGet<Sprite>(e); c && ImGui::CollapsingHeader("Sprite")) {
    ImGui::DragFloat2("size", glm::value_ptr(c->size), 0.5f, 0.0f, 10000.0f);
    ImGui::ColorEdit4("colour", glm::value_ptr(c->colour));
    ImGui::DragFloat("layer", &c->layer, 0.1f);
  }
  if (PlayerTag* c = world.tryGet<PlayerTag>(e); c && ImGui::CollapsingHeader("PlayerTag")) {
    ImGui::DragFloat("speed", &c->speed, 1.0f, 0.0f, 10000.0f);
  }
  if (MeshRenderer* c = world.tryGet<MeshRenderer>(e); c && ImGui::CollapsingHeader("MeshRenderer", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::TextDisabled("model: %s", c->model.c_str());
    ImGui::Checkbox("visible", &c->visible);
    ImGui::SameLine();
    ImGui::Checkbox("casts shadow", &c->castsShadow);
  }
  if (Material* c = world.tryGet<Material>(e); c && ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::ColorEdit4("tint", glm::value_ptr(c->tint));
    ImGui::SliderFloat("metallic x", &c->metallicScale, 0.0f, 2.0f);
    ImGui::SliderFloat("roughness x", &c->roughnessScale, 0.0f, 2.0f);
    ImGui::ColorEdit3("emissive", glm::value_ptr(c->emissive), ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float);
  }
  if (Light* c = world.tryGet<Light>(e); c && ImGui::CollapsingHeader("Light", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::TextDisabled("directional (the sun)");
    if (ImGui::DragFloat3("direction", glm::value_ptr(c->direction), 0.01f) && glm::length(c->direction) > 1e-4f) {
      c->direction = glm::normalize(c->direction);
    }
    ImGui::ColorEdit3("colour", glm::value_ptr(c->colour));
    ImGui::DragFloat("intensity", &c->intensity, 0.05f, 0.0f, 100.0f);
  }
  if (RigidBody* c = world.tryGet<RigidBody>(e); c && ImGui::CollapsingHeader("RigidBody", ImGuiTreeNodeFlags_DefaultOpen)) {
    int motion = static_cast<int>(c->motion);
    rebuild |= ImGui::Combo("motion", &motion, "static\0kinematic\0dynamic\0");
    c->motion = static_cast<physics::MotionType>(motion);
    rebuild |= ImGui::DragFloat("mass (0 = auto)", &c->mass, 0.5f, 0.0f, 100000.0f);
    rebuild |= ImGui::SliderFloat("friction", &c->friction, 0.0f, 1.5f);
    rebuild |= ImGui::SliderFloat("restitution", &c->restitution, 0.0f, 1.0f);
    rebuild |= ImGui::SliderFloat("gravity x", &c->gravityFactor, -2.0f, 4.0f);
    if (physics != nullptr && c->body.valid()) {
      if (const auto state = physics->physics().bodyState(c->body)) {
        ImGui::TextDisabled("speed %.2f m/s, %s", glm::length(state->linearVelocity), state->active ? "awake" : "asleep");
      }
    }
  }
  if (Collider* c = world.tryGet<Collider>(e); c && ImGui::CollapsingHeader("Collider", ImGuiTreeNodeFlags_DefaultOpen)) {
    physics::ShapeDesc& s = c->shape;
    if (s.type == physics::ShapeType::Mesh) {
      ImGui::TextDisabled("mesh: %zu triangles", s.indices.size() / 3);
    } else {
      int type = static_cast<int>(s.type);
      rebuild |= ImGui::Combo("shape", &type, "box\0sphere\0capsule\0");
      s.type = static_cast<physics::ShapeType>(type);
      if (s.type == physics::ShapeType::Box) {
        rebuild |= ImGui::DragFloat3("half extents", glm::value_ptr(s.halfExtents), 0.01f, 0.01f, 1000.0f);
      } else {
        rebuild |= ImGui::DragFloat("radius", &s.radius, 0.01f, 0.01f, 1000.0f);
        if (s.type == physics::ShapeType::Capsule) {
          rebuild |= ImGui::DragFloat("half height", &s.halfHeight, 0.01f, 0.01f, 1000.0f);
        }
      }
    }
  }
  if (CharacterController* c = world.tryGet<CharacterController>(e);
      c && ImGui::CollapsingHeader("CharacterController", ImGuiTreeNodeFlags_DefaultOpen)) {
    rebuild |= ImGui::DragFloat("radius", &c->radius, 0.01f, 0.05f, 5.0f);
    rebuild |= ImGui::DragFloat("height", &c->height, 0.01f, 0.2f, 10.0f);
    rebuild |= ImGui::SliderFloat("max slope", &c->maxSlopeDegrees, 0.0f, 89.0f);
    rebuild |= ImGui::DragFloat("step height", &c->stepHeight, 0.01f, 0.0f, 2.0f);
    ImGui::TextDisabled("%s, %.2f m/s", describe(c->ground), glm::length(c->velocity));
  }
  // Body settings are read when a body is made: edits rebuild it.
  if (rebuild && physics != nullptr) physics->rebuild(e);

  ImGui::Separator();
  if (ImGui::Button("Destroy entity")) {
    world.destroy(e);
    world.flush();
    selected_ = {};
  }
  ImGui::End();
}

void Editor::drawProfiler(const ProfilerData& data) {
  const auto frameMs = static_cast<f32>(data.frame.frameSeconds * 1000.0);
  frameMs_[frameCursor_] = frameMs;
  frameCursor_ = (frameCursor_ + 1) % frameMs_.size();
  smoothedFrameSeconds_ = smoothedFrameSeconds_ == 0.0 ? data.frame.frameSeconds
                                                       : smoothedFrameSeconds_ * 0.95 + data.frame.frameSeconds * 0.05;

  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos({viewport->WorkPos.x + viewport->WorkSize.x - 470, viewport->WorkPos.y + 10},
                          ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize({460, 330}, ImGuiCond_FirstUseEver);
  if (ImGui::Begin("Profiler", &showProfiler_)) {
    const f64 fps = smoothedFrameSeconds_ > 0.0 ? 1.0 / smoothedFrameSeconds_ : 0.0;
    ImGui::Text("frame  %.2f ms  (%.0f fps)", smoothedFrameSeconds_ * 1000.0, fps);
    const f32 worst = *std::max_element(frameMs_.begin(), frameMs_.end());
    ImGui::PlotLines("##frames", frameMs_.data(), static_cast<int>(frameMs_.size()), static_cast<int>(frameCursor_),
                     std::format("last {} frames, worst {:.1f} ms", frameMs_.size(), worst).c_str(), 0.0f,
                     std::max(worst, 1.0f), ImVec2(-1.0f, 60.0f));
    ImGui::Text("fixed  %u step(s), %.3f ms", data.frame.fixedSteps, data.frame.fixedSeconds * 1000.0);
    ImGui::Text("update %.3f ms   render %.3f ms", data.frame.updateSeconds * 1000.0,
                data.frame.renderSeconds * 1000.0);
    ImGui::Separator();
    ImGui::Text("entities     %zu", data.entities);
    ImGui::Text("draw calls   %u  (meshes %u, sprites %u, UI lists %u)", data.render.drawCalls, data.render.meshDraws,
                data.render.sprites, data.render.uiDrawLists);
    if (data.physics) {
      ImGui::Text("physics      %zu bodies, %zu awake, %zu character(s)", data.physics->bodies,
                  data.physics->activeBodies, data.physics->characters);
    }
    if (data.audio) {
      ImGui::Text("audio        %zu playing, %llu started", data.audio->playing,
                  static_cast<unsigned long long>(data.audio->started));
      ImGui::TextDisabled("             %s", data.audio->output.c_str());
    }
    if (data.net) {
      const NetProfile& n = *data.net;
      ImGui::Separator();
      ImGui::Text("network      %s", n.state.c_str());
      ImGui::Text("round trip   %.0f ms  (input acked after %.0f ms)", n.roundTripMs, n.ackDelayMs);
      ImGui::Text("snapshots    %llu  (%llu deltas, %llu out of order, %llu missing baseline)",
                  static_cast<unsigned long long>(n.snapshots), static_cast<unsigned long long>(n.deltaSnapshots),
                  static_cast<unsigned long long>(n.outOfOrder), static_cast<unsigned long long>(n.missingBaseline));
      ImGui::Text("inputs       %u sent, %u acked; %llu corrections", n.inputSeq, n.ackedInputSeq,
                  static_cast<unsigned long long>(n.corrections));
      ImGui::Text("entities     %zu known", n.knownEntities);
    }
  }
  ImGui::End();
}

}  // namespace playground::engine
