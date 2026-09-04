#include "Test.hpp"
#include "forge/core/Json.hpp"
#include "forge/core/Log.hpp"
#include "forge/core/Object.hpp"

using namespace forge;

// A pair of classes that exist only to exercise reflection: one base,
// one subclass that adds properties and overrides an inherited default.
class Widget : public Object {
    FORGE_OBJECT(Widget, Object)
public:
    float size = 1.0f;
    bool enabled = true;
    std::string label = "widget";
    Vec3 offset{0, 0, 0};
    Color tint = Color::fromHex("#ffffff");
    int changeCount = 0;
    std::string lastChanged;

    void onPropertyChanged(const std::string& name) override {
        ++changeCount;
        lastChanged = name;
    }
};

FORGE_CLASS_BEGIN(Widget)
    FORGE_DISPLAY("Widget")
    FORGE_CATEGORY("Test")
    FORGE_PROP(size).range(0.0f, 10.0f).cat("Shape").tooltip("How big.")
    FORGE_PROP(enabled).cat("Shape")
    FORGE_PROP(label)
    FORGE_PROP(offset).cat("Transform")
    FORGE_PROP(tint).cat("Look")
FORGE_CLASS_END()
FORGE_REGISTER(Widget)

enum class Spin { None, Slow, Fast };

class FancyWidget : public Widget {
    FORGE_OBJECT(FancyWidget, Widget)
public:
    FancyWidget() { size = 4.0f; }   // narrows the inherited default
    Rotator facing{0, 90, 0};
    Spin spin = Spin::Slow;
    std::string meshRef;
};

FORGE_CLASS_BEGIN(FancyWidget)
    FORGE_DISPLAY("Fancy Widget")
    FORGE_PROP(facing).cat("Transform")
    FORGE_PROP(spin).options({"None", "Slow", "Fast"})
    FORGE_PROP(meshRef).asset(RefKind::Mesh)
FORGE_CLASS_END()
FORGE_REGISTER(FancyWidget)

class AbstractThing : public Object {
    FORGE_OBJECT(AbstractThing, Object)
public:
    virtual void mustOverride() = 0;
    float weight = 2.0f;
};
FORGE_CLASS_BEGIN(AbstractThing)
    FORGE_PROP(weight)
FORGE_CLASS_END()
FORGE_REGISTER(AbstractThing)

TEST(class_registration) {
    const ClassInfo* w = ClassRegistry::get().find("Widget");
    CHECK(w != nullptr);
    CHECK(w->displayName() == "Widget");
    CHECK(w->category() == "Test");
    CHECK(!w->isAbstract());

    const ClassInfo* f = ClassRegistry::get().find("FancyWidget");
    CHECK(f != nullptr);
    CHECK(f->super() == w);
    CHECK(f->isA("Widget"));
    CHECK(f->isA("Object"));
    CHECK(!w->isA("FancyWidget"));
    // A subclass inherits its parent's category unless it sets its own.
    CHECK(f->category() == "Test");
}

TEST(abstract_classes_have_no_factory) {
    const ClassInfo* a = ClassRegistry::get().find("AbstractThing");
    CHECK(a != nullptr);
    CHECK(a->isAbstract());
    CHECK(a->construct() == nullptr);
    // ...but its properties are still reflected for any subclass.
    CHECK(a->findProperty("weight") != nullptr);
}

TEST(property_inheritance) {
    const ClassInfo* f = ClassRegistry::get().find("FancyWidget");
    // Own properties only.
    CHECK(f->properties().size() == 3);
    // Inherited ones come first, base class before subclass.
    const auto& all = f->allProperties();
    CHECK(all.size() == 8);
    CHECK(all[0]->name == "size");
    CHECK(all[5]->name == "facing");
    CHECK(f->findProperty("size") != nullptr);
    CHECK(f->findProperty("nosuch") == nullptr);

    const Property* size = f->findProperty("size");
    CHECK(size->hasRange);
    (void)size;
    CHECK_NEAR(size->maxValue, 10.0f, 1e-5);
    CHECK(size->category == "Shape");
    CHECK(size->tooltip == "How big.");
    // Defaults come from a class default object, so the subclass
    // constructor's narrower value is what serialisation compares to --
    // not the value the declaring class happened to start with.
    CHECK_NEAR(f->defaultValue("size")->asFloat, 4.0f, 1e-5);
    CHECK_NEAR(ClassRegistry::get().find("Widget")->defaultValue("size")->asFloat, 1.0f, 1e-5);
    CHECK(f->defaultObject() != nullptr);
    CHECK(ClassRegistry::get().find("AbstractThing")->defaultObject() == nullptr);
}

TEST(property_metadata) {
    const ClassInfo* f = ClassRegistry::get().find("FancyWidget");
    const Property* spin = f->findProperty("spin");
    CHECK(spin->type == PropType::Enum);
    CHECK(spin->enumNames.size() == 3);
    CHECK(spin->enumNames[2] == "Fast");
    CHECK(f->findProperty("meshRef")->ref == RefKind::Mesh);
    CHECK(f->findProperty("label")->ref == RefKind::None);
}

TEST(reflected_get_set) {
    std::unique_ptr<Object> o(ClassRegistry::get().find("FancyWidget")->construct());
    CHECK(o != nullptr);
    FancyWidget* fw = static_cast<FancyWidget*>(o.get());

    CHECK(o->setFloat("size", 7.5f));
    CHECK_NEAR(fw->size, 7.5f, 1e-5);
    CHECK_NEAR(o->getFloat("size"), 7.5f, 1e-5);
    // A property that does not exist fails rather than silently passing.
    CHECK(!o->setFloat("nosuch", 1.0f));

    CHECK(o->setString("label", "hello"));
    CHECK(fw->label == "hello");
    CHECK(o->setVec3("offset", {1, 2, 3}));
    CHECK_VEC(fw->offset, (Vec3{1, 2, 3}), 1e-5);
    CHECK(o->setBool("enabled", false));
    CHECK(!fw->enabled);

    PropertyValue v;
    CHECK(o->getProperty("facing", v));
    CHECK(v.type == PropType::Rotator);
    CHECK_NEAR(v.asRotator.yaw, 90.0f, 1e-4);

    // Setting a reflected property fires the construction callback.
    int before = fw->changeCount;
    o->setFloat("size", 3.0f);
    CHECK(fw->changeCount == before + 1);
    CHECK(fw->lastChanged == "size");
}

TEST(property_coercion_and_clamping) {
    std::unique_ptr<Object> o(ClassRegistry::get().find("Widget")->construct());
    // A range clamps rather than rejecting, so a script cannot push a
    // property outside what the class says is valid.
    o->setFloat("size", 100.0f);
    CHECK_NEAR(o->getFloat("size"), 10.0f, 1e-5);
    o->setFloat("size", -5.0f);
    CHECK_NEAR(o->getFloat("size"), 0.0f, 1e-5);

    // Cross-type assignment coerces instead of failing.
    CHECK(o->setProperty("size", PropertyValue::make(3)));
    CHECK_NEAR(o->getFloat("size"), 3.0f, 1e-5);
    CHECK(o->setProperty("enabled", PropertyValue::make(1.0f)));
    CHECK(o->getBool("enabled"));
    CHECK(o->setProperty("tint", PropertyValue::make(std::string("#ff0000"))));
    CHECK_NEAR(static_cast<Widget*>(o.get())->tint.r, 1.0f, 1e-4);
    CHECK(o->setProperty("label", PropertyValue::make(2.5f)));
    CHECK(o->getString("label") == "2.500");
}

TEST(serialization_writes_only_changes) {
    std::unique_ptr<Object> o(ClassRegistry::get().find("FancyWidget")->construct());
    // Untouched: nothing differs from the class default, so nothing is
    // written. This is what keeps level files small and readable.
    CHECK(o->serializeProperties().size() == 0);

    o->setFloat("size", 9.0f);
    o->setString("label", "custom");
    Json j = o->serializeProperties();
    CHECK(j.size() == 2);
    CHECK(j.has("size"));
    CHECK(j.has("label"));
    CHECK(!j.has("enabled"));

    std::unique_ptr<Object> copy(ClassRegistry::get().find("FancyWidget")->construct());
    copy->deserializeProperties(j);
    CHECK_NEAR(copy->getFloat("size"), 9.0f, 1e-5);
    CHECK(copy->getString("label") == "custom");
    // Anything absent from the file keeps the class default.
    CHECK(copy->getBool("enabled"));
}

TEST(serialization_round_trip_all_types) {
    std::unique_ptr<Object> o(ClassRegistry::get().find("FancyWidget")->construct());
    o->setFloat("size", 2.25f);
    o->setBool("enabled", false);
    o->setString("label", "a \"quoted\"\nname");
    o->setVec3("offset", {1.5f, -2.5f, 3.75f});
    o->setProperty("facing", PropertyValue::make(Rotator{10, -20, 30}));
    o->setProperty("tint", PropertyValue::make(Color::fromHex("#3366cc")));
    o->setProperty("spin", PropertyValue::make(2));
    o->setString("meshRef", "Cube");

    // Through text, not just through the Json object, so escaping counts.
    std::string text = o->serializeProperties().dump();
    std::string err;
    Json parsed = Json::parse(text, &err);
    CHECK(err.empty());

    std::unique_ptr<Object> copy(ClassRegistry::get().find("FancyWidget")->construct());
    copy->deserializeProperties(parsed);
    CHECK_NEAR(copy->getFloat("size"), 2.25f, 1e-5);
    CHECK(!copy->getBool("enabled"));
    CHECK(copy->getString("label") == "a \"quoted\"\nname");
    CHECK_VEC(copy->getVec3("offset"), (Vec3{1.5f, -2.5f, 3.75f}), 1e-5);
    PropertyValue v;
    copy->getProperty("facing", v);
    CHECK_NEAR(v.asRotator.roll, 30.0f, 1e-3);
    copy->getProperty("tint", v);
    CHECK(v.asColor.toHex() == "#3366cc");
    CHECK(copy->getInt("spin") == 2);
    CHECK(copy->getString("meshRef") == "Cube");
}

TEST(derived_class_listing) {
    auto list = ClassRegistry::get().derivedFrom("Widget", true);
    CHECK(list.size() == 2);
    bool sawFancy = false;
    for (const ClassInfo* c : list) if (c->name() == "FancyWidget") sawFancy = true;
    CHECK(sawFancy);
    // An abstract class is excluded when concreteOnly is asked for.
    auto objects = ClassRegistry::get().derivedFrom("Object", true);
    for (const ClassInfo* c : objects) CHECK(!c->isAbstract());
    CHECK(ClassRegistry::get().derivedFrom("NoSuchClass").empty());
}

TEST(json_parse_and_write) {
    std::string err;
    Json j = Json::parse(R"({
        "name": "level",
        "count": 3,
        "scale": 1.5,
        "on": true,
        "nothing": null,
        "vec": [1, 2, 3],
        "nested": { "deep": [ {"a": 1}, {"a": 2} ] }
    })", &err);
    CHECK(err.empty());
    CHECK(j.isObject());
    CHECK(j["name"].asString() == "level");
    CHECK(j["count"].asInt() == 3);
    CHECK_NEAR(j["scale"].asFloat(), 1.5f, 1e-6);
    CHECK(j["on"].asBool());
    CHECK(j["nothing"].isNull());
    CHECK_VEC(j["vec"].asVec3(), (Vec3{1, 2, 3}), 1e-6);
    CHECK(j["nested"]["deep"].size() == 2);
    CHECK(j["nested"]["deep"][1]["a"].asInt() == 2);

    // A missing key reads as null and yields the fallback rather than
    // crashing — loading an older level must degrade, not abort.
    CHECK(j["absent"].isNull());
    CHECK(j["absent"]["deeper"].asInt(42) == 42);
    CHECK(j["count"].asString("fallback") == "fallback");

    // Round trip through text.
    Json back = Json::parse(j.dump(), &err);
    CHECK(err.empty());
    CHECK(back["nested"]["deep"][0]["a"].asInt() == 1);
    CHECK(Json::parse(j.dump(0), &err)["name"].asString() == "level");
}

TEST(json_errors_and_extras) {
    std::string err;
    Json::parse("{ \"a\": }", &err);
    CHECK(!err.empty());
    err.clear();
    Json::parse("[1, 2", &err);
    CHECK(!err.empty());
    err.clear();
    // Comments are not JSON but every hand-edited config wants them.
    Json j = Json::parse("{ // a comment\n \"a\": 1 /* and another */ }", &err);
    CHECK(err.empty());
    CHECK(j["a"].asInt() == 1);
    // Escapes survive both directions.
    Json s = Json::parse(R"({"t":"tab\there é"})", &err);
    CHECK(err.empty());
    CHECK(s["t"].asString().find('\t') != std::string::npos);
    CHECK(Json::parse(s.dump(), &err)["t"].asString() == s["t"].asString());
}

TEST(logging) {
    Log::get().setEchoToConsole(false);
    Log::get().clear();
    FORGE_LOG("hello %d", 42);
    CHECK(Log::get().entries().size() == 1);
    CHECK(Log::get().entries()[0].message == "hello 42");
    // A repeated line collapses instead of flooding the panel.
    FORGE_LOG("hello %d", 42);
    CHECK(Log::get().entries().size() == 1);
    CHECK(Log::get().entries()[0].repeats == 2);
    FORGE_WARN("different");
    CHECK(Log::get().entries().size() == 2);
    CHECK(Log::get().entries()[1].level == LogLevel::Warning);
    Log::get().clear();
    Log::get().setEchoToConsole(true);
}

int main() { return forge_test::runAll("core"); }
