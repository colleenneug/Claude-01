#include "forge/editor/Editor.hpp"

#include <algorithm>
#include <cstdio>

#include "forge/components/ScriptComponent.hpp"
#include "forge/core/Log.hpp"
#include "forge/gameplay/Actors.hpp"
#include "forge/gameplay/GameFramework.hpp"
#include "forge/scene/Level.hpp"
#include "forge/script/ScriptGraph.hpp"

namespace forge {

const Vec3 Editor::kAxisDirections[3] = {Vec3::Right, Vec3::Up, Vec3::Back * -1.0f};
const Color Editor::kAxisColors[3] = {Color::fromHex("#e05252"), Color::fromHex("#5ad25a"),
                                      Color::fromHex("#4f8ff0")};

Editor::Editor() = default;

void Editor::setStatus(const std::string& message, float seconds) {
    statusMessage_ = message;
    statusTimer_ = seconds;
}

Editor::~Editor() = default;

void Editor::setup() {
    registerCoreNodes();
    assets_.createStarterContent();
    editWorld_ = std::make_unique<World>(true);
    editWorld_->setAssets(&assets_);
    createDefaultLevel();
    setStatus("Forge " + std::string("1.0.0") + " ready", 4.0f);
}

void Editor::createDefaultLevel() {
    World& w = *editWorld_;
    w.setLevelName("Untitled");

    auto* ground = w.spawn<StaticMeshActor>("Ground", {0, -0.5f, 0});
    ground->mesh = "Cube";
    ground->material = "Checker";
    ground->setScale({40, 1, 40});
    ground->rerunConstruction();

    auto* start = w.spawn<PlayerStart>("PlayerStart", {0, 1.5f, 6});
    (void)start;

    auto* platform = w.spawn<StaticMeshActor>("Platform", {0, 1.0f, -4});
    platform->mesh = "Cube";
    platform->material = "Concrete";
    platform->setScale({6, 1, 4});
    platform->rerunConstruction();

    auto* ramp = w.spawn<StaticMeshActor>("Ramp", {0, 0.75f, -1.0f});
    ramp->mesh = "Stairs";
    ramp->material = "Concrete";
    ramp->setScale({3, 1.5f, 2});
    ramp->rerunConstruction();

    auto* pickup = w.spawn<RotatingActor>("Pickup", {0, 2.6f, -4});
    pickup->rerunConstruction();

    auto* lamp = w.spawn<PointLightActor>("Lamp", {4, 4, 2});
    lamp->intensity = 40.0f;
    lamp->lightColor = Color::fromHex("#ffd9a0");
    lamp->rerunConstruction();

    dirty_ = false;
    selection_ = nullptr;
    camera_.position = {10, 7, 14};
    camera_.rotation = {-18, 35, 0};
}

// ---------------------------------------------------------------
//  Level
// ---------------------------------------------------------------

void Editor::newLevel() {
    stop();
    editWorld_ = std::make_unique<World>(true);
    editWorld_->setAssets(&assets_);
    selection_ = nullptr;
    levelPath_.clear();
    createDefaultLevel();
    setStatus("New level");
}

bool Editor::saveLevel(const std::string& path) {
    const std::string target = path.empty() ? levelPath_ : path;
    if (target.empty()) {
        setStatus("No path to save to");
        return false;
    }
    if (!LevelSerializer::saveToFile(*editWorld_, target)) {
        setStatus("Could not write " + target, 4.0f);
        return false;
    }
    levelPath_ = target;
    dirty_ = false;
    setStatus("Saved " + target);
    return true;
}

bool Editor::loadLevel(const std::string& path) {
    stop();
    auto fresh = std::make_unique<World>(true);
    fresh->setAssets(&assets_);
    if (!LevelSerializer::loadFromFile(*fresh, path)) {
        setStatus("Could not load " + path, 4.0f);
        return false;
    }
    editWorld_ = std::move(fresh);
    selection_ = nullptr;
    levelPath_ = path;
    dirty_ = false;
    setStatus("Loaded " + path);
    return true;
}

// ---------------------------------------------------------------
//  Play-In-Editor
// ---------------------------------------------------------------

void Editor::play() {
    if (playWorld_) return;
    // Snapshot to JSON and rebuild. Round-tripping through the same
    // format a saved level uses means play exercises the serialiser
    // every time, and guarantees nothing in the play session can reach
    // back into the world being edited.
    const Json snapshot = LevelSerializer::saveWorld(*editWorld_);
    playWorld_ = std::make_unique<World>(false);
    playWorld_->setAssets(&assets_);
    if (!LevelSerializer::loadWorld(*playWorld_, snapshot)) {
        playWorld_.reset();
        setStatus("Could not start play", 4.0f);
        return;
    }
    playWorld_->beginPlay();
    setStatus("Playing - press Escape or Stop to return");
}

void Editor::stop() {
    if (!playWorld_) return;
    playWorld_->endPlay();
    playWorld_.reset();
    setStatus("Stopped", 2.0f);
}

void Editor::togglePause() {
    if (playWorld_) playWorld_->setPaused(!playWorld_->isPaused());
}

// ---------------------------------------------------------------
//  Selection
// ---------------------------------------------------------------

void Editor::select(Actor* a) {
    selection_ = a;
    gizmoDragging_ = false;
    gizmoAxis_ = -1;
}

Actor* Editor::spawnClass(const ClassInfo* cls) {
    if (!cls || !cls->placeable()) return nullptr;
    // Drop it a few metres in front of the camera, where the user is
    // looking, rather than at the origin.
    const Vec3 at = camera_.position + camera_.rotation.toQuat().forward() * 8.0f;
    Vec3 snapped = at;
    if (gridSnap > 0.0f) {
        snapped.x = std::round(at.x / gridSnap) * gridSnap;
        snapped.y = std::round(at.y / gridSnap) * gridSnap;
        snapped.z = std::round(at.z / gridSnap) * gridSnap;
    }
    Actor* a = editWorld_->spawn(cls, {}, snapped);
    if (a) {
        select(a);
        markDirty();
        setStatus("Added " + a->actorName(), 2.0f);
    }
    return a;
}

void Editor::deleteSelection() {
    if (!selection_ || playWorld_) return;
    const std::string name = selection_->actorName();
    selection_->destroy();
    selection_ = nullptr;
    markDirty();
    setStatus("Deleted " + name, 2.0f);
}

void Editor::duplicateSelection() {
    if (!selection_ || playWorld_) return;
    const Json j = LevelSerializer::saveActor(*selection_);
    Actor* copy = LevelSerializer::loadActor(*editWorld_, j, false);
    if (!copy) return;
    // Offset the copy so it is visibly a second object rather than
    // hiding exactly inside the original.
    copy->addOffset({1.0f, 0.0f, 1.0f});
    select(copy);
    markDirty();
    setStatus("Duplicated", 2.0f);
}

void Editor::focusSelection() {
    if (selection_) camera_.focusOn(selection_->worldBounds());
    else camera_.focusOn(Box{{-10, -1, -10}, {10, 5, 10}});
}

// ---------------------------------------------------------------
//  Frame
// ---------------------------------------------------------------

void Editor::tick(float dt, InputState& in) {
    input_ = &in;

    if (statusTimer_ > 0.0f) statusTimer_ = std::max(0.0f, statusTimer_ - dt);

    // Shortcuts. Escape leaves play; while editing it clears selection.
    if (in.wasPressed(Key::Escape)) {
        if (playWorld_) stop();
        else select(nullptr);
    }
    if (!playWorld_ && !ui_.keyboardFocus()) {
        if (in.wasPressed(Key::W)) gizmoMode = GizmoMode::Translate;
        if (in.wasPressed(Key::R)) gizmoMode = GizmoMode::Rotate;
        if (in.wasPressed(Key::T)) gizmoMode = GizmoMode::Scale;
        if (in.wasPressed(Key::F)) focusSelection();
        if (in.wasPressed(Key::Delete)) deleteSelection();
        if (in.isDown(Key::LeftControl) && in.wasPressed(Key::D)) duplicateSelection();
        if (in.isDown(Key::LeftControl) && in.wasPressed(Key::S)) saveLevel({});
        if (in.wasPressed(Key::G)) showGrid = !showGrid;
    }
    if (in.wasPressed(Key::F5)) {
        if (playWorld_) stop();
        else play();
    }

    if (playWorld_) playWorld_->tick(dt);
    else editWorld_->tick(dt);
}

Rect Editor::viewportRect(const Framebuffer& target) const {
    const float w = (float)target.width(), h = (float)target.height();
    const float leftW = 190.0f, rightW = 340.0f;
    const float toolbarH = 32.0f, bottomH = 168.0f, statusH = 20.0f;
    return {leftW, toolbarH, w - leftW - rightW, h - toolbarH - bottomH - statusH};
}

void Editor::render(Framebuffer& target) {
    if (!target.valid()) return;
    const float w = (float)target.width(), h = (float)target.height();

    target.clearColor(ui_.theme().background);

    Rect screen{0, 0, w, h};
    const Rect toolbar = screen.cutTop(32.0f);
    const Rect status = screen.cutBottom(20.0f);
    const Rect bottom = screen.cutBottom(168.0f);
    const Rect left = screen.cutLeft(190.0f);
    const Rect right = screen.cutRight(340.0f);
    const Rect viewport = screen;

    // The viewport is rendered first so panels composite over it.
    renderViewport(target, viewport);

    ui_.beginFrame(target, *input_);

    drawToolbar(toolbar);
    drawPalette(left);

    // Right column: outliner over details.
    const float outlinerH = std::max(120.0f, right.h * 0.42f);
    Rect rightCol = right;
    drawOutliner(rightCol.cutTop(outlinerH));
    drawDetails(rightCol);

    // Bottom: content browser and log, side by side.
    Rect bottomRow = bottom;
    drawContentBrowser(bottomRow.cutLeft(bottom.w * 0.55f));
    drawOutputLog(bottomRow);

    drawViewportOverlay(viewport);
    drawStatusBar(status);

    // Input for the viewport is handled after the UI has had its say, so
    // a click on a panel never also lands in the world.
    handleViewportInput(viewport, *input_);

    ui_.endFrame();
}

// ---------------------------------------------------------------
//  Viewport
// ---------------------------------------------------------------

void Editor::renderViewport(Framebuffer& target, const Rect& r) {
    if (!r.valid()) return;
    const int vw = std::max(1, (int)(r.w * viewportScale_));
    const int vh = std::max(1, (int)(r.h * viewportScale_));
    if (viewportBuffer_.width() != vw || viewportBuffer_.height() != vh)
        viewportBuffer_.resize(vw, vh);

    World& w = world();
    RenderView view;
    if (playWorld_) {
        // While playing, look through the player's camera; fall back to
        // the editor camera if the game has not set one up.
        if (CameraComponent* cam = playWorld_->viewCamera()) {
            view.view = cam->viewMatrix();
            view.projection = cam->projectionMatrix(viewportBuffer_.aspect());
            view.cameraPosition = cam->worldLocation();
            view.nearClip = cam->nearClip;
            view.farClip = cam->farClip;
        } else {
            view = camera_.view(viewportBuffer_.aspect());
        }
    } else {
        view = camera_.view(viewportBuffer_.aspect());
    }
    lastView_ = view;

    RenderScene scene = RenderScene::collect(w, !playWorld_, selection_);
    renderer_.showBounds = showBounds;
    renderer_.render(viewportBuffer_, scene, view);
    lastFrameMs_ = renderer_.stats().milliseconds;

    if (showGrid && !playWorld_) drawGrid(viewportBuffer_, view);

    // Blit into the main framebuffer, scaling if the viewport is being
    // rendered below native resolution.
    for (int y = 0; y < (int)r.h; ++y) {
        const int sy = std::min(vh - 1, (int)((float)y * viewportScale_));
        const Color* src = viewportBuffer_.colorRow(sy);
        for (int x = 0; x < (int)r.w; ++x) {
            const int sx = std::min(vw - 1, (int)((float)x * viewportScale_));
            target.setPixel((int)r.x + x, (int)r.y + y, src[sx]);
        }
    }
}

void Editor::drawGrid(Framebuffer& target, const RenderView& view) {
    UIDraw d(target);
    const Mat4 viewProj = view.projection * view.view;
    const int extent = 20;
    const float step = 1.0f;

    auto project = [&](const Vec3& p, Vec2& out) {
        const Vec4 c = viewProj * Vec4(p, 1.0f);
        if (c.w <= 1e-4f) return false;
        out = {(c.x / c.w * 0.5f + 0.5f) * (float)target.width(),
               (1.0f - (c.y / c.w * 0.5f + 0.5f)) * (float)target.height()};
        return true;
    };

    // Centre the grid on the camera so it never runs out from under you.
    const float cx = std::round(view.cameraPosition.x / step) * step;
    const float cz = std::round(view.cameraPosition.z / step) * step;

    for (int i = -extent; i <= extent; ++i) {
        const float o = (float)i * step;
        // The axes through the origin get their own colours, the way
        // every 3D tool marks X and Z.
        const bool axisX = std::fabs(cz + o) < 1e-3f;
        const bool axisZ = std::fabs(cx + o) < 1e-3f;
        const Color lineColor = axisX ? Color::fromHex("#a04040")
                              : axisZ ? Color::fromHex("#4060a0")
                              : Color::fromHex("#2a2f38");
        const float alpha = (axisX || axisZ) ? 0.9f : (i % 10 == 0 ? 0.5f : 0.25f);

        Vec2 a, b;
        if (project({cx - (float)extent * step, 0, cz + o}, a) &&
            project({cx + (float)extent * step, 0, cz + o}, b))
            d.line(a.x, a.y, b.x, b.y, lineColor, alpha);
        if (project({cx + o, 0, cz - (float)extent * step}, a) &&
            project({cx + o, 0, cz + (float)extent * step}, b))
            d.line(a.x, a.y, b.x, b.y, lineColor, alpha);
    }
}

Actor* Editor::pickActor(const Rect& r, const Vec2& mouse) const {
    if (!r.contains(mouse) || !editWorld_) return nullptr;
    const float ndcX = ((mouse.x - r.x) / r.w) * 2.0f - 1.0f;
    const float ndcY = 1.0f - ((mouse.y - r.y) / r.h) * 2.0f;
    const Ray ray = camera_.rayThrough(ndcX, ndcY, r.w / std::max(1.0f, r.h));

    // Ray against every actor's bounds, nearest wins. Bounds rather than
    // triangles: it matches what the selection outline shows, and a
    // click is not precise enough for the difference to matter.
    Actor* best = nullptr;
    float bestT = 1e30f;
    for (const auto& a : editWorld_->actors()) {
        if (a->isPendingKill()) continue;
        const Box bounds = a->worldBounds();
        if (!bounds.valid()) continue;
        float t; Vec3 n;
        if (!rayBox(ray, bounds, 5000.0f, t, n)) continue;
        if (t >= bestT) continue;
        bestT = t;
        best = a.get();
    }
    return best;
}

void Editor::handleViewportInput(const Rect& r, InputState& in) {
    const bool overViewport = r.contains(ui_.mouse()) && !ui_.wantsMouse() && !ui_.popupOpen();

    if (!playWorld_) {
        camera_.update(in.mouseDelta().lengthSq() >= 0.0f ? 1.0f / 60.0f : 0.0f, in, overViewport);
        // Gizmo first: dragging a handle must not also re-pick.
        const bool usedGizmo = handleGizmoDrag(r, lastView_);
        if (!usedGizmo && overViewport && ui_.mouseClicked() && !in.isDown(Key::MouseRight))
            select(pickActor(r, ui_.mouse()));
    }
}

// ---------------------------------------------------------------
//  Gizmo
// ---------------------------------------------------------------

namespace {

// Where a world point lands in the viewport, or false if it is behind
// the camera.
bool projectToViewport(const Mat4& viewProj, const Rect& r, const Vec3& p, Vec2& out) {
    const Vec4 c = viewProj * Vec4(p, 1.0f);
    if (c.w <= 1e-4f) return false;
    out = {r.x + (c.x / c.w * 0.5f + 0.5f) * r.w,
           r.y + (1.0f - (c.y / c.w * 0.5f + 0.5f)) * r.h};
    return true;
}

// Distance from a point to a segment, in 2D. Used to decide which gizmo
// handle the pointer is over.
float distanceToSegment(const Vec2& p, const Vec2& a, const Vec2& b) {
    const Vec2 ab = b - a;
    const float len2 = ab.lengthSq();
    if (len2 < 1e-6f) return (p - a).length();
    const float t = clampf(dot(p - a, ab) / len2, 0.0f, 1.0f);
    return (p - (a + ab * t)).length();
}

} // namespace

void Editor::drawGizmo(const Rect& r, const RenderView& view) {
    if (!selection_ || playWorld_) return;
    UIDraw& d = ui_.draw();
    const Mat4 viewProj = view.projection * view.view;
    const Vec3 origin = selection_->transform().position;

    Vec2 originScreen;
    if (!projectToViewport(viewProj, r, origin, originScreen)) return;

    // Scale the gizmo with distance so it stays the same size on screen
    // whether the object is next to the camera or across the level.
    const float dist = distance(view.cameraPosition, origin);
    const float length = std::max(0.35f, dist * 0.16f);

    const Vec3* axes = kAxisDirections;
    const Color* colors = kAxisColors;

    d.pushClip(r);
    for (int i = 0; i < 3; ++i) {
        Vec2 tip;
        if (!projectToViewport(viewProj, r, origin + axes[i] * length, tip)) continue;
        const bool active = gizmoAxis_ == i;
        const Color c = active ? Color::fromHex("#ffd050") : colors[i];
        d.line(originScreen.x, originScreen.y, tip.x, tip.y, c);
        d.line(originScreen.x + 1.0f, originScreen.y, tip.x + 1.0f, tip.y, c);
        d.line(originScreen.x, originScreen.y + 1.0f, tip.x, tip.y + 1.0f, c);

        // The handle at the tip says what the mode does.
        switch (gizmoMode) {
            case GizmoMode::Translate: {
                const Vec2 dir = (tip - originScreen).normalized();
                const Vec2 side{-dir.y, dir.x};
                d.triangle(tip + dir * 8.0f, tip + side * 4.0f, tip - side * 4.0f, c);
                break;
            }
            case GizmoMode::Scale:
                d.fill({tip.x - 4.0f, tip.y - 4.0f, 8.0f, 8.0f}, c);
                break;
            case GizmoMode::Rotate:
                d.circle(tip.x, tip.y, 5.0f, c, false);
                break;
        }
    }
    d.circle(originScreen.x, originScreen.y, 3.0f, Color::fromHex("#e8e8e8"));
    d.popClip();
}

bool Editor::handleGizmoDrag(const Rect& r, const RenderView& view) {
    if (!selection_ || playWorld_) { gizmoDragging_ = false; return false; }

    const Mat4 viewProj = view.projection * view.view;
    const Vec3 origin = selection_->transform().position;
    Vec2 originScreen;
    if (!projectToViewport(viewProj, r, origin, originScreen)) return false;

    const float dist = distance(view.cameraPosition, origin);
    const float length = std::max(0.35f, dist * 0.16f);
    const Vec3* axes = kAxisDirections;

    if (!gizmoDragging_) {
        gizmoAxis_ = -1;
        if (r.contains(ui_.mouse()) && !ui_.wantsMouse()) {
            float best = 8.0f;   // pixels of grab tolerance
            for (int i = 0; i < 3; ++i) {
                Vec2 tip;
                if (!projectToViewport(viewProj, r, origin + axes[i] * length, tip)) continue;
                const float d = distanceToSegment(ui_.mouse(), originScreen, tip);
                if (d < best) { best = d; gizmoAxis_ = i; }
            }
        }
        if (gizmoAxis_ >= 0 && ui_.mouseClicked()) {
            gizmoDragging_ = true;
            const Vec3 t = selection_->transform().position;
            gizmoStartValue_ = gizmoMode == GizmoMode::Scale ? selection_->scale3D()
                             : gizmoMode == GizmoMode::Rotate
                                 ? Vec3{selection_->rotation().pitch, selection_->rotation().yaw,
                                        selection_->rotation().roll}
                                 : t;
            gizmoGrabPoint_ = {ui_.mouse().x, ui_.mouse().y, 0.0f};
        }
    }

    if (gizmoDragging_) {
        if (!ui_.mouseDown()) {
            gizmoDragging_ = false;
            markDirty();
            return true;
        }
        // Project the axis into screen space and move along it by however
        // far the pointer has travelled in that direction. This is what
        // makes dragging feel like it follows the handle.
        Vec2 tip;
        if (projectToViewport(viewProj, r, origin + axes[gizmoAxis_] * length, tip)) {
            const Vec2 axisScreen = tip - originScreen;
            const float axisLen = axisScreen.length();
            if (axisLen > 1e-3f) {
                const Vec2 dragged = ui_.mouse() - Vec2{gizmoGrabPoint_.x, gizmoGrabPoint_.y};
                const float along = dot(dragged, axisScreen / axisLen);
                const float worldPerPixel = length / axisLen;

                switch (gizmoMode) {
                    case GizmoMode::Translate: {
                        Vec3 p = gizmoStartValue_ + axes[gizmoAxis_] * (along * worldPerPixel);
                        if (gridSnap > 0.0f) {
                            p.x = std::round(p.x / gridSnap) * gridSnap;
                            p.y = std::round(p.y / gridSnap) * gridSnap;
                            p.z = std::round(p.z / gridSnap) * gridSnap;
                        }
                        selection_->setLocation(p);
                        break;
                    }
                    case GizmoMode::Scale: {
                        Vec3 s = gizmoStartValue_;
                        s[gizmoAxis_] = std::max(0.01f, s[gizmoAxis_] + along * worldPerPixel);
                        selection_->setScale(s);
                        break;
                    }
                    case GizmoMode::Rotate: {
                        Rotator rot{gizmoStartValue_.x, gizmoStartValue_.y, gizmoStartValue_.z};
                        const float degrees = along * 0.5f;
                        if (gizmoAxis_ == 0) rot.pitch += degrees;
                        else if (gizmoAxis_ == 1) rot.yaw += degrees;
                        else rot.roll += degrees;
                        selection_->setRotation(rot.normalized());
                        break;
                    }
                }
                selection_->rerunConstruction();
            }
        }
        return true;
    }
    return gizmoAxis_ >= 0 && ui_.mouseDown();
}

} // namespace forge
