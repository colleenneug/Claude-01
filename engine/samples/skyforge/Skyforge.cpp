#include "Skyforge.hpp"

#include <cmath>

#include "forge/assets/AssetLibrary.hpp"
#include "forge/components/ScriptComponent.hpp"
#include "forge/core/Log.hpp"
#include "forge/scene/World.hpp"
#include "forge/script/ScriptGraph.hpp"

namespace skyforge {

// ---------------------------------------------------------------
//  Orb
// ---------------------------------------------------------------

Orb::Orb() {
    name = "Orb";
    mesh_ = addComponent<StaticMeshComponent>("Mesh");
    mesh_->mesh = "Sphere";
    mesh_->material = "GlowCyan";
    mesh_->collisionResponse = (int)CollisionResponse::Overlap;
    mesh_->mobility = (int)Mobility::Movable;
    mesh_->collisionPadding = Vec3(0.35f);   // forgiving to walk into
    setRoot(mesh_);

    spin_ = addComponent<RotatingMovementComponent>("Spin");
    spin_->rotationRate = {0, 90, 0};
    spin_->bobAmplitude = 0.22f;
    spin_->bobSpeed = 0.7f;

    addTag("orb");
    tickEnabled = false;
}

void Orb::beginPlay() {
    collected_ = false;
    onBeginOverlap.bind([this](Actor* other) {
        // Only the player counts: a falling crate must not bank an orb.
        if (collected_ || !other || !other->getClass()->isA("Character")) return;
        collected_ = true;
        Log::get().write(LogLevel::Script, "Skyforge", "Collected " + name);
        destroy();
    });
}

FORGE_CLASS_BEGIN(Orb)
    FORGE_DISPLAY("Orb")
    FORGE_CATEGORY("Skyforge")
    FORGE_DESCRIBE("A collectible. Take them all to open the goal.")
    FORGE_PROP(value).range(0.0f, 10.0f).cat("Orb")
FORGE_CLASS_END()
FORGE_REGISTER(Orb)

// ---------------------------------------------------------------
//  MovingPlatform
// ---------------------------------------------------------------

MovingPlatform::MovingPlatform() {
    name = "Moving Platform";
    mesh_ = addComponent<StaticMeshComponent>("Mesh");
    mesh_->mesh = "Cube";
    mesh_->material = "Steel";
    // Movable, or its collider would stay where the level file left it
    // and the player would be standing on thin air.
    mesh_->mobility = (int)Mobility::Movable;
    setRoot(mesh_);
    tickEnabled = true;
}

void MovingPlatform::beginPlay() { origin_ = location(); }

void MovingPlatform::tick(float dt) {
    phase_ += dt * speed;
    // A cosine eases in and out at each end, so the platform does not
    // snap direction under the player's feet.
    const float t = 0.5f - 0.5f * std::cos(phase_ * kTwoPi);
    setLocation(origin_ + travel * t);
}

FORGE_CLASS_BEGIN(MovingPlatform)
    FORGE_DISPLAY("Moving Platform")
    FORGE_CATEGORY("Skyforge")
    FORGE_DESCRIBE("Travels between where it is placed and that plus Travel.")
    FORGE_PROP(travel).cat("Motion")
    FORGE_PROP(speed).range(0.01f, 2.0f).cat("Motion").tooltip("Round trips per second.")
FORGE_CLASS_END()
FORGE_REGISTER(MovingPlatform)

// ---------------------------------------------------------------
//  SkyforgeGameMode
// ---------------------------------------------------------------

SkyforgeGameMode::SkyforgeGameMode() {
    name = "Skyforge Rules";
    defaultPawnClass = "Character";
    tickEnabled = true;
}

void SkyforgeGameMode::beginPlay() {
    totalOrbs_ = (int)world()->actorsOfClass("Orb").size();
    lastRemaining_ = totalOrbs_;
    Log::get().write(LogLevel::Script, "Skyforge",
                     "Collect " + std::to_string(totalOrbs_) + " orbs, then reach the goal.");
}

void SkyforgeGameMode::tick(float dt) {
    (void)dt;
    if (finished_) return;
    World* w = world();
    if (!w) return;

    const int remaining = (int)w->actorsOfClass("Orb").size();
    if (remaining != lastRemaining_) {
        lastRemaining_ = remaining;
        if (remaining == 0) {
            Log::get().write(LogLevel::Script, "Skyforge", "All orbs collected. The goal is open.");
            // The goal only lights up once it has been earned.
            for (Actor* a : w->actorsWithTag("goal")) {
                if (auto* mesh = a->findComponentOfClass<StaticMeshComponent>()) {
                    mesh->material = "GlowAmber";
                    mesh->onConstruct();
                }
            }
            goalOpen_ = true;
        }
    }

    Pawn* pawn = controller() ? controller()->pawn() : nullptr;
    if (!pawn) return;

    // Fell off the course: put them back at the start rather than
    // letting them drop forever.
    if (pawn->location().y < killHeight) {
        if (Actor* start = choosePlayerStart()) pawn->setLocation(start->location() + Vec3{0, 1.0f, 0});
        else pawn->setLocation({0, 4, 0});
        if (auto* move = pawn->findComponentOfClass<CharacterMovementComponent>())
            move->velocity = Vec3::Zero;
        ++respawns_;
        Log::get().write(LogLevel::Script, "Skyforge", "Respawned.");
    }

    if (!goalOpen_) return;
    for (Actor* goal : w->actorsWithTag("goal")) {
        if (pawn->distanceTo(goal) > 2.5f) continue;
        finished_ = true;
        Log::get().write(LogLevel::Script, "Skyforge", "You made it. Course complete.");
        break;
    }
}

FORGE_CLASS_BEGIN(SkyforgeGameMode)
    FORGE_DISPLAY("Skyforge Rules")
    FORGE_CATEGORY("Skyforge")
    FORGE_PROP(killHeight).range(-200.0f, 0.0f).cat("Rules")
        .tooltip("Below this the player is put back at the start.")
FORGE_CLASS_END()
FORGE_REGISTER(SkyforgeGameMode)

namespace {

// A script graph, built in code the way the editor would build it by
// dragging nodes. Proof that scripts are data, not a separate language.
std::unique_ptr<ScriptGraph> makeBeaconScript() {
    auto g = std::make_unique<ScriptGraph>();
    g->name = "BeaconPulse";

    // Tick -> Set Scale to 1 + 0.15 * sin(time * 180)
    const int tick = g->addNode("Event.Tick", {0, 0});
    const int time = g->addNode("World.GetTime", {60, 170});
    const int rate = g->addNode("Math.Multiply", {200, 170});
    const int sine = g->addNode("Math.Sin", {340, 170});
    const int amp = g->addNode("Math.Multiply", {460, 170});
    const int one = g->addNode("Math.Add", {600, 170});
    const int makeVec = g->addNode("Vec.Make", {740, 170});
    const int setScale = g->addNode("Actor.SetScale", {900, 0});

    g->node(rate)->literals["B"] = ScriptValue::make(180.0f);
    g->node(amp)->literals["B"] = ScriptValue::make(0.15f);
    g->node(one)->literals["B"] = ScriptValue::make(1.0f);

    g->link(time, "Seconds", rate, "A");
    g->link(rate, "Result", sine, "Degrees");
    g->link(sine, "Result", amp, "A");
    g->link(amp, "Result", one, "A");
    g->link(one, "Result", makeVec, "X");
    g->link(one, "Result", makeVec, "Y");
    g->link(one, "Result", makeVec, "Z");
    g->link(makeVec, "Result", setScale, "Scale");
    g->link(tick, "Then", setScale, "In");
    return g;
}

StaticMeshActor* platform(World& w, const std::string& name, const Vec3& at, const Vec3& size,
                          const char* material = "Concrete") {
    auto* a = w.spawn<StaticMeshActor>(name, at);
    a->mesh = "Cube";
    a->material = material;
    a->setScale(size);
    a->rerunConstruction();
    return a;
}

void buildLevelImpl(World& w, AssetLibrary& assets) {
    w.setLevelName("Skyforge");
    w.settings().gameModeClass = "SkyforgeGameMode";
    w.settings().backgroundColor = Color::fromHex("#5b7fa8");
    w.settings().skyColor = Color::fromHex("#9dc0e8");
    w.settings().groundColor = Color::fromHex("#2e3a4a");
    w.settings().sunYaw = 150.0f;
    w.settings().sunPitch = 38.0f;
    w.settings().sunIntensity = 3.0f;
    w.settings().fogColor = Color::fromHex("#8fb0d0");
    w.settings().fogDensity = 0.008f;
    w.settings().gravity = {0, -24.0f, 0};

    assets.addScript(makeBeaconScript());

    // ---- the course ----
    platform(w, "Start", {0, 0, 0}, {10, 1, 10}, "Checker");
    w.spawn<PlayerStart>("PlayerStart", {0, 1.6f, 3});

    platform(w, "Step1", {0, 1.2f, -8}, {5, 1, 5});
    platform(w, "Step2", {-7, 2.4f, -14}, {5, 1, 5});
    platform(w, "Step3", {2, 3.6f, -20}, {5, 1, 5});

    auto* bridge = w.spawn<MovingPlatform>("Bridge", {8, 3.6f, -24});
    bridge->travel = {0, 0, -9};
    bridge->speed = 0.18f;
    bridge->setScale({4, 0.6f, 4});
    bridge->rerunConstruction();

    platform(w, "Summit", {8, 4.8f, -34}, {9, 1, 9}, "Steel");

    // A staircase back down, so the course loops rather than dead-ends.
    // The Stairs mesh is authored 2 wide, 2 tall and 4 deep, so its scale
    // is relative to that -- not to a unit cube. Treating it as unit
    // sized made a 32-metre slab that swallowed half the course.
    auto* stairs = w.spawn<StaticMeshActor>("Stairs", {-1, 2.4f, -34});
    stairs->mesh = "Stairs";
    stairs->material = "Concrete";
    stairs->setScale({2, 2.4f, 2});
    stairs->setRotation({0, 90, 0});
    stairs->rerunConstruction();

    // ---- orbs ----
    const Vec3 orbSpots[] = {
        {0, 2.4f, -8}, {-7, 3.6f, -14}, {2, 4.8f, -20},
        {8, 6.0f, -34}, {5, 6.0f, -34}, {11, 6.0f, -34},
    };
    int index = 1;
    for (const Vec3& spot : orbSpots) {
        auto* orb = w.spawn<Orb>("Orb" + std::to_string(index++), spot);
        orb->rerunConstruction();
    }

    // ---- hazard ----
    auto* fan = w.spawn<RotatingActor>("Fan", {2, 5.0f, -20});
    fan->mesh = "Cube";
    fan->material = "Rubber";
    fan->rotationRate = {0, 220, 0};
    fan->bobAmplitude = 0.0f;
    fan->collision = (int)CollisionResponse::Block;
    fan->setScale({6, 0.4f, 0.6f});
    fan->rerunConstruction();

    // ---- the goal ----
    auto* goal = w.spawn<StaticMeshActor>("Goal", {8, 6.2f, -37});
    goal->mesh = "Torus";
    goal->material = "Rubber";
    goal->collision = (int)CollisionResponse::None;
    goal->setScale({3, 3, 3});
    goal->setRotation({90, 0, 0});
    goal->addTag("goal");
    goal->rerunConstruction();

    // A beacon over the goal, animated by the script graph above.
    auto* beacon = w.spawn<StaticMeshActor>("Beacon", {8, 8.5f, -37});
    beacon->mesh = "Sphere";
    beacon->material = "GlowAmber";
    beacon->collision = (int)CollisionResponse::None;
    beacon->rerunConstruction();
    auto* script = beacon->addComponent<ScriptComponent>("Pulse");
    script->script = "BeaconPulse";
    script->onConstruct();

    // ---- lighting ----
    auto* lamp = w.spawn<PointLightActor>("SummitLamp", {8, 8, -34});
    lamp->intensity = 60.0f;
    lamp->radius = 22.0f;
    lamp->lightColor = Color::fromHex("#ffd9a0");
    lamp->rerunConstruction();

    auto* goalLight = w.spawn<PointLightActor>("GoalLight", {8, 7, -37});
    goalLight->intensity = 45.0f;
    goalLight->radius = 16.0f;
    goalLight->lightColor = Color::fromHex("#ffb454");
    goalLight->rerunConstruction();
}

}  // namespace

void buildLevel(World& world, AssetLibrary& assets) {
    buildLevelImpl(world, assets);
}

} // namespace skyforge
