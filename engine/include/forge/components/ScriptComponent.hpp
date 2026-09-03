// ============================================================
//  Attaching a script graph to an actor.
//
//  The component owns the running instance and forwards the actor's
//  lifecycle and events into it. Put one on any actor and its graph
//  becomes that actor's behaviour, with no C++ subclass in between.
// ============================================================
#pragma once

#include <memory>

#include "forge/scene/Component.hpp"

namespace forge {

class ScriptGraph;
class ScriptInstance;

class ScriptComponent : public ActorComponent {
    FORGE_OBJECT(ScriptComponent, ActorComponent)
public:
    ScriptComponent();
    ~ScriptComponent() override;

    std::string script;      // script asset name
    bool runInEditor = false;

    void onRegister() override;
    void onConstruct() override;
    void beginPlay() override;
    void tick(float dt) override;
    void endPlay() override;

    // Lets one script call another actor's Custom Event by name.
    void call(const std::string& eventName);
    ScriptInstance* instance() const { return instance_.get(); }

private:
    void rebind();
    void unsubscribe();

    std::unique_ptr<ScriptInstance> instance_;
    const ScriptGraph* bound_ = nullptr;
    int overlapBeginHandle_ = 0;
    int overlapEndHandle_ = 0;
    int damageHandle_ = 0;
};

} // namespace forge
