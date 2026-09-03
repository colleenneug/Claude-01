#include "forge/components/MovementComponents.hpp"

#include "forge/core/Log.hpp"
#include "forge/scene/Actor.hpp"
#include "forge/scene/World.hpp"

namespace forge {

void MovementComponent::addInput(const Vec3& direction, float scale) {
    pendingInput_ += direction * scale;
}

Vec3 MovementComponent::consumeInput() {
    Vec3 v = pendingInput_;
    pendingInput_ = Vec3::Zero;
    // Clamping the length rather than each axis stops diagonal input
    // from being faster than cardinal input.
    return clampLength(v, 1.0f);
}

FORGE_CLASS_BEGIN(MovementComponent)
    FORGE_DISPLAY("Movement")
    FORGE_CATEGORY("Component")
    FORGE_PROP(maxSpeed).range(0.0f, 100.0f).cat("Movement")
    FORGE_PROP(velocity).cat("Movement").transient()
FORGE_CLASS_END()
FORGE_REGISTER(MovementComponent)

// -----------------------------------------------------------------
//  CharacterMovementComponent
// -----------------------------------------------------------------

void CharacterMovementComponent::jump() { jumpQueued_ = true; }

void CharacterMovementComponent::tick(float dt) {
    Actor* a = owner();
    World* w = world();
    if (!a || !w || dt <= 0.0f) return;

    const Vec3 input = consumeInput();
    const Vec3 gravity = w->physics().gravity * gravityScale;

    // Horizontal: accelerate toward the input direction, brake toward a
    // stop when there is none. Air control scales both, so a jump keeps
    // most of the momentum it left the ground with.
    const float control = grounded_ ? 1.0f : airControl;
    Vec3 horizontal = velocity.flat();
    const Vec3 wish = input.flat() * maxSpeed;

    if (wish.lengthSq() > 1e-6f) {
        horizontal += (wish - horizontal).normalized() * (acceleration * control * dt);
        if (horizontal.lengthSq() > sqr(maxSpeed)) horizontal = horizontal.normalized() * maxSpeed;
    } else if (grounded_) {
        const float drop = braking * dt;
        const float len = horizontal.length();
        horizontal = len <= drop ? Vec3::Zero : horizontal * ((len - drop) / len);
    }

    velocity.x = horizontal.x;
    velocity.z = horizontal.z;

    // Vertical.
    if (grounded_ && velocity.y < 0.0f) {
        // Keep a small downward bias while grounded so the sweep stays in
        // contact walking down a slope instead of stepping off it.
        velocity.y = -2.0f;
        jumpsUsed_ = 0;
        coyoteTime_ = 0.12f;
    } else {
        velocity.y += gravity.y * dt;
        coyoteTime_ = std::max(0.0f, coyoteTime_ - dt);
    }

    if (jumpQueued_) {
        const bool canCoyote = coyoteTime_ > 0.0f && jumpsUsed_ == 0;
        if (grounded_ || canCoyote || jumpsUsed_ < maxJumps) {
            velocity.y = jumpSpeed;
            jumpsUsed_ = std::max(1, jumpsUsed_ + 1);
            grounded_ = false;
            coyoteTime_ = 0.0f;
        }
        jumpQueued_ = false;
    }

    SweepParams sweep;
    sweep.stepHeight = stepHeight;
    sweep.maxSlopeDegrees = maxSlopeDegrees;
    sweep.groundProbe = grounded_ ? 0.15f : 0.0f;
    sweep.ignore.push_back(a);

    const Vec3 start = a->transform().position;
    MoveResult result = w->physics().moveCapsule(start, capsuleRadius, capsuleHalfHeight,
                                                 velocity * dt, sweep);
    a->setLocation(result.position);

    const bool wasGrounded = grounded_;
    grounded_ = result.grounded;
    groundNormal_ = result.groundNormal;

    // Cancel the velocity component that went into whatever was hit, or
    // it accumulates against a wall and launches on release.
    if (result.grounded && velocity.y < 0.0f) velocity.y = 0.0f;
    if (result.blocked) {
        const Vec3 actual = result.position - start;
        Vec3 flat = velocity.flat();
        if (actual.flat().lengthSq() < sqr(dt) * flat.lengthSq() * 0.25f) {
            velocity.x *= 0.5f;
            velocity.z *= 0.5f;
        }
    }
    if (!wasGrounded && grounded_) jumpsUsed_ = 0;

    if (orientToMovement) {
        const Vec3 flat = velocity.flat();
        if (flat.lengthSq() > 0.25f) {
            const Rotator target = Rotator::fromDirection(flat.normalized());
            Rotator current = a->rotation();
            current.yaw = Rotator::normalizeAngle(
                current.yaw + Rotator::delta(current, target).yaw * saturate(rotationSpeed * dt));
            a->setRotation(current);
        }
    }
}

FORGE_CLASS_BEGIN(CharacterMovementComponent)
    FORGE_DISPLAY("Character Movement")
    FORGE_PROP(acceleration).range(1.0f, 300.0f).cat("Walking")
    FORGE_PROP(braking).range(0.0f, 300.0f).cat("Walking")
    FORGE_PROP(airControl).range(0.0f, 1.0f).cat("Falling")
    FORGE_PROP(jumpSpeed).range(0.0f, 40.0f).cat("Jumping")
    FORGE_PROP(maxJumps).range(0.0f, 5.0f).cat("Jumping")
        .tooltip("Two gives a double jump.")
    FORGE_PROP(gravityScale).range(0.0f, 5.0f).cat("Falling")
    FORGE_PROP(capsuleRadius).range(0.05f, 3.0f).cat("Capsule")
    FORGE_PROP(capsuleHalfHeight).range(0.0f, 5.0f).cat("Capsule")
    FORGE_PROP(stepHeight).range(0.0f, 2.0f).cat("Walking")
        .tooltip("Ledges no taller than this are walked over rather than blocking.")
    FORGE_PROP(maxSlopeDegrees).range(5.0f, 89.0f).cat("Walking")
        .tooltip("Steeper than this counts as a wall, not a floor.")
    FORGE_PROP(orientToMovement).cat("Rotation")
    FORGE_PROP(rotationSpeed).range(1.0f, 40.0f).cat("Rotation")
FORGE_CLASS_END()
FORGE_REGISTER(CharacterMovementComponent)

// -----------------------------------------------------------------
//  FloatingPawnMovementComponent
// -----------------------------------------------------------------

void FloatingPawnMovementComponent::tick(float dt) {
    Actor* a = owner();
    if (!a || dt <= 0.0f) return;

    const Vec3 input = consumeInput();
    if (input.lengthSq() > 1e-6f) {
        velocity += input * (acceleration * dt);
        velocity = clampLength(velocity, maxSpeed);
    } else {
        const float drop = deceleration * dt;
        const float len = velocity.length();
        velocity = len <= drop ? Vec3::Zero : velocity * ((len - drop) / len);
    }

    const Vec3 delta = velocity * dt;
    if (!collides || !world()) { a->addOffset(delta); return; }

    SweepParams sweep;
    sweep.maxSlopeDegrees = 89.0f;   // a flyer treats nothing as floor
    sweep.ignore.push_back(a);
    MoveResult r = world()->physics().moveCapsule(a->transform().position, radius, 0.0f, delta, sweep);
    a->setLocation(r.position);
    if (r.blocked || r.grounded) {
        // Keep only the part of the velocity that survived the move, so
        // pressing into a wall does not build up speed.
        velocity = projectOnPlane(velocity, r.groundNormal);
    }
}

FORGE_CLASS_BEGIN(FloatingPawnMovementComponent)
    FORGE_DISPLAY("Floating Movement")
    FORGE_PROP(acceleration).range(1.0f, 300.0f).cat("Movement")
    FORGE_PROP(deceleration).range(0.0f, 300.0f).cat("Movement")
    FORGE_PROP(collides).cat("Movement")
    FORGE_PROP(radius).range(0.05f, 3.0f).cat("Movement")
FORGE_CLASS_END()
FORGE_REGISTER(FloatingPawnMovementComponent)

// -----------------------------------------------------------------
//  ProjectileMovementComponent
// -----------------------------------------------------------------

void ProjectileMovementComponent::beginPlay() {
    // Fired along the actor's facing, which is what a muzzle transform
    // already encodes.
    if (Actor* a = owner()) velocity = a->forward() * initialSpeed;
    age_ = 0.0f;
}

void ProjectileMovementComponent::tick(float dt) {
    Actor* a = owner();
    World* w = world();
    if (!a || !w || dt <= 0.0f) return;

    age_ += dt;
    if (lifeSeconds > 0.0f && age_ >= lifeSeconds) { a->destroy(); return; }

    velocity += w->physics().gravity * (gravityScale * dt);

    const Vec3 start = a->transform().position;
    const Vec3 delta = velocity * dt;
    const float travel = delta.length();

    if (travel > 1e-5f) {
        QueryParams q;
        q.ignore.push_back(a);
        HitResult h = w->physics().raycast(start, delta / travel, travel + radius, q);
        if (h.hit) {
            a->setLocation(h.point + h.normal * radius);
            a->onHit.broadcast(h);
            if (h.actor) h.actor->takeDamage(0.0f, a);
            if (destroyOnHit && bounciness <= 0.0f) { a->destroy(); return; }
            velocity = reflect(velocity, h.normal) * bounciness;
            if (velocity.lengthSq() < 0.25f && destroyOnHit) { a->destroy(); return; }
        } else {
            a->addOffset(delta);
        }
    }

    if (rotateToVelocity && velocity.lengthSq() > 1e-4f)
        a->setRotation(Rotator::fromDirection(velocity.normalized()));
}

FORGE_CLASS_BEGIN(ProjectileMovementComponent)
    FORGE_DISPLAY("Projectile Movement")
    FORGE_PROP(initialSpeed).range(0.0f, 300.0f).cat("Projectile")
    FORGE_PROP(gravityScale).range(0.0f, 5.0f).cat("Projectile")
    FORGE_PROP(bounciness).range(0.0f, 1.0f).cat("Projectile")
        .tooltip("Zero stops on impact; higher values bounce.")
    FORGE_PROP(destroyOnHit).cat("Projectile")
    FORGE_PROP(rotateToVelocity).cat("Projectile")
    FORGE_PROP(lifeSeconds).range(0.0f, 60.0f).cat("Projectile")
    FORGE_PROP(radius).range(0.01f, 2.0f).cat("Projectile")
FORGE_CLASS_END()
FORGE_REGISTER(ProjectileMovementComponent)

// -----------------------------------------------------------------
//  RotatingMovementComponent
// -----------------------------------------------------------------

void RotatingMovementComponent::onRegister() { tickEnabled = true; }

void RotatingMovementComponent::apply(float dt) {
    Actor* a = owner();
    if (!a || !a->root()) return;

    if (!captured_) {
        // Remember where the actor was placed, so the bob oscillates
        // about the level's position rather than drifting from it.
        basePosition_ = a->location();
        captured_ = true;
    }

    a->addRotation(rotationRate * dt);

    if (bobAmplitude > 0.0f) {
        phase_ += dt * bobSpeed;
        const Vec3 axis = bobAxis.isNearlyZero() ? Vec3::Up : bobAxis.normalized();
        a->setLocation(basePosition_ + axis * (std::sin(phase_ * kTwoPi) * bobAmplitude));
    }
}

void RotatingMovementComponent::tick(float dt) { apply(dt); }

// Also runs in the editor, so a spinning pickup previews without
// pressing Play.
void RotatingMovementComponent::editorTick(float dt) { apply(dt); }

FORGE_CLASS_BEGIN(RotatingMovementComponent)
    FORGE_DISPLAY("Rotating Movement")
    FORGE_PROP(rotationRate).cat("Rotation").tooltip("Degrees per second.")
    FORGE_PROP(bobAxis).cat("Bob")
    FORGE_PROP(bobAmplitude).range(0.0f, 10.0f).cat("Bob")
    FORGE_PROP(bobSpeed).range(0.0f, 10.0f).cat("Bob")
FORGE_CLASS_END()
FORGE_REGISTER(RotatingMovementComponent)

} // namespace forge
