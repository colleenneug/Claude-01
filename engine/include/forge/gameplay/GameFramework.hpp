// ============================================================
//  The gameplay framework.
//
//  The division of labour Unreal settled on, because it holds up:
//
//    GameMode          the rules: what pawn the player gets, where
//                      they start, when the game is won or lost
//    PlayerController  the player's will: turns input into intent and
//                      owns the camera
//    Pawn              a thing that can be possessed and driven
//    Character         a Pawn with a capsule and a walking movement
//                      component, which is what most games want
//
//  Keeping the controller separate from the pawn is what lets a game
//  swap what you are driving without losing who is driving it.
// ============================================================
#pragma once

#include "forge/components/MovementComponents.hpp"
#include "forge/components/RenderComponents.hpp"
#include "forge/gameplay/Input.hpp"
#include "forge/scene/Actor.hpp"

namespace forge {

class PlayerController;

// ---------------------------------------------------------------
//  Pawn
// ---------------------------------------------------------------

class Pawn : public Actor {
    FORGE_OBJECT(Pawn, Actor)
public:
    Pawn();

    virtual void setupInput(PlayerController& pc, float dt);
    virtual void addMovementInput(const Vec3& direction, float scale);
    virtual void addYaw(float degrees);
    virtual void addPitch(float degrees);

    void possessedBy(PlayerController* pc) { controller_ = pc; onPossessed(); }
    void unpossessed() { controller_ = nullptr; onUnpossessed(); }
    PlayerController* controller() const { return controller_; }
    virtual void onPossessed() {}
    virtual void onUnpossessed() {}

    MovementComponent* movement() const { return movement_; }
    // Where the view should be for this pawn: a camera component if it
    // has one, otherwise the pawn's own eyes.
    virtual CameraComponent* pawnCamera() const;

    float minPitch = -85.0f;
    float maxPitch = 85.0f;
    // Turning the pawn body rather than only the camera. A first-person
    // pawn yaws its body; a third-person one usually does not.
    bool yawAffectsBody = true;

protected:
    MovementComponent* movement_ = nullptr;
    PlayerController* controller_ = nullptr;
    float viewPitch_ = 0.0f;
};

// A flying pawn with a camera. Good for spectating and for a first
// playable before there is a character.
class SpectatorPawn : public Pawn {
    FORGE_OBJECT(SpectatorPawn, Pawn)
public:
    SpectatorPawn();
    void setupInput(PlayerController& pc, float dt) override;
    CameraComponent* pawnCamera() const override { return camera_; }

private:
    CameraComponent* camera_ = nullptr;
};

// A walking pawn: capsule, mesh, spring arm, camera.
class Character : public Pawn {
    FORGE_OBJECT(Character, Pawn)
public:
    Character();

    void setupInput(PlayerController& pc, float dt) override;
    void onConstruct() override;
    void beginPlay() override;
    void tick(float dt) override;
    CameraComponent* pawnCamera() const override { return camera_; }

    CharacterMovementComponent* characterMovement() const { return charMove_; }
    StaticMeshComponent* meshComponent() const { return mesh_; }
    SpringArmComponent* springArm() const { return arm_; }

    void jump();

    // Reflected.
    bool firstPerson = false;
    float walkSpeed = 7.0f;
    float sprintSpeed = 12.0f;
    float cameraDistance = 6.0f;
    float eyeHeight = 1.6f;
    std::string bodyMesh = "Capsule";
    std::string bodyMaterial = "Default";
    float health = 100.0f;
    float maxHealth = 100.0f;

    float takeDamage(float amount, Actor* instigator) override;
    Delegate<> onDied;
    bool isAlive() const { return health > 0.0f; }

private:
    StaticMeshComponent* mesh_ = nullptr;
    SpringArmComponent* arm_ = nullptr;
    CameraComponent* camera_ = nullptr;
    CharacterMovementComponent* charMove_ = nullptr;
    bool died_ = false;
};

// ---------------------------------------------------------------
//  PlayerController
// ---------------------------------------------------------------

class PlayerController : public Actor {
    FORGE_OBJECT(PlayerController, Actor)
public:
    PlayerController();

    void possess(Pawn* p);
    void unpossess();
    Pawn* pawn() const { return pawn_; }

    // Called by the world before pawns tick, so this frame's input moves
    // the pawn this frame.
    void tickController(float dt);
    CameraComponent* viewCamera() const;

    InputState& input() { return *input_; }
    const InputState& input() const { return *input_; }
    void setInput(InputState* in) { input_ = in; }

    float mouseSensitivity = 1.0f;
    bool invertY = false;

private:
    Pawn* pawn_ = nullptr;
    InputState* input_ = nullptr;
    InputState ownedInput_;
};

// ---------------------------------------------------------------
//  GameMode
// ---------------------------------------------------------------

class GameMode : public Actor {
    FORGE_OBJECT(GameMode, Actor)
public:
    // Spawns the controller and the default pawn at a PlayerStart.
    virtual void startPlay();
    virtual Actor* choosePlayerStart();
    virtual Pawn* spawnDefaultPawn(const Transform& at);
    virtual void restartPlayer();

    std::string defaultPawnClass = "Character";
    std::string playerControllerClass = "PlayerController";
    bool spawnPlayerOnStart = true;

    PlayerController* controller() const { return controller_; }

protected:
    PlayerController* controller_ = nullptr;
};

// Marks where the player appears. Invisible while playing.
class PlayerStart : public Actor {
    FORGE_OBJECT(PlayerStart, Actor)
public:
    PlayerStart();
};

} // namespace forge
