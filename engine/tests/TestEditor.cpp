#include "Test.hpp"

#include "forge/core/Log.hpp"
#include "forge/editor/Editor.hpp"
#include "forge/gameplay/Actors.hpp"
#include "forge/gameplay/GameFramework.hpp"
#include "forge/render/Platform.hpp"
#include "forge/scene/Level.hpp"

using namespace forge;

namespace {

// Runs the editor for a number of frames with a stable timestep, so a
// test asserts on deterministic behaviour rather than on frame timing.
struct Harness {
    Editor editor;
    Framebuffer frame{960, 600};
    InputState input;

    Harness() {
        editor.setup();
        input.setMousePosition(480.0f, 300.0f);
    }

    void step(int frames = 1) {
        for (int i = 0; i < frames; ++i) {
            input.newFrame();
            editor.tick(1.0f / 60.0f, input);
            editor.render(frame);
        }
    }
};

} // namespace

TEST(editor_starts_with_a_usable_level) {
    Harness h;
    h.step(2);
    // A fresh editor should not be an empty void: there is something to
    // look at and somewhere for the player to start.
    CHECK(h.editor.editorWorld().actors().size() >= 4);
    CHECK(h.editor.editorWorld().findActorByName("Ground") != nullptr);
    CHECK(!h.editor.editorWorld().actorsOfClass("PlayerStart").empty());
    CHECK(!h.editor.isDirty());
    CHECK(h.editor.selection() == nullptr);
    // And it drew something rather than a blank frame.
    CHECK(h.frame.averageLuminance() > 0.01f);
}

TEST(editor_camera_stays_where_it_was_put) {
    Harness h;
    const Vec3 before = h.editor.camera().position;
    h.step(10);
    // With no input the camera must not drift. This pins down a bug where
    // the focus interpolation ran on the first frame toward a target
    // nobody had set, dragging the view to the origin.
    CHECK_VEC(h.editor.camera().position, before, 1e-3);
    CHECK(h.editor.camera().position.y > 0.0f);
}

TEST(editor_camera_focus_frames_a_target) {
    Harness h;
    Actor* ground = h.editor.editorWorld().findActorByName("Ground");
    CHECK(ground != nullptr);
    h.editor.select(ground);
    h.editor.focusSelection();
    h.step(40);
    // Framing must end up looking at the thing, from far enough away to
    // see all of it.
    const Vec3 toTarget = ground->worldBounds().center() - h.editor.camera().position;
    const Vec3 facing = h.editor.camera().rotation.toQuat().forward();
    CHECK(dot(toTarget.normalized(), facing) > 0.9f);
    CHECK(toTarget.length() > 10.0f);
}

TEST(editor_places_and_deletes_actors) {
    Harness h;
    const size_t before = h.editor.editorWorld().actors().size();

    Actor* placed = h.editor.spawnClass(ClassRegistry::get().find("PointLightActor"));
    CHECK(placed != nullptr);
    CHECK(h.editor.editorWorld().actors().size() == before + 1);
    // Placing selects what you placed, and marks the level dirty.
    CHECK(h.editor.selection() == placed);
    CHECK(h.editor.isDirty());
    // It lands in front of the camera, not at the origin.
    CHECK(distance(placed->location(), h.editor.camera().position) < 20.0f);

    h.editor.duplicateSelection();
    CHECK(h.editor.editorWorld().actors().size() == before + 2);
    CHECK(h.editor.selection() != placed);
    // A duplicate is offset, or it would hide inside the original.
    CHECK(distance(h.editor.selection()->location(), placed->location()) > 0.5f);

    h.editor.deleteSelection();
    CHECK(h.editor.editorWorld().actors().size() == before + 1);
    CHECK(h.editor.selection() == nullptr);

    // Abstract and non-placeable classes are refused rather than crashing.
    CHECK(h.editor.spawnClass(ClassRegistry::get().find("ActorComponent")) == nullptr);
    CHECK(h.editor.spawnClass(nullptr) == nullptr);
}

TEST(play_in_editor_leaves_the_edited_world_alone) {
    Harness h;
    World& edit = h.editor.editorWorld();
    const size_t editCount = edit.actors().size();
    Actor* pickup = edit.findActorByName("Pickup");
    CHECK(pickup != nullptr);
    const Vec3 pickupBefore = pickup->location();

    h.editor.play();
    CHECK(h.editor.isPlaying());
    // Play builds a second world; the GameMode adds a controller and a
    // pawn to it, and the edited world knows nothing about them.
    CHECK(&h.editor.world() != &edit);
    CHECK(h.editor.world().actors().size() > editCount);
    CHECK(h.editor.world().playerController() != nullptr);
    CHECK(h.editor.world().playerController()->pawn() != nullptr);
    CHECK(edit.playerController() == nullptr);

    h.step(90);
    CHECK(h.editor.isPlaying());
    CHECK(edit.actors().size() == editCount);
    // The pickup spins during play. The one in the edited world must not
    // have moved at all.
    CHECK_VEC(pickup->location(), pickupBefore, 1e-4);

    h.editor.stop();
    CHECK(!h.editor.isPlaying());
    CHECK(&h.editor.world() == &edit);
    CHECK(edit.actors().size() == editCount);
    CHECK_VEC(pickup->location(), pickupBefore, 1e-4);
}

TEST(play_can_be_paused_and_resumed) {
    Harness h;
    h.editor.play();
    h.step(30);
    Pawn* pawn = h.editor.world().playerController()->pawn();
    CHECK(pawn != nullptr);

    h.editor.togglePause();
    CHECK(h.editor.world().isPaused());
    const Vec3 held = pawn->location();
    h.step(30);
    CHECK_VEC(pawn->location(), held, 1e-4);

    h.editor.togglePause();
    CHECK(!h.editor.world().isPaused());
    h.editor.stop();
}

TEST(editor_saves_and_reloads_a_level) {
    Harness h;
    Actor* placed = h.editor.spawnClass(ClassRegistry::get().find("TriggerVolume"));
    CHECK(placed != nullptr);
    placed->setActorName("MyTrigger");
    placed->setLocation({3.5f, 2.0f, -7.25f});
    CHECK(h.editor.isDirty());

    const std::string path = "/tmp/forge_editor_test.flevel";
    CHECK(h.editor.saveLevel(path));
    // Saving clears the dirty marker and remembers where it went.
    CHECK(!h.editor.isDirty());
    CHECK(h.editor.levelPath() == path);

    const size_t count = h.editor.editorWorld().actors().size();
    h.editor.newLevel();
    CHECK(h.editor.editorWorld().findActorByName("MyTrigger") == nullptr);

    CHECK(h.editor.loadLevel(path));
    CHECK(h.editor.editorWorld().actors().size() == count);
    Actor* back = h.editor.editorWorld().findActorByName("MyTrigger");
    CHECK(back != nullptr);
    if (back) CHECK_VEC(back->location(), (Vec3{3.5f, 2.0f, -7.25f}), 1e-4);
    CHECK(!h.editor.isDirty());
    std::remove(path.c_str());

    // A path that does not exist fails rather than emptying the level.
    Log::get().setEchoToConsole(false);
    CHECK(!h.editor.loadLevel("/tmp/forge_no_such_level.flevel"));
    Log::get().setEchoToConsole(true);
}

TEST(details_panel_can_reach_every_property_of_every_class) {
    // The details panel picks a widget from a property's type. If a class
    // ever declares a type the panel has no case for, this catches it
    // before someone selects that actor and finds a blank row.
    Harness h;
    for (const ClassInfo* cls : ClassRegistry::get().derivedFrom("Actor", true)) {
        for (const Property* p : cls->allProperties()) {
            const bool drawable =
                p->type == PropType::Bool || p->type == PropType::Int ||
                p->type == PropType::Float || p->type == PropType::String ||
                p->type == PropType::Vec3 || p->type == PropType::Rotator ||
                p->type == PropType::Color || p->type == PropType::Enum;
            CHECK(drawable);
            // An enum without names would render an empty dropdown.
            if (p->type == PropType::Enum) CHECK(!p->enumNames.empty());
            // A class reference needs a base to filter by, or the picker
            // would offer every class in the engine.
            if (p->ref == RefKind::Class) CHECK(!p->classFilter.empty());
        }
    }
}

TEST(every_placeable_class_survives_being_selected) {
    // Placing one of everything and rendering with each selected exercises
    // the details panel, the component tree and the selection outline
    // against every class at once.
    Harness h;
    std::vector<Actor*> placed;
    for (const ClassInfo* cls : ClassRegistry::get().derivedFrom("Actor", true)) {
        if (!cls->placeable() || cls->isA("GameMode") || cls->isA("PlayerController")) continue;
        Actor* a = h.editor.editorWorld().spawn(cls, cls->name());
        CHECK(a != nullptr);
        if (a) placed.push_back(a);
    }
    CHECK(placed.size() >= 8);
    for (Actor* a : placed) {
        h.editor.select(a);
        h.step(1);
        CHECK(h.editor.selection() == a);
    }
}

TEST(headless_window_drives_and_captures) {
    HeadlessWindow window(320, 200);
    CHECK(window.isHeadless());
    CHECK(window.width() == 320);
    window.setFrameLimit(3);

    InputState input;
    Framebuffer frame(320, 200);
    frame.clearColor(Color::fromHex("#336699"));

    // Scripted input arrives through the same path a real window uses.
    window.pressKey(Key::W);
    window.moveMouse(100.0f, 50.0f);
    window.pollEvents(input);
    CHECK(input.isDown(Key::W));
    CHECK(input.wasPressed(Key::W));
    CHECK_NEAR(input.mousePosition().x, 100.0f, 1e-3);

    window.releaseKey(Key::W);
    window.pollEvents(input);
    CHECK(!input.isDown(Key::W));
    CHECK(input.wasReleased(Key::W));

    int presented = 0;
    while (!window.shouldClose()) {
        window.present(frame);
        if (++presented > 10) break;
    }
    CHECK(window.framesPresented() == 3);

    // Window::create falls back to headless rather than failing when
    // there is no display.
    WindowDesc desc;
    desc.width = 64;
    desc.height = 64;
    desc.wantDisplay = false;
    auto created = Window::create(desc);
    CHECK(created != nullptr);
    CHECK(created->isHeadless());
}

TEST(input_mapping_reads_actions_and_axes) {
    InputState in;
    in.addDefaultMappings();

    in.newFrame();
    in.setKey(Key::W, true);
    CHECK_NEAR(in.axis("MoveForward"), 1.0f, 1e-4);
    in.setKey(Key::S, true);
    // Opposite keys cancel rather than summing.
    CHECK_NEAR(in.axis("MoveForward"), 0.0f, 1e-4);
    in.setKey(Key::W, false);
    CHECK_NEAR(in.axis("MoveForward"), -1.0f, 1e-4);

    in.newFrame();
    in.setKey(Key::Space, true);
    CHECK(in.action("Jump"));
    CHECK(in.actionPressed("Jump"));
    in.newFrame();
    // Pressed is one frame only; held stays true.
    CHECK(in.action("Jump"));
    CHECK(!in.actionPressed("Jump"));
    in.setKey(Key::Space, false);
    CHECK(in.actionReleased("Jump"));

    // An unmapped name reads as zero rather than misbehaving.
    CHECK_NEAR(in.axis("NoSuchAxis"), 0.0f, 1e-6);
    CHECK(!in.action("NoSuchAction"));

    CHECK(keyFromName("Space") == Key::Space);
    CHECK(std::string(keyName(Key::MouseLeft)) == "MouseLeft");
    CHECK(keyFromName("NotAKey") == Key::Unknown);
}

TEST(ui_layout_helpers) {
    Rect r{10, 20, 100, 50};
    CHECK_NEAR(r.right(), 110.0f, 1e-5);
    CHECK(r.contains(50.0f, 40.0f));
    CHECK(!r.contains(5.0f, 40.0f));

    Rect cut = r;
    const Rect top = cut.cutTop(10.0f);
    CHECK_NEAR(top.h, 10.0f, 1e-5);
    // Cutting shrinks what is left, which is what makes chained layout
    // work without tracking a cursor by hand.
    CHECK_NEAR(cut.y, 30.0f, 1e-5);
    CHECK_NEAR(cut.h, 40.0f, 1e-5);

    const Rect a{0, 0, 10, 10};
    const Rect overlap = a.intersect(Rect{5, 5, 10, 10});
    CHECK_NEAR(overlap.x, 5.0f, 1e-5);
    CHECK_NEAR(overlap.w, 5.0f, 1e-5);
    // No overlap yields an empty rect, not a negative one.
    const Rect apart = a.intersect(Rect{50, 50, 10, 10});
    CHECK(!apart.valid());

    CHECK(UIDraw::textWidth("abc") > 0.0f);
    CHECK(UIDraw::ellipsize("a very long asset name", 40.0f).size() <
          std::string("a very long asset name").size());
}

int main() {
    Log::get().setEchoToConsole(false);
    return forge_test::runAll("editor");
}
