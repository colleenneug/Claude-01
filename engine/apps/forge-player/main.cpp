// ============================================================
//  forge-player
//
//  Plays a level with no editor around it: this is what a finished
//  game runs as. It loads a .flevel, starts play, and renders through
//  whatever camera the GameMode set up.
//
//    forge-player level.flevel
//    forge-player level.flevel --headless --frames 300 --shot out.png
// ============================================================
#include <cstdio>
#include <cstdlib>
#include <string>

#include "forge/assets/AssetLibrary.hpp"
#include "forge/components/RenderComponents.hpp"
#include "forge/core/Log.hpp"
#include "forge/gameplay/GameFramework.hpp"
#include "forge/render/Platform.hpp"
#include "forge/render/SoftwareRenderer.hpp"
#include "forge/scene/Level.hpp"
#include "forge/scene/World.hpp"
#include "forge/script/ScriptGraph.hpp"

using namespace forge;

int main(int argc, char** argv) {
    std::string levelPath, assetPath, shotPath;
    int width = 1280, height = 720, frames = 0;
    bool headless = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : std::string(); };
        if (arg == "--headless") headless = true;
        else if (arg == "--frames") { frames = std::atoi(next().c_str()); headless = true; }
        else if (arg == "--shot") shotPath = next();
        else if (arg == "--assets") assetPath = next();
        else if (arg == "--size") { width = std::atoi(next().c_str()); height = std::atoi(next().c_str()); }
        else if (arg == "--help") {
            std::printf("forge-player LEVEL [--assets FILE] [--headless] [--frames N] "
                        "[--shot PATH] [--size W H]\n");
            return 0;
        } else levelPath = arg;
    }

    if (levelPath.empty()) {
        std::fprintf(stderr, "usage: forge-player LEVEL.flevel [options]\n");
        return 2;
    }

    registerCoreNodes();
    AssetLibrary assets;
    assets.createStarterContent();
    if (!assetPath.empty()) assets.load(assetPath);

    World world(false);
    world.setAssets(&assets);
    if (!LevelSerializer::loadFromFile(world, levelPath)) return 1;

    WindowDesc desc;
    desc.title = "Forge - " + world.levelName();
    desc.width = width;
    desc.height = height;
    desc.wantDisplay = !headless;
    auto window = Window::create(desc);
    if (auto* h = dynamic_cast<HeadlessWindow*>(window.get()))
        h->setFrameLimit(frames > 0 ? frames : 120);

    Framebuffer frame(window->width(), window->height());
    SoftwareRenderer renderer;
    InputState input;
    input.addDefaultMappings();

    world.beginPlay();
    // The controller reads from the same input the window fills in, which
    // is the whole connection between the platform layer and gameplay.
    if (PlayerController* pc = world.playerController()) pc->setInput(&input);

    double previous = window->time();
    while (!window->shouldClose()) {
        const double now = window->time();
        float dt = (float)(now - previous);
        previous = now;
        // Headless has no real clock to speak of; a fixed step keeps a
        // recorded run reproducible.
        if (window->isHeadless()) dt = 1.0f / 60.0f;
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
            view.nearClip = cam->nearClip;
            view.farClip = cam->farClip;
        } else {
            // No camera is a level authoring problem, not a crash: show
            // an overview so the mistake is visible.
            const Vec3 eye{12, 10, 18};
            view.view = Mat4::lookAt(eye, Vec3::Zero, Vec3::Up);
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
