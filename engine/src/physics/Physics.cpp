#include "forge/physics/Physics.hpp"

#include <algorithm>

#include "forge/core/Log.hpp"
#include "forge/scene/Actor.hpp"
#include "forge/scene/World.hpp"

namespace forge {

// -----------------------------------------------------------------
//  Collider
// -----------------------------------------------------------------

void Collider::refresh() {
    if (!component) return;
    const Transform& t = component->worldTransform();
    position = t.position;
    rotation = t.rotation;

    const Vec3 s = absv(t.scale);
    switch (shape) {
        case CollisionShape::Box:
            halfExtent = maxv(localExtent * s, Vec3(1e-4f));
            break;
        case CollisionShape::Sphere:
            // A sphere cannot be squashed, so the largest axis wins and
            // the collider stays conservative rather than letting things
            // through the long side.
            radius = std::max(1e-4f, localExtent.x * maxComponent(s));
            break;
        case CollisionShape::Capsule:
            radius = std::max(1e-4f, localExtent.x * std::max(s.x, s.z));
            halfHeight = std::max(0.0f, localExtent.y * s.y - radius);
            break;
    }
    updateBounds();
}

void Collider::updateBounds() {
    if (shape == CollisionShape::Box) {
        // The AABB of an oriented box is its centre plus the absolute
        // rotation matrix times the half extents.
        Mat4 m = Mat4::rotation(rotation);
        Vec3 e{
            std::fabs(m.m[0]) * halfExtent.x + std::fabs(m.m[4]) * halfExtent.y + std::fabs(m.m[8])  * halfExtent.z,
            std::fabs(m.m[1]) * halfExtent.x + std::fabs(m.m[5]) * halfExtent.y + std::fabs(m.m[9])  * halfExtent.z,
            std::fabs(m.m[2]) * halfExtent.x + std::fabs(m.m[6]) * halfExtent.y + std::fabs(m.m[10]) * halfExtent.z};
        bounds = {position - e, position + e};
    } else {
        float hh = (shape == CollisionShape::Capsule) ? halfHeight : 0.0f;
        Vec3 e{radius, radius + hh, radius};
        bounds = {position - e, position + e};
    }
}

Vec3 Collider::closestPoint(const Vec3& p) const {
    switch (shape) {
        case CollisionShape::Sphere: {
            Vec3 d = p - position;
            float len = d.length();
            if (len < kSmall) return position;
            return position + d * (radius / len);
        }
        case CollisionShape::Capsule: {
            Vec3 axis = rotation * Vec3::Up;
            Vec3 a = position - axis * halfHeight, b = position + axis * halfHeight;
            Vec3 c = closestPointOnSegment(a, b, p);
            Vec3 d = p - c;
            float len = d.length();
            if (len < kSmall) return c;
            return c + d * (radius / len);
        }
        case CollisionShape::Box:
        default: {
            Vec3 local = rotation.inverse() * (p - position);
            local = clampv(local, -halfExtent, halfExtent);
            return position + rotation * local;
        }
    }
}

bool Collider::resolveSphere(const Vec3& p, float r, Vec3& outNormal, float& outDepth) const {
    if (shape == CollisionShape::Box) {
        Vec3 local = rotation.inverse() * (p - position);
        Vec3 clamped = clampv(local, -halfExtent, halfExtent);
        Vec3 delta = local - clamped;
        float d2 = delta.lengthSq();
        if (d2 > r * r) return false;

        if (d2 > 1e-10f) {
            float d = std::sqrt(d2);
            outNormal = rotation * (delta / d);
            outDepth = r - d;
        } else {
            // The centre is inside the box: escape along the nearest
            // face, which is the axis with the least distance to a wall.
            Vec3 pen = halfExtent - absv(local);
            Vec3 n;
            if (pen.x <= pen.y && pen.x <= pen.z) { n = {local.x < 0 ? -1.0f : 1.0f, 0, 0}; outDepth = pen.x + r; }
            else if (pen.y <= pen.z)              { n = {0, local.y < 0 ? -1.0f : 1.0f, 0}; outDepth = pen.y + r; }
            else                                   { n = {0, 0, local.z < 0 ? -1.0f : 1.0f}; outDepth = pen.z + r; }
            outNormal = rotation * n;
        }
        return true;
    }

    // Sphere and capsule both reduce to a point-to-segment test.
    Vec3 centre = position;
    if (shape == CollisionShape::Capsule) {
        Vec3 axis = rotation * Vec3::Up;
        centre = closestPointOnSegment(position - axis * halfHeight, position + axis * halfHeight, p);
    }
    Vec3 delta = p - centre;
    float d = delta.length();
    float sum = r + radius;
    if (d > sum) return false;
    outNormal = d < kSmall ? Vec3::Up : delta / d;
    outDepth = sum - d;
    return true;
}

bool Collider::raycast(const Ray& ray, float maxDist, float& outT, Vec3& outNormal) const {
    if (shape == CollisionShape::Box) {
        Quat inv = rotation.inverse();
        Ray local{inv * (ray.origin - position), inv * ray.direction};
        Box box{-halfExtent, halfExtent};
        Vec3 n;
        if (!rayBox(local, box, maxDist, outT, n)) return false;
        outNormal = rotation * n;
        return true;
    }
    if (shape == CollisionShape::Sphere) {
        if (!raySphere(ray, position, radius, maxDist, outT)) return false;
        outNormal = (ray.at(outT) - position).normalized();
        return true;
    }

    // Capsule: solve against the axis segment by sampling the two caps
    // and the centre. Exact enough for gameplay traces, and far cheaper
    // than the quartic the closed form needs.
    Vec3 axis = rotation * Vec3::Up;
    bool any = false;
    float best = maxDist;
    for (int i = -1; i <= 1; ++i) {
        Vec3 c = position + axis * (halfHeight * (float)i);
        float t;
        if (!raySphere(ray, c, radius, best, t)) continue;
        best = t;
        outT = t;
        outNormal = (ray.at(t) - c).normalized();
        any = true;
    }
    return any;
}

// -----------------------------------------------------------------
//  Broadphase
// -----------------------------------------------------------------

void BroadphaseGrid::clear() { buckets_.clear(); }

void BroadphaseGrid::insert(Collider* c) {
    int x0 = (int)std::floor(c->bounds.min.x / cell_), x1 = (int)std::floor(c->bounds.max.x / cell_);
    int z0 = (int)std::floor(c->bounds.min.z / cell_), z1 = (int)std::floor(c->bounds.max.z / cell_);
    // A collider spanning a huge area would be inserted into thousands of
    // cells and cost more than it saves; cap it and let the AABB test
    // catch the rest.
    const int kMaxSpan = 64;
    if (x1 - x0 > kMaxSpan) x1 = x0 + kMaxSpan;
    if (z1 - z0 > kMaxSpan) z1 = z0 + kMaxSpan;
    for (int x = x0; x <= x1; ++x)
        for (int z = z0; z <= z1; ++z)
            buckets_[key(x, z)].push_back(c);
}

void BroadphaseGrid::query(const Box& box, std::vector<Collider*>& out) {
    ++mark_;
    int x0 = (int)std::floor(box.min.x / cell_), x1 = (int)std::floor(box.max.x / cell_);
    int z0 = (int)std::floor(box.min.z / cell_), z1 = (int)std::floor(box.max.z / cell_);
    for (int x = x0; x <= x1; ++x) {
        for (int z = z0; z <= z1; ++z) {
            auto it = buckets_.find(key(x, z));
            if (it == buckets_.end()) continue;
            for (Collider* c : it->second) {
                // A visit stamp deduplicates without allocating a set;
                // this runs per character per substep.
                if (c->visitMark == mark_) continue;
                c->visitMark = mark_;
                if (c->enabled && c->bounds.intersects(box)) out.push_back(c);
            }
        }
    }
}

// -----------------------------------------------------------------
//  PhysicsScene
// -----------------------------------------------------------------

PhysicsScene::PhysicsScene(World* world) : world_(world) {}
PhysicsScene::~PhysicsScene() = default;

Collider* PhysicsScene::addCollider(SceneComponent* comp) {
    auto it = byComponent_.find(comp);
    if (it != byComponent_.end()) return it->second;

    auto owned = std::make_unique<Collider>();
    Collider* c = owned.get();
    c->component = comp;
    c->actor = comp->owner();
    colliders_.push_back(std::move(owned));
    byComponent_[comp] = c;
    return c;
}

void PhysicsScene::removeCollider(SceneComponent* comp) {
    auto it = byComponent_.find(comp);
    if (it == byComponent_.end()) return;
    Collider* c = it->second;

    // Any live overlap involving this collider must end, or the pair
    // would linger and never fire its End event.
    for (auto pit = pairActors_.begin(); pit != pairActors_.end();) {
        if (pit->second.first == c->actor || pit->second.second == c->actor) {
            overlapPairs_.erase(pit->first);
            pit = pairActors_.erase(pit);
        } else ++pit;
    }

    statics_.erase(std::remove(statics_.begin(), statics_.end(), c), statics_.end());
    movers_.erase(std::remove(movers_.begin(), movers_.end(), c), movers_.end());
    byComponent_.erase(it);
    colliders_.erase(std::remove_if(colliders_.begin(), colliders_.end(),
                                    [&](const std::unique_ptr<Collider>& p) { return p.get() == c; }),
                     colliders_.end());
    staticsDirty_ = true;
}

Collider* PhysicsScene::colliderFor(SceneComponent* comp) const {
    auto it = byComponent_.find(comp);
    return it == byComponent_.end() ? nullptr : it->second;
}

void PhysicsScene::refreshCollider(SceneComponent* comp) {
    Collider* c = colliderFor(comp);
    if (!c) return;
    c->enabled = c->response != CollisionResponse::None;
    c->refresh();

    const bool inStatics = std::find(statics_.begin(), statics_.end(), c) != statics_.end();
    const bool inMovers = std::find(movers_.begin(), movers_.end(), c) != movers_.end();
    if (c->isStatic && !inStatics) {
        movers_.erase(std::remove(movers_.begin(), movers_.end(), c), movers_.end());
        statics_.push_back(c);
        staticsDirty_ = true;
    } else if (!c->isStatic && !inMovers) {
        statics_.erase(std::remove(statics_.begin(), statics_.end(), c), statics_.end());
        movers_.push_back(c);
        staticsDirty_ = true;
    } else if (c->isStatic) {
        staticsDirty_ = true;
    }
}

void PhysicsScene::addBody(IPhysicsBody* body) {
    if (std::find(bodies_.begin(), bodies_.end(), body) == bodies_.end()) bodies_.push_back(body);
}

void PhysicsScene::removeBody(IPhysicsBody* body) {
    bodies_.erase(std::remove(bodies_.begin(), bodies_.end(), body), bodies_.end());
}

void PhysicsScene::rebuildStatics() {
    grid_.clear();
    for (Collider* c : statics_) {
        c->refresh();
        grid_.insert(c);
    }
    staticsDirty_ = false;
}

std::vector<Collider*>& PhysicsScene::near(const Box& box) {
    if (staticsDirty_) rebuildStatics();
    scratch_.clear();
    grid_.query(box, scratch_);
    // Movers are scanned linearly. There are rarely more than a few
    // dozen, and keeping them out of the grid means the grid never has
    // to be rebuilt mid-frame.
    for (Collider* c : movers_)
        if (c->enabled && c->bounds.intersects(box)) scratch_.push_back(c);
    return scratch_;
}

void PhysicsScene::step(float dt) {
    if (dt <= 0.0f) return;
    for (Collider* c : movers_) c->refresh();
    for (IPhysicsBody* b : bodies_) b->stepPhysics(dt);
    for (Collider* c : movers_) c->refresh();
    updateOverlaps();
}

// ---- queries ----

static bool ignored(const std::vector<Actor*>& list, Actor* a) {
    return !list.empty() && std::find(list.begin(), list.end(), a) != list.end();
}

HitResult PhysicsScene::raycast(const Vec3& origin, const Vec3& direction, float maxDistance,
                                const QueryParams& params) {
    HitResult out;
    Vec3 dir = direction.normalized();
    if (dir.isNearlyZero() || maxDistance <= 0.0f) return out;

    Box sweep;
    sweep.expand(origin);
    sweep.expand(origin + dir * maxDistance);
    sweep = sweep.grown(0.05f);

    std::vector<Collider*> list = near(sweep);
    float best = maxDistance;
    for (Collider* c : list) {
        if (!c->enabled || c->response == CollisionResponse::None) continue;
        if (!params.traceOverlaps && c->response == CollisionResponse::Overlap) continue;
        if (ignored(params.ignore, c->actor)) continue;
        if (params.filter && !params.filter(c->actor, c->component)) continue;

        float t; Vec3 n;
        if (!c->raycast(Ray{origin, dir}, best, t, n)) continue;
        best = t;
        out.hit = true;
        out.actor = c->actor;
        out.component = c->component;
        out.distance = t;
        out.point = origin + dir * t;
        out.normal = n;
    }
    return out;
}

std::vector<Actor*> PhysicsScene::overlapSphere(const Vec3& centre, float radius, const QueryParams& params) {
    std::vector<Actor*> out;
    Box box = Box::fromCenterExtents(centre, Vec3(radius));
    for (Collider* c : near(box)) {
        if (!c->enabled || c->response == CollisionResponse::None) continue;
        if (ignored(params.ignore, c->actor)) continue;
        if (params.filter && !params.filter(c->actor, c->component)) continue;
        Vec3 n; float d;
        if (!c->resolveSphere(centre, radius, n, d)) continue;
        if (std::find(out.begin(), out.end(), c->actor) == out.end()) out.push_back(c->actor);
    }
    return out;
}

bool PhysicsScene::capsuleOverlaps(const Vec3& centre, float radius, float halfHeight,
                                   const SweepParams& params) {
    Box box = Box::fromCenterExtents(centre, Vec3{radius, radius + halfHeight, radius});
    for (Collider* c : near(box)) {
        if (c->response != CollisionResponse::Block || !c->enabled) continue;
        if (ignored(params.ignore, c->actor)) continue;
        if (params.filter && !params.filter(c->actor, c->component)) continue;
        for (int k = -1; k <= 1; ++k) {
            if (halfHeight < 1e-4f && k != 0) continue;
            Vec3 p{centre.x, centre.y + halfHeight * (float)k, centre.z};
            Vec3 n; float d;
            if (c->resolveSphere(p, radius, n, d)) return true;
        }
    }
    return false;
}

MoveResult PhysicsScene::moveCapsule(const Vec3& start, float radius, float halfHeight,
                                     const Vec3& delta, const SweepParams& params) {
    MoveResult out;
    out.position = start;

    const float len = delta.length();
    // Substep so no single step exceeds half a radius: a fast body must
    // not tunnel through a thin wall between frames.
    const int steps = std::max(1, (int)std::ceil(len / std::max(0.01f, radius * 0.5f)));
    const Vec3 inc = delta / (float)steps;
    const float slopeCos = std::cos(radians(params.maxSlopeDegrees));

    for (int s = 0; s < steps; ++s) {
        const Vec3 before = out.position;
        out.position += inc;
        bool blockedThisStep = false;

        // Depenetrate. Four passes converge for the corner cases where
        // pushing out of one surface pushes into another.
        for (int iter = 0; iter < 4; ++iter) {
            Box box = Box::fromCenterExtents(out.position, Vec3{radius, radius + halfHeight, radius}).grown(0.02f);
            std::vector<Collider*> list = near(box);
            bool moved = false;

            for (Collider* c : list) {
                if (c->response != CollisionResponse::Block || !c->enabled) continue;
                if (ignored(params.ignore, c->actor)) continue;
                if (params.filter && !params.filter(c->actor, c->component)) continue;

                // A capsule is a segment of spheres; the two caps and the
                // middle catch everything a level throws at a character.
                for (int k = -1; k <= 1; ++k) {
                    if (halfHeight < 1e-4f && k != 0) continue;
                    Vec3 p{out.position.x, out.position.y + halfHeight * (float)k, out.position.z};
                    Vec3 n; float depth;
                    if (!c->resolveSphere(p, radius, n, depth)) continue;

                    out.position += n * (depth + 0.0005f);
                    moved = true;
                    if (n.y > slopeCos) {
                        out.grounded = true;
                        out.groundNormal = n;
                        out.groundActor = c->actor;
                    } else {
                        blockedThisStep = true;
                        out.blocked = true;
                    }
                    if (std::find(out.touched.begin(), out.touched.end(), c->actor) == out.touched.end())
                        out.touched.push_back(c->actor);
                }
            }
            if (!moved) break;
        }

        // Step-up: if a wall cancelled the move but is low enough to be a
        // stair, lift over it and keep going.
        if (params.stepHeight > 0.0f && blockedThisStep && !out.steppedUp) {
            Vec3 lifted = before + Vec3{inc.x, 0.0f, inc.z};
            lifted.y = before.y + params.stepHeight;
            if (!capsuleOverlaps(lifted, radius, halfHeight, params)) {
                out.position = lifted;
                out.steppedUp = true;
                out.blocked = false;
            }
        }
    }

    // A short probe under the capsule, so walking over a seam or a lip
    // does not flicker between grounded and airborne for a frame.
    if (!out.grounded && params.groundProbe > 0.0f) {
        QueryParams q;
        q.ignore = params.ignore;
        q.filter = params.filter;
        Vec3 from{out.position.x, out.position.y - halfHeight, out.position.z};
        HitResult h = raycast(from, Vec3::Down, radius + params.groundProbe, q);
        if (h.hit && h.normal.y > slopeCos) {
            out.grounded = true;
            out.groundNormal = h.normal;
            out.groundActor = h.actor;
        }
    }
    return out;
}

// ---- overlap events ----

bool PhysicsScene::collidersTouch(const Collider* a, const Collider* b) const {
    // Reduce whichever collider is simpler to a sphere (or a segment of
    // spheres) and let the other resolve it.
    Vec3 n; float d;
    if (a->shape == CollisionShape::Sphere) return b->resolveSphere(a->position, a->radius, n, d);
    if (b->shape == CollisionShape::Sphere) return a->resolveSphere(b->position, b->radius, n, d);
    if (a->shape == CollisionShape::Capsule) {
        Vec3 axis = a->rotation * Vec3::Up;
        for (int k = -1; k <= 1; ++k)
            if (b->resolveSphere(a->position + axis * (a->halfHeight * (float)k), a->radius, n, d)) return true;
        return false;
    }
    if (b->shape == CollisionShape::Capsule) return collidersTouch(b, a);
    // Box against box: the broadphase already proved the AABBs overlap,
    // and an exact SAT is not worth its cost for trigger volumes.
    return a->bounds.intersects(b->bounds);
}

void PhysicsScene::updateOverlaps() {
    std::unordered_set<uint64_t> seen;
    for (const auto& owned : colliders_) {
        Collider* c = owned.get();
        if (c->response != CollisionResponse::Overlap || !c->enabled) continue;
        if (!c->actor || c->actor->isPendingKill()) continue;

        for (Collider* other : near(c->bounds)) {
            if (other == c || other->actor == c->actor) continue;
            if (other->response == CollisionResponse::None || !other->enabled) continue;
            if (!other->actor || other->actor->isPendingKill()) continue;
            if (!collidersTouch(c, other)) continue;

            uint32_t a = c->actor->id(), b = other->actor->id();
            uint64_t pair = a < b ? ((uint64_t)a << 32) | b : ((uint64_t)b << 32) | a;
            seen.insert(pair);
            if (overlapPairs_.count(pair)) continue;

            overlapPairs_.insert(pair);
            pairActors_[pair] = {c->actor, other->actor};
            c->actor->onBeginOverlap.broadcast(other->actor);
            other->actor->onBeginOverlap.broadcast(c->actor);
        }
    }

    for (auto it = overlapPairs_.begin(); it != overlapPairs_.end();) {
        if (seen.count(*it)) { ++it; continue; }
        auto pit = pairActors_.find(*it);
        if (pit != pairActors_.end()) {
            Actor* a = pit->second.first;
            Actor* b = pit->second.second;
            if (a && !a->isPendingKill()) a->onEndOverlap.broadcast(b);
            if (b && !b->isPendingKill()) b->onEndOverlap.broadcast(a);
            pairActors_.erase(pit);
        }
        it = overlapPairs_.erase(it);
    }
}

} // namespace forge
