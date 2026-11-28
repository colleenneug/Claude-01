// =============================================================================
//  PhysicsWorld.cpp — physics::PhysicsWorld over Jolt Physics. The only file
//  in the engine that includes Jolt.
//
//  JOLT'S GLOBALS (its allocator hooks, the factory of shape types, the type
//  registry) are set up by the first PhysicsWorld and torn down by the last,
//  counted under a mutex, like ENet in net/src/Host.cpp. Jolt's asserts
//  (debug builds) and trace messages go to our log.
//
//  THE JOB BRIDGE: Jolt splits a step into jobs with dependencies and waits
//  on "barriers". JPH::JobSystemWithBarrier implements the barriers and the
//  dependency counting; we supply where jobs run: on playground::JobSystem's
//  workers. A barrier's waiter (the thread calling step()) also runs any
//  ready job of its own barrier itself, so a step never waits behind
//  unrelated work (asset decoding) on the workers.
// =============================================================================

// Jolt's own header comes FIRST, before anything else: it detects
// ThreadSanitizer with __has_feature, which spdlog (through our log header)
// defines as "always 0" on GCC. Included after it, Jolt would think TSan is
// off and use atomic fences that GCC refuses under -fsanitize=thread.
#include <Jolt/Jolt.h>

#include "playground/physics/PhysicsWorld.h"

#include "playground/core/JobSystem.h"
#include "playground/core/Log.h"

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/FixedSizeFreeList.h>
#include <Jolt/Core/IssueReporting.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/JobSystemWithBarrier.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <format>
#include <map>
#include <mutex>
#include <string>
#include <thread>

namespace playground::physics {
namespace {

using log::Category;

// --- Jolt's process-wide set-up, counted -------------------------------------

std::mutex gLibraryMutex;
int gLibraryUsers = 0;

void traceToLog(const char* format, ...) {
  std::array<char, 1024> text{};
  va_list args;
  va_start(args, format);
  std::vsnprintf(text.data(), text.size(), format, args);
  va_end(args);
  log::debug(Category::Engine, "jolt: {}", text.data());
}

#ifdef JPH_ENABLE_ASSERTS
bool assertToLog(const char* expression, const char* message, const char* file, JPH::uint line) {
  log::critical(Category::Engine, "jolt assertion failed: {} ({}) at {}:{}", expression,
                message != nullptr ? message : "", file, line);
  return true;  // stop here: a broken invariant inside the physics engine
}
#endif

void acquireLibrary() {
  const std::scoped_lock lock(gLibraryMutex);
  if (gLibraryUsers++ > 0) return;
  JPH::RegisterDefaultAllocator();
  JPH::Trace = traceToLog;
  JPH_IF_ENABLE_ASSERTS(JPH::AssertFailed = assertToLog;)
  JPH::Factory::sInstance = new JPH::Factory();
  JPH::RegisterTypes();
}

void releaseLibrary() noexcept {
  const std::scoped_lock lock(gLibraryMutex);
  if (--gLibraryUsers > 0) return;
  JPH::UnregisterTypes();
  delete JPH::Factory::sInstance;
  JPH::Factory::sInstance = nullptr;
}

// --- Conversions ---------------------------------------------------------------

JPH::Vec3 toJolt(glm::vec3 v) { return {v.x, v.y, v.z}; }
glm::vec3 toGlm(JPH::Vec3Arg v) { return {v.GetX(), v.GetY(), v.GetZ()}; }
JPH::Quat toJolt(glm::quat q) {
  const f32 length = glm::length(q);
  if (!(length > 1e-6f) || !std::isfinite(length)) return JPH::Quat::sIdentity();
  q /= length;
  return {q.x, q.y, q.z, q.w};
}
glm::quat toGlm(JPH::QuatArg q) { return {q.GetW(), q.GetX(), q.GetY(), q.GetZ()}; }

bool finite(glm::vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

// An entity in a body's 64-bit user data: generation above, index below. An
// invalid entity (index all ones) stays invalid.
JPH::uint64 pack(ecs::Entity e) { return (JPH::uint64{e.generation} << 32) | e.index; }
ecs::Entity unpack(JPH::uint64 data) {
  return {.index = static_cast<u32>(data & 0xFFFFFFFFu), .generation = static_cast<u32>(data >> 32)};
}

BodyId toId(JPH::BodyID id) { return {id.GetIndexAndSequenceNumber()}; }
JPH::BodyID toJolt(BodyId id) { return JPH::BodyID(id.value); }

// --- Layers ----------------------------------------------------------------------

constexpr JPH::ObjectLayer objectLayer(Layer layer) { return static_cast<JPH::ObjectLayer>(layer); }

namespace broadphase {
constexpr JPH::BroadPhaseLayer kNonMoving(0);
constexpr JPH::BroadPhaseLayer kMoving(1);
constexpr JPH::uint kCount = 2;
}  // namespace broadphase

// Which layers meet (symmetric). See the table in PhysicsWorld.h.
constexpr bool collides(JPH::ObjectLayer a, JPH::ObjectLayer b) {
  constexpr std::array<std::array<bool, kLayerCount>, kLayerCount> table{{
      //            Static Moving Debris
      /* Static */ {false, true, true},
      /* Moving */ {true, true, false},
      /* Debris */ {true, false, false},
  }};
  return a < kLayerCount && b < kLayerCount && table[a][b];
}

// Static bodies in one broad-phase tree (rebuilt rarely), the rest in another.
class BroadPhaseLayers final : public JPH::BroadPhaseLayerInterface {
 public:
  JPH::uint GetNumBroadPhaseLayers() const override { return broadphase::kCount; }
  JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override {
    return layer == objectLayer(Layer::Static) ? broadphase::kNonMoving : broadphase::kMoving;
  }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
  const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override {
    return layer == broadphase::kNonMoving ? "NonMoving" : "Moving";
  }
#endif
};

class ObjectVsBroadPhase final : public JPH::ObjectVsBroadPhaseLayerFilter {
 public:
  bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer tree) const override {
    if (tree == broadphase::kNonMoving) return collides(layer, objectLayer(Layer::Static));
    return collides(layer, objectLayer(Layer::Moving)) || collides(layer, objectLayer(Layer::Debris));
  }
};

class ObjectPairs final : public JPH::ObjectLayerPairFilter {
 public:
  bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override { return collides(a, b); }
};

// Queries: only the layers in a mask.
class MaskFilter final : public JPH::ObjectLayerFilter {
 public:
  explicit MaskFilter(LayerMask mask) : mask_(mask) {}
  bool ShouldCollide(JPH::ObjectLayer layer) const override { return layer < 32 && (mask_ >> layer & 1u) != 0; }

 private:
  LayerMask mask_;
};

// --- Shapes --------------------------------------------------------------------

Result<JPH::ShapeRefC> makeShape(const ShapeDesc& desc) {
  JPH::ShapeSettings::ShapeResult result;
  switch (desc.type) {
    case ShapeType::Box: {
      const glm::vec3 h = desc.halfExtents;
      if (!finite(h) || h.x <= 0.0f || h.y <= 0.0f || h.z <= 0.0f) {
        return fail(Errc::InvalidArgument, "physics: a box needs positive half extents, got ({}, {}, {})", h.x, h.y,
                    h.z);
      }
      // Rounded edges (the "convex radius") make collisions cheaper and
      // smoother; never more than half the thinnest side.
      const f32 rounding = std::min(JPH::cDefaultConvexRadius, 0.5f * std::min({h.x, h.y, h.z}));
      result = JPH::BoxShapeSettings(toJolt(h), rounding).Create();
      break;
    }
    case ShapeType::Sphere:
      if (!(desc.radius > 0.0f) || !std::isfinite(desc.radius)) {
        return fail(Errc::InvalidArgument, "physics: a sphere needs a positive radius, got {}", desc.radius);
      }
      result = JPH::SphereShapeSettings(desc.radius).Create();
      break;
    case ShapeType::Capsule:
      if (!(desc.radius > 0.0f) || !(desc.halfHeight > 0.0f) || !std::isfinite(desc.radius) ||
          !std::isfinite(desc.halfHeight)) {
        return fail(Errc::InvalidArgument, "physics: a capsule needs a positive radius and half height, got {} and {}",
                    desc.radius, desc.halfHeight);
      }
      result = JPH::CapsuleShapeSettings(desc.halfHeight, desc.radius).Create();
      break;
    case ShapeType::Mesh: {
      if (desc.indices.empty() || desc.indices.size() % 3 != 0) {
        return fail(Errc::InvalidArgument, "physics: a mesh needs whole triangles, got {} indices", desc.indices.size());
      }
      JPH::VertexList vertices;
      vertices.reserve(desc.vertices.size());
      for (const glm::vec3& v : desc.vertices) {
        if (!finite(v)) return fail(Errc::InvalidArgument, "physics: a mesh vertex is not a finite number");
        vertices.push_back(JPH::Float3(v.x, v.y, v.z));
      }
      JPH::IndexedTriangleList triangles;
      triangles.reserve(desc.indices.size() / 3);
      for (usize i = 0; i < desc.indices.size(); i += 3) {
        const u32 a = desc.indices[i];
        const u32 b = desc.indices[i + 1];
        const u32 c = desc.indices[i + 2];
        if (a >= vertices.size() || b >= vertices.size() || c >= vertices.size()) {
          return fail(Errc::InvalidArgument, "physics: mesh index out of range ({} vertices)", vertices.size());
        }
        triangles.push_back(JPH::IndexedTriangle(a, b, c));
      }
      result = JPH::MeshShapeSettings(std::move(vertices), std::move(triangles)).Create();
      break;
    }
  }
  if (result.HasError()) {
    return fail(Errc::InvalidArgument, "physics: the shape could not be built: {}", result.GetError().c_str());
  }
  return JPH::ShapeRefC(result.Get());
}

// --- The job bridge ------------------------------------------------------------

constexpr JPH::uint kMaxJobs = JPH::cMaxPhysicsJobs;
constexpr JPH::uint kMaxBarriers = JPH::cMaxPhysicsBarriers;

class JobBridge final : public JPH::JobSystemWithBarrier {
 public:
  explicit JobBridge(playground::JobSystem& jobs) : JPH::JobSystemWithBarrier(kMaxBarriers), jobs_(jobs) {
    jobPool_.Init(kMaxJobs, kMaxJobs);
  }
  JobBridge(const JobBridge&) = delete;
  JobBridge& operator=(const JobBridge&) = delete;
  ~JobBridge() override { pending_.wait(); }  // nothing of ours may still be queued on the workers

  int GetMaxConcurrency() const override { return static_cast<int>(jobs_.workerCount()) + 1; }

  JobHandle CreateJob(const char* name, JPH::ColorArg colour, const JobFunction& function,
                      JPH::uint32 dependencies) override {
    JPH::uint32 index;
    while ((index = jobPool_.ConstructObject(name, colour, this, function, dependencies)) ==
           JobPool::cInvalidObjectIndex) {
      // Every job slot in use: Jolt sizes cMaxPhysicsJobs so this does not happen; wait for one if it does.
      std::this_thread::yield();
    }
    Job* job = &jobPool_.Get(index);
    JobHandle handle(job);  // a reference: the job may run and finish before we return
    if (dependencies == 0) QueueJob(job);
    return handle;
  }

 protected:
  void QueueJob(Job* job) override {
    job->AddRef();  // the queued copy's reference
    jobs_.run(
        [job] {
          job->Execute();  // does nothing if the barrier's waiter already ran it
          job->Release();
        },
        pending_);
  }

  void QueueJobs(Job** jobs, JPH::uint count) override {
    for (JPH::uint i = 0; i < count; ++i) QueueJob(jobs[i]);
  }

  void FreeJob(Job* job) override { jobPool_.DestructObject(job); }

 private:
  using JobPool = JPH::FixedSizeFreeList<Job>;
  playground::JobSystem& jobs_;
  JobPool jobPool_;
  JobCounter pending_;
};

// New contacts, collected as Jolt finds them (on any of its threads).
class ContactRecorder final : public JPH::ContactListener {
 public:
  void OnContactAdded(const JPH::Body& a, const JPH::Body& b, const JPH::ContactManifold& manifold,
                      JPH::ContactSettings&) override {
    const JPH::RVec3 point = manifold.GetWorldSpaceContactPointOn1(0);
    const JPH::Vec3 relative = a.GetPointVelocity(point) - b.GetPointVelocity(point);
    const f32 speed = std::abs(relative.Dot(manifold.mWorldSpaceNormal));
    if (speed < minSpeed) return;
    const std::scoped_lock lock(mutex);
    if (events.size() >= PhysicsWorld::kMaxContactEvents) return;  // nobody is taking them: do not grow forever
    events.push_back({.a = unpack(a.GetUserData()), .b = unpack(b.GetUserData()), .bodyA = toId(a.GetID()),
                      .bodyB = toId(b.GetID()), .point = toGlm(JPH::Vec3(point)),
                      .normal = toGlm(manifold.mWorldSpaceNormal), .speed = speed});
  }

  f32 minSpeed = 0.5f;
  std::mutex mutex;
  std::vector<ContactEvent> events;
};

struct LibraryUse {
  LibraryUse() { acquireLibrary(); }
  ~LibraryUse() { releaseLibrary(); }
  LibraryUse(const LibraryUse&) = delete;
  LibraryUse& operator=(const LibraryUse&) = delete;
};

GroundState toGround(JPH::CharacterBase::EGroundState state) {
  switch (state) {
    case JPH::CharacterBase::EGroundState::OnGround: return GroundState::OnGround;
    case JPH::CharacterBase::EGroundState::OnSteepGround: return GroundState::OnSteepGround;
    case JPH::CharacterBase::EGroundState::NotSupported: return GroundState::NotSupported;
    case JPH::CharacterBase::EGroundState::InAir: return GroundState::InAir;
  }
  return GroundState::InAir;
}

}  // namespace

struct PhysicsWorld::Impl {
  // In construction order; destroyed in reverse (the library last).
  LibraryUse library;
  BroadPhaseLayers broadPhaseLayers;
  ObjectVsBroadPhase objectVsBroadPhase;
  ObjectPairs objectPairs;
  std::unique_ptr<JPH::TempAllocator> temp;
  std::unique_ptr<JPH::JobSystem> jobs;
  bool multithreaded = false;
  JPH::PhysicsSystem system;
  JPH::CharacterVsCharacterCollisionSimple characterVsCharacter;
  ContactRecorder contacts;

  struct Character {
    JPH::Ref<JPH::CharacterVirtual> character;
    CharacterDesc desc;
  };
  std::map<u32, Character> characters;  // ordered: characters move in the order they were added (determinism)
  u32 nextCharacter = 1;

  u32 addedSinceOptimise = 0;
  bool everOptimised = false;
  u64 steps = 0;
  u32 updateErrorsLogged = 0;

  ~Impl() {
    for (auto& [id, c] : characters) characterVsCharacter.Remove(c.character);
  }

  [[nodiscard]] JPH::BodyInterface& bodies() { return system.GetBodyInterface(); }
  [[nodiscard]] const JPH::BodyInterface& bodies() const { return system.GetBodyInterface(); }

  [[nodiscard]] bool has(BodyId id) const { return id.valid() && bodies().IsAdded(toJolt(id)); }

  [[nodiscard]] Character* find(CharacterId id) {
    const auto it = characters.find(id.value);
    return it == characters.end() ? nullptr : &it->second;
  }
  [[nodiscard]] const Character* find(CharacterId id) const {
    const auto it = characters.find(id.value);
    return it == characters.end() ? nullptr : &it->second;
  }

  [[nodiscard]] CharacterState stateOf(const JPH::CharacterVirtual& c) const {
    CharacterState state{.position = toGlm(JPH::Vec3(c.GetPosition())), .velocity = toGlm(c.GetLinearVelocity()),
                         .ground = toGround(c.GetGroundState()), .groundNormal = toGlm(c.GetGroundNormal())};
    const JPH::BodyID ground = c.GetGroundBodyID();
    if (!ground.IsInvalid() && bodies().IsAdded(ground)) state.groundEntity = unpack(bodies().GetUserData(ground));
    return state;
  }
};

Result<std::unique_ptr<PhysicsWorld>> PhysicsWorld::create(const PhysicsWorldDesc& desc, JobSystem* jobs) {
  if (desc.maxBodies == 0 || desc.maxBodyPairs == 0 || desc.maxContactConstraints == 0) {
    return fail(Errc::InvalidArgument, "physics: maxBodies, maxBodyPairs and maxContactConstraints must be positive");
  }
  if (!finite(desc.gravity)) return fail(Errc::InvalidArgument, "physics: gravity must be finite");
  auto impl = std::make_unique<Impl>();

  // Scratch memory for each step. Under AddressSanitizer, plain malloc, so
  // the sanitizer sees each allocation (a bump allocator would hide overruns).
#if defined(__SANITIZE_ADDRESS__)
  impl->temp = std::make_unique<JPH::TempAllocatorMalloc>();
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
  impl->temp = std::make_unique<JPH::TempAllocatorMalloc>();
#else
  impl->temp = std::make_unique<JPH::TempAllocatorImpl>(static_cast<JPH::uint>(desc.tempMemoryBytes));
#endif
#else
  impl->temp = std::make_unique<JPH::TempAllocatorImpl>(static_cast<JPH::uint>(desc.tempMemoryBytes));
#endif

  if (jobs != nullptr && jobs->workerCount() > 0) {
    impl->jobs = std::make_unique<JobBridge>(*jobs);
    impl->multithreaded = true;
  } else {
    impl->jobs = std::make_unique<JPH::JobSystemSingleThreaded>(kMaxJobs);
  }

  impl->system.Init(desc.maxBodies, 0, desc.maxBodyPairs, desc.maxContactConstraints, impl->broadPhaseLayers,
                    impl->objectVsBroadPhase, impl->objectPairs);
  impl->system.SetGravity(toJolt(desc.gravity));
  impl->contacts.minSpeed = desc.minContactSpeed;
  impl->system.SetContactListener(&impl->contacts);
  log::info(Category::Engine, "physics: Jolt {}.{}.{} ready (up to {} bodies, {}, gravity {:.2f} m/s²)",
            JPH_VERSION_MAJOR, JPH_VERSION_MINOR, JPH_VERSION_PATCH, desc.maxBodies,
            impl->multithreaded ? std::format("{} worker threads", jobs->workerCount()) : std::string("one thread"),
            glm::length(desc.gravity));
  return std::unique_ptr<PhysicsWorld>(new PhysicsWorld(std::move(impl)));
}

PhysicsWorld::PhysicsWorld(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}
PhysicsWorld::~PhysicsWorld() = default;

Result<BodyId> PhysicsWorld::addBody(const BodyDesc& desc, ecs::Entity entity) {
  if (desc.shape.type == ShapeType::Mesh && desc.motion == MotionType::Dynamic) {
    return fail(Errc::InvalidArgument, "physics: a mesh collider cannot be dynamic (it has no volume, so no mass); "
                "use a box, sphere or capsule, or make it static or kinematic");
  }
  if (!finite(desc.position) || !finite(desc.linearVelocity) || !finite(desc.angularVelocity)) {
    return fail(Errc::InvalidArgument, "physics: a body's position and velocities must be finite");
  }
  auto shape = makeShape(desc.shape);
  if (!shape) return tl::unexpected(std::move(shape.error()));

  const JPH::EMotionType motion = desc.motion == MotionType::Static      ? JPH::EMotionType::Static
                                  : desc.motion == MotionType::Kinematic ? JPH::EMotionType::Kinematic
                                                                         : JPH::EMotionType::Dynamic;
  const Layer layer = desc.layer.value_or(desc.motion == MotionType::Static ? Layer::Static : Layer::Moving);
  JPH::BodyCreationSettings settings(*shape, JPH::RVec3(toJolt(desc.position)), toJolt(desc.rotation), motion,
                                     objectLayer(layer));
  settings.mFriction = desc.friction;
  settings.mRestitution = desc.restitution;
  settings.mLinearDamping = desc.linearDamping;
  settings.mAngularDamping = desc.angularDamping;
  settings.mGravityFactor = desc.gravityFactor;
  settings.mLinearVelocity = toJolt(desc.linearVelocity);
  settings.mAngularVelocity = toJolt(desc.angularVelocity);
  settings.mUserData = pack(entity);
  if (desc.motion == MotionType::Dynamic && desc.mass > 0.0f) {
    settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
    settings.mMassPropertiesOverride.mMass = desc.mass;
  }

  JPH::Body* body = impl_->bodies().CreateBody(settings);
  if (body == nullptr) {
    return fail(Errc::InvalidState, "physics: no room for another body (maxBodies {} reached)",
                impl_->system.GetMaxBodies());
  }
  impl_->bodies().AddBody(body->GetID(), desc.motion == MotionType::Static ? JPH::EActivation::DontActivate
                                                                           : JPH::EActivation::Activate);
  ++impl_->addedSinceOptimise;
  return toId(body->GetID());
}

void PhysicsWorld::removeBody(BodyId body) {
  if (!impl_->has(body)) return;
  impl_->bodies().RemoveBody(toJolt(body));
  impl_->bodies().DestroyBody(toJolt(body));
}

bool PhysicsWorld::contains(BodyId body) const { return impl_->has(body); }

std::optional<BodyState> PhysicsWorld::bodyState(BodyId body) const {
  if (!impl_->has(body)) return std::nullopt;
  const JPH::BodyInterface& bodies = impl_->bodies();
  JPH::RVec3 position;
  JPH::Quat rotation;
  bodies.GetPositionAndRotation(toJolt(body), position, rotation);
  return BodyState{.position = toGlm(JPH::Vec3(position)), .rotation = toGlm(rotation),
                   .linearVelocity = toGlm(bodies.GetLinearVelocity(toJolt(body))),
                   .angularVelocity = toGlm(bodies.GetAngularVelocity(toJolt(body))),
                   .active = bodies.IsActive(toJolt(body))};
}

ecs::Entity PhysicsWorld::entityOf(BodyId body) const {
  if (!impl_->has(body)) return {};
  return unpack(impl_->bodies().GetUserData(toJolt(body)));
}

void PhysicsWorld::setTransform(BodyId body, glm::vec3 position, glm::quat rotation) {
  if (!impl_->has(body) || !finite(position)) return;
  const bool isStatic = impl_->bodies().GetMotionType(toJolt(body)) == JPH::EMotionType::Static;
  impl_->bodies().SetPositionAndRotation(toJolt(body), JPH::RVec3(toJolt(position)), toJolt(rotation),
                                         isStatic ? JPH::EActivation::DontActivate : JPH::EActivation::Activate);
}

void PhysicsWorld::moveKinematic(BodyId body, glm::vec3 position, glm::quat rotation, f32 dt) {
  if (!impl_->has(body) || !finite(position) || !(dt > 0.0f)) return;
  impl_->bodies().MoveKinematic(toJolt(body), JPH::RVec3(toJolt(position)), toJolt(rotation), dt);
}

void PhysicsWorld::setVelocity(BodyId body, glm::vec3 linear, glm::vec3 angular) {
  if (!impl_->has(body) || !finite(linear) || !finite(angular)) return;
  impl_->bodies().SetLinearAndAngularVelocity(toJolt(body), toJolt(linear), toJolt(angular));
}

void PhysicsWorld::addImpulse(BodyId body, glm::vec3 impulse, glm::vec3 atPoint) {
  if (!impl_->has(body) || !finite(impulse) || !finite(atPoint)) return;
  if (impl_->bodies().GetMotionType(toJolt(body)) != JPH::EMotionType::Dynamic) return;  // only dynamic bodies are pushed
  impl_->bodies().AddImpulse(toJolt(body), toJolt(impulse), JPH::RVec3(toJolt(atPoint)));
}

Result<CharacterId> PhysicsWorld::addCharacter(const CharacterDesc& desc, ecs::Entity entity) {
  if (!(desc.radius > 0.0f) || !(desc.height > 2.0f * desc.radius) || !finite(desc.position)) {
    return fail(Errc::InvalidArgument, "physics: a character needs a positive radius and a height over twice it "
                "(got radius {}, height {})", desc.radius, desc.height);
  }
  // A capsule standing on the character's position (its feet).
  const f32 halfStraight = 0.5f * desc.height - desc.radius;
  auto shape = JPH::RotatedTranslatedShapeSettings(JPH::Vec3(0.0f, 0.5f * desc.height, 0.0f), JPH::Quat::sIdentity(),
                                                   new JPH::CapsuleShape(halfStraight, desc.radius))
                   .Create();
  if (shape.HasError()) {
    return fail(Errc::InvalidArgument, "physics: the character's shape could not be built: {}",
                shape.GetError().c_str());
  }
  JPH::Ref<JPH::CharacterVirtualSettings> settings = new JPH::CharacterVirtualSettings();
  settings->mShape = shape.Get();
  settings->mMaxSlopeAngle = JPH::DegreesToRadians(desc.maxSlopeDegrees);
  settings->mMass = desc.mass;
  settings->mMaxStrength = desc.maxStrength;
  // Only contacts with the lower sphere count as ground (not a wall at the waist).
  settings->mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisY(), -desc.radius);

  const u32 id = impl_->nextCharacter++;
  JPH::Ref<JPH::CharacterVirtual> character =
      new JPH::CharacterVirtual(settings, JPH::RVec3(toJolt(desc.position)), JPH::Quat::sIdentity(), pack(entity),
                                &impl_->system);
  character->SetCharacterVsCharacterCollision(&impl_->characterVsCharacter);
  impl_->characterVsCharacter.Add(character);
  impl_->characters.emplace(id, Impl::Character{.character = character, .desc = desc});
  return CharacterId{id};
}

void PhysicsWorld::removeCharacter(CharacterId character) {
  const auto it = impl_->characters.find(character.value);
  if (it == impl_->characters.end()) return;
  impl_->characterVsCharacter.Remove(it->second.character);
  impl_->characters.erase(it);
}

CharacterState PhysicsWorld::moveCharacter(CharacterId id, const CharacterMove& move, f32 dt) {
  Impl::Character* entry = impl_->find(id);
  if (entry == nullptr || !(dt > 0.0f)) return {};
  JPH::CharacterVirtual& c = *entry->character;
  const JPH::Vec3 up = c.GetUp();
  const JPH::Vec3 gravity = impl_->system.GetGravity();

  // The velocity for this step (the recipe from Jolt's character samples):
  // on the ground, move with the ground (a lift, a platform) and maybe
  // jump; in the air, keep falling. Gravity always; the wanted horizontal
  // velocity always (full air control: it is a game, not a simulation).
  c.UpdateGroundVelocity();
  const JPH::Vec3 velocity = c.GetLinearVelocity();
  const JPH::Vec3 vertical = velocity.Dot(up) * up;
  const JPH::Vec3 groundVelocity = c.GetGroundVelocity();
  const bool towardsGround = (vertical - groundVelocity).Dot(up) < 0.1f;
  JPH::Vec3 next;
  if (c.GetGroundState() == JPH::CharacterBase::EGroundState::OnGround && towardsGround) {
    next = groundVelocity;
    if (move.jumpSpeed > 0.0f) next += move.jumpSpeed * up;
  } else {
    next = vertical;
  }
  next += gravity * dt;
  const glm::vec3 wanted = finite(move.velocity) ? glm::vec3(move.velocity.x, 0.0f, move.velocity.z) : glm::vec3(0.0f);
  next += toJolt(wanted);
  c.SetLinearVelocity(next);

  JPH::CharacterVirtual::ExtendedUpdateSettings update;
  update.mStickToFloorStepDown = JPH::Vec3(0.0f, -entry->desc.stickDownDistance, 0.0f);
  update.mWalkStairsStepUp = JPH::Vec3(0.0f, entry->desc.stepHeight, 0.0f);
  const JPH::ObjectLayer layer = objectLayer(Layer::Moving);
  c.ExtendedUpdate(dt, gravity, update, impl_->system.GetDefaultBroadPhaseLayerFilter(layer),
                   impl_->system.GetDefaultLayerFilter(layer), {}, {}, *impl_->temp);
  return impl_->stateOf(c);
}

std::optional<CharacterState> PhysicsWorld::characterState(CharacterId id) const {
  const Impl::Character* entry = impl_->find(id);
  if (entry == nullptr) return std::nullopt;
  return impl_->stateOf(*entry->character);
}

void PhysicsWorld::setCharacterPosition(CharacterId id, glm::vec3 feet) {
  Impl::Character* entry = impl_->find(id);
  if (entry == nullptr || !finite(feet)) return;
  entry->character->SetPosition(JPH::RVec3(toJolt(feet)));
  entry->character->SetLinearVelocity(JPH::Vec3::sZero());
}

void PhysicsWorld::step(f32 dt) {
  if (!(dt > 0.0f)) return;
  // The broad phase is a tree of bounding boxes; after many bodies are
  // added at once, rebuilding it makes queries and collisions faster.
  if (!impl_->everOptimised || impl_->addedSinceOptimise >= 64) {
    impl_->system.OptimizeBroadPhase();
    impl_->everOptimised = true;
    impl_->addedSinceOptimise = 0;
  }
  const JPH::EPhysicsUpdateError error = impl_->system.Update(dt, 1, impl_->temp.get(), impl_->jobs.get());
  ++impl_->steps;
  if (error != JPH::EPhysicsUpdateError::None && impl_->updateErrorsLogged < 10) {
    ++impl_->updateErrorsLogged;  // each step would repeat it: the first few are enough
    log::warn(Category::Engine, "physics: step {} ran out of room (error flags {:#x}): raise maxBodyPairs or "
              "maxContactConstraints; some collisions were skipped", impl_->steps, static_cast<u32>(error));
  }
}

std::optional<RaycastHit> PhysicsWorld::raycast(glm::vec3 origin, glm::vec3 direction, f32 maxDistance,
                                                LayerMask mask) const {
  const f32 length = glm::length(direction);
  if (!finite(origin) || !(length > 1e-9f) || !std::isfinite(length) || !(maxDistance > 0.0f)) return std::nullopt;
  const glm::vec3 unit = direction / length;
  const JPH::RRayCast ray(JPH::RVec3(toJolt(origin)), toJolt(unit * maxDistance));
  JPH::RayCastResult result;
  const MaskFilter layers(mask);
  if (!impl_->system.GetNarrowPhaseQuery().CastRay(ray, result, {}, layers)) return std::nullopt;

  const JPH::RVec3 point = ray.GetPointOnRay(result.mFraction);
  RaycastHit hit{.body = toId(result.mBodyID), .point = toGlm(JPH::Vec3(point)),
                 .distance = result.mFraction * maxDistance};
  const JPH::BodyLockRead lock(impl_->system.GetBodyLockInterface(), result.mBodyID);
  if (lock.Succeeded()) {
    const JPH::Body& body = lock.GetBody();
    hit.entity = unpack(body.GetUserData());
    hit.normal = toGlm(body.GetWorldSpaceSurfaceNormal(result.mSubShapeID2, point));
    if (glm::dot(hit.normal, unit) > 0.0f) hit.normal = -hit.normal;  // a back face (inside a mesh): face the ray
  }
  return hit;
}

glm::vec3 PhysicsWorld::gravity() const noexcept { return toGlm(impl_->system.GetGravity()); }

std::vector<ContactEvent> PhysicsWorld::takeContactEvents() {
  std::vector<ContactEvent> events;
  {
    const std::scoped_lock lock(impl_->contacts.mutex);
    events.swap(impl_->contacts.events);
  }
  // Jolt's threads find contacts in any order: sort, so the result does not depend on them.
  std::sort(events.begin(), events.end(), [](const ContactEvent& x, const ContactEvent& y) {
    return x.bodyA.value != y.bodyA.value ? x.bodyA.value < y.bodyA.value : x.bodyB.value < y.bodyB.value;
  });
  return events;
}

PhysicsStats PhysicsWorld::stats() const {
  return {.steps = impl_->steps, .bodies = impl_->system.GetNumBodies(),
          .activeBodies = impl_->system.GetNumActiveBodies(JPH::EBodyType::RigidBody),
          .characters = impl_->characters.size()};
}

bool PhysicsWorld::multithreaded() const noexcept { return impl_->multithreaded; }

}  // namespace playground::physics
