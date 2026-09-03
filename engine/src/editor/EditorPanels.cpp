// ============================================================
//  The editor's panels.
//
//  Nothing here knows about any particular class. The palette lists
//  whatever is registered as a placeable Actor; the details panel walks
//  the selected object's reflected properties and picks a widget per
//  type. Adding a class or a property to the engine makes it show up
//  here with no edit to this file.
// ============================================================
#include <algorithm>
#include <cstdio>

#include "forge/components/ScriptComponent.hpp"
#include "forge/core/Log.hpp"
#include "forge/editor/Editor.hpp"
#include "forge/gameplay/GameFramework.hpp"
#include "forge/script/ScriptGraph.hpp"

namespace forge {

namespace {

bool matchesFilter(const std::string& text, const std::string& filter) {
    if (filter.empty()) return true;
    std::string a, b;
    for (char c : text) a += (char)std::tolower((unsigned char)c);
    for (char c : filter) b += (char)std::tolower((unsigned char)c);
    return a.find(b) != std::string::npos;
}

std::string formatNumber(double v, int decimals = 1) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*f", decimals, v);
    return buf;
}

} // namespace

// ---------------------------------------------------------------
//  Toolbar
// ---------------------------------------------------------------

void Editor::drawToolbar(const Rect& r) {
    ui_.markPanel(r);
    ui_.draw().fill(r, ui_.theme().panelHeader);
    ui_.draw().fill({r.x, r.bottom() - 1.0f, r.w, 1.0f}, ui_.theme().border);

    Rect row = r.inset(4.0f);
    const float bw = 58.0f;

    if (ui_.button(row.cutLeft(bw), "New")) newLevel();
    row.cutLeft(2.0f);
    if (ui_.button(row.cutLeft(bw), "Save")) saveLevel(levelPath_.empty() ? "level.flevel" : levelPath_);
    row.cutLeft(2.0f);
    if (ui_.button(row.cutLeft(bw), "Open")) loadLevel(levelPath_.empty() ? "level.flevel" : levelPath_);
    row.cutLeft(12.0f);

    if (!isPlaying()) {
        if (ui_.button(row.cutLeft(bw), "Play", true)) play();
    } else {
        if (ui_.button(row.cutLeft(bw), "Stop", true)) stop();
        row.cutLeft(2.0f);
        if (ui_.button(row.cutLeft(bw), world().isPaused() ? "Resume" : "Pause")) togglePause();
    }
    row.cutLeft(12.0f);

    // Transform mode. The three keys are the ones muscle memory expects.
    const char* modeNames[3] = {"Move", "Rotate", "Scale"};
    for (int i = 0; i < 3; ++i) {
        if (ui_.button(row.cutLeft(52.0f), modeNames[i], (int)gizmoMode == i))
            gizmoMode = (GizmoMode)i;
        row.cutLeft(2.0f);
    }
    row.cutLeft(10.0f);

    if (ui_.button(row.cutLeft(52.0f), "Grid", showGrid)) showGrid = !showGrid;
    row.cutLeft(2.0f);
    if (ui_.button(row.cutLeft(62.0f), "Bounds", showBounds)) showBounds = !showBounds;
    row.cutLeft(10.0f);

    ui_.label(row.cutLeft(38.0f), "Snap");
    ui_.dragFloat(row.cutLeft(50.0f), "snap", gridSnap, 0.05f, 0.0f, 10.0f);

    // The level name, right-aligned, with the usual dirty marker.
    const std::string title = world().levelName() + (dirty_ ? " *" : "");
    ui_.draw().textRight(r, title, ui_.theme().textDim, 10.0f);
}

// ---------------------------------------------------------------
//  Place Actors
// ---------------------------------------------------------------

void Editor::drawPalette(const Rect& r) {
    ui_.panel(r, "Place Actors");
    Rect body = r.inset(1.0f, 21.0f, 1.0f, 1.0f);

    ui_.textField(body.cutTop(22.0f).inset(4.0f, 2.0f, 4.0f, 2.0f), "palettefilter", paletteFilter_);

    const auto classes = ClassRegistry::get().derivedFrom("Actor", true);
    std::vector<const ClassInfo*> shown;
    for (const ClassInfo* c : classes) {
        if (!c->placeable()) continue;
        // The framework classes are spawned by play, not placed by hand.
        if (c->isA("GameMode") || c->isA("PlayerController")) continue;
        if (!matchesFilter(c->displayName(), paletteFilter_) &&
            !matchesFilter(c->category(), paletteFilter_))
            continue;
        shown.push_back(c);
    }

    const float rowH = 22.0f;
    // Category headers take a row each.
    float contentH = 0.0f;
    std::string lastCat;
    for (const ClassInfo* c : shown) {
        if (c->category() != lastCat) { contentH += rowH; lastCat = c->category(); }
        contentH += rowH;
    }

    const float scroll = ui_.scrollRegion(body, "palette", contentH);
    ui_.draw().pushClip(body);
    float y = body.y - scroll;
    lastCat.clear();
    for (const ClassInfo* c : shown) {
        if (c->category() != lastCat) {
            lastCat = c->category();
            const Rect head{body.x, y, body.w, rowH};
            if (head.bottom() > body.y && head.y < body.bottom()) ui_.header(head, lastCat);
            y += rowH;
        }
        const Rect row{body.x + 2.0f, y, body.w - 8.0f, rowH};
        if (row.bottom() > body.y && row.y < body.bottom()) {
            if (ui_.listItem(row, c->displayName(), false, 1)) spawnClass(c);
            if (row.contains(ui_.mouse()) && !c->description().empty()) ui_.tooltip(c->description());
        }
        y += rowH;
    }
    ui_.draw().popClip();
}

// ---------------------------------------------------------------
//  World Outliner
// ---------------------------------------------------------------

void Editor::drawOutliner(const Rect& r) {
    const size_t count = world().actors().size();
    ui_.panel(r, "World Outliner  (" + std::to_string(count) + ")");
    Rect body = r.inset(1.0f, 21.0f, 1.0f, 1.0f);

    const float rowH = 20.0f;
    const float scroll = ui_.scrollRegion(body, "outliner", (float)count * rowH);
    ui_.draw().pushClip(body);

    float y = body.y - scroll;
    for (const auto& a : world().actors()) {
        if (a->isPendingKill()) { continue; }
        const Rect row{body.x + 1.0f, y, body.w - 6.0f, rowH};
        y += rowH;
        if (row.bottom() < body.y || row.y > body.bottom()) continue;

        // A coloured stripe per category, so a busy outliner is still
        // scannable at a glance.
        const std::string& category = a->getClass()->category();
        Color accent = ui_.theme().textDim;
        if (category == "Lighting") accent = Color::fromHex("#e0a458");
        else if (category == "Gameplay") accent = Color::fromHex("#7bb661");
        else if (category == "Basic") accent = Color::fromHex("#5fa8d3");

        const std::string label = a->actorName() + "   " + a->getClass()->displayName();
        if (ui_.listItem(row, label, a.get() == selection_, 0, &accent))
            select(a.get());
    }
    ui_.draw().popClip();
}

// ---------------------------------------------------------------
//  Details
// ---------------------------------------------------------------

bool Editor::drawProperty(const Rect& row, Object& object, const Property& prop) {
    if (prop.hidden) return false;

    Rect r = row;
    const Rect labelRect = r.cutLeft(std::min(148.0f, r.w * 0.46f));
    ui_.label(labelRect.inset(4.0f, 0.0f, 2.0f, 0.0f), prop.label(), false);
    if (labelRect.contains(ui_.mouse()) && !prop.tooltip.empty()) ui_.tooltip(prop.tooltip);

    PropertyValue value;
    if (!object.getProperty(prop.name, value)) return false;
    if (prop.readOnly) {
        ui_.label(r.inset(4.0f, 0.0f, 2.0f, 0.0f), value.toDisplayString(), true);
        return false;
    }

    bool changed = false;
    const std::string fieldId = object.className() + "." + prop.name;

    switch (prop.type) {
        case PropType::Bool: {
            bool v = value.asBool;
            if (ui_.checkbox(r, {}, v)) { value.asBool = v; changed = true; }
            break;
        }
        case PropType::Float: {
            float v = value.asFloat;
            const bool bounded = prop.hasRange;
            const bool edited = bounded
                ? ui_.slider(r.inset(2.0f, 2.0f, 4.0f, 2.0f), fieldId, v, prop.minValue, prop.maxValue)
                : ui_.dragFloat(r.inset(2.0f, 2.0f, 4.0f, 2.0f), fieldId, v, 0.05f);
            if (edited) { value.asFloat = v; changed = true; }
            break;
        }
        case PropType::Int: {
            int v = value.asInt;
            if (ui_.dragInt(r.inset(2.0f, 2.0f, 4.0f, 2.0f), fieldId, v, 0.2f,
                            prop.hasRange ? (int)prop.minValue : 0,
                            prop.hasRange ? (int)prop.maxValue : 0)) {
                value.asInt = v;
                changed = true;
            }
            break;
        }
        case PropType::Enum: {
            std::vector<std::string> options(prop.enumNames.begin(), prop.enumNames.end());
            int sel = value.asInt;
            if (ui_.dropdown(r.inset(2.0f, 2.0f, 4.0f, 2.0f), fieldId, options, sel)) {
                value.asInt = sel;
                changed = true;
            }
            break;
        }
        case PropType::Color: {
            Color c = value.asColor;
            if (ui_.colorField(r.inset(2.0f, 2.0f, 4.0f, 2.0f), fieldId, c)) {
                value.asColor = c;
                changed = true;
            }
            break;
        }
        case PropType::Vec3:
        case PropType::Rotator: {
            // Three fields sharing the row, coloured like the gizmo axes
            // so X, Y and Z read the same everywhere in the editor.
            const bool isRotator = prop.type == PropType::Rotator;
            float comps[3] = {isRotator ? value.asRotator.pitch : value.asVec3.x,
                              isRotator ? value.asRotator.yaw : value.asVec3.y,
                              isRotator ? value.asRotator.roll : value.asVec3.z};
            const Color axisColors[3] = {Color::fromHex("#e05252"), Color::fromHex("#5ad25a"),
                                         Color::fromHex("#4f8ff0")};
            const float each = r.w / 3.0f;
            for (int i = 0; i < 3; ++i) {
                Rect cell{r.x + each * (float)i, r.y, each, r.h};
                ui_.draw().fill({cell.x + 1.0f, cell.y + 2.0f, 2.0f, cell.h - 4.0f}, axisColors[i]);
                if (ui_.dragFloat(cell.inset(5.0f, 2.0f, 3.0f, 2.0f), fieldId + std::to_string(i),
                                  comps[i], isRotator ? 0.5f : 0.03f))
                    changed = true;
            }
            if (changed) {
                if (isRotator) value.asRotator = {comps[0], comps[1], comps[2]};
                else value.asVec3 = {comps[0], comps[1], comps[2]};
            }
            break;
        }
        case PropType::String: {
            // An asset or class reference gets a picker instead of a text
            // box, populated from whatever is actually available.
            std::vector<std::string> options;
            switch (prop.ref) {
                case RefKind::Mesh: options = assets_.meshNames(); break;
                case RefKind::Material: options = assets_.materialNames(); break;
                case RefKind::Texture: options = assets_.textureNames(); break;
                case RefKind::Script: options = assets_.scriptNames(); break;
                case RefKind::Class: {
                    for (const ClassInfo* c : ClassRegistry::get().derivedFrom(prop.classFilter, true))
                        options.push_back(c->name());
                    break;
                }
                default: break;
            }
            if (options.empty()) {
                std::string text = value.asString;
                if (ui_.textField(r.inset(2.0f, 2.0f, 4.0f, 2.0f), fieldId, text)) {
                    value.asString = text;
                    changed = true;
                }
            } else {
                if (prop.ref != RefKind::Class) options.insert(options.begin(), "<none>");
                int sel = 0;
                for (size_t i = 0; i < options.size(); ++i)
                    if (options[i] == value.asString) sel = (int)i;
                if (ui_.dropdown(r.inset(2.0f, 2.0f, 4.0f, 2.0f), fieldId, options, sel)) {
                    value.asString = (options[(size_t)sel] == "<none>") ? std::string()
                                                                       : options[(size_t)sel];
                    changed = true;
                }
            }
            break;
        }
    }

    if (changed) {
        object.setProperty(prop.name, value);
        markDirty();
    }
    return changed;
}

void Editor::drawComponentTree(const Rect& r, Actor& actor, float& y) {
    const float rowH = 20.0f;
    for (const auto& comp : actor.components()) {
        const Rect head{r.x, y, r.w, rowH};
        y += rowH;
        if (head.bottom() < r.y || head.y > r.bottom()) {
            // Still has to advance y for the properties it is skipping.
            for (const Property* p : comp->getClass()->allProperties())
                if (!p->hidden && p->name != "name") y += rowH;
            continue;
        }

        const std::string title = comp->name + "  (" + comp->getClass()->displayName() + ")";
        if (!ui_.collapsingHeader(head, title, "comp" + comp->name + std::to_string(actor.id()))) {
            for (const Property* p : comp->getClass()->allProperties())
                if (!p->hidden && p->name != "name") y += rowH;
            continue;
        }

        std::string category;
        for (const Property* p : comp->getClass()->allProperties()) {
            if (p->hidden || p->name == "name") continue;
            if (p->category != category) {
                category = p->category;
                const Rect catRow{r.x + 8.0f, y, r.w - 8.0f, rowH};
                if (catRow.bottom() > r.y && catRow.y < r.bottom())
                    ui_.label(catRow.inset(4.0f, 0.0f, 0.0f, 0.0f), category, true);
                y += rowH;
            }
            const Rect row{r.x + 8.0f, y, r.w - 12.0f, rowH};
            y += rowH;
            if (row.bottom() < r.y || row.y > r.bottom()) continue;
            drawProperty(row, *comp, *p);
        }
    }
}

void Editor::drawDetails(const Rect& r) {
    ui_.panel(r, selection_ ? "Details  -  " + selection_->actorName() : "Details");
    Rect body = r.inset(1.0f, 21.0f, 1.0f, 1.0f);

    Object* object = selection_ ? (Object*)selection_ : (Object*)&world().settings();
    const bool showingWorld = selection_ == nullptr;

    if (showingWorld) {
        const Rect note = body.cutTop(18.0f);
        ui_.label(note.inset(6.0f, 0.0f, 0.0f, 0.0f), "World Settings (nothing selected)", true);
    }

    const float rowH = 20.0f;
    // Estimate the height so the scroll bar is honest: a row per
    // property, a row per category header, plus the component tree.
    float contentH = 0.0f;
    {
        std::string category;
        for (const Property* p : object->getClass()->allProperties()) {
            if (p->hidden) continue;
            if (p->category != category) { category = p->category; contentH += rowH; }
            contentH += rowH;
        }
        if (selection_) {
            contentH += rowH;
            for (const auto& c : selection_->components()) {
                contentH += rowH;
                std::string cat;
                for (const Property* p : c->getClass()->allProperties()) {
                    if (p->hidden || p->name == "name") continue;
                    if (p->category != cat) { cat = p->category; contentH += rowH; }
                    contentH += rowH;
                }
            }
        }
    }

    const float scroll = ui_.scrollRegion(body, "details", contentH);
    ui_.draw().pushClip(body);
    float y = body.y - scroll;

    std::string category;
    for (const Property* p : object->getClass()->allProperties()) {
        if (p->hidden) continue;
        if (p->category != category) {
            category = p->category;
            const Rect catRow{body.x, y, body.w - 6.0f, rowH};
            if (catRow.bottom() > body.y && catRow.y < body.bottom()) ui_.header(catRow, category);
            y += rowH;
        }
        const Rect row{body.x, y, body.w - 6.0f, rowH};
        y += rowH;
        if (row.bottom() < body.y || row.y > body.bottom()) continue;
        if (drawProperty(row, *object, *p) && selection_) selection_->rerunConstruction();
    }

    if (selection_) {
        const Rect head{body.x, y, body.w - 6.0f, rowH};
        if (head.bottom() > body.y && head.y < body.bottom()) ui_.header(head, "Components");
        y += rowH;
        drawComponentTree({body.x, body.y, body.w - 6.0f, body.h}, *selection_, y);
    }
    ui_.draw().popClip();
}

// ---------------------------------------------------------------
//  Content Browser
// ---------------------------------------------------------------

void Editor::drawContentBrowser(const Rect& r) {
    ui_.panel(r, "Content Browser");
    Rect body = r.inset(1.0f, 21.0f, 1.0f, 1.0f);

    Rect tabs = body.cutTop(22.0f);
    const char* names[4] = {"Meshes", "Materials", "Textures", "Scripts"};
    for (int i = 0; i < 4; ++i)
        if (ui_.button(tabs.cutLeft(76.0f), names[i], contentTab_ == i)) contentTab_ = i;
    tabs.cutLeft(8.0f);
    ui_.textField(tabs.inset(2.0f, 2.0f, 4.0f, 2.0f), "contentfilter", contentFilter_);

    std::vector<std::string> entries;
    switch (contentTab_) {
        case 0: entries = assets_.meshNames(); break;
        case 1: entries = assets_.materialNames(); break;
        case 2: entries = assets_.textureNames(); break;
        default: entries = assets_.scriptNames(); break;
    }
    std::vector<std::string> shown;
    for (const std::string& e : entries)
        if (matchesFilter(e, contentFilter_)) shown.push_back(e);

    // A grid of tiles, which is how content is browsed everywhere.
    const float tileW = 92.0f, tileH = 56.0f;
    const int columns = std::max(1, (int)(body.w / tileW));
    const int rows = ((int)shown.size() + columns - 1) / columns;
    const float scroll = ui_.scrollRegion(body, "content", (float)rows * tileH);

    ui_.draw().pushClip(body);
    for (size_t i = 0; i < shown.size(); ++i) {
        const int col = (int)i % columns, row = (int)i / columns;
        const Rect tile{body.x + 4.0f + (float)col * tileW, body.y + 4.0f + (float)row * tileH - scroll,
                        tileW - 6.0f, tileH - 6.0f};
        if (tile.bottom() < body.y || tile.y > body.bottom()) continue;

        const bool hovered = tile.contains(ui_.mouse());
        ui_.draw().roundedFill(tile, hovered ? ui_.theme().controlHover : ui_.theme().control, 3.0f);

        // A swatch that says something about the asset: a material's
        // colour, a texture's own pixels.
        const Rect swatch = tile.inset(6.0f, 5.0f, 6.0f, 18.0f);
        if (contentTab_ == 1) {
            if (Material* m = assets_.material(shown[i])) ui_.draw().roundedFill(swatch, m->baseColor, 2.0f);
        } else if (contentTab_ == 2) {
            if (Texture* t = assets_.texture(shown[i])) {
                for (int py = 0; py < (int)swatch.h; ++py)
                    for (int px = 0; px < (int)swatch.w; ++px)
                        ui_.draw().fill({swatch.x + (float)px, swatch.y + (float)py, 1.0f, 1.0f},
                                        t->sample((float)px / swatch.w, (float)py / swatch.h));
            }
        } else {
            ui_.draw().roundedFill(swatch, ui_.theme().panelHeader, 2.0f);
            ui_.draw().textCentered(swatch, contentTab_ == 0 ? "MESH" : "SCRIPT", ui_.theme().textDim);
        }
        ui_.draw().textClipped({tile.x, tile.bottom() - 16.0f, tile.w, 14.0f}, shown[i],
                               ui_.theme().text, 3.0f);

        // Clicking a mesh applies it to the selected actor, which is the
        // fastest path from "browsing" to "using".
        if (hovered && ui_.mouseClicked() && selection_ && contentTab_ <= 1) {
            const char* prop = contentTab_ == 0 ? "mesh" : "material";
            if (selection_->setString(prop, shown[i])) {
                selection_->rerunConstruction();
                markDirty();
            }
        }
        if (hovered) ui_.tooltip(shown[i]);
    }
    ui_.draw().popClip();
}

// ---------------------------------------------------------------
//  Output Log
// ---------------------------------------------------------------

void Editor::drawOutputLog(const Rect& r) {
    ui_.panel(r, "Output Log");
    Rect body = r.inset(1.0f, 21.0f, 1.0f, 1.0f);

    Rect head = body.cutTop(22.0f);
    if (ui_.button(head.cutLeft(60.0f), "Clear")) Log::get().clear();

    const auto& entries = Log::get().entries();
    const float rowH = 15.0f;
    const float contentH = (float)entries.size() * rowH;
    // Stick to the bottom: a log you have to scroll to see the newest
    // line of is not doing its job.
    const float scroll = std::max(0.0f, contentH - body.h);

    ui_.draw().pushClip(body);
    float y = body.y - scroll;
    for (const LogEntry& e : entries) {
        const Rect row{body.x + 4.0f, y, body.w - 8.0f, rowH};
        y += rowH;
        if (row.bottom() < body.y || row.y > body.bottom()) continue;

        Color c = ui_.theme().textDim;
        switch (e.level) {
            case LogLevel::Warning: c = ui_.theme().warning; break;
            case LogLevel::Error: c = ui_.theme().danger; break;
            case LogLevel::Script: c = ui_.theme().accent; break;
            default: c = ui_.theme().text; break;
        }
        std::string text = e.message;
        if (e.repeats > 1) text += "  (x" + std::to_string(e.repeats) + ")";
        ui_.draw().textClipped(row, text, c, 0.0f);
    }
    ui_.draw().popClip();
}

// ---------------------------------------------------------------
//  Status bar and viewport overlay
// ---------------------------------------------------------------

void Editor::drawStatusBar(const Rect& r) {
    ui_.markPanel(r);
    ui_.draw().fill(r, ui_.theme().panelHeader);
    ui_.draw().fill({r.x, r.y, r.w, 1.0f}, ui_.theme().border);

    const std::string left = statusTimer_ > 0.0f
        ? statusMessage_
        : (selection_ ? selection_->actorName() + "  [" + selection_->className() + "]"
                      : std::string("Right-drag to look, WASD to fly, F to frame, Delete to remove"));
    ui_.draw().text(r.x + 8.0f, r.y + 3.0f, left, ui_.theme().textDim);

    const RenderStats& s = renderer_.stats();
    const std::string right =
        formatNumber(lastFrameMs_) + " ms   " +
        std::to_string(s.trianglesDrawn) + " tris   " +
        std::to_string(world().actors().size()) + " actors" +
        (isPlaying() ? "   PLAYING" : "");
    ui_.draw().textRight(r, right, ui_.theme().textDim, 8.0f);
}

void Editor::drawViewportOverlay(const Rect& r) {
    drawGizmo(r, lastView_);

    // A frame round the viewport, tinted while playing so the mode is
    // never in doubt.
    ui_.draw().frame(r, isPlaying() ? ui_.theme().success : ui_.theme().border);

    if (isPlaying()) {
        const Rect badge{r.x + 10.0f, r.y + 10.0f, 96.0f, 22.0f};
        ui_.draw().roundedFill(badge, ui_.theme().success, 3.0f, 0.85f);
        ui_.draw().textCentered(badge, world().isPaused() ? "PAUSED" : "PLAYING",
                                ui_.theme().background);
        return;
    }

    // Axis indicator, bottom left: which way is X, Y and Z from here.
    const Vec2 origin{r.x + 42.0f, r.bottom() - 42.0f};
    const Quat inv = camera_.rotation.toQuat().inverse();
    const Vec3 axes[3] = {Vec3::Right, Vec3::Up, Vec3::Back * -1.0f};
    const char* labels[3] = {"X", "Y", "Z"};
    const Color colors[3] = {Color::fromHex("#e05252"), Color::fromHex("#5ad25a"),
                             Color::fromHex("#4f8ff0")};
    for (int i = 0; i < 3; ++i) {
        const Vec3 v = inv * axes[i];
        const Vec2 tip{origin.x + v.x * 26.0f, origin.y - v.y * 26.0f};
        ui_.draw().line(origin.x, origin.y, tip.x, tip.y, colors[i]);
        ui_.draw().text(tip.x - 3.0f, tip.y - 6.0f, labels[i], colors[i]);
    }
}

} // namespace forge
