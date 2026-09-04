#include "forge/render/Platform.hpp"

#include <chrono>
#include <cstdio>

#include "forge/core/Log.hpp"

namespace forge {

double Window::time() const {
    using clock = std::chrono::steady_clock;
    static clock::time_point start = clock::now();
    return std::chrono::duration<double>(clock::now() - start).count();
}

// Provided by OpenGLBackend.cpp when it is built into the library.
#if defined(FORGE_HAS_OPENGL)
std::unique_ptr<Window> createOpenGLWindow(const WindowDesc& desc);
bool openGLDisplayAvailable();
#endif

bool Window::displayAvailable() {
#if defined(FORGE_HAS_OPENGL)
    return openGLDisplayAvailable();
#else
    return false;
#endif
}

std::unique_ptr<Window> Window::create(const WindowDesc& desc) {
#if defined(FORGE_HAS_OPENGL)
    if (desc.wantDisplay) {
        if (auto window = createOpenGLWindow(desc)) return window;
        // Not an error: a build machine has no display, and rendering to
        // files is a perfectly good thing to do there.
        FORGE_LOG("No display available; running headless");
    }
#endif
    return std::make_unique<HeadlessWindow>(desc.width, desc.height);
}

// -----------------------------------------------------------------

HeadlessWindow::HeadlessWindow(int width, int height) : width_(width), height_(height) {}

void HeadlessWindow::pollEvents(InputState& input) {
    input.newFrame();
    for (const KeyEvent& e : pending_) input.setKey(e.key, e.down);
    pending_.clear();
    input.setMousePosition(mouse_.x, mouse_.y);
    if (wheel_ != 0.0f) {
        input.addMouseWheel(wheel_);
        wheel_ = 0.0f;
    }
}

void HeadlessWindow::present(const Framebuffer& frame) {
    ++frames_;
    if (!captureOnce_.empty()) {
        frame.savePNG(captureOnce_);
        captureOnce_.clear();
    }
    if (!capturePrefix_.empty()) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%04d.png", frames_);
        frame.savePNG(capturePrefix_ + buf);
    }
}

} // namespace forge
