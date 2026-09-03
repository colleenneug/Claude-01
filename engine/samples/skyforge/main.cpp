// ============================================================
//  Skyforge — the application.
//
//  The game itself is in Skyforge.cpp; this only builds the level,
//  opens a window and runs the loop. Keeping the two apart is what
//  lets the game be played by a person here and driven by a test in
//  tests/TestGame.cpp.
//
//    forge-sample                 build the level and play it
//    forge-sample --write PATH    write the level as a .flevel and exit
//    forge-sample --headless --frames 600 --shot out.png
// ============================================================
#include <cstdio>
#include <cstdlib>
#include <string>

#include "Skyforge.hpp"

#include "forge/assets/AssetLibrary.hpp"
#include "forge/components/RenderComponents.hpp"
#include "forge/core/Log.hpp"
#include "forge/render/Platform.hpp"
#include "forge/render/SoftwareRenderer.hpp"
#include "forge/scene/Level.hpp"
#include "forge/scene/World.hpp"
#include "forge/script/ScriptGraph.hpp"

using namespace forge;

int main(int argc, char** argv) {
    std::string writePath, shotPath;
    int frames = 0, width = 1280, height = 720;
    bool headless = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : std::string(); };
        if (arg == "--write") writePath = next();
        else if (arg == "--headless") headless = true;
        else if (arg == "--frames") { frames = std::atoi(next().c_str()); headless = true; }
        else if (arg == "--shot") shotPath = next();
        else if (arg == "--size") { width = std::atoi(next().c_str()); height = std::atoi(next().c_str()); }
        else if (arg == "--help") {
            std::printf("forge-sample [--write LEVEL] [--headless] [--frames N] "
                        "[--shot PNG] [--size W H]\n");
            return 0;
        }
    }

    registerCoreNodes();
    AssetLibrary assets;
    assets.createStarterContent();

    World world(false);
    world.setAssets(&assets);
    skyforge::buildLevel(world, assets);

    if (!writePath.empty()) {
        const bool ok = LevelSerializer::saveToFile(world, writePath);
        std::printf("%s %s\n", ok ? "wrote" : "failed to write", writePath.c_str());
        return ok ? 0 : 1;
    }

    WindowDesc desc;
    desc.title = "Skyforge";
    desc.width = width;
    desc.height = height;
    desc.wantDisplay = !headless;
    auto window = Window::create(desc);
    if (auto* h = dynamic_cast<HeadlessWindow*>(window.get()))
        h->setFrameLimit(frames > 0 ? frames : 240);

    Framebuffer frame(window->width(), window->height());
    SoftwareRenderer renderer;
    InputState input;
    input.addDefaultMappings();

    world.beginPlay();
    // The controller reads the same input the window fills in; that is
    // the whole connection between the platform layer and gameplay.
    if (PlayerController* pc = world.playerController()) pc->setInput(&input);

    double previous = window->time();
    while (!window->shouldClose()) {
        const double now = window->time();
        // Headless has no meaningful clock, and a fixed step keeps a
        // recorded run reproducible.
        float dt = window->isHeadless() ? 1.0f / 60.0f : (float)(now - previous);
        previous = now;
        dt = clampf(dt, 0.0f, 0.1f);

        window->pollEvents(input);
        if (input.wasPressed(Key::Escape)) window->requestClose();

        if (frame.width() != window->width() || frame.height() != window->height())
            frame.resize(window->width(), window->height());

        world.tick(dt);

        RenderView view;
        if (CameraComponent* cam = world.viewCamera()) {
            view.view = cam->viewMatrix();
            view.projection = cam->projectionMatrix(frame.aspect());
            view.cameraPosition = cam->worldLocation();
        } else {
            const Vec3 eye{18, 14, 16};
            view.view = Mat4::lookAt(eye, {0, 2, -14}, Vec3::Up);
            view.projection = Mat4::perspective(60.0f, frame.aspect(), 0.1f, 500.0f);
            view.cameraPosition = eye;
        }
        renderer.render(frame, RenderScene::collect(world, false), view);
        window->present(frame);
    }

    if (!shotPath.empty()) frame.savePNG(shotPath);
    world.endPlay();
    return 0;
}
