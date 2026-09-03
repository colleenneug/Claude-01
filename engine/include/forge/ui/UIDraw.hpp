// ============================================================
//  2D drawing.
//
//  The editor draws its own interface, so it needs rectangles, lines,
//  text and a clip stack — and nothing more. Everything lands in the
//  same linear-colour framebuffer the 3D renderer wrote to, so a panel
//  composites over the viewport without a second surface.
// ============================================================
#pragma once

#include <string>

#include "forge/render/Framebuffer.hpp"

namespace forge {

struct Rect {
    float x = 0, y = 0, w = 0, h = 0;

    Rect() = default;
    Rect(float x_, float y_, float w_, float h_) : x(x_), y(y_), w(w_), h(h_) {}

    float right() const { return x + w; }
    float bottom() const { return y + h; }
    Vec2 center() const { return {x + w * 0.5f, y + h * 0.5f}; }
    bool contains(float px, float py) const {
        return px >= x && px < x + w && py >= y && py < y + h;
    }
    bool contains(const Vec2& p) const { return contains(p.x, p.y); }
    bool valid() const { return w > 0.0f && h > 0.0f; }

    Rect inset(float d) const { return {x + d, y + d, w - d * 2.0f, h - d * 2.0f}; }
    Rect inset(float left, float top, float right, float bottom) const {
        return {x + left, y + top, w - left - right, h - top - bottom};
    }
    Rect intersect(const Rect& o) const;

    // Slice a strip off an edge and shrink this rect by it. Almost all
    // editor layout is a sequence of these.
    Rect cutTop(float amount);
    Rect cutBottom(float amount);
    Rect cutLeft(float amount);
    Rect cutRight(float amount);
};

class UIDraw {
public:
    explicit UIDraw(Framebuffer& target) : fb_(&target) {}

    void pushClip(const Rect& r);
    void popClip();
    const Rect& clip() const { return clip_; }

    void fill(const Rect& r, const Color& c, float alpha = 1.0f);
    void frame(const Rect& r, const Color& c, float thickness = 1.0f, float alpha = 1.0f);
    // A rounded fill, done by insetting the corner rows rather than by
    // anti-aliasing: at one pixel of radius per row it reads as round
    // and costs nothing.
    void roundedFill(const Rect& r, const Color& c, float radius, float alpha = 1.0f);
    void line(float x0, float y0, float x1, float y1, const Color& c, float alpha = 1.0f);
    // A cubic curve, for the script editor's wires.
    void bezier(const Vec2& a, const Vec2& ca, const Vec2& cb, const Vec2& b,
                const Color& c, float thickness = 1.0f);
    void circle(float cx, float cy, float radius, const Color& c, bool filled = true);
    void triangle(const Vec2& a, const Vec2& b, const Vec2& c, const Color& color);

    void text(float x, float y, const std::string& s, const Color& c, float alpha = 1.0f);
    void textCentered(const Rect& r, const std::string& s, const Color& c);
    void textRight(const Rect& r, const std::string& s, const Color& c, float padding = 4.0f);
    // Draws as much as fits and ends with an ellipsis, so a long asset
    // name degrades instead of running into the next column.
    void textClipped(const Rect& r, const std::string& s, const Color& c, float padding = 4.0f);

    static float textWidth(const std::string& s);
    static float charWidth();
    static float lineHeight();
    static std::string ellipsize(const std::string& s, float maxWidth);

    Framebuffer& target() { return *fb_; }

private:
    void blendClipped(int x, int y, const Color& c, float alpha);

    Framebuffer* fb_ = nullptr;
    Rect clip_{0, 0, 1e9f, 1e9f};
    Rect clipStack_[16];
    int clipDepth_ = 0;
};

} // namespace forge
