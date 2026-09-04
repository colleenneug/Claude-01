#include "forge/scene/Level.hpp"

#include "forge/core/Log.hpp"
#include "forge/physics/Physics.hpp"
#include "forge/scene/Actor.hpp"
#include "forge/scene/World.hpp"

namespace forge {

static Json saveComponent(const ActorComponent& c, bool whole) {
    Json j = Json::object();
    j.set("name", c.name);
    if (whole) j.set("class", c.className());
    Json props = c.serializeProperties();
    // The name is already a field of its own; leaving it in the property
    // bag as well would let the two disagree.
    props.erase("name");
    if (props.size() > 0) j.set("props", props);

    if (const auto* sc = dynamic_cast<const SceneComponent*>(&c)) {
        if (sc->parent() && whole) j.set("parent", sc->parent()->name);
    }
    return j;
}

Json LevelSerializer::saveActor(const Actor& a) {
    Json j = Json::object();
    j.set("class", a.className());
    j.set("name", a.name);

    Json props = a.serializeProperties();
    props.erase("name");
    if (props.size() > 0) j.set("props", props);

    // Components the class built: name plus overrides only.
    Json comps = Json::array();
    for (const auto& c : a.components()) {
        Json jc = saveComponent(*c, false);
        // Nothing overridden means nothing worth writing.
        if (jc.has("props")) comps.push(jc);
    }
    if (comps.size() > 0) j.set("components", comps);
    return j;
}

Actor* LevelSerializer::loadActor(World& world, const Json& j, bool keepName) {
    const std::string className = j["class"].asString();
    const ClassInfo* cls = ClassRegistry::get().find(className);
    if (!cls || !cls->isA("Actor")) {
        FORGE_WARN("level: unknown actor class '%s'; skipped", className.c_str());
        return nullptr;
    }

    Object* raw = cls->construct();
    if (!raw) {
        FORGE_WARN("level: '%s' is abstract; skipped", className.c_str());
        return nullptr;
    }
    std::unique_ptr<Actor> actor(static_cast<Actor*>(raw));

    actor->deserializeProperties(j["props"]);
    const std::string name = j["name"].asString(cls->displayName());
    actor->name = keepName ? name : world.makeUniqueName(name);

    const Json& comps = j["components"];
    for (size_t i = 0; i < comps.size(); ++i) {
        const Json& jc = comps[i];
        const std::string cname = jc["name"].asString();
        ActorComponent* c = actor->findComponent(cname);
        if (!c) {
            // Not a component the class builds, so it was added to this
            // instance and has to be recreated from its class.
            const std::string ccls = jc["class"].asString();
            if (ccls.empty()) {
                FORGE_WARN("level: actor '%s' has no component '%s' and the file does not say its class",
                           actor->name.c_str(), cname.c_str());
                continue;
            }
            SceneComponent* parent = nullptr;
            if (jc.has("parent"))
                parent = dynamic_cast<SceneComponent*>(actor->findComponent(jc["parent"].asString()));
            c = actor->addComponentByClass(ClassRegistry::get().find(ccls), cname, parent);
            if (!c) continue;
        }
        c->deserializeProperties(jc["props"]);
    }

    return world.adopt(std::move(actor));
}

Json LevelSerializer::saveWorld(const World& world) {
    Json doc = Json::object();
    doc.set("format", kFormat);
    doc.set("version", kVersion);
    doc.set("name", world.levelName());
    doc.set("settings", world.settings().serializeProperties());

    Json actors = Json::array();
    for (const auto& a : world.actors()) {
        if (a->isPendingKill()) continue;
        // The game mode and the player's pawn are spawned by play, not
        // placed, so writing them would duplicate them on the next load.
        if (a->getClass()->isA("GameMode")) continue;
        if (a->getClass()->isA("PlayerController")) continue;
        if (a->name == "PlayerPawn") continue;
        actors.push(saveActor(*a));
    }
    doc.set("actors", actors);
    return doc;
}

bool LevelSerializer::loadWorld(World& world, const Json& doc) {
    if (doc["format"].asString() != kFormat) {
        FORGE_ERROR("level: not a Forge level (format is '%s')", doc["format"].asString().c_str());
        return false;
    }
    const int version = doc["version"].asInt(1);
    if (version > kVersion)
        FORGE_WARN("level: written by a newer build (version %d); loading anyway", version);

    world.setLevelName(doc["name"].asString("Untitled"));
    world.settings().deserializeProperties(doc["settings"]);
    world.physics().gravity = world.settings().gravity;

    const Json& actors = doc["actors"];
    size_t loaded = 0;
    for (size_t i = 0; i < actors.size(); ++i)
        if (loadActor(world, actors[i])) ++loaded;

    FORGE_LOG("Loaded level '%s': %zu actors", world.levelName().c_str(), loaded);
    return true;
}

bool LevelSerializer::saveToFile(const World& world, const std::string& path) {
    if (!saveWorld(world).saveFile(path)) {
        FORGE_ERROR("level: cannot write %s", path.c_str());
        return false;
    }
    return true;
}

bool LevelSerializer::loadFromFile(World& world, const std::string& path) {
    std::string err;
    Json doc = Json::loadFile(path, &err);
    if (!err.empty()) {
        FORGE_ERROR("level: %s", err.c_str());
        return false;
    }
    return loadWorld(world, doc);
}

} // namespace forge
