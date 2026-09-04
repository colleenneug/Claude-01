// ============================================================
//  Levels on disk.
//
//  A level is JSON: world settings, then a list of actors. Each actor
//  records its class, its name, and only the properties that differ
//  from that class's defaults — so changing a class updates every
//  level already saved, and a diff between two saves shows what
//  actually changed.
//
//  Components come in two kinds. Ones the actor's class builds are
//  matched by name and only their overrides are stored. Ones a
//  designer added to that instance are stored whole, because nothing
//  else would recreate them.
// ============================================================
#pragma once

#include <string>

#include "forge/core/Json.hpp"

namespace forge {

class World;
class Actor;

class LevelSerializer {
public:
    static Json saveWorld(const World& world);
    // Replaces everything in `world`. Returns false and leaves the world
    // empty if the document is not a level.
    static bool loadWorld(World& world, const Json& doc);

    static bool saveToFile(const World& world, const std::string& path);
    static bool loadFromFile(World& world, const std::string& path);

    // One actor, for copy and paste and for the undo stack.
    static Json saveActor(const Actor& actor);
    static Actor* loadActor(World& world, const Json& j, bool keepName = true);

    static constexpr const char* kFormat = "forge.level";
    static constexpr int kVersion = 1;
};

} // namespace forge
