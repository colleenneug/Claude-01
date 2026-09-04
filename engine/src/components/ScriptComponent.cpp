#include "forge/components/ScriptComponent.hpp"

#include "forge/assets/AssetLibrary.hpp"
#include "forge/core/Log.hpp"
#include "forge/scene/Actor.hpp"
#include "forge/scene/World.hpp"
#include "forge/script/ScriptGraph.hpp"

namespace forge {

ScriptComponent::ScriptComponent() { tickEnabled = true; }

ScriptComponent::~ScriptComponent() { unsubscribe(); }

void ScriptComponent::onRegister() {
    tickEnabled = true;
    rebind();
}

void ScriptComponent::onConstruct() { rebind(); }

void ScriptComponent::unsubscribe() {
    Actor* a = owner();
    if (!a) return;
    if (overlapBeginHandle_) { a->onBeginOverlap.unbind(overlapBeginHandle_); overlapBeginHandle_ = 0; }
    if (overlapEndHandle_) { a->onEndOverlap.unbind(overlapEndHandle_); overlapEndHandle_ = 0; }
    if (damageHandle_) { a->onDamaged.unbind(damageHandle_); damageHandle_ = 0; }
}

void ScriptComponent::rebind() {
    World* w = world();
    AssetLibrary* lib = w ? w->assets() : nullptr;
    const ScriptGraph* graph = lib ? lib->script(script) : nullptr;

    if (graph == bound_) return;
    bound_ = graph;
    unsubscribe();
    instance_.reset();

    if (!graph || !owner()) {
        if (!script.empty() && !graph)
            FORGE_WARN("ScriptComponent: no script asset named '%s'", script.c_str());
        return;
    }

    instance_ = std::make_unique<ScriptInstance>(graph, owner());

    // The actor's events become script events. Subscribing here rather
    // than polling is what keeps an idle script free.
    Actor* a = owner();
    ScriptInstance* inst = instance_.get();
    overlapBeginHandle_ = a->onBeginOverlap.bind([inst](Actor* other) {
        inst->fireEvent("Event.BeginOverlap", other);
    });
    overlapEndHandle_ = a->onEndOverlap.bind([inst](Actor* other) {
        inst->fireEvent("Event.EndOverlap", other);
    });
    damageHandle_ = a->onDamaged.bind([inst](float amount, Actor* instigator) {
        inst->setVariable("DamageAmount", ScriptValue::make(amount));
        inst->fireEvent("Event.Damaged", instigator);
    });
}

void ScriptComponent::beginPlay() {
    rebind();
    if (instance_) instance_->beginPlay();
}

void ScriptComponent::tick(float dt) {
    World* w = world();
    if (!instance_) return;
    // In the editor a script only runs when it asks to, so opening a
    // level does not start its logic.
    if (w && !w->isPlaying() && !runInEditor) return;
    instance_->tick(dt);
}

void ScriptComponent::endPlay() {
    if (instance_) instance_->endPlay();
}

void ScriptComponent::call(const std::string& eventName) {
    if (!instance_) return;
    // A named custom event is matched by the node's configured name, so
    // several custom events can live in one graph.
    const ScriptGraph* g = instance_->graph();
    if (!g) return;
    for (const ScriptNode& n : g->nodes) {
        if (n.type != "Event.Custom") continue;
        auto it = n.config.find("name");
        if (it != n.config.end() && it->second != eventName) continue;
        instance_->resume(n.id, 0);
    }
}

FORGE_CLASS_BEGIN(ScriptComponent)
    FORGE_DISPLAY("Script")
    FORGE_CATEGORY("Component")
    FORGE_PROP(script).asset(RefKind::Script).cat("Script")
    FORGE_PROP(runInEditor).cat("Script")
        .tooltip("Also tick this script in the editor viewport, without pressing Play.")
FORGE_CLASS_END()
FORGE_REGISTER(ScriptComponent)

} // namespace forge
