#include "playground/engine/SceneSerializer.h"

#include "playground/core/Log.h"
#include "playground/engine/Components.h"
#include "playground/engine/Physics.h"

#include <fstream>
#include <optional>
#include <set>
#include <sstream>
#include <vector>

#include <nlohmann/json.hpp>

namespace playground::engine {
namespace {

using log::Category;
using nlohmann::json;

constexpr std::string_view kFormat = "aedans-playground-scene";

// --- glm <-> JSON arrays -----------------------------------------------------------

json arr(glm::vec2 v) { return json::array({v.x, v.y}); }
json arr(glm::vec3 v) { return json::array({v.x, v.y, v.z}); }
json arr(glm::vec4 v) { return json::array({v.x, v.y, v.z, v.w}); }
json arr(glm::quat q) { return json::array({q.w, q.x, q.y, q.z}); }

// Reading throws (caught once, at the top) with a message naming what was wrong.
struct BadScene : std::runtime_error {
  using std::runtime_error::runtime_error;
};

const json& field(const json& object, const char* key) {
  if (!object.is_object() || !object.contains(key)) throw BadScene(std::format("missing \"{}\"", key));
  return object.at(key);
}

f32 number(const json& value, const char* what) {
  if (!value.is_number()) throw BadScene(std::format("\"{}\" must be a number", what));
  return value.get<f32>();
}

template <usize N>
std::array<f32, N> numbers(const json& value, const char* what) {
  if (!value.is_array() || value.size() != N) throw BadScene(std::format("\"{}\" must be {} numbers", what, N));
  std::array<f32, N> out{};
  for (usize i = 0; i < N; ++i) out[i] = number(value[i], what);
  return out;
}

glm::vec2 vec2(const json& o, const char* key) {
  const auto a = numbers<2>(field(o, key), key);
  return {a[0], a[1]};
}
glm::vec3 vec3(const json& o, const char* key) {
  const auto a = numbers<3>(field(o, key), key);
  return {a[0], a[1], a[2]};
}
glm::vec4 vec4(const json& o, const char* key) {
  const auto a = numbers<4>(field(o, key), key);
  return {a[0], a[1], a[2], a[3]};
}
glm::quat quat(const json& o, const char* key) {
  const auto a = numbers<4>(field(o, key), key);
  return {a[0], a[1], a[2], a[3]};
}
f32 scalar(const json& o, const char* key) { return number(field(o, key), key); }
bool boolean(const json& o, const char* key) {
  const json& v = field(o, key);
  if (!v.is_boolean()) throw BadScene(std::format("\"{}\" must be true or false", key));
  return v.get<bool>();
}
std::string text(const json& o, const char* key) {
  const json& v = field(o, key);
  if (!v.is_string()) throw BadScene(std::format("\"{}\" must be a string", key));
  return v.get<std::string>();
}
// Optional keys keep the component's default when absent.
template <class T, class Read>
void optional(const json& o, const char* key, T& out, Read read) {
  if (o.contains(key)) out = read(o, key);
}

// --- Enums by name -------------------------------------------------------------------

std::string_view motionName(physics::MotionType m) {
  switch (m) {
    case physics::MotionType::Static: return "static";
    case physics::MotionType::Kinematic: return "kinematic";
    case physics::MotionType::Dynamic: return "dynamic";
  }
  return "dynamic";
}
physics::MotionType motionFrom(const std::string& s) {
  if (s == "static") return physics::MotionType::Static;
  if (s == "kinematic") return physics::MotionType::Kinematic;
  if (s == "dynamic") return physics::MotionType::Dynamic;
  throw BadScene(std::format("unknown motion \"{}\" (static, kinematic or dynamic)", s));
}
std::string_view layerName(physics::Layer l) {
  switch (l) {
    case physics::Layer::Static: return "static";
    case physics::Layer::Moving: return "moving";
    case physics::Layer::Debris: return "debris";
  }
  return "moving";
}
physics::Layer layerFrom(const std::string& s) {
  if (s == "static") return physics::Layer::Static;
  if (s == "moving") return physics::Layer::Moving;
  if (s == "debris") return physics::Layer::Debris;
  throw BadScene(std::format("unknown layer \"{}\" (static, moving or debris)", s));
}

// --- Components ----------------------------------------------------------------------

json save(const Transform& t) {
  return {{"position", arr(t.position)}, {"rotation", t.rotation}, {"scale", arr(t.scale)}};
}
json save(const Transform3D& t) {
  return {{"position", arr(t.position)}, {"rotation", arr(t.rotation)}, {"scale", arr(t.scale)}};
}
json save(const Velocity& v) { return {{"linear", arr(v.linear)}, {"angular", v.angular}}; }
json save(const Sprite& s) { return {{"size", arr(s.size)}, {"colour", arr(s.colour)}, {"layer", s.layer}}; }
json save(const PlayerTag& p) { return {{"speed", p.speed}}; }
json save(const MeshRenderer& m) {
  return {{"model", m.model}, {"visible", m.visible}, {"castsShadow", m.castsShadow}};
}
json save(const Material& m) {
  return {{"tint", arr(m.tint)}, {"metallicScale", m.metallicScale}, {"roughnessScale", m.roughnessScale},
          {"emissive", arr(m.emissive)}};
}
json save(const Light& l) {
  return {{"type", "directional"}, {"direction", arr(l.direction)}, {"colour", arr(l.colour)},
          {"intensity", l.intensity}};
}
json save(const RigidBody& r) {
  json out{{"motion", motionName(r.motion)}, {"mass", r.mass}, {"friction", r.friction},
           {"restitution", r.restitution}, {"linearDamping", r.linearDamping}, {"angularDamping", r.angularDamping},
           {"gravityFactor", r.gravityFactor}, {"initialVelocity", arr(r.initialVelocity)}};
  if (r.layer) out["layer"] = layerName(*r.layer);
  return out;
}
json save(const Collider& c) {
  const physics::ShapeDesc& s = c.shape;
  switch (s.type) {
    case physics::ShapeType::Box: return {{"shape", "box"}, {"halfExtents", arr(s.halfExtents)}};
    case physics::ShapeType::Sphere: return {{"shape", "sphere"}, {"radius", s.radius}};
    case physics::ShapeType::Capsule:
      return {{"shape", "capsule"}, {"radius", s.radius}, {"halfHeight", s.halfHeight}};
    case physics::ShapeType::Mesh: {
      json vertices = json::array();
      for (const glm::vec3& v : s.vertices) vertices.push_back(arr(v));
      return {{"shape", "mesh"}, {"vertices", vertices}, {"indices", s.indices}};
    }
  }
  return {};
}
json save(const CharacterController& c) {
  return {{"radius", c.radius}, {"height", c.height}, {"maxSlopeDegrees", c.maxSlopeDegrees},
          {"stepHeight", c.stepHeight}};
}

Transform loadTransform(const json& o) {
  Transform t;
  t.position = vec2(o, "position");
  t.rotation = scalar(o, "rotation");
  optional(o, "scale", t.scale, vec2);
  return t;
}
Transform3D loadTransform3D(const json& o) {
  Transform3D t;
  t.position = vec3(o, "position");
  optional(o, "rotation", t.rotation, quat);
  optional(o, "scale", t.scale, vec3);
  return t;
}
Velocity loadVelocity(const json& o) {
  Velocity v;
  optional(o, "linear", v.linear, vec2);
  optional(o, "angular", v.angular, scalar);
  return v;
}
Sprite loadSprite(const json& o) {
  Sprite s;
  optional(o, "size", s.size, vec2);
  optional(o, "colour", s.colour, vec4);
  optional(o, "layer", s.layer, scalar);
  return s;
}
PlayerTag loadPlayerTag(const json& o) {
  PlayerTag p;
  optional(o, "speed", p.speed, scalar);
  return p;
}
MeshRenderer loadMeshRenderer(const json& o) {
  MeshRenderer m;
  m.model = text(o, "model");
  optional(o, "visible", m.visible, boolean);
  optional(o, "castsShadow", m.castsShadow, boolean);
  return m;
}
Material loadMaterial(const json& o) {
  Material m;
  optional(o, "tint", m.tint, vec4);
  optional(o, "metallicScale", m.metallicScale, scalar);
  optional(o, "roughnessScale", m.roughnessScale, scalar);
  optional(o, "emissive", m.emissive, vec3);
  return m;
}
Light loadLight(const json& o) {
  Light l;
  if (o.contains("type") && text(o, "type") != "directional") {
    throw BadScene(std::format("unknown light type \"{}\" (only directional)", text(o, "type")));
  }
  optional(o, "direction", l.direction, vec3);
  optional(o, "colour", l.colour, vec3);
  optional(o, "intensity", l.intensity, scalar);
  return l;
}
RigidBody loadRigidBody(const json& o) {
  RigidBody r;
  r.motion = motionFrom(text(o, "motion"));
  optional(o, "mass", r.mass, scalar);
  optional(o, "friction", r.friction, scalar);
  optional(o, "restitution", r.restitution, scalar);
  optional(o, "linearDamping", r.linearDamping, scalar);
  optional(o, "angularDamping", r.angularDamping, scalar);
  optional(o, "gravityFactor", r.gravityFactor, scalar);
  optional(o, "initialVelocity", r.initialVelocity, vec3);
  if (o.contains("layer")) r.layer = layerFrom(text(o, "layer"));
  return r;
}
Collider loadCollider(const json& o) {
  const std::string shape = text(o, "shape");
  Collider c;
  if (shape == "box") {
    c.shape = physics::ShapeDesc::box(vec3(o, "halfExtents"));
  } else if (shape == "sphere") {
    c.shape = physics::ShapeDesc::sphere(scalar(o, "radius"));
  } else if (shape == "capsule") {
    c.shape = physics::ShapeDesc::capsule(scalar(o, "halfHeight"), scalar(o, "radius"));
  } else if (shape == "mesh") {
    const json& vertices = field(o, "vertices");
    const json& indices = field(o, "indices");
    if (!vertices.is_array() || !indices.is_array()) throw BadScene("a mesh needs \"vertices\" and \"indices\" arrays");
    std::vector<glm::vec3> points;
    for (const json& v : vertices) {
      const auto a = numbers<3>(v, "vertices");
      points.push_back({a[0], a[1], a[2]});
    }
    std::vector<u32> triangles;
    for (const json& i : indices) {
      if (!i.is_number_unsigned()) throw BadScene("mesh indices must be whole numbers");
      triangles.push_back(i.get<u32>());
    }
    c.shape = physics::ShapeDesc::mesh(std::move(points), std::move(triangles));
  } else {
    throw BadScene(std::format("unknown collider shape \"{}\" (box, sphere, capsule or mesh)", shape));
  }
  return c;
}
CharacterController loadCharacter(const json& o) {
  CharacterController c;
  optional(o, "radius", c.radius, scalar);
  optional(o, "height", c.height, scalar);
  optional(o, "maxSlopeDegrees", c.maxSlopeDegrees, scalar);
  optional(o, "stepHeight", c.stepHeight, scalar);
  return c;
}

// One entity, parsed and checked, not yet in any world.
struct Record {
  std::optional<Name> name;
  std::optional<Transform> transform;
  std::optional<Transform3D> transform3d;
  std::optional<Velocity> velocity;
  std::optional<Sprite> sprite;
  std::optional<PlayerTag> player;
  std::optional<MeshRenderer> meshRenderer;
  std::optional<Material> material;
  std::optional<Light> light;
  std::optional<RigidBody> rigidBody;
  std::optional<Collider> collider;
  std::optional<CharacterController> character;
};

template <class T>
void addIf(ecs::World& world, ecs::Entity e, std::optional<T>& component) {
  if (component) world.add<T>(e, std::move(*component));
}

}  // namespace

std::string SceneSerializer::toJson(ecs::World& world) {
  json entities = json::array();
  for (const ecs::Entity e : world.entities()) {
    json out = json::object();
    if (const auto* c = world.tryGet<Name>(e)) out["Name"] = c->value;
    if (const auto* c = world.tryGet<Transform>(e)) out["Transform"] = save(*c);
    if (const auto* c = world.tryGet<Transform3D>(e)) out["Transform3D"] = save(*c);
    if (const auto* c = world.tryGet<Velocity>(e)) out["Velocity"] = save(*c);
    if (const auto* c = world.tryGet<Sprite>(e)) out["Sprite"] = save(*c);
    if (const auto* c = world.tryGet<PlayerTag>(e)) out["PlayerTag"] = save(*c);
    if (const auto* c = world.tryGet<MeshRenderer>(e)) out["MeshRenderer"] = save(*c);
    if (const auto* c = world.tryGet<Material>(e)) out["Material"] = save(*c);
    if (const auto* c = world.tryGet<Light>(e)) out["Light"] = save(*c);
    if (const auto* c = world.tryGet<RigidBody>(e)) out["RigidBody"] = save(*c);
    if (const auto* c = world.tryGet<Collider>(e)) out["Collider"] = save(*c);
    if (const auto* c = world.tryGet<CharacterController>(e)) out["CharacterController"] = save(*c);
    entities.push_back(std::move(out));
  }
  const json scene{{"format", kFormat}, {"version", kVersion}, {"entities", entities}};
  return scene.dump(2);
}

Result<usize> SceneSerializer::fromJson(ecs::World& world, std::string_view text) {
  const json scene = json::parse(text, nullptr, /*allow_exceptions=*/false);
  if (scene.is_discarded()) return fail(Errc::InvalidArgument, "scene: not valid JSON");
  if (!scene.is_object() || scene.value("format", std::string()) != kFormat) {
    return fail(Errc::InvalidArgument, "scene: not a scene file (no \"format\": \"{}\")", kFormat);
  }
  if (!scene.contains("version") || !scene["version"].is_number_integer() || scene["version"].get<int>() > kVersion) {
    return fail(Errc::Unsupported, "scene: version {} is newer than this program reads ({})",
                scene.value("version", json()).dump(), kVersion);
  }
  if (!scene.contains("entities") || !scene["entities"].is_array()) {
    return fail(Errc::InvalidArgument, "scene: missing the \"entities\" list");
  }

  // Everything parsed and checked before the world is touched.
  static const std::set<std::string> kKnown{"Name",      "Transform",    "Transform3D", "Velocity",
                                            "Sprite",    "PlayerTag",    "MeshRenderer", "Material",
                                            "Light",     "RigidBody",    "Collider",    "CharacterController"};
  std::vector<Record> records;
  std::set<std::string> skipped;
  usize index = 0;
  try {
    for (const json& e : scene["entities"]) {
      if (!e.is_object()) throw BadScene("an entity must be a JSON object");
      Record r;
      for (const auto& [key, value] : e.items()) {
        if (!kKnown.contains(key)) {
          skipped.insert(key);
          continue;
        }
        if (key == "Name") {
          if (!value.is_string()) throw BadScene("\"Name\" must be a string");
          r.name = Name{value.get<std::string>()};
          continue;
        }
        if (!value.is_object()) throw BadScene(std::format("\"{}\" must be a JSON object", key));
        if (key == "Transform") r.transform = loadTransform(value);
        else if (key == "Transform3D") r.transform3d = loadTransform3D(value);
        else if (key == "Velocity") r.velocity = loadVelocity(value);
        else if (key == "Sprite") r.sprite = loadSprite(value);
        else if (key == "PlayerTag") r.player = loadPlayerTag(value);
        else if (key == "MeshRenderer") r.meshRenderer = loadMeshRenderer(value);
        else if (key == "Material") r.material = loadMaterial(value);
        else if (key == "Light") r.light = loadLight(value);
        else if (key == "RigidBody") r.rigidBody = loadRigidBody(value);
        else if (key == "Collider") r.collider = loadCollider(value);
        else if (key == "CharacterController") r.character = loadCharacter(value);
      }
      records.push_back(std::move(r));
      ++index;
    }
  } catch (const BadScene& error) {
    return fail(Errc::InvalidArgument, "scene: entity {}: {}", index, error.what());
  } catch (const json::exception& error) {
    return fail(Errc::InvalidArgument, "scene: entity {}: {}", index, error.what());
  }
  for (const std::string& key : skipped) {
    log::warn(Category::Engine, "scene: skipped the unknown component \"{}\" (a newer file?)", key);
  }

  world.clear();
  for (Record& r : records) {
    const ecs::Entity e = world.create();
    addIf(world, e, r.name);
    addIf(world, e, r.transform);
    addIf(world, e, r.transform3d);
    addIf(world, e, r.velocity);
    addIf(world, e, r.sprite);
    addIf(world, e, r.player);
    addIf(world, e, r.meshRenderer);
    addIf(world, e, r.material);
    addIf(world, e, r.light);
    addIf(world, e, r.rigidBody);
    addIf(world, e, r.collider);
    addIf(world, e, r.character);
  }
  return records.size();
}

Status SceneSerializer::saveToFile(ecs::World& world, const std::filesystem::path& path) {
  const std::string text = toJson(world);
  // Written beside the target, then renamed over it: a crash mid-write never
  // leaves half a scene where the old one was.
  std::filesystem::path temporary = path;
  temporary += ".saving";
  {
    std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
    if (!out) return fail(Errc::IoFailure, "scene: could not write {}", temporary.string());
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!out) return fail(Errc::IoFailure, "scene: writing {} failed (disk full?)", temporary.string());
  }
  std::error_code error;
  std::filesystem::rename(temporary, path, error);
  if (error) return fail(Errc::IoFailure, "scene: could not replace {}: {}", path.string(), error.message());
  log::info(Category::Engine, "scene: saved {} entities to {} ({} bytes)", world.entityCount(), path.string(),
            text.size());
  return {};
}

Result<usize> SceneSerializer::loadFromFile(ecs::World& world, const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return fail(Errc::IoFailure, "scene: could not open {}", path.string());
  std::ostringstream text;
  text << in.rdbuf();
  auto loaded = fromJson(world, text.str());
  if (!loaded) return fail(loaded.error().code, "{} ({})", loaded.error().message, path.string());
  log::info(Category::Engine, "scene: loaded {} entities from {}", *loaded, path.string());
  return loaded;
}

}  // namespace playground::engine
