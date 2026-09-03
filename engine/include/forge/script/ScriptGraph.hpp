// ============================================================
//  Visual scripting.
//
//  A script is a graph: event nodes start execution, white "exec" wires
//  say what happens next, and coloured data wires carry values. It is
//  the same model Blueprints use, and for the same reason — most
//  gameplay is a handful of reactions to events, and a graph says that
//  more directly than a subclass does.
//
//  Two kinds of node:
//
//    impure   has exec pins, runs when execution reaches it, and names
//             which exec output to follow next
//    pure     has no exec pins and is evaluated on demand when a data
//             pin downstream is read (arithmetic, comparisons, getters)
//
//  Data is pulled, not pushed: reading an input walks up the wire and
//  evaluates whatever pure nodes it finds, so nothing is computed that
//  nothing asked for.
// ============================================================
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "forge/core/Json.hpp"
#include "forge/core/Object.hpp"
#include "forge/math/Math.hpp"

namespace forge {

class Actor;
class World;
class ScriptInstance;

enum class PinType { Exec, Bool, Int, Float, String, Vec3, Rotator, Color, Actor, Wildcard };

// One value on a wire. Reuses the reflection value type so a script can
// read and write any reflected property without a conversion layer.
struct ScriptValue {
    PropType type = PropType::Float;
    bool asBool = false;
    int asInt = 0;
    float asFloat = 0.0f;
    std::string asString;
    Vec3 asVec3;
    Rotator asRotator;
    Color asColor;
    forge::Actor* asActor = nullptr;

    static ScriptValue fromProperty(const PropertyValue& p);
    PropertyValue toProperty() const;

    // Readers coerce, so a Float pin wired to an Int socket just works.
    bool boolean() const;
    int integer() const;
    float number() const;
    std::string text() const;
    Vec3 vector() const;
    Rotator rotator() const;
    Color color() const;

    static ScriptValue make(bool v);
    static ScriptValue make(int v);
    static ScriptValue make(float v);
    static ScriptValue make(const std::string& v);
    static ScriptValue make(const Vec3& v);
    static ScriptValue make(const Rotator& v);
    static ScriptValue make(const Color& v);
    static ScriptValue makeActor(forge::Actor* a);
};

struct PinDef {
    std::string name;
    PinType type = PinType::Float;
    bool input = true;
    ScriptValue defaultValue;
};

class ScriptExec;

struct NodeDef {
    std::string type;                  // stable identifier stored in files
    std::string display;
    std::string category = "Misc";
    std::string tooltip;
    std::vector<PinDef> pins;
    bool isEvent = false;
    bool pure = false;
    // Impure nodes run this and return the index of the exec output to
    // follow, or -1 to stop this branch.
    std::function<int(ScriptExec&)> run;

    int findPin(const std::string& name, bool input) const;
    std::vector<const PinDef*> inputs() const;
    std::vector<const PinDef*> outputs() const;
};

class NodeLibrary {
public:
    static NodeLibrary& get();
    void add(NodeDef def);
    const NodeDef* find(const std::string& type) const;
    const std::vector<NodeDef>& all() const { return defs_; }
    std::vector<const NodeDef*> byCategory(const std::string& category) const;
    std::vector<std::string> categories() const;
    // Substring match over name and tooltip, for the editor's node search.
    std::vector<const NodeDef*> search(const std::string& query) const;

private:
    NodeLibrary();
    std::vector<NodeDef> defs_;
    std::unordered_map<std::string, size_t> index_;
};

struct ScriptNode {
    int id = 0;
    std::string type;
    Vec2 editorPos{0, 0};
    std::string comment;
    // Literals typed into unconnected input pins, by pin name.
    std::unordered_map<std::string, ScriptValue> literals;
    // Free-form settings a node reads for itself: the variable a Get/Set
    // node targets, the property a Set Property node writes.
    std::unordered_map<std::string, std::string> config;
};

struct ScriptLink {
    int fromNode = 0;
    std::string fromPin;
    int toNode = 0;
    std::string toPin;
};

struct ScriptVariable {
    std::string name;
    PinType type = PinType::Float;
    ScriptValue defaultValue;
    bool exposed = false;   // shown on the actor's details panel
};

class ScriptGraph {
public:
    std::string name;
    std::vector<ScriptNode> nodes;
    std::vector<ScriptLink> links;
    std::vector<ScriptVariable> variables;

    int addNode(const std::string& type, const Vec2& pos);
    void removeNode(int id);
    ScriptNode* node(int id);
    const ScriptNode* node(int id) const;
    bool link(int fromNode, const std::string& fromPin, int toNode, const std::string& toPin);
    void unlink(int toNode, const std::string& toPin);
    void unlinkAll(int nodeId);
    const ScriptLink* linkInto(int node, const std::string& pin) const;
    std::vector<const ScriptLink*> linksOut(int node, const std::string& pin) const;
    std::vector<int> eventNodes(const std::string& eventType) const;
    ScriptVariable* variable(const std::string& name);
    const ScriptVariable* variable(const std::string& name) const;

    // Reports cycles among pure nodes and links to pins that no longer
    // exist, which is what a graph edited across a version change hits.
    std::vector<std::string> validate() const;

    Json toJson() const;
    static std::unique_ptr<ScriptGraph> fromJson(const Json& j);

private:
    int nextId_ = 1;
};

// A running copy of a graph, bound to one actor.
class ScriptInstance {
public:
    ScriptInstance(const ScriptGraph* graph, Actor* owner);

    void beginPlay();
    void tick(float dt);
    void endPlay();
    void fireEvent(const std::string& eventType, Actor* other = nullptr);

    ScriptValue getVariable(const std::string& name) const;
    void setVariable(const std::string& name, const ScriptValue& v);

    Actor* owner() const { return owner_; }
    World* world() const;
    const ScriptGraph* graph() const { return graph_; }
    float deltaSeconds() const { return delta_; }

    // Resume from a node's exec output. Latent nodes (Delay) come back
    // through here when their timer fires.
    void resume(int nodeId, int execOutput);

private:
    friend class ScriptExec;

    void runFrom(int nodeId);
    ScriptValue readPin(int nodeId, const std::string& pinName);
    ScriptValue evaluate(int nodeId, const std::string& outputPin);

    const ScriptGraph* graph_ = nullptr;
    Actor* owner_ = nullptr;
    float delta_ = 0.0f;
    std::unordered_map<std::string, ScriptValue> variables_;
    // Per-node persistent state: DoOnce's latch, Gate's open flag,
    // FlipFlop's side, a loop's counter.
    std::unordered_map<int, ScriptValue> nodeState_;
    // Guards against a graph that wires itself into a loop with no exit.
    int execDepth_ = 0;
    int pureDepth_ = 0;
    Actor* eventOther_ = nullptr;
};

// What a node's run function is handed.
class ScriptExec {
public:
    ScriptExec(ScriptInstance& inst, const ScriptNode& node, const NodeDef& def)
        : instance(inst), node(node), def(def) {}

    ScriptValue in(const std::string& pin) { return instance.readPin(node.id, pin); }
    void out(const std::string& pin, const ScriptValue& v) { outputs[pin] = v; }
    const std::string& config(const std::string& key) const;

    Actor* owner() const { return instance.owner(); }
    World* world() const { return instance.world(); }
    Actor* eventOther() const { return instance.eventOther_; }
    // Every Actor.* node reads an optional "Target" pin and falls back to
    // the node's own actor when it is unwired -- this is that pattern.
    Actor* target(const std::string& pin = "Target") {
        Actor* a = in(pin).asActor;
        return a ? a : owner();
    }

    ScriptValue& state() { return instance.nodeState_[node.id]; }

    ScriptInstance& instance;
    const ScriptNode& node;
    const NodeDef& def;
    std::unordered_map<std::string, ScriptValue> outputs;

private:
    static const std::string kEmpty;
};

// Registers the built-in node set. Called once at start-up.
void registerCoreNodes();

} // namespace forge
