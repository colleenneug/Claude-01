#include "Test.hpp"

#include "forge/assets/AssetLibrary.hpp"
#include "forge/core/Log.hpp"
#include "forge/components/ScriptComponent.hpp"
#include "forge/gameplay/Actors.hpp"
#include "forge/gameplay/GameFramework.hpp"
#include "forge/scene/Level.hpp"
#include "forge/scene/World.hpp"
#include "forge/script/ScriptGraph.hpp"

using namespace forge;

namespace {

// Every test needs a world with content in it; this bundles the setup.
struct Fixture {
    AssetLibrary assets;
    World world;

    Fixture() : world(false) {
        registerCoreNodes();
        assets.createStarterContent();
        world.setAssets(&assets);
    }

    // Runs a fixed number of steps at a fixed timestep, so a test asserts
    // on deterministic motion rather than on wall-clock timing.
    void run(float seconds, float step = 1.0f / 60.0f) {
        for (float t = 0.0f; t < seconds; t += step) world.tick(step);
    }

    StaticMeshActor* ground(float size = 40.0f, float y = 0.0f) {
        auto* g = world.spawn<StaticMeshActor>("Ground", {0, y, 0});
        g->mesh = "Cube";
        g->setScale({size, 1.0f, size});
        g->rerunConstruction();
        return g;
    }
};

} // namespace

TEST(world_spawn_and_find) {
    Fixture f;
    auto* a = f.world.spawn<StaticMeshActor>("Wall", {1, 2, 3});
    CHECK(a != nullptr);
    CHECK(a->world() == &f.world);
    CHECK(a->id() != 0);
    CHECK(f.world.findActor(a->id()) == a);
    CHECK(f.world.findActorByName("Wall") == a);
    CHECK_VEC(a->location(), (Vec3{1, 2, 3}), 1e-5);

    // Names are made unique, or the outliner shows two rows you cannot
    // tell apart.
    auto* b = f.world.spawn<StaticMeshActor>();
    auto* c = f.world.spawn<StaticMeshActor>();
    CHECK(b->name != c->name);

    CHECK(f.world.actorsOfClass("StaticMeshActor").size() == 3);
    CHECK(f.world.actorsOfClass("Actor").size() == 3);   // base class matches too
    CHECK(f.world.actorsOfType<StaticMeshActor>().size() == 3);
}

TEST(world_destroy_is_deferred_during_play) {
    Fixture f;
    auto* a = f.world.spawn<StaticMeshActor>("Doomed");
    const uint32_t id = a->id();

    // Outside of play, destruction is immediate so the editor's outliner
    // updates on the same frame as the delete.
    a->destroy();
    CHECK(f.world.findActor(id) == nullptr);

    auto* b = f.world.spawn<StaticMeshActor>("Doomed2");
    const uint32_t id2 = b->id();
    f.world.beginPlay();
    b->destroy();
    // Still resolvable this frame: a script may destroy something it is
    // in the middle of iterating over.
    CHECK(b->isPendingKill());
    f.world.tick(1.0f / 60.0f);
    CHECK(f.world.findActor(id2) == nullptr);
}

TEST(component_hierarchy_transforms) {
    Fixture f;
    auto* a = f.world.spawn<StaticMeshActor>("Parent");
    a->setLocation({10, 0, 0});
    a->setRotation({0, 90, 0});

    auto* child = a->addComponent<StaticMeshComponent>("Child", a->meshComponent());
    child->setLocation({0, 0, -2});   // two units in front of the parent

    // Yawed 90 degrees, the parent's forward is -X.
    CHECK_VEC(child->worldLocation(), (Vec3{8, 0, 0}), 1e-4);

    // Moving the parent moves the child, without the child being touched.
    a->setLocation({0, 5, 0});
    CHECK_VEC(child->worldLocation(), (Vec3{-2, 5, 0}), 1e-4);

    // Scale compounds down the chain.
    a->setScale(Vec3(2.0f));
    CHECK_VEC(child->worldLocation(), (Vec3{-4, 5, 0}), 1e-4);

    child->detach();
    CHECK(child->parent() == nullptr);
    CHECK_VEC(child->worldLocation(), (Vec3{0, 0, -2}), 1e-4);
}

TEST(component_attachment_rejects_cycles) {
    Fixture f;
    auto* a = f.world.spawn<StaticMeshActor>();
    auto* root = a->meshComponent();
    auto* mid = a->addComponent<SceneComponent>("Mid", root);
    auto* leaf = a->addComponent<SceneComponent>("Leaf", mid);

    CHECK(leaf->isDescendantOf(root));
    CHECK(!root->isDescendantOf(leaf));

    // Attaching a parent under its own descendant would make the world
    // transform walk never terminate.
    Log::get().setEchoToConsole(false);
    root->attachTo(leaf);
    Log::get().setEchoToConsole(true);
    CHECK(root->parent() != leaf);
}

TEST(physics_raycast) {
    Fixture f;
    auto* box = f.world.spawn<StaticMeshActor>("Box", {0, 0, 0});
    box->rerunConstruction();
    f.world.tick(0.0f);

    HitResult hit = f.world.physics().raycast({0, 0, 10}, Vec3::Forward, 100.0f);
    CHECK(hit.hit);
    CHECK(hit.actor == box);
    CHECK_NEAR(hit.distance, 9.5f, 1e-2);   // unit cube, so the face is at z = 0.5
    CHECK_VEC(hit.normal, (Vec3{0, 0, 1}), 1e-3);

    // A ray that misses reports a miss rather than a nearest hit.
    CHECK(!f.world.physics().raycast({0, 50, 10}, Vec3::Forward, 100.0f).hit);
    // Too short to reach is also a miss.
    CHECK(!f.world.physics().raycast({0, 0, 10}, Vec3::Forward, 5.0f).hit);

    // Ignoring the only thing in the way leaves nothing to hit.
    QueryParams q;
    q.ignore.push_back(box);
    CHECK(!f.world.physics().raycast({0, 0, 10}, Vec3::Forward, 100.0f, q).hit);
}

TEST(physics_raycast_respects_rotation) {
    Fixture f;
    auto* box = f.world.spawn<StaticMeshActor>("Box");
    box->setScale({4, 4, 0.2f});      // a thin wall in the XY plane
    box->rerunConstruction();
    f.world.tick(0.0f);
    CHECK(f.world.physics().raycast({0, 0, 10}, Vec3::Forward, 100.0f).hit);

    // Turned edge-on, a ray down the old axis should now pass beside it.
    box->setRotation({0, 90, 0});
    box->rerunConstruction();
    f.world.tick(0.0f);
    HitResult straightOn = f.world.physics().raycast({0, 0, 10}, Vec3::Forward, 100.0f);
    CHECK(straightOn.hit);
    // Yawed 90, the wall's 4-unit width now faces the ray, so its near
    // face is at z = 2 rather than z = 0.1.
    CHECK_NEAR(straightOn.distance, 8.0f, 0.05);
    // And a ray along the new facing meets the broad side.
    HitResult fromSide = f.world.physics().raycast({10, 0, 0}, Vec3::Left, 100.0f);
    CHECK(fromSide.hit);
    CHECK_NEAR(fromSide.distance, 9.9f, 0.05);
}

TEST(physics_sphere_and_capsule_shapes) {
    Fixture f;
    auto* sphere = f.world.spawn<StaticMeshActor>("Ball");
    sphere->mesh = "Sphere";
    sphere->rerunConstruction();
    f.world.tick(0.0f);

    // A sphere of radius 0.5 is hit head-on at 9.5 but missed at y = 0.49
    // off-centre only near the rim, unlike a box which would still be hit.
    CHECK_NEAR(f.world.physics().raycast({0, 0, 10}, Vec3::Forward, 100.0f).distance, 9.5f, 1e-2);
    CHECK(!f.world.physics().raycast({0.49f, 0.49f, 10}, Vec3::Forward, 100.0f).hit);

    auto overlapping = f.world.physics().overlapSphere(Vec3::Zero, 1.0f);
    CHECK(overlapping.size() == 1);
    CHECK(overlapping[0] == sphere);
    CHECK(f.world.physics().overlapSphere({50, 0, 0}, 1.0f).empty());
}

TEST(character_falls_and_lands) {
    Fixture f;
    f.ground();
    auto* c = f.world.spawn<Character>("Hero", {0, 6, 0});
    f.world.beginPlay();

    CHECK(c->characterMovement()->isFalling());
    f.run(3.0f);

    // The ground's top is at y = 0.5; the capsule's centre rests a
    // half-height plus a radius above it.
    CHECK(c->characterMovement()->isGrounded());
    CHECK(c->location().y > 0.5f);
    CHECK(c->location().y < 3.0f);
    // Landed means the fall has actually stopped, not merely slowed.
    CHECK_NEAR(c->characterMovement()->velocity.y, 0.0f, 0.5);
}

TEST(character_walks_and_is_blocked_by_walls) {
    Fixture f;
    f.ground();
    auto* wall = f.world.spawn<StaticMeshActor>("Wall", {0, 2, -5});
    wall->setScale({10, 4, 1});
    wall->rerunConstruction();

    auto* c = f.world.spawn<Character>("Hero", {0, 2, 0});
    f.world.beginPlay();
    f.run(1.0f);   // settle onto the ground

    const float startZ = c->location().z;
    for (int i = 0; i < 240; ++i) {
        // Walk forward: -Z is forward.
        c->characterMovement()->addInput(Vec3::Forward, 1.0f);
        f.world.tick(1.0f / 60.0f);
    }
    const float endZ = c->location().z;
    CHECK(endZ < startZ);            // it moved
    CHECK(endZ > -4.6f);             // but the wall stopped it short
}

TEST(character_jumps) {
    Fixture f;
    f.ground();
    auto* c = f.world.spawn<Character>("Hero", {0, 2, 0});
    f.world.beginPlay();
    f.run(1.0f);
    const float restY = c->location().y;
    CHECK(c->characterMovement()->isGrounded());

    c->jump();
    f.run(0.25f);
    CHECK(c->location().y > restY + 0.5f);
    CHECK(c->characterMovement()->isFalling());

    f.run(3.0f);
    // What goes up comes back to where it started.
    CHECK_NEAR(c->location().y, restY, 0.15);
    CHECK(c->characterMovement()->isGrounded());
}

TEST(character_walks_up_a_step) {
    Fixture f;
    f.ground();
    // A raised platform whose lip is well under the step height. It is
    // long enough that the character stays on it rather than crossing to
    // the far edge and stepping off again, which would be a different
    // behaviour from the one under test.
    auto* step = f.world.spawn<StaticMeshActor>("Step", {0, 0.6f, -8});
    step->setScale({6, 0.6f, 12});
    step->rerunConstruction();

    auto* c = f.world.spawn<Character>("Hero", {0, 2, 0});
    f.world.beginPlay();
    f.run(1.0f);
    const float startY = c->location().y;

    for (int i = 0; i < 60; ++i) {
        c->characterMovement()->addInput(Vec3::Forward, 1.0f);
        f.world.tick(1.0f / 60.0f);
    }
    CHECK(c->location().z < -2.0f);         // it got onto the step
    CHECK(c->location().y > startY + 0.3f); // and it is standing higher
    CHECK(c->characterMovement()->isGrounded());
}

TEST(overlap_events_fire_once_each) {
    Fixture f;
    auto* trigger = f.world.spawn<TriggerVolume>("Trigger", {0, 0, 0});
    trigger->extent = {2, 2, 2};
    trigger->rerunConstruction();

    auto* mover = f.world.spawn<StaticMeshActor>("Mover", {10, 0, 0});
    mover->movable = true;
    mover->rerunConstruction();

    int begins = 0, ends = 0;
    trigger->onBeginOverlap.bind([&](Actor*) { ++begins; });
    trigger->onEndOverlap.bind([&](Actor*) { ++ends; });

    f.world.beginPlay();
    f.world.tick(1.0f / 60.0f);
    CHECK(begins == 0);

    mover->setLocation({0, 0, 0});
    // Several frames inside the volume must still be one Begin.
    for (int i = 0; i < 5; ++i) f.world.tick(1.0f / 60.0f);
    CHECK(begins == 1);
    CHECK(ends == 0);

    mover->setLocation({10, 0, 0});
    for (int i = 0; i < 5; ++i) f.world.tick(1.0f / 60.0f);
    CHECK(begins == 1);
    CHECK(ends == 1);
}

TEST(game_mode_spawns_player_at_start) {
    Fixture f;
    f.ground();
    auto* start = f.world.spawn<PlayerStart>("Start", {7, 1, -3});
    (void)start;

    f.world.beginPlay();
    CHECK(f.world.gameMode() != nullptr);
    CHECK(f.world.playerController() != nullptr);

    Pawn* p = f.world.playerController()->pawn();
    CHECK(p != nullptr);
    CHECK(p->getClass()->isA("Character"));
    CHECK_NEAR(p->location().x, 7.0f, 1e-3);
    CHECK_NEAR(p->location().z, -3.0f, 1e-3);
    // The controller's view resolves to the pawn's camera.
    CHECK(f.world.viewCamera() != nullptr);
}

TEST(level_round_trip) {
    Fixture f;
    f.world.setLevelName("TestLevel");
    f.world.settings().sunYaw = 123.0f;
    f.world.settings().fogDensity = 0.02f;

    auto* a = f.world.spawn<StaticMeshActor>("Crate", {1, 2, 3});
    a->mesh = "Sphere";
    a->material = "Gold";
    a->setRotation({10, 20, 30});
    a->setScale({2, 2, 2});
    a->tags = "loot,shiny";
    a->rerunConstruction();

    auto* light = f.world.spawn<PointLightActor>("Lamp", {0, 4, 0});
    light->intensity = 42.0f;
    light->lightColor = Color::fromHex("#ff8800");
    light->rerunConstruction();

    f.world.spawn<PlayerStart>("Start", {0, 1, 0});

    const std::string text = LevelSerializer::saveWorld(f.world).dump();

    // Through text, so anything that fails to escape or parse shows up.
    std::string err;
    Json doc = Json::parse(text, &err);
    CHECK(err.empty());

    AssetLibrary assets2;
    assets2.createStarterContent();
    World loaded(false);
    loaded.setAssets(&assets2);
    CHECK(LevelSerializer::loadWorld(loaded, doc));

    CHECK(loaded.levelName() == "TestLevel");
    CHECK_NEAR(loaded.settings().sunYaw, 123.0f, 1e-3);
    CHECK_NEAR(loaded.settings().fogDensity, 0.02f, 1e-5);
    CHECK(loaded.actors().size() == 3);

    auto* crate = dynamic_cast<StaticMeshActor*>(loaded.findActorByName("Crate"));
    CHECK(crate != nullptr);
    CHECK(crate->mesh == "Sphere");
    CHECK(crate->material == "Gold");
    CHECK_VEC(crate->location(), (Vec3{1, 2, 3}), 1e-4);
    CHECK_VEC(crate->scale3D(), (Vec3{2, 2, 2}), 1e-4);
    CHECK_NEAR(crate->rotation().yaw, 20.0f, 1e-3);
    CHECK(crate->hasTag("loot"));
    CHECK(crate->hasTag("shiny"));
    CHECK(!crate->hasTag("loo"));
    // The component the class built picked up the actor's construction.
    CHECK(crate->meshComponent()->mesh == "Sphere");

    auto* lamp = dynamic_cast<PointLightActor*>(loaded.findActorByName("Lamp"));
    CHECK(lamp != nullptr);
    CHECK_NEAR(lamp->intensity, 42.0f, 1e-3);
    CHECK(lamp->lightColor.toHex() == "#ff8800");
}

TEST(level_load_survives_unknown_classes) {
    Fixture f;
    Json doc = Json::object();
    doc.set("format", "forge.level");
    doc.set("version", 1);
    doc.set("name", "Partial");
    Json actors = Json::array();
    Json good = Json::object();
    good.set("class", "StaticMeshActor");
    good.set("name", "Keeper");
    actors.push(good);
    Json bad = Json::object();
    bad.set("class", "SomethingFromTheFuture");
    bad.set("name", "Ghost");
    actors.push(bad);
    doc.set("actors", actors);

    Log::get().setEchoToConsole(false);
    // A level referencing a class this build does not have must load what
    // it can rather than refusing to open.
    CHECK(LevelSerializer::loadWorld(f.world, doc));
    Log::get().setEchoToConsole(true);
    CHECK(f.world.actors().size() == 1);
    CHECK(f.world.findActorByName("Keeper") != nullptr);

    // Something that is not a level at all is rejected outright.
    Log::get().setEchoToConsole(false);
    World other(false);
    CHECK(!LevelSerializer::loadWorld(other, Json::parse("{\"format\":\"something-else\"}")));
    Log::get().setEchoToConsole(true);
}

TEST(script_runs_on_begin_play) {
    Fixture f;
    auto graph = std::make_unique<ScriptGraph>();
    graph->name = "Counter";

    // Begin Play -> Set "count" to 7
    const int begin = graph->addNode("Event.BeginPlay", {0, 0});
    const int set = graph->addNode("Var.Set", {200, 0});
    graph->node(set)->config["variable"] = "count";
    graph->node(set)->literals["Value"] = ScriptValue::make(7);
    CHECK(graph->link(begin, "Then", set, "In"));

    ScriptVariable v;
    v.name = "count";
    v.type = PinType::Int;
    v.defaultValue = ScriptValue::make(0);
    graph->variables.push_back(v);

    CHECK(graph->validate().empty());
    f.assets.addScript(std::move(graph));

    auto* a = f.world.spawn<StaticMeshActor>("Scripted");
    auto* sc = a->addComponent<ScriptComponent>("Script");
    sc->script = "Counter";
    sc->onConstruct();

    CHECK(sc->instance() != nullptr);
    CHECK(sc->instance()->getVariable("count").integer() == 0);
    f.world.beginPlay();
    CHECK(sc->instance()->getVariable("count").integer() == 7);
}

TEST(script_branch_and_math) {
    Fixture f;
    auto graph = std::make_unique<ScriptGraph>();
    graph->name = "Logic";

    // BeginPlay -> Branch( 3 + 4 > 5 ) -> Set result true / false
    const int begin = graph->addNode("Event.BeginPlay", {0, 0});
    const int add = graph->addNode("Math.Add", {100, 100});
    const int greater = graph->addNode("Math.Greater", {200, 100});
    const int branch = graph->addNode("Flow.Branch", {300, 0});
    const int setTrue = graph->addNode("Var.Set", {450, -50});
    const int setFalse = graph->addNode("Var.Set", {450, 50});

    graph->node(add)->literals["A"] = ScriptValue::make(3.0f);
    graph->node(add)->literals["B"] = ScriptValue::make(4.0f);
    graph->node(greater)->literals["B"] = ScriptValue::make(5.0f);
    graph->node(setTrue)->config["variable"] = "result";
    graph->node(setTrue)->literals["Value"] = ScriptValue::make(std::string("yes"));
    graph->node(setFalse)->config["variable"] = "result";
    graph->node(setFalse)->literals["Value"] = ScriptValue::make(std::string("no"));

    CHECK(graph->link(add, "Result", greater, "A"));
    CHECK(graph->link(greater, "Result", branch, "Condition"));
    CHECK(graph->link(begin, "Then", branch, "In"));
    CHECK(graph->link(branch, "True", setTrue, "In"));
    CHECK(graph->link(branch, "False", setFalse, "In"));
    // Exec must not connect to a data pin.
    CHECK(!graph->link(begin, "Then", greater, "A"));

    f.assets.addScript(std::move(graph));
    auto* a = f.world.spawn<StaticMeshActor>("Scripted");
    auto* sc = a->addComponent<ScriptComponent>("Script");
    sc->script = "Logic";
    sc->onConstruct();

    f.world.beginPlay();
    CHECK(sc->instance()->getVariable("result").text() == "yes");
}

TEST(script_tick_and_actor_nodes) {
    Fixture f;
    auto graph = std::make_unique<ScriptGraph>();
    graph->name = "Riser";

    // Tick -> Add Offset (0, 1 * delta, 0): a steady climb.
    const int tick = graph->addNode("Event.Tick", {0, 0});
    const int delta = graph->addNode("World.GetDelta", {100, 120});
    const int makeVec = graph->addNode("Vec.Make", {220, 120});
    const int offset = graph->addNode("Actor.AddOffset", {380, 0});
    CHECK(graph->link(delta, "Seconds", makeVec, "Y"));
    CHECK(graph->link(makeVec, "Result", offset, "Delta"));
    CHECK(graph->link(tick, "Then", offset, "In"));

    f.assets.addScript(std::move(graph));
    auto* a = f.world.spawn<StaticMeshActor>("Riser", {0, 0, 0});
    auto* sc = a->addComponent<ScriptComponent>("Script");
    sc->script = "Riser";
    sc->onConstruct();

    f.world.beginPlay();
    f.run(1.0f);
    // One unit per second, so about one unit after a second.
    CHECK_NEAR(a->location().y, 1.0f, 0.05);
}

TEST(script_overlap_event) {
    Fixture f;
    auto graph = std::make_unique<ScriptGraph>();
    graph->name = "OnTouch";
    const int ev = graph->addNode("Event.BeginOverlap", {0, 0});
    const int set = graph->addNode("Var.Set", {200, 0});
    graph->node(set)->config["variable"] = "touched";
    graph->node(set)->literals["Value"] = ScriptValue::make(true);
    CHECK(graph->link(ev, "Then", set, "In"));
    f.assets.addScript(std::move(graph));

    auto* trigger = f.world.spawn<TriggerVolume>("Trigger");
    trigger->extent = {2, 2, 2};
    trigger->rerunConstruction();
    auto* sc = trigger->addComponent<ScriptComponent>("Script");
    sc->script = "OnTouch";
    sc->onConstruct();

    auto* mover = f.world.spawn<StaticMeshActor>("Mover", {10, 0, 0});
    mover->movable = true;
    mover->rerunConstruction();

    f.world.beginPlay();
    f.world.tick(1.0f / 60.0f);
    CHECK(!sc->instance()->getVariable("touched").boolean());

    mover->setLocation(Vec3::Zero);
    f.world.tick(1.0f / 60.0f);
    CHECK(sc->instance()->getVariable("touched").boolean());
}

TEST(script_delay_resumes_later) {
    Fixture f;
    auto graph = std::make_unique<ScriptGraph>();
    graph->name = "Later";
    const int begin = graph->addNode("Event.BeginPlay", {0, 0});
    const int delay = graph->addNode("Flow.Delay", {150, 0});
    const int set = graph->addNode("Var.Set", {320, 0});
    graph->node(delay)->literals["Seconds"] = ScriptValue::make(0.5f);
    graph->node(set)->config["variable"] = "fired";
    graph->node(set)->literals["Value"] = ScriptValue::make(true);
    CHECK(graph->link(begin, "Then", delay, "In"));
    CHECK(graph->link(delay, "Then", set, "In"));
    f.assets.addScript(std::move(graph));

    auto* a = f.world.spawn<StaticMeshActor>("Waiter");
    auto* sc = a->addComponent<ScriptComponent>("Script");
    sc->script = "Later";
    sc->onConstruct();

    f.world.beginPlay();
    f.run(0.25f);
    CHECK(!sc->instance()->getVariable("fired").boolean());
    f.run(0.5f);
    CHECK(sc->instance()->getVariable("fired").boolean());
}

TEST(script_graph_round_trip_and_validation) {
    registerCoreNodes();
    ScriptGraph g;
    g.name = "Saved";
    const int begin = g.addNode("Event.BeginPlay", {10, 20});
    const int print = g.addNode("Debug.Print", {200, 20});
    g.node(print)->literals["Text"] = ScriptValue::make(std::string("hi"));
    g.node(print)->comment = "says hello";
    CHECK(g.link(begin, "Then", print, "In"));

    std::string err;
    Json parsed = Json::parse(g.toJson().dump(), &err);
    CHECK(err.empty());
    auto back = ScriptGraph::fromJson(parsed);
    CHECK(back->name == "Saved");
    CHECK(back->nodes.size() == 2);
    CHECK(back->links.size() == 1);
    CHECK(back->node(print)->literals["Text"].text() == "hi");
    CHECK(back->node(print)->comment == "says hello");
    CHECK_NEAR(back->node(begin)->editorPos.x, 10.0f, 1e-4);
    CHECK(back->validate().empty());

    // Removing a node takes its wires with it.
    back->removeNode(begin);
    CHECK(back->links.empty());
    CHECK(back->validate().empty());

    // A link to a pin that no longer exists is reported, not ignored.
    ScriptGraph broken;
    const int a = broken.addNode("Event.BeginPlay", {0, 0});
    const int b = broken.addNode("Debug.Print", {100, 0});
    broken.links.push_back({a, "NoSuchPin", b, "In"});
    CHECK(!broken.validate().empty());
}

TEST(node_library_is_populated) {
    registerCoreNodes();
    NodeLibrary& lib = NodeLibrary::get();
    CHECK(lib.all().size() > 40);
    CHECK(lib.find("Flow.Branch") != nullptr);
    CHECK(lib.find("Flow.Branch")->display == "Branch");
    CHECK(lib.find("NotAThing") == nullptr);
    CHECK(lib.categories().size() >= 6);
    CHECK(!lib.byCategory("Math").empty());
    // Search puts an exact prefix match first, so typing "bran" offers
    // Branch before anything that merely mentions it.
    auto found = lib.search("bran");
    CHECK(!found.empty());
    CHECK(found[0]->type == "Flow.Branch");
}

TEST(reflection_covers_every_actor_class) {
    // Every placeable class must be constructible and have a root, or it
    // would break the moment someone dropped it in a level.
    auto classes = ClassRegistry::get().derivedFrom("Actor", true);
    CHECK(classes.size() >= 8);
    Fixture f;
    for (const ClassInfo* cls : classes) {
        Actor* a = f.world.spawn(cls, cls->name());
        CHECK(a != nullptr);
        if (!a) continue;
        CHECK(a->root() != nullptr);
        // And it must survive a save/load round trip.
        Json j = LevelSerializer::saveActor(*a);
        CHECK(j["class"].asString() == cls->name());
    }
}

int main() {
    Log::get().setEchoToConsole(false);
    return forge_test::runAll("scene");
}
