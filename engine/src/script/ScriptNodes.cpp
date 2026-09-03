// ============================================================
//  The built-in node library.
//
//  Grouped the way the editor's palette shows them: Events, Flow,
//  Variables, Math, Vector, Actor, World, Debug.
//
//  Every node is a NodeDef with a run function. An impure node returns
//  the index of the exec output to follow (0 for the usual single
//  output, -1 to end the branch); a pure node writes its outputs and
//  the return value is ignored.
// ============================================================
#include <algorithm>

#include "forge/core/Log.hpp"
#include "forge/physics/Physics.hpp"
#include "forge/scene/Actor.hpp"
#include "forge/scene/World.hpp"
#include "forge/script/ScriptGraph.hpp"

namespace forge {
namespace {

PinDef exec(const char* name, bool input) {
    PinDef p;
    p.name = name;
    p.type = PinType::Exec;
    p.input = input;
    return p;
}

PinDef pin(const char* name, PinType type, bool input, ScriptValue def = {}) {
    PinDef p;
    p.name = name;
    p.type = type;
    p.input = input;
    p.defaultValue = def;
    return p;
}

void define(const char* type, const char* display, const char* category, const char* tooltip,
            std::vector<PinDef> pins, std::function<int(ScriptExec&)> run,
            bool isEvent = false, bool pure = false) {
    NodeDef d;
    d.type = type;
    d.display = display;
    d.category = category;
    d.tooltip = tooltip;
    d.pins = std::move(pins);
    d.run = std::move(run);
    d.isEvent = isEvent;
    d.pure = pure;
    NodeLibrary::get().add(std::move(d));
}

// An event node is a bare entry point: one exec output and nothing else
// to do when execution starts there.
void defineEvent(const char* type, const char* display, const char* tooltip,
                 std::vector<PinDef> extraPins = {}) {
    std::vector<PinDef> pins{exec("Then", false)};
    for (PinDef& p : extraPins) pins.push_back(std::move(p));
    define(type, display, "Events", tooltip, std::move(pins), [](ScriptExec& e) {
        // Events with a payload publish it on their data output.
        if (e.def.findPin("Other Actor", false) >= 0)
            e.out("Other Actor", ScriptValue::makeActor(e.eventOther()));
        return 0;
    }, true, false);
}

} // namespace

void registerCoreNodes() {
    static bool done = false;
    if (done) return;
    done = true;

    // ---------------- Events ----------------
    defineEvent("Event.BeginPlay", "Begin Play", "Runs once when the level starts.");
    defineEvent("Event.Tick", "Tick", "Runs every frame while playing.");
    defineEvent("Event.EndPlay", "End Play", "Runs when the actor is destroyed or the level ends.");
    defineEvent("Event.BeginOverlap", "On Begin Overlap",
                "Runs when another actor starts overlapping this one.",
                {pin("Other Actor", PinType::Actor, false)});
    defineEvent("Event.EndOverlap", "On End Overlap",
                "Runs when another actor stops overlapping this one.",
                {pin("Other Actor", PinType::Actor, false)});
    defineEvent("Event.Damaged", "On Damaged", "Runs when this actor takes damage.",
                {pin("Other Actor", PinType::Actor, false)});
    defineEvent("Event.Custom", "Custom Event",
                "An entry point other scripts can call by name. Set the name in Details.");

    // ---------------- Flow ----------------
    define("Flow.Branch", "Branch", "Flow", "Takes True or False depending on a condition.",
           {exec("In", true), pin("Condition", PinType::Bool, true, ScriptValue::make(true)),
            exec("True", false), exec("False", false)},
           [](ScriptExec& e) { return e.in("Condition").boolean() ? 0 : 1; });

    define("Flow.Sequence", "Sequence", "Flow", "Runs each output in order.",
           {exec("In", true), exec("Then 0", false), exec("Then 1", false), exec("Then 2", false)},
           [](ScriptExec& e) {
               // Each branch runs to completion before the next starts,
               // which is what makes the order meaningful.
               for (int i = 0; i < 3; ++i) e.instance.resume(e.node.id, i);
               return -1;
           });

    define("Flow.DoOnce", "Do Once", "Flow", "Passes through the first time, then blocks until reset.",
           {exec("In", true), exec("Reset", true), exec("Then", false)},
           [](ScriptExec& e) {
               ScriptValue& fired = e.state();
               if (fired.type != PropType::Bool) fired = ScriptValue::make(false);
               // The Reset input arrives as its own execution, so a node
               // that has fired is unlatched by anything wired to Reset.
               if (e.instance.graph()->linkInto(e.node.id, "Reset") && fired.asBool) {
                   fired.asBool = false;
                   return -1;
               }
               if (fired.asBool) return -1;
               fired.asBool = true;
               return 0;
           });

    define("Flow.Gate", "Gate", "Flow", "Passes through only while open.",
           {exec("In", true), pin("Start Closed", PinType::Bool, true, ScriptValue::make(false)),
            pin("Open", PinType::Bool, true, ScriptValue::make(true)), exec("Then", false)},
           [](ScriptExec& e) {
               ScriptValue& init = e.state();
               if (init.type != PropType::Bool) {
                   init = ScriptValue::make(true);
                   if (e.in("Start Closed").boolean()) return -1;
               }
               return e.in("Open").boolean() ? 0 : -1;
           });

    define("Flow.FlipFlop", "Flip Flop", "Flow", "Alternates between A and B each time it runs.",
           {exec("In", true), exec("A", false), exec("B", false), pin("Is A", PinType::Bool, false)},
           [](ScriptExec& e) {
               ScriptValue& side = e.state();
               if (side.type != PropType::Bool) side = ScriptValue::make(false);
               side.asBool = !side.asBool;
               e.out("Is A", ScriptValue::make(side.asBool));
               return side.asBool ? 0 : 1;
           });

    define("Flow.ForLoop", "For Loop", "Flow", "Runs the body once per index, then Completed.",
           {exec("In", true), pin("First", PinType::Int, true, ScriptValue::make(0)),
            pin("Last", PinType::Int, true, ScriptValue::make(9)),
            exec("Body", false), pin("Index", PinType::Int, false), exec("Completed", false)},
           [](ScriptExec& e) {
               const int first = e.in("First").integer();
               const int last = e.in("Last").integer();
               // Capped: a graph asking for a million iterations would
               // hang the frame, and that is a bug worth reporting.
               const int kMax = 100000;
               if (last - first > kMax) {
                   FORGE_ERROR("script: For Loop asked for %d iterations, capped at %d", last - first, kMax);
               }
               for (int i = first; i <= last && i - first < kMax; ++i) {
                   e.out("Index", ScriptValue::make(i));
                   e.instance.setVariable("__loopIndex", ScriptValue::make(i));
                   e.instance.resume(e.node.id, 0);
               }
               e.instance.resume(e.node.id, 1);
               return -1;
           });

    define("Flow.Delay", "Delay", "Flow", "Waits, then continues. Does not block anything else.",
           {exec("In", true), pin("Seconds", PinType::Float, true, ScriptValue::make(1.0f)),
            exec("Then", false)},
           [](ScriptExec& e) {
               World* w = e.world();
               if (!w) return 0;
               const float seconds = std::max(0.0f, e.in("Seconds").number());
               ScriptInstance* inst = &e.instance;
               const int nodeId = e.node.id;
               Actor* owner = e.owner();
               const uint32_t ownerId = owner ? owner->id() : 0;
               w->setTimer(seconds, false, [w, inst, nodeId, ownerId] {
                   // The actor may be gone by the time the timer fires;
                   // resolving the id rather than holding the pointer is
                   // what makes that safe.
                   if (!w->findActor(ownerId)) return;
                   inst->resume(nodeId, 0);
               });
               return -1;
           });

    define("Flow.SetTimer", "Set Timer", "Flow", "Runs the output repeatedly on an interval.",
           {exec("In", true), pin("Interval", PinType::Float, true, ScriptValue::make(1.0f)),
            pin("Looping", PinType::Bool, true, ScriptValue::make(true)),
            exec("Then", false), exec("On Timer", false)},
           [](ScriptExec& e) {
               World* w = e.world();
               if (!w) return 0;
               ScriptInstance* inst = &e.instance;
               const int nodeId = e.node.id;
               const uint32_t ownerId = e.owner() ? e.owner()->id() : 0;
               w->setTimer(std::max(0.01f, e.in("Interval").number()), e.in("Looping").boolean(),
                           [w, inst, nodeId, ownerId] {
                               if (!w->findActor(ownerId)) return;
                               inst->resume(nodeId, 1);
                           });
               return 0;
           });

    // ---------------- Variables ----------------
    define("Var.Get", "Get Variable", "Variables", "Reads a script variable. Name it in Details.",
           {pin("Value", PinType::Wildcard, false)},
           [](ScriptExec& e) {
               e.out("Value", e.instance.getVariable(e.config("variable")));
               return 0;
           }, false, true);

    define("Var.Set", "Set Variable", "Variables", "Writes a script variable. Name it in Details.",
           {exec("In", true), pin("Value", PinType::Wildcard, true), exec("Then", false),
            pin("Result", PinType::Wildcard, false)},
           [](ScriptExec& e) {
               ScriptValue v = e.in("Value");
               e.instance.setVariable(e.config("variable"), v);
               e.out("Result", v);
               return 0;
           });

    // ---------------- Math ----------------
    auto binaryFloat = [](const char* type, const char* display, const char* tip,
                          float (*op)(float, float)) {
        define(type, display, "Math", tip,
               {pin("A", PinType::Float, true, ScriptValue::make(0.0f)),
                pin("B", PinType::Float, true, ScriptValue::make(0.0f)),
                pin("Result", PinType::Float, false)},
               [op](ScriptExec& e) {
                   e.out("Result", ScriptValue::make(op(e.in("A").number(), e.in("B").number())));
                   return 0;
               }, false, true);
    };
    binaryFloat("Math.Add", "Add", "A + B", [](float a, float b) { return a + b; });
    binaryFloat("Math.Subtract", "Subtract", "A - B", [](float a, float b) { return a - b; });
    binaryFloat("Math.Multiply", "Multiply", "A * B", [](float a, float b) { return a * b; });
    binaryFloat("Math.Divide", "Divide", "A / B, or 0 when B is zero",
                [](float a, float b) { return std::fabs(b) < 1e-8f ? 0.0f : a / b; });
    binaryFloat("Math.Min", "Min", "The smaller of A and B", [](float a, float b) { return std::min(a, b); });
    binaryFloat("Math.Max", "Max", "The larger of A and B", [](float a, float b) { return std::max(a, b); });
    binaryFloat("Math.Power", "Power", "A raised to B", [](float a, float b) { return std::pow(a, b); });

    auto compare = [](const char* type, const char* display, const char* tip, bool (*op)(float, float)) {
        define(type, display, "Math", tip,
               {pin("A", PinType::Float, true, ScriptValue::make(0.0f)),
                pin("B", PinType::Float, true, ScriptValue::make(0.0f)),
                pin("Result", PinType::Bool, false)},
               [op](ScriptExec& e) {
                   e.out("Result", ScriptValue::make(op(e.in("A").number(), e.in("B").number())));
                   return 0;
               }, false, true);
    };
    compare("Math.Greater", "Greater Than", "A > B", [](float a, float b) { return a > b; });
    compare("Math.Less", "Less Than", "A < B", [](float a, float b) { return a < b; });
    compare("Math.Equal", "Equal", "A is nearly equal to B",
            [](float a, float b) { return std::fabs(a - b) < 1e-4f; });

    define("Math.Lerp", "Lerp", "Math", "Blends from A to B by Alpha.",
           {pin("A", PinType::Float, true, ScriptValue::make(0.0f)),
            pin("B", PinType::Float, true, ScriptValue::make(1.0f)),
            pin("Alpha", PinType::Float, true, ScriptValue::make(0.5f)),
            pin("Result", PinType::Float, false)},
           [](ScriptExec& e) {
               e.out("Result", ScriptValue::make(lerpf(e.in("A").number(), e.in("B").number(),
                                                       saturate(e.in("Alpha").number()))));
               return 0;
           }, false, true);

    define("Math.Clamp", "Clamp", "Math", "Holds a value between Min and Max.",
           {pin("Value", PinType::Float, true, ScriptValue::make(0.0f)),
            pin("Min", PinType::Float, true, ScriptValue::make(0.0f)),
            pin("Max", PinType::Float, true, ScriptValue::make(1.0f)),
            pin("Result", PinType::Float, false)},
           [](ScriptExec& e) {
               e.out("Result", ScriptValue::make(clampf(e.in("Value").number(), e.in("Min").number(),
                                                        e.in("Max").number())));
               return 0;
           }, false, true);

    define("Math.Sin", "Sine", "Math", "Sine of an angle in degrees.",
           {pin("Degrees", PinType::Float, true, ScriptValue::make(0.0f)),
            pin("Result", PinType::Float, false)},
           [](ScriptExec& e) {
               e.out("Result", ScriptValue::make(std::sin(radians(e.in("Degrees").number()))));
               return 0;
           }, false, true);

    define("Math.Cos", "Cosine", "Math", "Cosine of an angle in degrees.",
           {pin("Degrees", PinType::Float, true, ScriptValue::make(0.0f)),
            pin("Result", PinType::Float, false)},
           [](ScriptExec& e) {
               e.out("Result", ScriptValue::make(std::cos(radians(e.in("Degrees").number()))));
               return 0;
           }, false, true);

    define("Math.RandomFloat", "Random Float", "Math", "A random number in a range.",
           {pin("Min", PinType::Float, true, ScriptValue::make(0.0f)),
            pin("Max", PinType::Float, true, ScriptValue::make(1.0f)),
            pin("Result", PinType::Float, false)},
           [](ScriptExec& e) {
               static Random rng(0xC0FFEEu);
               e.out("Result", ScriptValue::make(rng.range(e.in("Min").number(), e.in("Max").number())));
               return 0;
           }, false, true);

    define("Math.Not", "Not", "Math", "Inverts a boolean.",
           {pin("Value", PinType::Bool, true, ScriptValue::make(false)),
            pin("Result", PinType::Bool, false)},
           [](ScriptExec& e) {
               e.out("Result", ScriptValue::make(!e.in("Value").boolean()));
               return 0;
           }, false, true);

    define("Math.And", "And", "Math", "True when both inputs are true.",
           {pin("A", PinType::Bool, true, ScriptValue::make(true)),
            pin("B", PinType::Bool, true, ScriptValue::make(true)),
            pin("Result", PinType::Bool, false)},
           [](ScriptExec& e) {
               e.out("Result", ScriptValue::make(e.in("A").boolean() && e.in("B").boolean()));
               return 0;
           }, false, true);

    define("Math.Or", "Or", "Math", "True when either input is true.",
           {pin("A", PinType::Bool, true, ScriptValue::make(false)),
            pin("B", PinType::Bool, true, ScriptValue::make(false)),
            pin("Result", PinType::Bool, false)},
           [](ScriptExec& e) {
               e.out("Result", ScriptValue::make(e.in("A").boolean() || e.in("B").boolean()));
               return 0;
           }, false, true);

    // ---------------- Vector ----------------
    define("Vec.Make", "Make Vector", "Vector", "Builds a vector from three numbers.",
           {pin("X", PinType::Float, true, ScriptValue::make(0.0f)),
            pin("Y", PinType::Float, true, ScriptValue::make(0.0f)),
            pin("Z", PinType::Float, true, ScriptValue::make(0.0f)),
            pin("Result", PinType::Vec3, false)},
           [](ScriptExec& e) {
               e.out("Result", ScriptValue::make(Vec3{e.in("X").number(), e.in("Y").number(),
                                                      e.in("Z").number()}));
               return 0;
           }, false, true);

    define("Vec.Break", "Break Vector", "Vector", "Splits a vector into three numbers.",
           {pin("Vector", PinType::Vec3, true), pin("X", PinType::Float, false),
            pin("Y", PinType::Float, false), pin("Z", PinType::Float, false)},
           [](ScriptExec& e) {
               Vec3 v = e.in("Vector").vector();
               e.out("X", ScriptValue::make(v.x));
               e.out("Y", ScriptValue::make(v.y));
               e.out("Z", ScriptValue::make(v.z));
               return 0;
           }, false, true);

    define("Vec.Add", "Add Vectors", "Vector", "A + B",
           {pin("A", PinType::Vec3, true), pin("B", PinType::Vec3, true),
            pin("Result", PinType::Vec3, false)},
           [](ScriptExec& e) {
               e.out("Result", ScriptValue::make(e.in("A").vector() + e.in("B").vector()));
               return 0;
           }, false, true);

    define("Vec.Scale", "Scale Vector", "Vector", "Multiplies a vector by a number.",
           {pin("Vector", PinType::Vec3, true), pin("Scale", PinType::Float, true, ScriptValue::make(1.0f)),
            pin("Result", PinType::Vec3, false)},
           [](ScriptExec& e) {
               e.out("Result", ScriptValue::make(e.in("Vector").vector() * e.in("Scale").number()));
               return 0;
           }, false, true);

    define("Vec.Length", "Vector Length", "Vector", "How long a vector is.",
           {pin("Vector", PinType::Vec3, true), pin("Result", PinType::Float, false)},
           [](ScriptExec& e) {
               e.out("Result", ScriptValue::make(e.in("Vector").vector().length()));
               return 0;
           }, false, true);

    define("Vec.Normalize", "Normalize", "Vector", "Scales a vector to length one.",
           {pin("Vector", PinType::Vec3, true), pin("Result", PinType::Vec3, false)},
           [](ScriptExec& e) {
               e.out("Result", ScriptValue::make(e.in("Vector").vector().normalized()));
               return 0;
           }, false, true);

    define("Vec.Distance", "Distance", "Vector", "Distance between two points.",
           {pin("A", PinType::Vec3, true), pin("B", PinType::Vec3, true),
            pin("Result", PinType::Float, false)},
           [](ScriptExec& e) {
               e.out("Result", ScriptValue::make(distance(e.in("A").vector(), e.in("B").vector())));
               return 0;
           }, false, true);

    // ---------------- Actor ----------------
    define("Actor.Self", "Self", "Actor", "The actor this script is on.",
           {pin("Self", PinType::Actor, false)},
           [](ScriptExec& e) {
               e.out("Self", ScriptValue::makeActor(e.owner()));
               return 0;
           }, false, true);

    define("Actor.GetLocation", "Get Location", "Actor", "An actor's position in the world.",
           {pin("Target", PinType::Actor, true), pin("Location", PinType::Vec3, false)},
           [](ScriptExec& e) {
               Actor* a = e.in("Target").asActor;
               if (!a) a = e.owner();
               e.out("Location", ScriptValue::make(a ? a->transform().position : Vec3::Zero));
               return 0;
           }, false, true);

    define("Actor.SetLocation", "Set Location", "Actor", "Moves an actor to a position.",
           {exec("In", true), pin("Target", PinType::Actor, true),
            pin("Location", PinType::Vec3, true), exec("Then", false)},
           [](ScriptExec& e) {
               Actor* a = e.in("Target").asActor;
               if (!a) a = e.owner();
               if (a) a->setLocation(e.in("Location").vector());
               return 0;
           });

    define("Actor.AddOffset", "Add Offset", "Actor", "Moves an actor by a delta.",
           {exec("In", true), pin("Target", PinType::Actor, true),
            pin("Delta", PinType::Vec3, true), exec("Then", false)},
           [](ScriptExec& e) {
               Actor* a = e.in("Target").asActor;
               if (!a) a = e.owner();
               if (a) a->addOffset(e.in("Delta").vector());
               return 0;
           });

    define("Actor.GetRotation", "Get Rotation", "Actor", "An actor's rotation in degrees.",
           {pin("Target", PinType::Actor, true), pin("Rotation", PinType::Rotator, false)},
           [](ScriptExec& e) {
               Actor* a = e.in("Target").asActor;
               if (!a) a = e.owner();
               e.out("Rotation", ScriptValue::make(a ? a->rotation() : Rotator{}));
               return 0;
           }, false, true);

    define("Actor.SetRotation", "Set Rotation", "Actor", "Sets an actor's rotation.",
           {exec("In", true), pin("Target", PinType::Actor, true),
            pin("Rotation", PinType::Rotator, true), exec("Then", false)},
           [](ScriptExec& e) {
               Actor* a = e.in("Target").asActor;
               if (!a) a = e.owner();
               if (a) a->setRotation(e.in("Rotation").rotator());
               return 0;
           });

    define("Actor.AddRotation", "Add Rotation", "Actor", "Rotates an actor by a delta.",
           {exec("In", true), pin("Target", PinType::Actor, true),
            pin("Delta", PinType::Rotator, true), exec("Then", false)},
           [](ScriptExec& e) {
               Actor* a = e.in("Target").asActor;
               if (!a) a = e.owner();
               if (a) a->addRotation(e.in("Delta").rotator());
               return 0;
           });

    define("Actor.SetScale", "Set Scale", "Actor", "Sets an actor's scale.",
           {exec("In", true), pin("Target", PinType::Actor, true),
            pin("Scale", PinType::Vec3, true, ScriptValue::make(Vec3::One)), exec("Then", false)},
           [](ScriptExec& e) {
               Actor* a = e.in("Target").asActor;
               if (!a) a = e.owner();
               if (a) a->setScale(e.in("Scale").vector());
               return 0;
           });

    define("Actor.Destroy", "Destroy Actor", "Actor", "Removes an actor from the world.",
           {exec("In", true), pin("Target", PinType::Actor, true), exec("Then", false)},
           [](ScriptExec& e) {
               Actor* a = e.in("Target").asActor;
               if (!a) a = e.owner();
               if (a) a->destroy();
               return 0;
           });

    define("Actor.GetName", "Get Actor Name", "Actor", "An actor's display name.",
           {pin("Target", PinType::Actor, true), pin("Name", PinType::String, false)},
           [](ScriptExec& e) {
               Actor* a = e.in("Target").asActor;
               if (!a) a = e.owner();
               e.out("Name", ScriptValue::make(a ? a->actorName() : std::string()));
               return 0;
           }, false, true);

    define("Actor.HasTag", "Has Tag", "Actor", "Whether an actor carries a tag.",
           {pin("Target", PinType::Actor, true), pin("Tag", PinType::String, true),
            pin("Result", PinType::Bool, false)},
           [](ScriptExec& e) {
               Actor* a = e.in("Target").asActor;
               if (!a) a = e.owner();
               e.out("Result", ScriptValue::make(a && a->hasTag(e.in("Tag").text())));
               return 0;
           }, false, true);

    define("Actor.SetProperty", "Set Property", "Actor",
           "Writes any reflected property by name. Name it in Details.",
           {exec("In", true), pin("Target", PinType::Actor, true),
            pin("Value", PinType::Wildcard, true), exec("Then", false)},
           [](ScriptExec& e) {
               Actor* a = e.in("Target").asActor;
               if (!a) a = e.owner();
               if (!a) return 0;
               const std::string& prop = e.config("property");
               // Reflection means a script can reach anything the details
               // panel can, without a node per property.
               if (!prop.empty() && !a->setProperty(prop, e.in("Value").toProperty()))
                   FORGE_WARN("script: %s has no property '%s'", a->className().c_str(), prop.c_str());
               return 0;
           });

    define("Actor.GetProperty", "Get Property", "Actor",
           "Reads any reflected property by name. Name it in Details.",
           {pin("Target", PinType::Actor, true), pin("Value", PinType::Wildcard, false)},
           [](ScriptExec& e) {
               Actor* a = e.in("Target").asActor;
               if (!a) a = e.owner();
               PropertyValue v;
               if (a && a->getProperty(e.config("property"), v))
                   e.out("Value", ScriptValue::fromProperty(v));
               return 0;
           }, false, true);

    define("Actor.ApplyDamage", "Apply Damage", "Actor", "Damages an actor.",
           {exec("In", true), pin("Target", PinType::Actor, true),
            pin("Amount", PinType::Float, true, ScriptValue::make(10.0f)), exec("Then", false)},
           [](ScriptExec& e) {
               Actor* a = e.in("Target").asActor;
               if (a) a->takeDamage(e.in("Amount").number(), e.owner());
               return 0;
           });

    // ---------------- World ----------------
    define("World.Spawn", "Spawn Actor", "World",
           "Creates an actor. Choose the class in Details.",
           {exec("In", true), pin("Location", PinType::Vec3, true),
            pin("Rotation", PinType::Rotator, true), exec("Then", false),
            pin("Spawned", PinType::Actor, false)},
           [](ScriptExec& e) {
               World* w = e.world();
               if (!w) return 0;
               const ClassInfo* cls = ClassRegistry::get().find(e.config("class"));
               if (!cls || !cls->isA("Actor")) {
                   FORGE_WARN("script: Spawn Actor has no valid class set");
                   return 0;
               }
               Actor* a = w->spawn(cls, {}, e.in("Location").vector(), e.in("Rotation").rotator());
               e.out("Spawned", ScriptValue::makeActor(a));
               return 0;
           });

    define("World.GetTime", "Get Game Time", "World", "Seconds since play started.",
           {pin("Seconds", PinType::Float, false)},
           [](ScriptExec& e) {
               e.out("Seconds", ScriptValue::make(e.world() ? e.world()->timeSeconds() : 0.0f));
               return 0;
           }, false, true);

    define("World.GetDelta", "Get Delta Seconds", "World", "How long the last frame took.",
           {pin("Seconds", PinType::Float, false)},
           [](ScriptExec& e) {
               e.out("Seconds", ScriptValue::make(e.instance.deltaSeconds()));
               return 0;
           }, false, true);

    define("World.FindByName", "Find Actor By Name", "World", "Looks an actor up by its name.",
           {pin("Name", PinType::String, true), pin("Actor", PinType::Actor, false),
            pin("Found", PinType::Bool, false)},
           [](ScriptExec& e) {
               Actor* a = e.world() ? e.world()->findActorByName(e.in("Name").text()) : nullptr;
               e.out("Actor", ScriptValue::makeActor(a));
               e.out("Found", ScriptValue::make(a != nullptr));
               return 0;
           }, false, true);

    define("World.CountWithTag", "Count Actors With Tag", "World",
           "How many actors carry a tag. Useful for win conditions.",
           {pin("Tag", PinType::String, true), pin("Count", PinType::Int, false)},
           [](ScriptExec& e) {
               int n = e.world() ? (int)e.world()->actorsWithTag(e.in("Tag").text()).size() : 0;
               e.out("Count", ScriptValue::make(n));
               return 0;
           }, false, true);

    define("World.LineTrace", "Line Trace", "World", "Fires a ray and reports the first thing it hits.",
           {exec("In", true), pin("Start", PinType::Vec3, true), pin("Direction", PinType::Vec3, true),
            pin("Distance", PinType::Float, true, ScriptValue::make(100.0f)),
            exec("Hit", false), exec("Miss", false), pin("Hit Actor", PinType::Actor, false),
            pin("Hit Point", PinType::Vec3, false), pin("Hit Normal", PinType::Vec3, false)},
           [](ScriptExec& e) {
               World* w = e.world();
               if (!w) return 1;
               QueryParams q;
               if (e.owner()) q.ignore.push_back(e.owner());
               HitResult h = w->physics().raycast(e.in("Start").vector(), e.in("Direction").vector(),
                                                  e.in("Distance").number(), q);
               e.out("Hit Actor", ScriptValue::makeActor(h.actor));
               e.out("Hit Point", ScriptValue::make(h.point));
               e.out("Hit Normal", ScriptValue::make(h.normal));
               return h.hit ? 0 : 1;
           });

    define("World.SetGravity", "Set Gravity", "World", "Changes world gravity while playing.",
           {exec("In", true), pin("Gravity", PinType::Vec3, true,
                                  ScriptValue::make(Vec3{0.0f, -22.0f, 0.0f})),
            exec("Then", false)},
           [](ScriptExec& e) {
               if (World* w = e.world()) w->physics().gravity = e.in("Gravity").vector();
               return 0;
           });

    // ---------------- Debug ----------------
    define("Debug.Print", "Print", "Debug", "Writes a line to the output log.",
           {exec("In", true), pin("Text", PinType::String, true, ScriptValue::make(std::string("Hello"))),
            exec("Then", false)},
           [](ScriptExec& e) {
               Log::get().write(LogLevel::Script, "Script", e.in("Text").text());
               return 0;
           });

    define("Debug.PrintValue", "Print Value", "Debug", "Writes a label and a value to the log.",
           {exec("In", true), pin("Label", PinType::String, true, ScriptValue::make(std::string("Value"))),
            pin("Value", PinType::Wildcard, true), exec("Then", false)},
           [](ScriptExec& e) {
               Log::get().write(LogLevel::Script, "Script",
                                e.in("Label").text() + ": " + e.in("Value").text());
               return 0;
           });
}

} // namespace forge
