// ============================================================
//  The editor.
//
//  The layout is the one every 3D tool converges on, because it works:
//  a toolbar across the top, a palette of things you can place on the
//  left, the viewport in the middle, the world outliner and a details
//  panel on the right, and the content browser and output log along the
//  bottom.
//
//  The details panel is the part worth pointing at. It contains no
//  per-class code at all: it walks the selected actor's reflected
//  properties, groups them by their declared category, and picks a
//  widget from each property's type. Adding a property to a class makes
//  it appear here, editable, undoable and saved, with no other edit.
//
//  Play-In-Editor keeps two worlds. Pressing Play snapshots the world
//  being edited to JSON and builds a second one from it; Stop throws
//  that away. The edited world is never touched, so playing cannot
//  disturb the level.
// ============================================================
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "forge/assets/AssetLibrary.hpp"
#include "forge/render/SoftwareRenderer.hpp"
#include "forge/scene/World.hpp"
#include "forge/ui/UI.hpp"

namespace forge {

// A fly camera with the controls a 3D tool is expected to have: hold
// right mouse to look and WASD to move, middle-drag to pan, wheel to
// dolly, F to frame the selection.
class EditorCamera {
public:
    Vec3 position{8, 6, 12};
    Rotator rotation{-20, 35, 0};
    float fieldOfView = 65.0f;
    float moveSpeed = 10.0f;
    float lookSensitivity = 0.18f;

    void update(float dt, const InputState& input, bool viewportHasMouse);
    // Frames a box, backing off far enough for it to fit the view.
    void focusOn(const Box& bounds);
    RenderView view(float aspect) const;
    Ray rayThrough(float ndcX, float ndcY, float aspect) const;

private:
    Vec3 velocity_{0, 0, 0};
    Vec3 focusTarget_{0, 0, 0};
    // 1 means "no focus animation pending". Starting at 0 would mean one
    // is in progress toward a target nobody has set yet.
    float focusBlend_ = 1.0f;
    Vec3 focusFrom_{0, 0, 0};
};

enum class GizmoMode { Translate, Rotate, Scale };

class Editor {
public:
    Editor();
    ~Editor();

    void setup();
    void tick(float dt, InputState& input);
    void render(Framebuffer& target);

    // ---- level ----
    void newLevel();
    bool saveLevel(const std::string& path);
    bool loadLevel(const std::string& path);
    const std::string& levelPath() const { return levelPath_; }
    bool isDirty() const { return dirty_; }
    void markDirty() { dirty_ = true; }

    // ---- play ----
    void play();
    void stop();
    void togglePause();
    bool isPlaying() const { return playWorld_ != nullptr; }

    // ---- selection and editing ----
    void select(Actor* a);
    Actor* selection() const { return selection_; }
    Actor* spawnClass(const ClassInfo* cls);
    void deleteSelection();
    void duplicateSelection();
    void focusSelection();

    World& world() { return playWorld_ ? *playWorld_ : *editWorld_; }
    const World& world() const { return playWorld_ ? *playWorld_ : *editWorld_; }
    World& editorWorld() { return *editWorld_; }
    AssetLibrary& assets() { return assets_; }
    UIContext& ui() { return ui_; }
    EditorCamera& camera() { return camera_; }

    // Builds a small level so a fresh editor is not an empty void.
    void createDefaultLevel();

    // Exposed so a headless run can drive the editor for a screenshot.
    Rect viewportRect(const Framebuffer& target) const;

    GizmoMode gizmoMode = GizmoMode::Translate;
    bool showGrid = true;
    bool showBounds = false;
    float gridSnap = 0.0f;

private:
    // ---- panels ----
    void drawToolbar(const Rect& r);
    void drawPalette(const Rect& r);
    void drawOutliner(const Rect& r);
    void drawDetails(const Rect& r);
    void drawContentBrowser(const Rect& r);
    void drawOutputLog(const Rect& r);
    void drawStatusBar(const Rect& r);
    void drawViewportOverlay(const Rect& r);

    // Draws one reflected property. The whole details panel is this
    // function in a loop.
    bool drawProperty(const Rect& row, Object& object, const Property& prop);
    void drawComponentTree(const Rect& r, Actor& actor, float& y);

    // ---- viewport ----
    void renderViewport(Framebuffer& target, const Rect& r);
    void handleViewportInput(const Rect& r, InputState& input);
    Actor* pickActor(const Rect& r, const Vec2& mouse) const;
    void drawGrid(Framebuffer& target, const RenderView& view);
    void drawGizmo(const Rect& r, const RenderView& view);
    bool handleGizmoDrag(const Rect& r, const RenderView& view);

    AssetLibrary assets_;
    std::unique_ptr<World> editWorld_;
    std::unique_ptr<World> playWorld_;
    SoftwareRenderer renderer_;
    Framebuffer viewportBuffer_;
    RenderView lastView_;
    UIContext ui_;
    EditorCamera camera_;
    InputState* input_ = nullptr;

    Actor* selection_ = nullptr;
    std::string levelPath_;
    bool dirty_ = false;
    std::string statusMessage_;
    float statusTimer_ = 0.0f;

    // Gizmo drag state.
    int gizmoAxis_ = -1;
    bool gizmoDragging_ = false;
    Vec3 gizmoStartValue_{0, 0, 0};
    Vec3 gizmoGrabPoint_{0, 0, 0};

    std::string paletteFilter_;
    std::string contentFilter_;
    int contentTab_ = 0;
    std::string renameBuffer_;
    float viewportScale_ = 1.0f;
    double lastFrameMs_ = 0.0;
};

} // namespace forge
