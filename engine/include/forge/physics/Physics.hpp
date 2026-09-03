// ============================================================
//  Physics.
//
//  A small, honest collision system rather than a wrapped third-party
//  solver: a game engine that cannot tell you what you are standing on
//  is not an engine, and the constraint here is one build with no
//  package manager.
//
//  Shapes are oriented boxes, spheres and capsules. Every test happens
//  in the box's own space, which makes an oriented box no dearer than
//  an axis-aligned one — transform the query by the inverse rotation
//  and the box is axis-aligned again. Scale is baked into the extents
//  when a collider refreshes, so that transform stays rigid and
//  distances survive it.
//
//  Three consumers:
//    - characters, which sweep a capsule and slide
//    - rigid bodies, which integrate and bounce
//    - queries, which raycast for weapons, cameras and scripts
// ============================================================
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "forge/math/Math.hpp"

namespace forge {

class Actor;
class SceneComponent;
class World;

enum class CollisionShape { Box, Sphere, Capsule };
enum class CollisionResponse { None, Overlap, Block };
enum class Mobility { Static, Movable };

struct HitResult {
    bool hit = false;
    Actor* actor = nullptr;
    SceneComponent* component = nullptr;
    Vec3 point{0, 0, 0};
    Vec3 normal{0, 1, 0};
    float distance = 0.0f;
    explicit operator bool() const { return hit; }
};

// The result of sweeping a capsule: where it ended up, whether it is
// standing on something, and what it touched on the way.
struct MoveResult {
    Vec3 position{0, 0, 0};
    bool grounded = false;
    bool blocked = false;
    bool steppedUp = false;
    Vec3 groundNormal{0, 1, 0};
    Actor* groundActor = nullptr;
    std::vector<Actor*> touched;
};

struct SweepParams {
    float stepHeight = 0.0f;       // how high a ledge may be walked over
    float maxSlopeDegrees = 50.0f; // steeper than this is a wall, not a floor
    float groundProbe = 0.0f;      // extra downward probe to hold "grounded"
    std::vector<Actor*> ignore;
    std::function<bool(Actor*, SceneComponent*)> filter;
};

struct QueryParams {
    std::vector<Actor*> ignore;
    bool traceOverlaps = false;    // include Overlap-response colliders
    std::function<bool(Actor*, SceneComponent*)> filter;
};

class Collider {
public:
    SceneComponent* component = nullptr;
    Actor* actor = nullptr;
    CollisionShape shape = CollisionShape::Box;
    CollisionResponse response = CollisionResponse::Block;
    bool isStatic = true;
    bool enabled = true;

    // Local, pre-scale shape parameters. For a box these are half
    // extents; for a sphere, x is the radius; for a capsule, x is the
    // radius and y the half height including the caps.
    Vec3 localExtent{0.5f, 0.5f, 0.5f};

    // World placement, refreshed from the component's matrix. The
    // rotation is kept scale-free on purpose so it stays rigid.
    Vec3 position{0, 0, 0};
    Quat rotation{};
    Vec3 halfExtent{0.5f, 0.5f, 0.5f};
    float radius = 0.5f;
    float halfHeight = 0.0f;   // capsule: centre to cap centre
    Box bounds;

    void refresh();
    void updateBounds();

    Vec3 closestPoint(const Vec3& p) const;
    // Push a sphere out of this collider. Returns false when they miss.
    bool resolveSphere(const Vec3& p, float r, Vec3& outNormal, float& outDepth) const;
    bool raycast(const Ray& ray, float maxDist, float& outT, Vec3& outNormal) const;

    uint32_t visitMark = 0;
};

// A uniform grid on the ground plane. Levels are mostly horizontal, so
// a 2D grid gives nearly all the win of a 3D one for a third of the
// bookkeeping.
class BroadphaseGrid {
public:
    explicit BroadphaseGrid(float cellSize = 8.0f) : cell_(cellSize) {}
    void clear();
    void insert(Collider* c);
    void query(const Box& box, std::vector<Collider*>& out);

private:
    static int64_t key(int x, int z) { return ((int64_t)x << 32) ^ (uint32_t)z; }
    float cell_;
    std::unordered_map<int64_t, std::vector<Collider*>> buckets_;
    uint32_t mark_ = 0;
};

// A component that wants to be pushed around by the solver implements
// this. The character controller and the rigid-body component both do.
class IPhysicsBody {
public:
    virtual ~IPhysicsBody() = default;
    virtual void stepPhysics(float dt) = 0;
};

class PhysicsScene {
public:
    explicit PhysicsScene(World* world);
    ~PhysicsScene();

    Collider* addCollider(SceneComponent* comp);
    void removeCollider(SceneComponent* comp);
    Collider* colliderFor(SceneComponent* comp) const;
    // Re-read a collider's settings after an editor edit or a move.
    void refreshCollider(SceneComponent* comp);

    void addBody(IPhysicsBody* body);
    void removeBody(IPhysicsBody* body);

    void step(float dt);
    void markStaticsDirty() { staticsDirty_ = true; }

    // ---- queries ----
    HitResult raycast(const Vec3& origin, const Vec3& direction, float maxDistance,
                      const QueryParams& params = {});
    std::vector<Actor*> overlapSphere(const Vec3& centre, float radius, const QueryParams& params = {});
    bool capsuleOverlaps(const Vec3& centre, float radius, float halfHeight, const SweepParams& params);

    // Move a capsule by `delta`, sliding along whatever it hits.
    MoveResult moveCapsule(const Vec3& position, float radius, float halfHeight,
                           const Vec3& delta, const SweepParams& params);

    Vec3 gravity{0.0f, -22.0f, 0.0f};
    size_t colliderCount() const { return colliders_.size(); }

private:
    void rebuildStatics();
    std::vector<Collider*>& near(const Box& box);
    void updateOverlaps();
    bool collidersTouch(const Collider* a, const Collider* b) const;

    World* world_ = nullptr;
    std::vector<std::unique_ptr<Collider>> colliders_;
    std::unordered_map<const SceneComponent*, Collider*> byComponent_;
    std::vector<Collider*> statics_;
    std::vector<Collider*> movers_;
    std::vector<IPhysicsBody*> bodies_;
    BroadphaseGrid grid_{8.0f};
    bool staticsDirty_ = true;
    std::vector<Collider*> scratch_;

    // Overlap pairs from last step, diffed each frame so Begin and End
    // each fire exactly once.
    std::unordered_set<uint64_t> overlapPairs_;
    std::unordered_map<uint64_t, std::pair<Actor*, Actor*>> pairActors_;
};

} // namespace forge
