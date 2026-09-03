#include "forge/ui/UIDraw.hpp"

#include "../render/FontData.inl"

namespace forge {

Rect Rect::intersect(const Rect& o) const {
    const float nx = std::max(x, o.x);
    const float ny = std::max(y, o.y);
    const float nr = std::min(right(), o.right());
    const float nb = std::min(bottom(), o.bottom());
    return {nx, ny, std::max(0.0f, nr - nx), std::max(0.0f, nb - ny)};
}

Rect Rect::cutTop(float amount) {
    amount = std::min(amount, h);
    Rect out{x, y, w, amount};
    y += amount;
    h -= amount;
    return out;
}
Rect Rect::cutBottom(float amount) {
    amount = std::min(amount, h);
    h -= amount;
    return {x, y + h, w, amount};
}
Rect Rect::cutLeft(float amount) {
    amount = std::min(amount, w);
    Rect out{x, y, amount, h};
    x += amount;
    w -= amount;
    return out;
}
Rect Rect::cutRight(float amount) {
    amount = std::min(amount, w);
    w -= amount;
    return {x + w, y, amount, h};
}

// -----------------------------------------------------------------

void UIDraw::pushClip(const Rect& r) {
    if (clipDepth_ < 16) clipStack_[clipDepth_] = clip_;
    ++clipDepth_;
    // Nested clips intersect, so a child can never draw outside its
    // parent no matter what rectangle it asks for.
    clip_ = clip_.intersect(r);
}

void UIDraw::popClip() {
    if (clipDepth_ > 0) {
        --clipDepth_;
        if (clipDepth_ < 16) clip_ = clipStack_[clipDepth_];
    }
    if (clipDepth_ == 0) clip_ = Rect{0, 0, 1e9f, 1e9f};
}

void UIDraw::blendClipped(int x, int y, const Color& c, float alpha) {
    if (!clip_.contains((float)x + 0.5f, (float)y + 0.5f)) return;
    fb_->blend(x, y, c, alpha);
}

void UIDraw::fill(const Rect& r, const Color& c, float alpha) {
    const Rect a = r.intersect(clip_);
    if (!a.valid()) return;
    const int x0 = (int)std::floor(a.x), x1 = (int)std::ceil(a.right());
    const int y0 = (int)std::floor(a.y), y1 = (int)std::ceil(a.bottom());
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x)
            fb_->blend(x, y, c, alpha);
}

void UIDraw::frame(const Rect& r, const Color& c, float thickness, float alpha) {
    const float t = std::max(1.0f, thickness);
    fill({r.x, r.y, r.w, t}, c, alpha);
    fill({r.x, r.bottom() - t, r.w, t}, c, alpha);
    fill({r.x, r.y + t, t, r.h - t * 2.0f}, c, alpha);
    fill({r.right() - t, r.y + t, t, r.h - t * 2.0f}, c, alpha);
}

void UIDraw::roundedFill(const Rect& r, const Color& c, float radius, float alpha) {
    const int rad = (int)clampf(radius, 0.0f, std::min(r.w, r.h) * 0.5f);
    if (rad <= 0) { fill(r, c, alpha); return; }
    fill({r.x, r.y + (float)rad, r.w, r.h - (float)rad * 2.0f}, c, alpha);
    for (int i = 0; i < rad; ++i) {
        // Inset each row by the horizontal distance to the corner arc.
        const float dy = (float)(rad - i) - 0.5f;
        const float dx = (float)rad - std::sqrt(std::max(0.0f, (float)(rad * rad) - dy * dy));
        fill({r.x + dx, r.y + (float)i, r.w - dx * 2.0f, 1.0f}, c, alpha);
        fill({r.x + dx, r.bottom() - (float)i - 1.0f, r.w - dx * 2.0f, 1.0f}, c, alpha);
    }
}

void UIDraw::line(float x0, float y0, float x1, float y1, const Color& c, float alpha) {
    const float dx = x1 - x0, dy = y1 - y0;
    const int steps = (int)std::max(std::fabs(dx), std::fabs(dy)) + 1;
    if (steps > 8000) return;
    for (int i = 0; i <= steps; ++i) {
        const float t = steps > 0 ? (float)i / (float)steps : 0.0f;
        blendClipped((int)std::lround(x0 + dx * t), (int)std::lround(y0 + dy * t), c, alpha);
    }
}

void UIDraw::bezier(const Vec2& a, const Vec2& ca, const Vec2& cb, const Vec2& b,
                    const Color& c, float thickness) {
    // Segment count from the control polygon's length, so a short wire is
    // not oversampled and a long one does not go faceted.
    const float span = (ca - a).length() + (cb - ca).length() + (b - cb).length();
    const int steps = (int)clampf(span * 0.25f, 8.0f, 96.0f);
    Vec2 prev = a;
    for (int i = 1; i <= steps; ++i) {
        const float t = (float)i / (float)steps;
        const float u = 1.0f - t;
        const Vec2 p = a * (u * u * u) + ca * (3.0f * u * u * t) +
                       cb * (3.0f * u * t * t) + b * (t * t * t);
        line(prev.x, prev.y, p.x, p.y, c);
        if (thickness > 1.0f) line(prev.x, prev.y + 1.0f, p.x, p.y + 1.0f, c);
        prev = p;
    }
}

void UIDraw::circle(float cx, float cy, float radius, const Color& c, bool filled) {
    const int r = (int)std::ceil(radius);
    for (int y = -r; y <= r; ++y) {
        for (int x = -r; x <= r; ++x) {
            const float d = std::sqrt((float)(x * x + y * y));
            if (filled ? (d > radius) : (d > radius || d < radius - 1.5f)) continue;
            blendClipped((int)cx + x, (int)cy + y, c, 1.0f);
        }
    }
}

void UIDraw::triangle(const Vec2& a, const Vec2& b, const Vec2& c, const Color& color) {
    const int minX = (int)std::floor(std::min({a.x, b.x, c.x}));
    const int maxX = (int)std::ceil(std::max({a.x, b.x, c.x}));
    const int minY = (int)std::floor(std::min({a.y, b.y, c.y}));
    const int maxY = (int)std::ceil(std::max({a.y, b.y, c.y}));
    const float area = (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y);
    if (std::fabs(area) < 1e-5f) return;
    const float inv = 1.0f / area;
    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            const float px = (float)x + 0.5f, py = (float)y + 0.5f;
            const float w0 = ((b.x - px) * (c.y - py) - (c.x - px) * (b.y - py)) * inv;
            const float w1 = ((c.x - px) * (a.y - py) - (a.x - px) * (c.y - py)) * inv;
            const float w2 = 1.0f - w0 - w1;
            if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f) continue;
            blendClipped(x, y, color, 1.0f);
        }
    }
}

// -----------------------------------------------------------------
//  Text
// -----------------------------------------------------------------

float UIDraw::charWidth() { return (float)fontdata::kCellWidth; }
float UIDraw::lineHeight() { return (float)fontdata::kCellHeight; }
float UIDraw::textWidth(const std::string& s) { return (float)s.size() * charWidth(); }

void UIDraw::text(float x, float y, const std::string& s, const Color& c, float alpha) {
    int cx = (int)std::lround(x);
    const int cy = (int)std::lround(y);
    for (unsigned char ch : s) {
        if (ch == '\n') break;
        if (ch >= fontdata::kFirstChar && ch <= fontdata::kLastChar) {
            const unsigned char* glyph = fontdata::kGlyphs[ch - fontdata::kFirstChar];
            for (int row = 0; row < fontdata::kCellHeight; ++row) {
                const unsigned char bits = glyph[row];
                if (!bits) continue;
                for (int col = 0; col < fontdata::kCellWidth; ++col)
                    if (bits & (1u << col)) blendClipped(cx + col, cy + row, c, alpha);
            }
        }
        cx += fontdata::kCellWidth;
    }
}

void UIDraw::textCentered(const Rect& r, const std::string& s, const Color& c) {
    text(r.x + (r.w - textWidth(s)) * 0.5f, r.y + (r.h - lineHeight()) * 0.5f, s, c);
}

void UIDraw::textRight(const Rect& r, const std::string& s, const Color& c, float padding) {
    text(r.right() - padding - textWidth(s), r.y + (r.h - lineHeight()) * 0.5f, s, c);
}

std::string UIDraw::ellipsize(const std::string& s, float maxWidth) {
    if (textWidth(s) <= maxWidth) return s;
    const int fits = (int)(maxWidth / charWidth());
    if (fits <= 1) return std::string();
    if (fits <= 3) return s.substr(0, (size_t)fits);
    return s.substr(0, (size_t)(fits - 1)) + "\xE2\x80\xA6";   // ellipsis is 3 bytes, 1 cell
}

void UIDraw::textClipped(const Rect& r, const std::string& s, const Color& c, float padding) {
    const float avail = r.w - padding * 2.0f;
    if (avail <= 0.0f) return;
    std::string shown = s;
    if (textWidth(shown) > avail) {
        const int fits = std::max(0, (int)(avail / charWidth()) - 1);
        shown = s.substr(0, (size_t)std::min((size_t)fits, s.size())) + "...";
        while (textWidth(shown) > avail && shown.size() > 3) shown.erase(shown.size() - 4, 1);
    }
    text(r.x + padding, r.y + (r.h - lineHeight()) * 0.5f, shown, c);
}

} // namespace forge
