// ============================================================
//  Input.
//
//  Gameplay never asks about keys. It asks about *actions* ("Jump")
//  and *axes* ("MoveForward"), and a mapping table says which physical
//  inputs feed them. That indirection is what lets a project rebind
//  controls, support a gamepad, and read the same on every backend.
//
//  The platform layer pushes raw key and mouse state in; everything
//  above reads actions and axes out.
// ============================================================
#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "forge/math/Math.hpp"

namespace forge {

// Backend-independent key codes. The GL backend maps GLFW codes onto
// these; the headless backend synthesises them directly.
enum class Key {
    Unknown = 0,
    A, B, C, D, E, F, G, H, I, J, K, L, M,
    N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    Num0, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
    Space, Enter, Escape, Tab, Backspace, Delete,
    Left, Right, Up, Down,
    LeftShift, LeftControl, LeftAlt, RightShift, RightControl, RightAlt,
    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
    Comma, Period, Slash, Semicolon, Minus, Equal, LeftBracket, RightBracket,
    MouseLeft, MouseRight, MouseMiddle,
    Count
};

const char* keyName(Key k);
Key keyFromName(const std::string& name);

struct AxisBinding {
    Key key = Key::Unknown;
    float scale = 1.0f;
};

// Mouse motion and the wheel are continuous, so they feed axes directly
// rather than through a key.
enum class AnalogSource { None, MouseX, MouseY, MouseWheel };

struct AxisMapping {
    std::string name;
    std::vector<AxisBinding> keys;
    AnalogSource analog = AnalogSource::None;
    float analogScale = 1.0f;
};

struct ActionMapping {
    std::string name;
    std::vector<Key> keys;
};

class InputState {
public:
    // ---- pushed in by the platform layer ----
    void setKey(Key k, bool down);
    void setMousePosition(float x, float y);
    void addMouseWheel(float delta);
    void setMouseCaptured(bool captured);
    // Call once per frame, after events and before gameplay reads.
    void newFrame();

    // ---- read by gameplay ----
    bool isDown(Key k) const;
    bool wasPressed(Key k) const;    // this frame only
    bool wasReleased(Key k) const;

    float axis(const std::string& name) const;
    bool action(const std::string& name) const;         // held
    bool actionPressed(const std::string& name) const;  // this frame
    bool actionReleased(const std::string& name) const;

    Vec2 mousePosition() const { return mouse_; }
    Vec2 mouseDelta() const { return mouseDelta_; }
    float mouseWheel() const { return wheel_; }
    bool mouseCaptured() const { return captured_; }

    // ---- mappings ----
    void addAxis(const std::string& name, Key positive, Key negative);
    void addAxis(const std::string& name, AnalogSource analog, float scale = 1.0f);
    void addAction(const std::string& name, Key key);
    void clearMappings();
    // Movement, looking and jumping, so a new project is playable before
    // anyone opens the input settings.
    void addDefaultMappings();

    const std::vector<AxisMapping>& axes() const { return axes_; }
    const std::vector<ActionMapping>& actions() const { return actions_; }

private:
    const AxisMapping* findAxis(const std::string& name) const;
    const ActionMapping* findAction(const std::string& name) const;

    bool down_[(size_t)Key::Count] = {};
    bool prev_[(size_t)Key::Count] = {};
    Vec2 mouse_{0, 0};
    Vec2 mouseDelta_{0, 0};
    Vec2 lastMouse_{0, 0};
    float wheel_ = 0.0f;
    bool captured_ = false;
    bool haveMouse_ = false;

    std::vector<AxisMapping> axes_;
    std::vector<ActionMapping> actions_;
};

} // namespace forge
