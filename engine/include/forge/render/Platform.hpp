// ============================================================
//  Windowing.
//
//  The engine draws into a Framebuffer; a Window's only jobs are to
//  put that on screen and to report input. Two exist: one backed by
//  GLFW and OpenGL, and a headless one that renders to files.
//
//  Keeping presentation behind this interface is what lets the editor
//  and the game run unchanged on a build machine with no display —
//  and lets a test drive the editor and check the result.
// ============================================================
#pragma once

#include <memory>
#include <string>

#include "forge/gameplay/Input.hpp"
#include "forge/render/Framebuffer.hpp"

namespace forge {

struct WindowDesc {
    std::string title = "Forge";
    int width = 1280;
    int height = 720;
    bool resizable = true;
    // Ask for a real window even where one may not be available. When
    // false, or when no display can be opened, the headless path is used.
    bool wantDisplay = true;
};

class Window {
public:
    virtual ~Window() = default;

    // Falls back to the headless window when no display can be opened,
    // so a program never has to branch on whether it has a screen.
    static std::unique_ptr<Window> create(const WindowDesc& desc);
    static bool displayAvailable();

    virtual bool shouldClose() const = 0;
    virtual void requestClose() = 0;
    virtual void pollEvents(InputState& input) = 0;
    virtual void present(const Framebuffer& frame) = 0;

    virtual int width() const = 0;
    virtual int height() const = 0;
    virtual bool isHeadless() const = 0;
    // Seconds since the window opened, from whatever clock it has.
    virtual double time() const;

    virtual void setCursorCaptured(bool captured) { (void)captured; }
    virtual bool cursorCaptured() const { return false; }
};

// Renders to memory and, optionally, to numbered PNG files. Input is
// scripted: a caller pushes key and mouse state directly, which is how
// the editor gets driven in a test.
class HeadlessWindow : public Window {
public:
    HeadlessWindow(int width, int height);

    bool shouldClose() const override { return frames_ >= maxFrames_; }
    void requestClose() override { maxFrames_ = frames_; }
    void pollEvents(InputState& input) override;
    void present(const Framebuffer& frame) override;

    int width() const override { return width_; }
    int height() const override { return height_; }
    bool isHeadless() const override { return true; }
    double time() const override { return (double)frames_ / 60.0; }

    // Stop after this many frames. Zero means run until asked to close.
    void setFrameLimit(int frames) { maxFrames_ = frames; }
    int framesPresented() const { return frames_; }
    // Write every presented frame to <prefix>NNNN.png.
    void setCapturePrefix(std::string prefix) { capturePrefix_ = std::move(prefix); }
    // Write only the next presented frame, to this exact path.
    void captureNextFrame(std::string path) { captureOnce_ = std::move(path); }

    // Scripted input, applied on the next pollEvents.
    void pressKey(Key k) { pending_.push_back({k, true}); }
    void releaseKey(Key k) { pending_.push_back({k, false}); }
    void moveMouse(float x, float y) { mouse_ = {x, y}; }
    void scroll(float delta) { wheel_ += delta; }

private:
    struct KeyEvent { Key key; bool down; };
    int width_, height_;
    int frames_ = 0;
    int maxFrames_ = 0;
    std::string capturePrefix_;
    std::string captureOnce_;
    std::vector<KeyEvent> pending_;
    Vec2 mouse_{0, 0};
    float wheel_ = 0.0f;
};

} // namespace forge
