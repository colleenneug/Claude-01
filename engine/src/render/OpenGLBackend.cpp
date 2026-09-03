// ============================================================
//  The OpenGL window.
//
//  The engine rasterises on the CPU, so this backend's job is
//  presentation and input, not shading: it opens a window through
//  GLFW, uploads each finished frame as a texture, and draws it over
//  the viewport with a fullscreen triangle.
//
//  That split is deliberate. The renderer stays one implementation
//  with one set of results, verifiable without a GPU, and this file
//  stays small enough to be obviously correct. A hardware scene
//  renderer would slot in behind the same RenderScene the software
//  path already consumes.
// ============================================================
#include <GLFW/glfw3.h>

#include <cstdio>
#include <cstdlib>
#include <vector>

#include "forge/core/Log.hpp"
#include "forge/render/Platform.hpp"

namespace forge {

namespace {

// GLFW key codes to the engine's, so nothing above this file includes a
// windowing header.
Key translateKey(int glfwKey) {
    if (glfwKey >= GLFW_KEY_A && glfwKey <= GLFW_KEY_Z)
        return (Key)((int)Key::A + (glfwKey - GLFW_KEY_A));
    if (glfwKey >= GLFW_KEY_0 && glfwKey <= GLFW_KEY_9)
        return (Key)((int)Key::Num0 + (glfwKey - GLFW_KEY_0));
    if (glfwKey >= GLFW_KEY_F1 && glfwKey <= GLFW_KEY_F12)
        return (Key)((int)Key::F1 + (glfwKey - GLFW_KEY_F1));
    switch (glfwKey) {
        case GLFW_KEY_SPACE: return Key::Space;
        case GLFW_KEY_ENTER: case GLFW_KEY_KP_ENTER: return Key::Enter;
        case GLFW_KEY_ESCAPE: return Key::Escape;
        case GLFW_KEY_TAB: return Key::Tab;
        case GLFW_KEY_BACKSPACE: return Key::Backspace;
        case GLFW_KEY_DELETE: return Key::Delete;
        case GLFW_KEY_LEFT: return Key::Left;
        case GLFW_KEY_RIGHT: return Key::Right;
        case GLFW_KEY_UP: return Key::Up;
        case GLFW_KEY_DOWN: return Key::Down;
        case GLFW_KEY_LEFT_SHIFT: return Key::LeftShift;
        case GLFW_KEY_LEFT_CONTROL: return Key::LeftControl;
        case GLFW_KEY_LEFT_ALT: return Key::LeftAlt;
        case GLFW_KEY_RIGHT_SHIFT: return Key::RightShift;
        case GLFW_KEY_RIGHT_CONTROL: return Key::RightControl;
        case GLFW_KEY_RIGHT_ALT: return Key::RightAlt;
        case GLFW_KEY_COMMA: return Key::Comma;
        case GLFW_KEY_PERIOD: return Key::Period;
        case GLFW_KEY_SLASH: return Key::Slash;
        case GLFW_KEY_SEMICOLON: return Key::Semicolon;
        case GLFW_KEY_MINUS: return Key::Minus;
        case GLFW_KEY_EQUAL: return Key::Equal;
        case GLFW_KEY_LEFT_BRACKET: return Key::LeftBracket;
        case GLFW_KEY_RIGHT_BRACKET: return Key::RightBracket;
        default: return Key::Unknown;
    }
}

bool g_glfwReady = false;

bool ensureGlfw() {
    if (g_glfwReady) return true;
    glfwSetErrorCallback([](int code, const char* description) {
        FORGE_WARN("glfw error %d: %s", code, description ? description : "");
    });
    if (!glfwInit()) return false;
    g_glfwReady = true;
    return true;
}

class OpenGLWindow : public Window {
public:
    OpenGLWindow(GLFWwindow* handle, int width, int height)
        : handle_(handle), width_(width), height_(height) {
        glfwSetWindowUserPointer(handle_, this);
        glfwSetScrollCallback(handle_, [](GLFWwindow* w, double, double y) {
            auto* self = (OpenGLWindow*)glfwGetWindowUserPointer(w);
            self->wheel_ += (float)y;
        });
        glfwSetCharCallback(handle_, [](GLFWwindow* w, unsigned int codepoint) {
            auto* self = (OpenGLWindow*)glfwGetWindowUserPointer(w);
            if (codepoint >= 32 && codepoint < 127) self->typed_ += (char)codepoint;
        });
        glfwSetFramebufferSizeCallback(handle_, [](GLFWwindow* w, int fw, int fh) {
            auto* self = (OpenGLWindow*)glfwGetWindowUserPointer(w);
            self->width_ = fw;
            self->height_ = fh;
        });

        glGenTextures(1, &texture_);
        glBindTexture(GL_TEXTURE_2D, texture_);
        // Nearest: the framebuffer is already at window resolution, and
        // filtering would only blur the interface's one-pixel lines.
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }

    ~OpenGLWindow() override {
        if (texture_) glDeleteTextures(1, &texture_);
        if (handle_) glfwDestroyWindow(handle_);
    }

    bool shouldClose() const override { return glfwWindowShouldClose(handle_); }
    void requestClose() override { glfwSetWindowShouldClose(handle_, 1); }

    void pollEvents(InputState& input) override {
        input.newFrame();
        typed_.clear();
        glfwPollEvents();

        for (int code = 32; code < GLFW_KEY_LAST; ++code) {
            const Key key = translateKey(code);
            if (key == Key::Unknown) continue;
            input.setKey(key, glfwGetKey(handle_, code) == GLFW_PRESS);
        }
        input.setKey(Key::MouseLeft, glfwGetMouseButton(handle_, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS);
        input.setKey(Key::MouseRight, glfwGetMouseButton(handle_, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS);
        input.setKey(Key::MouseMiddle, glfwGetMouseButton(handle_, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS);

        double mx = 0.0, my = 0.0;
        glfwGetCursorPos(handle_, &mx, &my);
        input.setMousePosition((float)mx, (float)my);
        if (wheel_ != 0.0f) {
            input.addMouseWheel(wheel_);
            wheel_ = 0.0f;
        }
    }

    void present(const Framebuffer& frame) override {
        if (!frame.valid()) return;
        glfwMakeContextCurrent(handle_);

        const std::vector<uint8_t> rgba = frame.toRGBA8();
        glBindTexture(GL_TEXTURE_2D, texture_);
        if (frame.width() != texWidth_ || frame.height() != texHeight_) {
            texWidth_ = frame.width();
            texHeight_ = frame.height();
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, texWidth_, texHeight_, 0, GL_RGBA,
                         GL_UNSIGNED_BYTE, rgba.data());
        } else {
            // Re-uploading into an existing allocation avoids a
            // reallocation every frame.
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, texWidth_, texHeight_, GL_RGBA,
                            GL_UNSIGNED_BYTE, rgba.data());
        }

        glViewport(0, 0, width_, height_);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glEnable(GL_TEXTURE_2D);
        glDisable(GL_DEPTH_TEST);

        // Fixed-function is all this needs, and it works on every GL
        // context GLFW will hand back including the compatibility ones
        // a headless or virtualised driver tends to offer.
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        glBegin(GL_TRIANGLE_STRIP);
        // V is flipped: the framebuffer's first row is the top, and GL's
        // texture origin is the bottom.
        glTexCoord2f(0.0f, 1.0f); glVertex2f(-1.0f, -1.0f);
        glTexCoord2f(1.0f, 1.0f); glVertex2f( 1.0f, -1.0f);
        glTexCoord2f(0.0f, 0.0f); glVertex2f(-1.0f,  1.0f);
        glTexCoord2f(1.0f, 0.0f); glVertex2f( 1.0f,  1.0f);
        glEnd();

        glfwSwapBuffers(handle_);
    }

    int width() const override { return width_; }
    int height() const override { return height_; }
    bool isHeadless() const override { return false; }
    double time() const override { return glfwGetTime(); }

    void setCursorCaptured(bool captured) override {
        captured_ = captured;
        glfwSetInputMode(handle_, GLFW_CURSOR, captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    }
    bool cursorCaptured() const override { return captured_; }

private:
    GLFWwindow* handle_ = nullptr;
    int width_ = 0, height_ = 0;
    unsigned int texture_ = 0;
    int texWidth_ = 0, texHeight_ = 0;
    float wheel_ = 0.0f;
    std::string typed_;
    bool captured_ = false;
};

} // namespace

bool openGLDisplayAvailable() {
    // Asking GLFW to start is the only reliable test: an environment can
    // have the libraries and still have nothing to display on.
    const char* display = std::getenv("DISPLAY");
    const char* wayland = std::getenv("WAYLAND_DISPLAY");
    if (!display && !wayland) return false;
    return ensureGlfw();
}

std::unique_ptr<Window> createOpenGLWindow(const WindowDesc& desc) {
    if (!openGLDisplayAvailable()) return nullptr;

    glfwWindowHint(GLFW_RESIZABLE, desc.resizable ? GLFW_TRUE : GLFW_FALSE);
    GLFWwindow* handle = glfwCreateWindow(desc.width, desc.height, desc.title.c_str(),
                                          nullptr, nullptr);
    if (!handle) return nullptr;

    glfwMakeContextCurrent(handle);
    glfwSwapInterval(1);

    int fw = desc.width, fh = desc.height;
    glfwGetFramebufferSize(handle, &fw, &fh);
    FORGE_LOG("Opened a %dx%d window", fw, fh);
    return std::make_unique<OpenGLWindow>(handle, fw, fh);
}

} // namespace forge
