// ============================================================
//  forge-editor
//
//  Opens the editor. With a display it runs interactively; without
//  one it runs headless, which is how the editor gets exercised on a
//  build machine and how the screenshots in the documentation are
//  produced.
//
//    forge-editor [level.flevel]
//    forge-editor --headless --frames 120 --shot out.png
// ============================================================
#include <cstdio>
#include <cstring>
#include <string>

#include "forge/core/Log.hpp"
#include "forge/editor/Editor.hpp"
#include "forge/render/Platform.hpp"

using namespace forge;

namespace {

struct Options {
    std::string levelPath;
    std::string shotPath;
    std::string capturePrefix;
    int width = 1440;
    int height = 860;
    int frames = 0;
    bool headless = false;
    bool listClasses = false;
};

void printUsage() {
    std::printf(
        "forge-editor -- the Forge level editor\n\n"
        "  forge-editor [level.flevel] [options]\n\n"
        "  --headless          run without a window\n"
        "  --frames N          stop after N frames (implies --headless)\n"
        "  --shot PATH         write the final frame to PATH\n"
        "  --capture PREFIX    write every frame to PREFIXnnnn.png\n"
        "  --size W H          framebuffer size (default 1440x860)\n"
        "  --list-classes      print every registered class and exit\n"
        "  --help              this text\n");
}

Options parse(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next = [&](const char* what) -> std::string {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "%s needs a value\n", what);
                std::exit(2);
            }
            return argv[++i];
        };
        if (arg == "--help" || arg == "-h") { printUsage(); std::exit(0); }
        else if (arg == "--headless") o.headless = true;
        else if (arg == "--frames") { o.frames = std::atoi(next("--frames").c_str()); o.headless = true; }
        else if (arg == "--shot") o.shotPath = next("--shot");
        else if (arg == "--capture") o.capturePrefix = next("--capture");
        else if (arg == "--list-classes") o.listClasses = true;
        else if (arg == "--size") {
            o.width = std::atoi(next("--size").c_str());
            o.height = std::atoi(next("--size").c_str());
        } else if (arg.rfind("--", 0) == 0) {
            std::fprintf(stderr, "unknown option %s\n", arg.c_str());
            std::exit(2);
        } else {
            o.levelPath = arg;
        }
    }
    return o;
}

} // namespace

int main(int argc, char** argv) {
    const Options options = parse(argc, argv);

    Editor editor;
    editor.setup();

    if (options.listClasses) {
        // Handy for checking what a build actually registered, and the
        // simplest possible smoke test of the reflection system.
        for (const ClassInfo* c : ClassRegistry::get().all()) {
            std::printf("%-28s %-14s %s%s\n", c->name().c_str(), c->category().c_str(),
                        c->isAbstract() ? "[abstract] " : "", c->description().c_str());
        }
        return 0;
    }

    if (!options.levelPath.empty()) editor.loadLevel(options.levelPath);

    WindowDesc desc;
    desc.title = "Forge Editor";
    desc.width = options.width;
    desc.height = options.height;
    desc.wantDisplay = !options.headless;
    auto window = Window::create(desc);

    if (auto* headless = dynamic_cast<HeadlessWindow*>(window.get())) {
        headless->setFrameLimit(options.frames > 0 ? options.frames : 2);
        if (!options.capturePrefix.empty()) headless->setCapturePrefix(options.capturePrefix);
        // Put the pointer over the viewport so the first frames show the
        // editor as a user would meet it rather than with nothing hovered.
        headless->moveMouse((float)options.width * 0.5f, (float)options.height * 0.45f);
    }

    Framebuffer frame(window->width(), window->height());
    InputState input;
    double previous = window->time();

    while (!window->shouldClose()) {
        const double now = window->time();
        // Clamp the step: a stall must not teleport everything through a
        // wall on the frame after it.
        const float dt = (float)clampf((float)(now - previous), 0.0f, 0.1f);
        previous = now;

        window->pollEvents(input);

        if (frame.width() != window->width() || frame.height() != window->height())
            frame.resize(window->width(), window->height());

        editor.tick(dt > 0.0f ? dt : 1.0f / 60.0f, input);
        editor.render(frame);

        if (auto* headless = dynamic_cast<HeadlessWindow*>(window.get()))
            if (!options.shotPath.empty() && headless->framesPresented() + 1 >= options.frames)
                headless->captureNextFrame(options.shotPath);

        window->present(frame);
    }

    if (!options.shotPath.empty() && !window->isHeadless()) frame.savePNG(options.shotPath);
    return 0;
}
