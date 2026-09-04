#include "forge/gameplay/GameFramework.hpp"

#include "forge/core/Log.hpp"
#include "forge/scene/World.hpp"

namespace forge {

// ---------------------------------------------------------------
//  Pawn
// ---------------------------------------------------------------

Pawn::Pawn() {
    auto* root = addComponent<SceneComponent>("Root");
    setRoot(root);
    name = "Pawn";
}

CameraComponent* Pawn::pawnCamera() const { return findComponentOfClass<CameraComponent>(); }

void Pawn::addMovementInput(const Vec3& direction, float scale) {
    if (movement_) movement_->addInput(direction, scale);
}

void Pawn::addYaw(float degrees) {
    if (!yawAffectsBody) return;
    Rotator r = rotation();
    r.yaw = Rotator::normalizeAngle(r.yaw + degrees);
    setRotation(r);
}

void Pawn::addPitch(float degrees) {
    // Pitch is clamped and kept on the view rather than the body: a
    // character that pitches its whole body faceplants.
    viewPitch_ = clampf(viewPitch_ + degrees, minPitch, maxPitch);
    if (CameraComponent* cam = pawnCamera()) {
        Rotator r = cam->rotation;
        r.pitch = viewPitch_;
        cam->setRotation(r);
    }
    if (auto* arm = findComponentOfClass<SpringArmComponent>()) {
        Rotator r = arm->rotation;
        r.pitch = viewPitch_;
        arm->setRotation(r);
    }
}

void Pawn::setupInput(PlayerController& pc, float dt) {
    (void)dt;
    const InputState& in = pc.input();
    // Movement is relative to where the pawn faces, so "forward" means
    // forward from the player's point of view.
    const Vec3 fwd = forward().flat().normalized();
    const Vec3 rgt = right().flat().normalized();
    addMovementInput(fwd, in.axis("MoveForward"));
    addMovementInput(rgt, in.axis("MoveRight"));
}

FORGE_CLASS_BEGIN(Pawn)
    FORGE_DISPLAY("Pawn")
    FORGE_CATEGORY("Gameplay")
    FORGE_ICON("P")
    FORGE_DESCRIBE("An actor a controller can possess and drive.")
    FORGE_PROP(minPitch).range(-89.0f, 0.0f).cat("View")
    FORGE_PROP(maxPitch).range(0.0f, 89.0f).cat("View")
    FORGE_PROP(yawAffectsBody).cat("View")
FORGE_CLASS_END()
FORGE_REGISTER(Pawn)

// ---------------------------------------------------------------
//  SpectatorPawn
// ---------------------------------------------------------------

SpectatorPawn::SpectatorPawn() {
    name = "Spectator";
    camera_ = addComponent<CameraComponent>("Camera");
    auto* move = addComponent<FloatingPawnMovementComponent>("Movement");
    move->maxSpeed = 14.0f;
    move->collides = false;
    movement_ = move;
}

void SpectatorPawn::setupInput(PlayerController& pc, float dt) {
    Pawn::setupInput(pc, dt);
    // A flyer also moves along its view direction and straight up.
    const InputState& in = pc.input();
    addMovementInput(Vec3::Up, in.axis("MoveUp"));
    if (camera_ && std::fabs(in.axis("MoveForward")) > 0.01f) {
        // Fold in the vertical part of where the camera looks, so looking
        // down and pushing forward descends.
        const Vec3 look = camera_->forward();
        addMovementInput(Vec3{0, look.y, 0}, in.axis("MoveForward"));
    }
}

FORGE_CLASS_BEGIN(SpectatorPawn)
    FORGE_DISPLAY("Spectator Pawn")
    FORGE_DESCRIBE("A flying camera pawn. No gravity, no collision.")
FORGE_CLASS_END()
FORGE_REGISTER(SpectatorPawn)

// ---------------------------------------------------------------
//  Character
// ---------------------------------------------------------------

Character::Character() {
    name = "Character";
    mesh_ = addComponent<StaticMeshComponent>("Mesh");
    mesh_->mesh = "Capsule";
    mesh_->collisionResponse = (int)CollisionResponse::None;   // movement owns collision
    setRoot(mesh_);

    arm_ = addComponent<SpringArmComponent>("SpringArm", mesh_);
    arm_->setLocation({0, 1.2f, 0});
    arm_->targetLength = 6.0f;

    camera_ = addComponent<CameraComponent>("Camera", arm_);

    charMove_ = addComponent<CharacterMovementComponent>("Movement");
    charMove_->maxSpeed = 7.0f;
    movement_ = charMove_;

    // Collision presence. Without it nothing in the world can detect the
    // player: triggers never fire and overlap queries come back empty,
    // because the movement component's sweep is a query, not a collider.
    capsule_ = addComponent<CapsuleComponent>("Capsule", mesh_);
    capsule_->radius = charMove_->capsuleRadius;
    capsule_->halfHeight = charMove_->capsuleHalfHeight;

    yawAffectsBody = true;
}

void Character::onConstruct() {
    if (mesh_) {
        mesh_->mesh = bodyMesh;
        mesh_->material = bodyMaterial;
    }
    if (arm_) {
        // First person puts the camera at the eyes with no arm; third
        // person swings it back. One flag, two rigs.
        arm_->targetLength = firstPerson ? 0.0f : cameraDistance;
        arm_->setLocation({0, firstPerson ? eyeHeight : 1.2f, 0});
        arm_->collisionTest = !firstPerson;
    }
    if (mesh_) mesh_->visible = !firstPerson;
    if (charMove_) charMove_->maxSpeed = walkSpeed;
    if (capsule_ && charMove_) {
        // The collider tracks the movement shape, so widening the
        // character in the details panel widens what triggers see.
        capsule_->radius = charMove_->capsuleRadius;
        capsule_->halfHeight = charMove_->capsuleHalfHeight;
        // The mesh may be scaled by the actor; the collider must not be
        // scaled twice, so it sits at unit scale under the mesh.
        capsule_->setScale(Vec3::One);
        capsule_->updateCollider();
    }
    health = std::min(health, maxHealth);
}

void Character::beginPlay() {
    health = maxHealth;
    died_ = false;
}

void Character::tick(float dt) {
    (void)dt;
}

void Character::jump() {
    if (charMove_) charMove_->jump();
}

void Character::setupInput(PlayerController& pc, float dt) {
    Pawn::setupInput(pc, dt);
    const InputState& in = pc.input();
    if (charMove_) {
        charMove_->maxSpeed = in.action("Sprint") ? sprintSpeed : walkSpeed;
        if (in.actionPressed("Jump")) charMove_->jump();
    }
}

float Character::takeDamage(float amount, Actor* instigator) {
    if (!isAlive()) return 0.0f;
    const float applied = Actor::takeDamage(amount, instigator);
    health = std::max(0.0f, health - applied);
    if (health <= 0.0f && !died_) {
        died_ = true;
        onDied.broadcast();
    }
    return applied;
}

FORGE_CLASS_BEGIN(Character)
    FORGE_DISPLAY("Character")
    FORGE_DESCRIBE("A walking pawn with a capsule, a camera and a movement component.")
    FORGE_PROP(firstPerson).cat("View").tooltip("Puts the camera at the eyes and hides the body.")
    FORGE_PROP(cameraDistance).range(0.0f, 30.0f).cat("View")
    FORGE_PROP(eyeHeight).range(0.0f, 4.0f).cat("View")
    FORGE_PROP(walkSpeed).range(0.0f, 40.0f).cat("Movement")
    FORGE_PROP(sprintSpeed).range(0.0f, 60.0f).cat("Movement")
    FORGE_PROP(bodyMesh).asset(RefKind::Mesh).cat("Appearance")
    FORGE_PROP(bodyMaterial).asset(RefKind::Material).cat("Appearance")
    FORGE_PROP(health).range(0.0f, 1000.0f).cat("Health")
    FORGE_PROP(maxHealth).range(1.0f, 1000.0f).cat("Health")
FORGE_CLASS_END()
FORGE_REGISTER(Character)

// ---------------------------------------------------------------
//  PlayerController
// ---------------------------------------------------------------

PlayerController::PlayerController() {
    name = "PlayerController";
    setRoot(addComponent<SceneComponent>("Root"));
    input_ = &ownedInput_;
    ownedInput_.addDefaultMappings();
}

void PlayerController::possess(Pawn* p) {
    if (pawn_ == p) return;
    unpossess();
    pawn_ = p;
    if (pawn_) pawn_->possessedBy(this);
}

void PlayerController::unpossess() {
    if (!pawn_) return;
    pawn_->unpossessed();
    pawn_ = nullptr;
}

CameraComponent* PlayerController::viewCamera() const {
    if (pawn_ && !pawn_->isPendingKill()) {
        if (CameraComponent* c = pawn_->pawnCamera()) return c;
    }
    return nullptr;
}

void PlayerController::tickController(float dt) {
    if (!pawn_ || pawn_->isPendingKill() || !input_) return;

    // Looking first: the pawn should move in the direction it is facing
    // after this frame's mouse motion, not before it.
    const float turn = input_->axis("Turn") * mouseSensitivity;
    const float look = input_->axis("LookUp") * mouseSensitivity * (invertY ? -1.0f : 1.0f);
    if (std::fabs(turn) > 1e-5f) pawn_->addYaw(turn);
    if (std::fabs(look) > 1e-5f) pawn_->addPitch(look);

    pawn_->setupInput(*this, dt);
}

FORGE_CLASS_BEGIN(PlayerController)
    FORGE_DISPLAY("Player Controller")
    FORGE_CATEGORY("Gameplay")
    FORGE_DESCRIBE("Turns input into intent and owns the view.")
    .notPlaceable()   // spawned by GameMode::startPlay, not dropped by hand
    FORGE_PROP(mouseSensitivity).range(0.05f, 5.0f).cat("Input")
    FORGE_PROP(invertY).cat("Input")
FORGE_CLASS_END()
FORGE_REGISTER(PlayerController)

// ---------------------------------------------------------------
//  GameMode
// ---------------------------------------------------------------

Actor* GameMode::choosePlayerStart() {
    if (!world()) return nullptr;
    auto starts = world()->actorsOfClass("PlayerStart");
    return starts.empty() ? nullptr : starts.front();
}

Pawn* GameMode::spawnDefaultPawn(const Transform& at) {
    World* w = world();
    if (!w) return nullptr;
    const ClassInfo* cls = ClassRegistry::get().find(defaultPawnClass);
    if (!cls || !cls->isA("Pawn")) {
        FORGE_WARN("GameMode: '%s' is not a Pawn class; using Character",
                   defaultPawnClass.c_str());
        cls = Character::staticClass();
    }
    Actor* a = w->spawn(cls, "PlayerPawn", at.position, Rotator::fromQuat(at.rotation));
    return dynamic_cast<Pawn*>(a);
}

void GameMode::startPlay() {
    World* w = world();
    if (!w || !spawnPlayerOnStart) return;

    const ClassInfo* pcClass = ClassRegistry::get().find(playerControllerClass);
    if (!pcClass || !pcClass->isA("PlayerController")) pcClass = PlayerController::staticClass();
    controller_ = static_cast<PlayerController*>(w->spawn(pcClass, "PlayerController"));
    w->setPlayerController(controller_);

    restartPlayer();
}

void GameMode::restartPlayer() {
    World* w = world();
    if (!w || !controller_) return;

    Transform at;
    if (Actor* start = choosePlayerStart()) {
        at = start->transform();
        // Lift the spawn clear of the floor so the first physics step is
        // not resolving a penetration.
        at.position.y += 0.1f;
    } else {
        at.position = {0.0f, 2.0f, 0.0f};
        FORGE_WARN("GameMode: no PlayerStart in the level; spawning at the origin");
    }

    if (Pawn* p = spawnDefaultPawn(at)) controller_->possess(p);
}

FORGE_CLASS_BEGIN(GameMode)
    FORGE_DISPLAY("Game Mode")
    FORGE_CATEGORY("Gameplay")
    FORGE_DESCRIBE("The rules: which pawn the player gets and where they start.")
    .notPlaceable()   // set in World Settings, not dropped into a level
    FORGE_PROP(defaultPawnClass).classRef("Pawn").cat("Classes").label("Default Pawn")
    FORGE_PROP(playerControllerClass).classRef("PlayerController").cat("Classes").label("Player Controller")
    FORGE_PROP(spawnPlayerOnStart).cat("Classes")
FORGE_CLASS_END()
FORGE_REGISTER(GameMode)

// ---------------------------------------------------------------
//  PlayerStart
// ---------------------------------------------------------------

PlayerStart::PlayerStart() {
    name = "PlayerStart";
    auto* marker = addComponent<StaticMeshComponent>("Marker");
    marker->mesh = "Capsule";
    marker->material = "GlowCyan";
    marker->collisionResponse = (int)CollisionResponse::None;
    // A marker is an editor aid, not level geometry.
    marker->castShadow = false;
    setRoot(marker);
    hiddenInGame = true;
    tickEnabled = false;
}

FORGE_CLASS_BEGIN(PlayerStart)
    FORGE_DISPLAY("Player Start")
    FORGE_CATEGORY("Gameplay")
    FORGE_ICON("S")
    FORGE_DESCRIBE("Where the player appears when play begins.")
FORGE_CLASS_END()
FORGE_REGISTER(PlayerStart)

} // namespace forge
