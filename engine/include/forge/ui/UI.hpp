// ============================================================
//  Immediate-mode widgets.
//
//  There is no retained widget tree: each frame the editor calls
//  button(), slider(), treeItem() and so on, and each call both draws
//  the control and reports what the user did to it. State that has to
//  survive between frames — which control is being dragged, which text
//  field has focus, how far a list is scrolled — lives in the context,
//  keyed by an id derived from the caller's label and position.
//
//  This suits an editor being built alongside the engine: a panel is
//  the function that draws it, and adding a control is one line.
// ============================================================
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "forge/gameplay/Input.hpp"
#include "forge/ui/UIDraw.hpp"

namespace forge {

// One place for every colour the editor uses, so the whole thing can be
// re-themed by editing this and nothing else.
struct UITheme {
    Color background = Color::fromHex("#1b1e24");
    Color panel = Color::fromHex("#23272f");
    Color panelHeader = Color::fromHex("#2c313a");
    Color control = Color::fromHex("#31363f");
    Color controlHover = Color::fromHex("#3b414c");
    Color controlActive = Color::fromHex("#4a5160");
    Color accent = Color::fromHex("#5fa8d3");
    Color accentDim = Color::fromHex("#3d6d8a");
    Color warning = Color::fromHex("#e0a458");
    Color danger = Color::fromHex("#d1615d");
    Color success = Color::fromHex("#7bb661");
    Color text = Color::fromHex("#d8dce3");
    Color textDim = Color::fromHex("#8b93a1");
    Color textBright = Color::fromHex("#ffffff");
    Color border = Color::fromHex("#14171c");
    Color selection = Color::fromHex("#33526b");
    float rowHeight = 20.0f;
    float padding = 6.0f;
};

using UIId = uint64_t;

class UIContext {
public:
    UIContext() = default;

    void beginFrame(Framebuffer& target, const InputState& input);
    void endFrame();

    UIDraw& draw() { return *draw_; }
    const UITheme& theme() const { return theme_; }
    UITheme& theme() { return theme_; }

    // ---- input helpers, all in screen pixels ----
    Vec2 mouse() const { return mouse_; }
    Vec2 mouseDelta() const { return mouseDelta_; }
    bool mouseDown() const { return mouseDown_; }
    bool mouseClicked() const { return mouseDown_ && !mouseWasDown_; }
    bool mouseReleased() const { return !mouseDown_ && mouseWasDown_; }
    bool rightDown() const { return rightDown_; }
    bool rightClicked() const { return rightDown_ && !rightWasDown_; }
    float wheel() const { return wheel_; }
    const InputState* input() const { return input_; }

    // True when the pointer is over any panel, so the viewport knows not
    // to steal a click that belongs to the interface.
    bool wantsMouse() const { return hoveringPanel_ || active_ != 0; }
    void markPanel(const Rect& r);

    UIId id(const std::string& label, const Rect& r = {}) const;
    bool isActive(UIId i) const { return active_ == i; }
    bool isHot(UIId i) const { return hot_ == i; }
    void setActive(UIId i) { active_ = i; }

    // ---- widgets ----
    void panel(const Rect& r, const std::string& title = {});
    void label(const Rect& r, const std::string& text, bool dim = false);
    void header(const Rect& r, const std::string& text);
    bool button(const Rect& r, const std::string& text, bool highlighted = false,
                bool enabled = true);
    bool toggleButton(const Rect& r, const std::string& text, bool& value);
    bool checkbox(const Rect& r, const std::string& text, bool& value);
    // A number field you scrub by dragging, the way every 3D tool does it.
    bool dragFloat(const Rect& r, const std::string& id, float& value, float speed = 0.05f,
                   float lo = 0.0f, float hi = 0.0f);
    bool dragInt(const Rect& r, const std::string& id, int& value, float speed = 0.2f,
                 int lo = 0, int hi = 0);
    bool slider(const Rect& r, const std::string& id, float& value, float lo, float hi);
    bool textField(const Rect& r, const std::string& id, std::string& text);
    bool dropdown(const Rect& r, const std::string& id, const std::vector<std::string>& options,
                  int& selected);
    bool colorField(const Rect& r, const std::string& id, Color& value);
    // A selectable row, for outliners and lists.
    bool listItem(const Rect& r, const std::string& text, bool selected, int indent = 0,
                  const Color* accent = nullptr);
    // Returns the open state; the caller draws the contents when open.
    bool collapsingHeader(const Rect& r, const std::string& text, const std::string& id);
    void separator(const Rect& r);
    void tooltip(const std::string& text);
    void progressBar(const Rect& r, float fraction, const std::string& text = {});

    // ---- scrolling ----
    // Call before drawing a list; returns the offset to draw at and
    // consumes the wheel when the pointer is inside.
    float scrollRegion(const Rect& r, const std::string& id, float contentHeight);

    // ---- modal-ish popups ----
    // Only one popup is open at a time, and it draws above everything.
    void openPopup(const std::string& id);
    bool beginPopup(const std::string& id, const Rect& r);
    void endPopup();
    void closePopup();
    bool popupOpen() const { return !openPopup_.empty(); }
    const std::string& currentPopup() const { return openPopup_; }

    // Text typed this frame, for the focused field.
    void addTypedCharacter(char c) { typed_ += c; }
    void setKeyboardFocus(UIId i) { focus_ = i; }
    UIId keyboardFocus() const { return focus_; }

private:
    bool behaviour(UIId id, const Rect& r, bool& outHovered);

    UIDraw* draw_ = nullptr;
    const InputState* input_ = nullptr;
    UITheme theme_;

    Vec2 mouse_{0, 0}, mouseDelta_{0, 0};
    bool mouseDown_ = false, mouseWasDown_ = false;
    bool rightDown_ = false, rightWasDown_ = false;
    float wheel_ = 0.0f;

    UIId hot_ = 0, active_ = 0, focus_ = 0;
    bool hoveringPanel_ = false;
    std::string typed_;
    std::string editBuffer_;
    UIId editing_ = 0;
    float dragAccum_ = 0.0f;

    std::string openPopup_;
    std::string pendingPopup_;
    bool inPopup_ = false;

    std::unordered_map<UIId, float> scroll_;
    std::unordered_map<UIId, bool> openState_;
    std::string tooltip_;
    Vec2 tooltipAt_{0, 0};
};

} // namespace forge
