#include "forge/script/ScriptGraph.hpp"

#include <algorithm>
#include <cstdio>

#include "forge/core/Log.hpp"
#include "forge/scene/Actor.hpp"
#include "forge/scene/World.hpp"

namespace forge {

const std::string ScriptExec::kEmpty;

// -----------------------------------------------------------------
//  ScriptValue
// -----------------------------------------------------------------

ScriptValue ScriptValue::fromProperty(const PropertyValue& p) {
    ScriptValue v;
    v.type = p.type;
    v.asBool = p.asBool; v.asInt = p.asInt; v.asFloat = p.asFloat;
    v.asString = p.asString; v.asVec3 = p.asVec3; v.asRotator = p.asRotator; v.asColor = p.asColor;
    return v;
}

PropertyValue ScriptValue::toProperty() const {
    PropertyValue p;
    p.type = type;
    p.asBool = asBool; p.asInt = asInt; p.asFloat = asFloat;
    p.asString = asString; p.asVec3 = asVec3; p.asRotator = asRotator; p.asColor = asColor;
    return p;
}

bool ScriptValue::boolean() const {
    switch (type) {
        case PropType::Bool: return asBool;
        case PropType::Int:
        case PropType::Enum: return asInt != 0;
        case PropType::Float: return asFloat != 0.0f;
        case PropType::String: return !asString.empty() && asString != "false" && asString != "0";
        case PropType::Vec3: return !asVec3.isNearlyZero();
        default: return asActor != nullptr;
    }
}

int ScriptValue::integer() const {
    switch (type) {
        case PropType::Int:
        case PropType::Enum: return asInt;
        case PropType::Float: return (int)std::lround(asFloat);
        case PropType::Bool: return asBool ? 1 : 0;
        case PropType::String: return std::atoi(asString.c_str());
        default: return 0;
    }
}

float ScriptValue::number() const {
    switch (type) {
        case PropType::Float: return asFloat;
        case PropType::Int:
        case PropType::Enum: return (float)asInt;
        case PropType::Bool: return asBool ? 1.0f : 0.0f;
        case PropType::String: return (float)std::atof(asString.c_str());
        default: return 0.0f;
    }
}

std::string ScriptValue::text() const {
    if (type == PropType::String) return asString;
    if (asActor) return asActor->actorName();
    return toProperty().toDisplayString();
}

Vec3 ScriptValue::vector() const {
    if (type == PropType::Vec3) return asVec3;
    if (type == PropType::Rotator) return {asRotator.pitch, asRotator.yaw, asRotator.roll};
    if (type == PropType::Color) return asColor.rgb();
    if (type == PropType::Float) return Vec3(asFloat);
    return Vec3::Zero;
}

Rotator ScriptValue::rotator() const {
    if (type == PropType::Rotator) return asRotator;
    if (type == PropType::Vec3) return {asVec3.x, asVec3.y, asVec3.z};
    return Rotator{};
}

Color ScriptValue::color() const {
    if (type == PropType::Color) return asColor;
    if (type == PropType::Vec3) return {asVec3.x, asVec3.y, asVec3.z, 1.0f};
    if (type == PropType::String) return Color::fromHex(asString);
    return Color{};
}

ScriptValue ScriptValue::make(bool v) { ScriptValue s; s.type = PropType::Bool; s.asBool = v; return s; }
ScriptValue ScriptValue::make(int v) { ScriptValue s; s.type = PropType::Int; s.asInt = v; return s; }
ScriptValue ScriptValue::make(float v) { ScriptValue s; s.type = PropType::Float; s.asFloat = v; return s; }
ScriptValue ScriptValue::make(const std::string& v) { ScriptValue s; s.type = PropType::String; s.asString = v; return s; }
ScriptValue ScriptValue::make(const Vec3& v) { ScriptValue s; s.type = PropType::Vec3; s.asVec3 = v; return s; }
ScriptValue ScriptValue::make(const Rotator& v) { ScriptValue s; s.type = PropType::Rotator; s.asRotator = v; return s; }
ScriptValue ScriptValue::make(const Color& v) { ScriptValue s; s.type = PropType::Color; s.asColor = v; return s; }
ScriptValue ScriptValue::makeActor(Actor* a) { ScriptValue s; s.type = PropType::String; s.asActor = a; return s; }

// -----------------------------------------------------------------
//  NodeDef / NodeLibrary
// -----------------------------------------------------------------

int NodeDef::findPin(const std::string& n, bool isInput) const {
    for (size_t i = 0; i < pins.size(); ++i)
        if (pins[i].input == isInput && pins[i].name == n) return (int)i;
    return -1;
}

std::vector<const PinDef*> NodeDef::inputs() const {
    std::vector<const PinDef*> out;
    for (const PinDef& p : pins) if (p.input) out.push_back(&p);
    return out;
}

std::vector<const PinDef*> NodeDef::outputs() const {
    std::vector<const PinDef*> out;
    for (const PinDef& p : pins) if (!p.input) out.push_back(&p);
    return out;
}

NodeLibrary::NodeLibrary() = default;

NodeLibrary& NodeLibrary::get() {
    static NodeLibrary lib;
    return lib;
}

void NodeLibrary::add(NodeDef def) {
    auto it = index_.find(def.type);
    if (it != index_.end()) { defs_[it->second] = std::move(def); return; }
    index_[def.type] = defs_.size();
    defs_.push_back(std::move(def));
}

const NodeDef* NodeLibrary::find(const std::string& type) const {
    auto it = index_.find(type);
    return it == index_.end() ? nullptr : &defs_[it->second];
}

std::vector<const NodeDef*> NodeLibrary::byCategory(const std::string& category) const {
    std::vector<const NodeDef*> out;
    for (const NodeDef& d : defs_) if (d.category == category) out.push_back(&d);
    return out;
}

std::vector<std::string> NodeLibrary::categories() const {
    std::vector<std::string> out;
    for (const NodeDef& d : defs_)
        if (std::find(out.begin(), out.end(), d.category) == out.end()) out.push_back(d.category);
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<const NodeDef*> NodeLibrary::search(const std::string& query) const {
    const std::string q = toLower(query);
    std::vector<const NodeDef*> out;
    if (q.empty()) {
        for (const NodeDef& d : defs_) out.push_back(&d);
        return out;
    }
    // Exact-prefix matches first, so typing "br" puts Branch on top
    // rather than burying it under anything mentioning "branch".
    for (const NodeDef& d : defs_) if (toLower(d.display).rfind(q, 0) == 0) out.push_back(&d);
    for (const NodeDef& d : defs_) {
        if (std::find(out.begin(), out.end(), &d) != out.end()) continue;
        if (containsCI(d.display, query) || containsCI(d.category, query) || containsCI(d.tooltip, query))
            out.push_back(&d);
    }
    return out;
}

// -----------------------------------------------------------------
//  ScriptGraph
// -----------------------------------------------------------------

int ScriptGraph::addNode(const std::string& type, const Vec2& pos) {
    if (!NodeLibrary::get().find(type)) {
        FORGE_WARN("script: unknown node type '%s'", type.c_str());
        return 0;
    }
    ScriptNode n;
    n.id = nextId_++;
    n.type = type;
    n.editorPos = pos;
    nodes.push_back(std::move(n));
    return nodes.back().id;
}

void ScriptGraph::removeNode(int id) {
    unlinkAll(id);
    nodes.erase(std::remove_if(nodes.begin(), nodes.end(),
                               [&](const ScriptNode& n) { return n.id == id; }),
                nodes.end());
}

ScriptNode* ScriptGraph::node(int id) {
    for (ScriptNode& n : nodes) if (n.id == id) return &n;
    return nullptr;
}

const ScriptNode* ScriptGraph::node(int id) const {
    for (const ScriptNode& n : nodes) if (n.id == id) return &n;
    return nullptr;
}

bool ScriptGraph::link(int fromNode, const std::string& fromPin, int toNode, const std::string& toPin) {
    const ScriptNode* a = node(fromNode);
    const ScriptNode* b = node(toNode);
    if (!a || !b) return false;
    const NodeDef* da = NodeLibrary::get().find(a->type);
    const NodeDef* db = NodeLibrary::get().find(b->type);
    if (!da || !db) return false;

    int pa = da->findPin(fromPin, false);
    int pb = db->findPin(toPin, true);
    if (pa < 0 || pb < 0) return false;

    const PinType ta = da->pins[(size_t)pa].type;
    const PinType tb = db->pins[(size_t)pb].type;
    // Exec only connects to exec. Everything else coerces, so a Float
    // may drive an Int pin without the graph refusing the wire.
    if ((ta == PinType::Exec) != (tb == PinType::Exec)) return false;

    if (ta == PinType::Exec) {
        // An exec output drives one target; a data input takes one source.
        // Both are single-slot, so the new wire replaces the old.
        links.erase(std::remove_if(links.begin(), links.end(), [&](const ScriptLink& l) {
                        return l.fromNode == fromNode && l.fromPin == fromPin;
                    }), links.end());
    } else {
        unlink(toNode, toPin);
    }
    links.push_back({fromNode, fromPin, toNode, toPin});
    return true;
}

void ScriptGraph::unlink(int toNode, const std::string& toPin) {
    links.erase(std::remove_if(links.begin(), links.end(), [&](const ScriptLink& l) {
                    return l.toNode == toNode && l.toPin == toPin;
                }), links.end());
}

void ScriptGraph::unlinkAll(int nodeId) {
    links.erase(std::remove_if(links.begin(), links.end(), [&](const ScriptLink& l) {
                    return l.fromNode == nodeId || l.toNode == nodeId;
                }), links.end());
}

const ScriptLink* ScriptGraph::linkInto(int n, const std::string& pin) const {
    for (const ScriptLink& l : links)
        if (l.toNode == n && l.toPin == pin) return &l;
    return nullptr;
}

std::vector<const ScriptLink*> ScriptGraph::linksOut(int n, const std::string& pin) const {
    std::vector<const ScriptLink*> out;
    for (const ScriptLink& l : links)
        if (l.fromNode == n && l.fromPin == pin) out.push_back(&l);
    return out;
}

std::vector<int> ScriptGraph::eventNodes(const std::string& eventType) const {
    std::vector<int> out;
    for (const ScriptNode& n : nodes) if (n.type == eventType) out.push_back(n.id);
    return out;
}

ScriptVariable* ScriptGraph::variable(const std::string& n) {
    for (ScriptVariable& v : variables) if (v.name == n) return &v;
    return nullptr;
}

const ScriptVariable* ScriptGraph::variable(const std::string& n) const {
    for (const ScriptVariable& v : variables) if (v.name == n) return &v;
    return nullptr;
}

std::vector<std::string> ScriptGraph::validate() const {
    std::vector<std::string> issues;
    NodeLibrary& lib = NodeLibrary::get();

    for (const ScriptNode& n : nodes)
        if (!lib.find(n.type))
            issues.push_back("node " + std::to_string(n.id) + ": unknown type '" + n.type + "'");

    for (const ScriptLink& l : links) {
        const ScriptNode* a = node(l.fromNode);
        const ScriptNode* b = node(l.toNode);
        if (!a || !b) { issues.push_back("link refers to a node that no longer exists"); continue; }
        const NodeDef* da = lib.find(a->type);
        const NodeDef* db = lib.find(b->type);
        if (!da || !db) continue;
        if (da->findPin(l.fromPin, false) < 0)
            issues.push_back(a->type + " has no output pin '" + l.fromPin + "'");
        if (db->findPin(l.toPin, true) < 0)
            issues.push_back(b->type + " has no input pin '" + l.toPin + "'");
    }

    // A cycle among pure nodes would make evaluation recurse forever, so
    // it is worth reporting before the graph is ever run.
    std::unordered_map<int, int> mark;   // 0 unseen, 1 on stack, 2 done
    std::function<bool(int)> visit = [&](int id) -> bool {
        int& m = mark[id];
        if (m == 1) return true;
        if (m == 2) return false;
        m = 1;
        const ScriptNode* n = node(id);
        const NodeDef* d = n ? lib.find(n->type) : nullptr;
        if (d && d->pure) {
            for (const PinDef* p : d->inputs()) {
                const ScriptLink* l = linkInto(id, p->name);
                if (l && visit(l->fromNode)) return true;
            }
        }
        m = 2;
        return false;
    };
    for (const ScriptNode& n : nodes) {
        const NodeDef* d = lib.find(n.type);
        if (d && d->pure && visit(n.id)) {
            issues.push_back("cycle among pure nodes reaching node " + std::to_string(n.id));
            break;
        }
    }
    return issues;
}

static Json valueToJson(const ScriptValue& v) {
    Json j = Json::object();
    j.set("t", (int)v.type);
    switch (v.type) {
        case PropType::Bool: j.set("v", v.asBool); break;
        case PropType::Int:
        case PropType::Enum: j.set("v", v.asInt); break;
        case PropType::Float: j.set("v", v.asFloat); break;
        case PropType::String: j.set("v", v.asString); break;
        case PropType::Vec3: j.set("v", Json::fromVec3(v.asVec3)); break;
        case PropType::Rotator: j.set("v", Json::fromRotator(v.asRotator)); break;
        case PropType::Color: j.set("v", Json::fromColor(v.asColor)); break;
    }
    return j;
}

static ScriptValue valueFromJson(const Json& j) {
    ScriptValue v;
    v.type = (PropType)j["t"].asInt((int)PropType::Float);
    const Json& raw = j["v"];
    switch (v.type) {
        case PropType::Bool: v.asBool = raw.asBool(); break;
        case PropType::Int:
        case PropType::Enum: v.asInt = raw.asInt(); break;
        case PropType::Float: v.asFloat = raw.asFloat(); break;
        case PropType::String: v.asString = raw.asString(); break;
        case PropType::Vec3: v.asVec3 = raw.asVec3(); break;
        case PropType::Rotator: v.asRotator = raw.asRotator(); break;
        case PropType::Color: v.asColor = raw.asColor(); break;
    }
    return v;
}

Json ScriptGraph::toJson() const {
    Json j = Json::object();
    j.set("name", name);
    j.set("format", "forge.script");

    Json ns = Json::array();
    for (const ScriptNode& n : nodes) {
        Json jn = Json::object();
        jn.set("id", n.id);
        jn.set("type", n.type);
        Json pos = Json::array();
        pos.push(n.editorPos.x);
        pos.push(n.editorPos.y);
        jn.set("pos", pos);
        if (!n.comment.empty()) jn.set("comment", n.comment);
        if (!n.literals.empty()) {
            Json lits = Json::object();
            for (const auto& kv : n.literals) lits.set(kv.first, valueToJson(kv.second));
            jn.set("literals", lits);
        }
        if (!n.config.empty()) {
            Json cfg = Json::object();
            for (const auto& kv : n.config) cfg.set(kv.first, kv.second);
            jn.set("config", cfg);
        }
        ns.push(jn);
    }
    j.set("nodes", ns);

    Json ls = Json::array();
    for (const ScriptLink& l : links) {
        Json jl = Json::array();
        jl.push(l.fromNode);
        jl.push(l.fromPin);
        jl.push(l.toNode);
        jl.push(l.toPin);
        ls.push(jl);
    }
    j.set("links", ls);

    Json vs = Json::array();
    for (const ScriptVariable& v : variables) {
        Json jv = Json::object();
        jv.set("name", v.name);
        jv.set("type", (int)v.type);
        jv.set("default", valueToJson(v.defaultValue));
        jv.set("exposed", v.exposed);
        vs.push(jv);
    }
    j.set("variables", vs);
    return j;
}

std::unique_ptr<ScriptGraph> ScriptGraph::fromJson(const Json& j) {
    auto g = std::make_unique<ScriptGraph>();
    g->name = j["name"].asString("Script");

    const Json& ns = j["nodes"];
    for (size_t i = 0; i < ns.size(); ++i) {
        const Json& jn = ns[i];
        ScriptNode n;
        n.id = jn["id"].asInt();
        n.type = jn["type"].asString();
        if (jn["pos"].size() >= 2) n.editorPos = {jn["pos"][0].asFloat(), jn["pos"][1].asFloat()};
        n.comment = jn["comment"].asString("");
        for (const auto& kv : jn["literals"].items()) n.literals[kv.first] = valueFromJson(kv.second);
        for (const auto& kv : jn["config"].items()) n.config[kv.first] = kv.second.asString();
        g->nextId_ = std::max(g->nextId_, n.id + 1);
        g->nodes.push_back(std::move(n));
    }

    const Json& ls = j["links"];
    for (size_t i = 0; i < ls.size(); ++i) {
        const Json& jl = ls[i];
        if (jl.size() < 4) continue;
        g->links.push_back({jl[0].asInt(), jl[1].asString(), jl[2].asInt(), jl[3].asString()});
    }

    const Json& vs = j["variables"];
    for (size_t i = 0; i < vs.size(); ++i) {
        ScriptVariable v;
        v.name = vs[i]["name"].asString();
        v.type = (PinType)vs[i]["type"].asInt();
        v.defaultValue = valueFromJson(vs[i]["default"]);
        v.exposed = vs[i]["exposed"].asBool();
        g->variables.push_back(std::move(v));
    }
    return g;
}

// -----------------------------------------------------------------
//  ScriptInstance
// -----------------------------------------------------------------

const std::string& ScriptExec::config(const std::string& key) const {
    auto it = node.config.find(key);
    return it == node.config.end() ? kEmpty : it->second;
}

ScriptInstance::ScriptInstance(const ScriptGraph* graph, Actor* owner)
    : graph_(graph), owner_(owner) {
    if (!graph_) return;
    for (const ScriptVariable& v : graph_->variables) variables_[v.name] = v.defaultValue;
}

World* ScriptInstance::world() const { return owner_ ? owner_->world() : nullptr; }

ScriptValue ScriptInstance::getVariable(const std::string& name) const {
    auto it = variables_.find(name);
    return it == variables_.end() ? ScriptValue{} : it->second;
}

void ScriptInstance::setVariable(const std::string& name, const ScriptValue& v) {
    variables_[name] = v;
}

void ScriptInstance::beginPlay() { fireEvent("Event.BeginPlay"); }
void ScriptInstance::endPlay() { fireEvent("Event.EndPlay"); }

void ScriptInstance::tick(float dt) {
    delta_ = dt;
    fireEvent("Event.Tick");
}

void ScriptInstance::fireEvent(const std::string& eventType, Actor* other) {
    if (!graph_) return;
    Actor* saved = eventOther_;
    eventOther_ = other;
    for (int id : graph_->eventNodes(eventType)) runFrom(id);
    eventOther_ = saved;
}

void ScriptInstance::resume(int nodeId, int execOutput) {
    if (!graph_) return;
    const ScriptNode* n = graph_->node(nodeId);
    if (!n) return;
    const NodeDef* def = NodeLibrary::get().find(n->type);
    if (!def) return;
    auto outs = def->outputs();
    int seen = 0;
    for (const PinDef* p : outs) {
        if (p->type != PinType::Exec) continue;
        if (seen++ != execOutput) continue;
        for (const ScriptLink* l : graph_->linksOut(nodeId, p->name)) runFrom(l->toNode);
        return;
    }
}

void ScriptInstance::runFrom(int nodeId) {
    if (!graph_ || !owner_ || owner_->isPendingKill()) return;

    // A graph can be wired into a loop with no exit; a depth cap turns
    // that into a logged error instead of a blown stack.
    if (execDepth_ > 512) {
        FORGE_ERROR("script '%s': execution nested too deeply, aborting (a loop with no exit?)",
                    graph_->name.c_str());
        return;
    }
    ++execDepth_;

    int current = nodeId;
    int guard = 0;
    while (current != 0) {
        if (++guard > 10000) {
            FORGE_ERROR("script '%s': ran 10000 nodes in one event, aborting", graph_->name.c_str());
            break;
        }
        const ScriptNode* n = graph_->node(current);
        if (!n) break;
        const NodeDef* def = NodeLibrary::get().find(n->type);
        if (!def) break;

        int followed = 0;
        if (def->run) {
            ScriptExec exec(*this, *n, *def);
            followed = def->run(exec);
        }
        if (followed < 0) break;   // the node ended this branch

        // Walk to the node on the chosen exec output. A node with several
        // exec outputs (Sequence) runs the rest itself and returns -1.
        int nextNode = 0;
        int seen = 0;
        for (const PinDef& p : def->pins) {
            if (p.input || p.type != PinType::Exec) continue;
            if (seen++ != followed) continue;
            const auto outs = graph_->linksOut(current, p.name);
            if (!outs.empty()) nextNode = outs[0]->toNode;
            break;
        }
        current = nextNode;
        if (owner_->isPendingKill()) break;
    }

    --execDepth_;
}

ScriptValue ScriptInstance::readPin(int nodeId, const std::string& pinName) {
    if (!graph_) return {};
    const ScriptLink* l = graph_->linkInto(nodeId, pinName);
    if (l) return evaluate(l->fromNode, l->fromPin);

    // Unconnected: the literal typed into the pin, else the pin default.
    const ScriptNode* n = graph_->node(nodeId);
    if (n) {
        auto it = n->literals.find(pinName);
        if (it != n->literals.end()) return it->second;
        const NodeDef* def = NodeLibrary::get().find(n->type);
        if (def) {
            int idx = def->findPin(pinName, true);
            if (idx >= 0) return def->pins[(size_t)idx].defaultValue;
        }
    }
    return {};
}

ScriptValue ScriptInstance::evaluate(int nodeId, const std::string& outputPin) {
    const ScriptNode* n = graph_ ? graph_->node(nodeId) : nullptr;
    if (!n) return {};
    const NodeDef* def = NodeLibrary::get().find(n->type);
    if (!def || !def->run) return {};

    if (pureDepth_ > 256) {
        FORGE_ERROR("script '%s': data evaluation nested too deeply (a cycle among pure nodes?)",
                    graph_->name.c_str());
        return {};
    }
    ++pureDepth_;
    ScriptExec exec(*this, *n, *def);
    def->run(exec);
    --pureDepth_;

    auto it = exec.outputs.find(outputPin);
    return it == exec.outputs.end() ? ScriptValue{} : it->second;
}

} // namespace forge
