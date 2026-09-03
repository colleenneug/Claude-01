#include "forge/render/RenderScene.hpp"

#include "forge/assets/AssetLibrary.hpp"
#include "forge/components/RenderComponents.hpp"
#include "forge/scene/Actor.hpp"
#include "forge/scene/World.hpp"

namespace forge {

RenderScene RenderScene::collect(const World& world, bool editorView, const Actor* selected) {
    RenderScene scene;
    const WorldSettings& s = world.settings();

    scene.environment.background = s.backgroundColor;
    scene.environment.skyColor = s.skyColor;
    scene.environment.groundColor = s.groundColor;
    scene.environment.ambientIntensity = s.ambientIntensity;
    scene.environment.fogEnabled = s.fogEnabled;
    scene.environment.fogColor = s.fogColor;
    scene.environment.fogDensity = s.fogDensity;
    scene.environment.exposure = s.exposure;
    scene.environment.vignette = s.vignette;
    scene.environment.shadows = s.sunShadows;

    scene.hasSun = true;
    scene.sun.kind = LightKind::Directional;
    scene.sun.direction = s.sunDirection();
    scene.sun.color = s.sunColor;
    scene.sun.intensity = s.sunIntensity;

    AssetLibrary* assets = world.assets();

    for (const auto& actor : world.actors()) {
        if (actor->isPendingKill()) continue;
        // Hidden-in-game actors are editor aids — a player start, a
        // trigger volume — so they are drawn in the editor and not in play.
        if (actor->hiddenInGame && !editorView) continue;
        const bool isSelected = selected && actor.get() == selected;

        for (const auto& comp : actor->components()) {
            if (auto* smc = dynamic_cast<StaticMeshComponent*>(comp.get())) {
                if (!smc->visibleInHierarchy()) continue;
                Mesh* mesh = smc->resolvedMesh();
                if (!mesh || mesh->indices.empty()) continue;

                DrawItem item;
                item.mesh = mesh;
                item.material = smc->resolvedMaterial();
                if (item.material && assets && !item.material->texture.empty())
                    item.texture = assets->texture(item.material->texture);
                item.transform = smc->worldMatrix();
                item.normalBasis = Mat4::rotation(smc->worldTransform().rotation);
                item.tint = smc->tint;
                item.worldBounds = mesh->bounds.transformed(item.transform);
                item.castShadow = smc->castShadow && (!item.material || item.material->castsShadow);
                item.selected = isSelected;
                item.actor = actor.get();
                scene.items.push_back(item);
                continue;
            }

            if (auto* light = dynamic_cast<LightComponent*>(comp.get())) {
                if (!light->visibleInHierarchy()) continue;
                LightItem li;
                li.color = light->color;
                li.intensity = light->intensity;
                li.position = light->worldLocation();
                li.direction = light->forward();

                if (auto* spot = dynamic_cast<SpotLightComponent*>(light)) {
                    li.kind = LightKind::Spot;
                    li.radius = spot->radius;
                    li.innerCos = std::cos(radians(std::min(spot->innerConeDegrees, spot->outerConeDegrees)));
                    li.outerCos = std::cos(radians(std::max(spot->innerConeDegrees, spot->outerConeDegrees)));
                } else if (auto* point = dynamic_cast<PointLightComponent*>(light)) {
                    li.kind = LightKind::Point;
                    li.radius = point->radius;
                } else {
                    li.kind = LightKind::Directional;
                }
                scene.lights.push_back(li);
            }
        }
    }
    return scene;
}

} // namespace forge
