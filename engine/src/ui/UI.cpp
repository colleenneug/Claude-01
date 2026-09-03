#include "forge/ui/UI.hpp"

#include <algorithm>
#include <cstdio>

namespace forge {

void UIContext::beginFrame(Framebuffer& target, const InputState& input) {
    static UIDraw drawInstance(target);
    drawInstance = UIDraw(target);
    draw_ = &drawInstance;
    input_ = &input;

    mouseWasDown_ = mouseDown_;
    rightWasDown_ = rightDown_;
    const Vec2 previous = mouse_;
    mouse_ = input.mousePosition();
    mouseDelta_ = mouse_ - previous;
    mouseDown_ = input.isDown(Key::MouseLeft);
    rightDown_ = input.isDown(Key::MouseRight);
    wheel_ = input.mouseWheel();

    hot_ = 0;
    hoveringPanel_ = false;
    tooltip_.clear();

    // A popup opened during the previous frame becomes current now, so
    // the click that opened it does not also land inside it.
    if (!pendingPopup_.empty()) {
        openPopup_ = pendingPopup_;
        pendingPopup_.clear();
    }
}

void UIContext::endFrame() {
    if (!mouseDown_) active_ = 0;
    if (!tooltip_.empty() && draw_) {
        const float w = UIDraw::textWidth(tooltip_) + 10.0f;
        const float h = UIDraw::lineHeight() + 6.0f;
        Rect r{tooltipAt_.x + 14.0f, tooltipAt_.y + 18.0f, w, h};
        // Keep it on screen: a tooltip near the right edge should flip
        // rather than run off.
        const float screenW = (float)draw_->target().width();
        const float screenH = (float)draw_->target().height();
        if (r.right() > screenW) r.x = screenW - r.w - 2.0f;
        if (r.bottom() > screenH) r.y = tooltipAt_.y - h - 6.0f;
        draw_->fill(r, theme_.background, 0.96f);
        draw_->frame(r, theme_.accentDim);
        draw_->text(r.x + 5.0f, r.y + 3.0f, tooltip_, theme_.text);
    }
    typed_.clear();
}

void UIContext::markPanel(const Rect& r) {
    if (r.contains(mouse_)) hoveringPanel_ = true;
}

UIId UIContext::id(const std::string& label, const Rect& r) const {
    // Position is folded in so the same label used twice in a list still
    // gets distinct ids without the caller inventing names.
    uint64_t h = 1469598103934665603ull;
    for (unsigned char c : label) { h ^= c; h *= 1099511628211ull; }
    auto mix = [&](float v) {
        const int q = (int)std::lround(v);
        h ^= (uint64_t)(uint32_t)q;
        h *= 1099511628211ull;
    };
    mix(r.x); mix(r.y);
    return h ? h : 1;
}

bool UIContext::behaviour(UIId widgetId, const Rect& r, bool& outHovered) {
    const bool inside = r.contains(mouse_) && (inPopup_ || openPopup_.empty());
    outHovered = inside;
    if (inside) hot_ = widgetId;
    if (inside && mouseClicked()) active_ = widgetId;
    // A click counts on release, and only if it is released over the same
    // control it started on — the behaviour every toolkit has settled on.
    if (active_ == widgetId && mouseReleased()) {
        active_ = 0;
        return inside;
    }
    return false;
}

// -----------------------------------------------------------------

void UIContext::panel(const Rect& r, const std::string& title) {
    markPanel(r);
    draw_->fill(r, theme_.panel);
    draw_->frame(r, theme_.border);
    if (!title.empty()) {
        const Rect head{r.x + 1.0f, r.y + 1.0f, r.w - 2.0f, 20.0f};
        draw_->fill(head, theme_.panelHeader);
        draw_->text(head.x + 7.0f, head.y + 4.0f, title, theme_.text);
    }
}

void UIContext::label(const Rect& r, const std::string& text, bool dim) {
    draw_->textClipped(r, text, dim ? theme_.textDim : theme_.text, 2.0f);
}

void UIContext::header(const Rect& r, const std::string& text) {
    draw_->fill(r, theme_.panelHeader);
    draw_->text(r.x + 6.0f, r.y + (r.h - UIDraw::lineHeight()) * 0.5f, text, theme_.textBright);
}

bool UIContext::button(const Rect& r, const std::string& text, bool highlighted, bool enabled) {
    const UIId widgetId = id(text, r);
    bool hovered = false;
    bool clicked = false;
    if (enabled) clicked = behaviour(widgetId, r, hovered);
    else hovered = false;

    Color fill = highlighted ? theme_.accentDim : theme_.control;
    if (!enabled) fill = theme_.panel;
    else if (active_ == widgetId) fill = theme_.controlActive;
    else if (hovered) fill = highlighted ? theme_.accent : theme_.controlHover;

    draw_->roundedFill(r, fill, 3.0f);
    draw_->textCentered(r, text, enabled ? theme_.text : theme_.textDim);
    return clicked;
}

bool UIContext::toggleButton(const Rect& r, const std::string& text, bool& value) {
    if (button(r, text, value)) { value = !value; return true; }
    return false;
}

bool UIContext::checkbox(const Rect& r, const std::string& text, bool& value) {
    const UIId widgetId = id(text + "##cb", r);
    bool hovered = false;
    const bool clicked = behaviour(widgetId, r, hovered);

    const float side = std::min(14.0f, r.h - 2.0f);
    const Rect box{r.x + 2.0f, r.y + (r.h - side) * 0.5f, side, side};
    draw_->roundedFill(box, hovered ? theme_.controlHover : theme_.control, 2.0f);
    draw_->frame(box, theme_.border);
    if (value) {
        const Rect inner = box.inset(3.0f);
        draw_->roundedFill(inner, theme_.accent, 1.0f);
    }
    if (!text.empty())
        draw_->text(box.right() + 6.0f, r.y + (r.h - UIDraw::lineHeight()) * 0.5f, text, theme_.text);
    if (clicked) { value = !value; return true; }
    return false;
}

namespace {
std::string formatFloat(float v) {
    char buf[48];
    // Trim to something a person can read: three decimals is plenty for
    // a transform, and trailing zeros are noise.
    std::snprintf(buf, sizeof(buf), "%.3f", v);
    std::string s = buf;
    if (s.find('.') != std::string::npos) {
        while (!s.empty() && s.back() == '0') s.pop_back();
        if (!s.empty() && s.back() == '.') s.pop_back();
    }
    return s.empty() ? "0" : s;
}
}

bool UIContext::dragFloat(const Rect& r, const std::string& fieldId, float& value, float speed,
                          float lo, float hi) {
    const UIId widgetId = id(fieldId + "##drag", r);
    const bool inside = r.contains(mouse_) && openPopup_.empty();
    if (inside) hot_ = widgetId;
    if (inside && mouseClicked()) { active_ = widgetId; dragAccum_ = 0.0f; }

    bool changed = false;
    if (active_ == widgetId && mouseDown_) {
        if (std::fabs(mouseDelta_.x) > 0.0f) {
            // Holding Shift scrubs finely; a modifier is far easier than
            // reaching for a second control.
            float step = speed;
            if (input_ && input_->isDown(Key::LeftShift)) step *= 0.1f;
            if (input_ && input_->isDown(Key::LeftControl)) step *= 10.0f;
            value += mouseDelta_.x * step;
            if (hi > lo) value = clampf(value, lo, hi);
            changed = true;
        }
    }

    const Color fill = (active_ == widgetId) ? theme_.controlActive
                     : (hot_ == widgetId) ? theme_.controlHover : theme_.control;
    draw_->roundedFill(r, fill, 3.0f);
    if (hi > lo) {
        // A faint bar showing where the value sits in its range, which
        // makes a bounded property readable at a glance.
        const float t = saturate(invLerp(lo, hi, value));
        draw_->fill({r.x + 1.0f, r.bottom() - 3.0f, (r.w - 2.0f) * t, 2.0f}, theme_.accentDim);
    }
    draw_->textCentered(r, formatFloat(value), theme_.text);
    return changed;
}

bool UIContext::dragInt(const Rect& r, const std::string& fieldId, int& value, float speed,
                        int lo, int hi) {
    float f = (float)value;
    const bool changed = dragFloat(r, fieldId, f, speed, (float)lo, (float)hi);
    if (changed) {
        value = (int)std::lround(f);
        if (hi > lo) value = std::max(lo, std::min(hi, value));
    }
    return changed;
}

bool UIContext::slider(const Rect& r, const std::string& fieldId, float& value, float lo, float hi) {
    const UIId widgetId = id(fieldId + "##slider", r);
    const bool inside = r.contains(mouse_) && openPopup_.empty();
    if (inside) hot_ = widgetId;
    if (inside && mouseClicked()) active_ = widgetId;

    bool changed = false;
    if (active_ == widgetId && mouseDown_) {
        const float t = saturate((mouse_.x - r.x) / std::max(1.0f, r.w));
        const float next = lerpf(lo, hi, t);
        if (next != value) { value = next; changed = true; }
    }

    draw_->roundedFill(r, theme_.control, 3.0f);
    const float t = saturate(invLerp(lo, hi, value));
    draw_->roundedFill({r.x, r.y, r.w * t, r.h}, theme_.accentDim, 3.0f);
    draw_->textCentered(r, formatFloat(value), theme_.text);
    return changed;
}

bool UIContext::textField(const Rect& r, const std::string& fieldId, std::string& text) {
    const UIId widgetId = id(fieldId + "##text", r);
    const bool inside = r.contains(mouse_) && openPopup_.empty();
    if (inside) hot_ = widgetId;
    if (inside && mouseClicked()) {
        focus_ = widgetId;
        editing_ = widgetId;
        editBuffer_ = text;
    } else if (mouseClicked() && focus_ == widgetId) {
        // Clicking away commits, which is what a user expects from a
        // property field.
        focus_ = 0;
        editing_ = 0;
    }

    bool committed = false;
    if (focus_ == widgetId) {
        for (char c : typed_) {
            if (c == '\b') { if (!editBuffer_.empty()) editBuffer_.pop_back(); }
            else if (c == '\r' || c == '\n') { text = editBuffer_; committed = true; focus_ = 0; }
            else if (c == 27) { focus_ = 0; }          // Escape abandons the edit
            else if ((unsigned char)c >= 32) editBuffer_ += c;
        }
        if (focus_ == 0 && !committed && editing_ == widgetId) {
            text = editBuffer_;
            committed = true;
        }
    }

    draw_->roundedFill(r, focus_ == widgetId ? theme_.controlActive : theme_.control, 3.0f);
    if (focus_ == widgetId) draw_->frame(r, theme_.accent);

    const std::string shown = focus_ == widgetId ? editBuffer_ : text;
    const Rect inner = r.inset(4.0f, 0.0f, 4.0f, 0.0f);
    // Scroll a long value so the caret end stays visible while typing.
    std::string visible = shown;
    const int maxChars = (int)(inner.w / UIDraw::charWidth());
    if ((int)visible.size() > maxChars && maxChars > 0)
        visible = visible.substr(visible.size() - (size_t)maxChars);
    draw_->text(inner.x, r.y + (r.h - UIDraw::lineHeight()) * 0.5f, visible,
                shown.empty() ? theme_.textDim : theme_.text);
    if (focus_ == widgetId)
        draw_->fill({inner.x + UIDraw::textWidth(visible) + 1.0f, r.y + 3.0f, 1.0f, r.h - 6.0f},
                    theme_.accent);
    return committed;
}

bool UIContext::dropdown(const Rect& r, const std::string& fieldId,
                         const std::vector<std::string>& options, int& selected) {
    const std::string popupId = fieldId + "##dropdown";
    const UIId widgetId = id(popupId, r);
    bool hovered = false;
    if (behaviour(widgetId, r, hovered)) openPopup(popupId);

    draw_->roundedFill(r, hovered ? theme_.controlHover : theme_.control, 3.0f);
    const std::string current = (selected >= 0 && selected < (int)options.size())
                              ? options[(size_t)selected] : std::string("<none>");
    draw_->textClipped({r.x, r.y, r.w - 16.0f, r.h}, current, theme_.text, 5.0f);
    // The little triangle that says this opens.
    const float cx = r.right() - 10.0f, cy = r.center().y;
    draw_->triangle({cx - 4.0f, cy - 2.0f}, {cx + 4.0f, cy - 2.0f}, {cx, cy + 3.0f}, theme_.textDim);

    bool changed = false;
    const float rowH = theme_.rowHeight;
    const float listH = std::min((float)options.size() * rowH + 4.0f, 260.0f);
    Rect list{r.x, r.bottom() + 2.0f, std::max(r.w, 140.0f), listH};
    if (draw_ && list.bottom() > (float)draw_->target().height())
        list.y = std::max(0.0f, r.y - listH - 2.0f);

    if (beginPopup(popupId, list)) {
        const float scroll = scrollRegion(list, popupId + "scroll", (float)options.size() * rowH);
        draw_->pushClip(list);
        for (size_t i = 0; i < options.size(); ++i) {
            const Rect row{list.x + 2.0f, list.y + 2.0f + (float)i * rowH - scroll, list.w - 4.0f, rowH};
            if (row.bottom() < list.y || row.y > list.bottom()) continue;
            if (listItem(row, options[i], (int)i == selected)) {
                selected = (int)i;
                changed = true;
                closePopup();
            }
        }
        draw_->popClip();
        endPopup();
    }
    return changed;
}

bool UIContext::colorField(const Rect& r, const std::string& fieldId, Color& value) {
    const UIId widgetId = id(fieldId + "##color", r);
    bool hovered = false;
    behaviour(widgetId, r, hovered);

    const Rect swatch{r.x, r.y, std::min(28.0f, r.w * 0.35f), r.h};
    draw_->roundedFill(swatch, value, 2.0f);
    draw_->frame(swatch, theme_.border);

    const Rect field{swatch.right() + 4.0f, r.y, r.w - swatch.w - 4.0f, r.h};
    std::string hex = value.toHex();
    if (textField(field, fieldId + "hex", hex)) {
        value = Color::fromHex(hex);
        return true;
    }
    return false;
}

bool UIContext::listItem(const Rect& r, const std::string& text, bool selected, int indent,
                         const Color* accent) {
    const UIId widgetId = id(text + "##item", r);
    bool hovered = false;
    const bool clicked = behaviour(widgetId, r, hovered);

    if (selected) draw_->fill(r, theme_.selection);
    else if (hovered) draw_->fill(r, theme_.controlHover, 0.7f);

    const float x = r.x + 4.0f + (float)indent * 12.0f;
    if (accent) {
        draw_->fill({r.x, r.y, 3.0f, r.h}, *accent);
    }
    draw_->textClipped({x, r.y, r.right() - x, r.h}, text,
                       selected ? theme_.textBright : theme_.text, 0.0f);
    return clicked;
}

bool UIContext::collapsingHeader(const Rect& r, const std::string& text, const std::string& headerId) {
    const UIId widgetId = id(headerId + "##hdr", r);
    bool& open = openState_.emplace(widgetId, true).first->second;
    bool hovered = false;
    if (behaviour(widgetId, r, hovered)) open = !open;

    draw_->fill(r, hovered ? theme_.controlHover : theme_.panelHeader);
    const float cx = r.x + 9.0f, cy = r.center().y;
    if (open) draw_->triangle({cx - 4.0f, cy - 2.0f}, {cx + 4.0f, cy - 2.0f}, {cx, cy + 3.0f}, theme_.textDim);
    else draw_->triangle({cx - 2.0f, cy - 4.0f}, {cx + 3.0f, cy}, {cx - 2.0f, cy + 4.0f}, theme_.textDim);
    draw_->text(r.x + 18.0f, r.y + (r.h - UIDraw::lineHeight()) * 0.5f, text, theme_.textBright);
    return open;
}

void UIContext::separator(const Rect& r) {
    draw_->fill({r.x, r.center().y, r.w, 1.0f}, theme_.border);
}

void UIContext::tooltip(const std::string& text) {
    tooltip_ = text;
    tooltipAt_ = mouse_;
}

void UIContext::progressBar(const Rect& r, float fraction, const std::string& text) {
    draw_->roundedFill(r, theme_.control, 3.0f);
    draw_->roundedFill({r.x, r.y, r.w * saturate(fraction), r.h}, theme_.accentDim, 3.0f);
    if (!text.empty()) draw_->textCentered(r, text, theme_.text);
}

float UIContext::scrollRegion(const Rect& r, const std::string& regionId, float contentHeight) {
    const UIId widgetId = id(regionId + "##scroll", r);
    float& offset = scroll_.emplace(widgetId, 0.0f).first->second;
    const float maxScroll = std::max(0.0f, contentHeight - r.h);

    if (r.contains(mouse_) && std::fabs(wheel_) > 0.0f)
        offset -= wheel_ * theme_.rowHeight * 3.0f;
    offset = clampf(offset, 0.0f, maxScroll);

    if (maxScroll > 0.0f) {
        // A thin indicator rather than a draggable bar: the wheel is how
        // these lists are actually used.
        const float trackH = r.h;
        const float thumbH = std::max(20.0f, trackH * (r.h / contentHeight));
        const float t = maxScroll > 0.0f ? offset / maxScroll : 0.0f;
        const Rect thumb{r.right() - 4.0f, r.y + (trackH - thumbH) * t, 3.0f, thumbH};
        draw_->fill({r.right() - 4.0f, r.y, 3.0f, trackH}, theme_.border, 0.5f);
        draw_->roundedFill(thumb, theme_.controlActive, 1.5f);
    }
    return offset;
}

void UIContext::openPopup(const std::string& popupId) { pendingPopup_ = popupId; }

bool UIContext::beginPopup(const std::string& popupId, const Rect& r) {
    if (openPopup_ != popupId) return false;
    inPopup_ = true;
    markPanel(r);
    draw_->fill(r, theme_.background, 0.98f);
    draw_->frame(r, theme_.accentDim);
    // Clicking outside dismisses, which is the one behaviour a popup
    // must have or it traps the user.
    if (mouseClicked() && !r.contains(mouse_)) closePopup();
    return true;
}

void UIContext::endPopup() { inPopup_ = false; }

void UIContext::closePopup() {
    openPopup_.clear();
    pendingPopup_.clear();
}

} // namespace forge
