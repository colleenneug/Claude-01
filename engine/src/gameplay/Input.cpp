#include "forge/gameplay/Input.hpp"

#include <cstring>

namespace forge {

namespace {
struct KeyName { Key key; const char* name; };
const KeyName kKeyNames[] = {
    {Key::A, "A"}, {Key::B, "B"}, {Key::C, "C"}, {Key::D, "D"}, {Key::E, "E"}, {Key::F, "F"},
    {Key::G, "G"}, {Key::H, "H"}, {Key::I, "I"}, {Key::J, "J"}, {Key::K, "K"}, {Key::L, "L"},
    {Key::M, "M"}, {Key::N, "N"}, {Key::O, "O"}, {Key::P, "P"}, {Key::Q, "Q"}, {Key::R, "R"},
    {Key::S, "S"}, {Key::T, "T"}, {Key::U, "U"}, {Key::V, "V"}, {Key::W, "W"}, {Key::X, "X"},
    {Key::Y, "Y"}, {Key::Z, "Z"},
    {Key::Num0, "0"}, {Key::Num1, "1"}, {Key::Num2, "2"}, {Key::Num3, "3"}, {Key::Num4, "4"},
    {Key::Num5, "5"}, {Key::Num6, "6"}, {Key::Num7, "7"}, {Key::Num8, "8"}, {Key::Num9, "9"},
    {Key::Space, "Space"}, {Key::Enter, "Enter"}, {Key::Escape, "Escape"}, {Key::Tab, "Tab"},
    {Key::Backspace, "Backspace"}, {Key::Delete, "Delete"},
    {Key::Left, "Left"}, {Key::Right, "Right"}, {Key::Up, "Up"}, {Key::Down, "Down"},
    {Key::LeftShift, "LeftShift"}, {Key::LeftControl, "LeftControl"}, {Key::LeftAlt, "LeftAlt"},
    {Key::RightShift, "RightShift"}, {Key::RightControl, "RightControl"}, {Key::RightAlt, "RightAlt"},
    {Key::F1, "F1"}, {Key::F2, "F2"}, {Key::F3, "F3"}, {Key::F4, "F4"}, {Key::F5, "F5"},
    {Key::F6, "F6"}, {Key::F7, "F7"}, {Key::F8, "F8"}, {Key::F9, "F9"}, {Key::F10, "F10"},
    {Key::F11, "F11"}, {Key::F12, "F12"},
    {Key::Comma, ","}, {Key::Period, "."}, {Key::Slash, "/"}, {Key::Semicolon, ";"},
    {Key::Minus, "-"}, {Key::Equal, "="}, {Key::LeftBracket, "["}, {Key::RightBracket, "]"},
    {Key::MouseLeft, "MouseLeft"}, {Key::MouseRight, "MouseRight"}, {Key::MouseMiddle, "MouseMiddle"},
};
}

const char* keyName(Key k) {
    for (const KeyName& kn : kKeyNames) if (kn.key == k) return kn.name;
    return "Unknown";
}

Key keyFromName(const std::string& name) {
    for (const KeyName& kn : kKeyNames) if (name == kn.name) return kn.key;
    return Key::Unknown;
}

void InputState::setKey(Key k, bool isDown) {
    if (k == Key::Unknown || k >= Key::Count) return;
    down_[(size_t)k] = isDown;
}

void InputState::setMousePosition(float x, float y) {
    if (!haveMouse_) {
        // The first sample would otherwise read as an enormous delta from
        // the origin and snap the camera round.
        lastMouse_ = {x, y};
        haveMouse_ = true;
    }
    mouse_ = {x, y};
}

void InputState::addMouseWheel(float delta) { wheel_ += delta; }

void InputState::setMouseCaptured(bool captured) {
    captured_ = captured;
    // Capturing warps the cursor, so the next delta must not include the
    // jump to the centre.
    haveMouse_ = false;
}

void InputState::newFrame() {
    std::memcpy(prev_, down_, sizeof(prev_));
    mouseDelta_ = mouse_ - lastMouse_;
    lastMouse_ = mouse_;
    wheel_ = 0.0f;
}

bool InputState::isDown(Key k) const {
    return k != Key::Unknown && k < Key::Count && down_[(size_t)k];
}
bool InputState::wasPressed(Key k) const {
    return k != Key::Unknown && k < Key::Count && down_[(size_t)k] && !prev_[(size_t)k];
}
bool InputState::wasReleased(Key k) const {
    return k != Key::Unknown && k < Key::Count && !down_[(size_t)k] && prev_[(size_t)k];
}

const AxisMapping* InputState::findAxis(const std::string& name) const {
    for (const AxisMapping& a : axes_) if (a.name == name) return &a;
    return nullptr;
}

const ActionMapping* InputState::findAction(const std::string& name) const {
    for (const ActionMapping& a : actions_) if (a.name == name) return &a;
    return nullptr;
}

float InputState::axis(const std::string& name) const {
    const AxisMapping* m = findAxis(name);
    if (!m) return 0.0f;
    float value = 0.0f;
    for (const AxisBinding& b : m->keys)
        if (isDown(b.key)) value += b.scale;
    switch (m->analog) {
        case AnalogSource::MouseX: value += mouseDelta_.x * m->analogScale; break;
        case AnalogSource::MouseY: value += mouseDelta_.y * m->analogScale; break;
        case AnalogSource::MouseWheel: value += wheel_ * m->analogScale; break;
        case AnalogSource::None: break;
    }
    // Digital bindings clamp so opposite keys cancel to zero rather than
    // summing to two; analog axes pass through unclamped.
    if (m->analog == AnalogSource::None) value = clampf(value, -1.0f, 1.0f);
    return value;
}

bool InputState::action(const std::string& name) const {
    const ActionMapping* m = findAction(name);
    if (!m) return false;
    for (Key k : m->keys) if (isDown(k)) return true;
    return false;
}

bool InputState::actionPressed(const std::string& name) const {
    const ActionMapping* m = findAction(name);
    if (!m) return false;
    for (Key k : m->keys) if (wasPressed(k)) return true;
    return false;
}

bool InputState::actionReleased(const std::string& name) const {
    const ActionMapping* m = findAction(name);
    if (!m) return false;
    // Released only when the last held key of the action comes up.
    bool any = false;
    for (Key k : m->keys) {
        if (isDown(k)) return false;
        if (wasReleased(k)) any = true;
    }
    return any;
}

void InputState::addAxis(const std::string& name, Key positive, Key negative) {
    for (AxisMapping& a : axes_) {
        if (a.name != name) continue;
        if (positive != Key::Unknown) a.keys.push_back({positive, 1.0f});
        if (negative != Key::Unknown) a.keys.push_back({negative, -1.0f});
        return;
    }
    AxisMapping m;
    m.name = name;
    if (positive != Key::Unknown) m.keys.push_back({positive, 1.0f});
    if (negative != Key::Unknown) m.keys.push_back({negative, -1.0f});
    axes_.push_back(std::move(m));
}

void InputState::addAxis(const std::string& name, AnalogSource analog, float scale) {
    for (AxisMapping& a : axes_) {
        if (a.name != name) continue;
        a.analog = analog;
        a.analogScale = scale;
        return;
    }
    AxisMapping m;
    m.name = name;
    m.analog = analog;
    m.analogScale = scale;
    axes_.push_back(std::move(m));
}

void InputState::addAction(const std::string& name, Key key) {
    for (ActionMapping& a : actions_) {
        if (a.name != name) continue;
        a.keys.push_back(key);
        return;
    }
    actions_.push_back({name, {key}});
}

void InputState::clearMappings() {
    axes_.clear();
    actions_.clear();
}

void InputState::addDefaultMappings() {
    addAxis("MoveForward", Key::W, Key::S);
    addAxis("MoveForward", Key::Up, Key::Down);
    addAxis("MoveRight", Key::D, Key::A);
    addAxis("MoveRight", Key::Right, Key::Left);
    addAxis("MoveUp", Key::E, Key::Q);
    addAxis("Turn", AnalogSource::MouseX, 0.15f);
    addAxis("LookUp", AnalogSource::MouseY, -0.15f);
    addAxis("Zoom", AnalogSource::MouseWheel, 1.0f);

    addAction("Jump", Key::Space);
    addAction("Sprint", Key::LeftShift);
    addAction("Crouch", Key::LeftControl);
    addAction("Fire", Key::MouseLeft);
    addAction("Aim", Key::MouseRight);
    addAction("Interact", Key::F);
    addAction("Pause", Key::Escape);
}

} // namespace forge
