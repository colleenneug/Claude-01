// ============================================================
//  Movement.
//
//  Each of these turns an intent — "go this way", "fall", "spin" —
//  into a transform change, and each one owns exactly one movement
//  style. A pawn picks the one that matches how it should feel.
//
//  CharacterMovement is the interesting one: it sweeps a capsule
//  through the physics scene, slides along what it hits, walks up
//  stairs, and tracks whether it is standing on anything. That last
//  part is what a jump needs to be honest about.
// ============================================================
#pragma once

#include "forge/physics/Physics.hpp"
#include "forge/scene/Component.hpp"

namespace forge {

class MovementComponent : public ActorComponent {
    FORGE_OBJECT(MovementComponent, ActorComponent)
public:
    Vec3 velocity{0, 0, 0};
    float maxSpeed = 8.0f;

    void onRegister() override { tickEnabled = true; }

    // Gameplay calls this; the component decides what to do with it.
    void addInput(const Vec3& direction, float scale = 1.0f);
    Vec3 consumeInput();

protected:
    Vec3 pendingInput_{0, 0, 0};
};

// Walks, falls, jumps. The workhorse.
class CharacterMovementComponent : public MovementComponent {
    FORGE_OBJECT(CharacterMovementComponent, MovementComponent)
public:
    float acceleration = 60.0f;
    float braking = 50.0f;
    float airControl = 0.35f;
    float jumpSpeed = 9.0f;
    float gravityScale = 1.0f;
    float capsuleRadius = 0.4f;
    float capsuleHalfHeight = 0.9f;   // centre to cap centre, caps excluded
    float stepHeight = 0.45f;
    float maxSlopeDegrees = 50.0f;
    bool orientToMovement = false;
    float rotationSpeed = 12.0f;
    int maxJumps = 1;

    void tick(float dt) override;

    void jump();
    bool isGrounded() const { return grounded_; }
    bool isFalling() const { return !grounded_; }
    Vec3 groundNormal() const { return groundNormal_; }
    float speed() const { return velocity.flat().length(); }

private:
    bool grounded_ = false;
    Vec3 groundNormal_{0, 1, 0};
    int jumpsUsed_ = 0;
    bool jumpQueued_ = false;
    // Grace period after walking off a ledge in which a jump still
    // counts. Without it, jumping off the edge of a platform feels
    // broken even though the code is correct.
    float coyoteTime_ = 0.0f;
};

// Flies. No gravity, no ground: an editor-style free camera pawn, or a
// spectator.
class FloatingPawnMovementComponent : public MovementComponent {
    FORGE_OBJECT(FloatingPawnMovementComponent, MovementComponent)
public:
    float acceleration = 40.0f;
    float deceleration = 30.0f;
    bool collides = true;
    float radius = 0.4f;

    void tick(float dt) override;
};

// Travels in a straight line under gravity and reports what it hits.
class ProjectileMovementComponent : public MovementComponent {
    FORGE_OBJECT(ProjectileMovementComponent, MovementComponent)
public:
    float initialSpeed = 30.0f;
    float gravityScale = 1.0f;
    float bounciness = 0.0f;
    bool destroyOnHit = true;
    bool rotateToVelocity = true;
    float lifeSeconds = 8.0f;
    float radius = 0.15f;

    void beginPlay() override;
    void tick(float dt) override;

private:
    float age_ = 0.0f;
};

// Spins forever. Good for pickups, fans, rotating platforms.
class RotatingMovementComponent : public MovementComponent {
    FORGE_OBJECT(RotatingMovementComponent, MovementComponent)
public:
    Rotator rotationRate{0, 90, 0};   // degrees per second
    Vec3 bobAxis{0, 1, 0};
    float bobAmplitude = 0.0f;
    float bobSpeed = 1.0f;

    void onRegister() override;
    void tick(float dt) override;
    void editorTick(float dt) override;

private:
    void apply(float dt);
    float phase_ = 0.0f;
    Vec3 basePosition_{0, 0, 0};
    bool captured_ = false;
};

} // namespace forge
