// ============================================================
//  Object and reflection.
//
//  Every engine class describes itself: its name, where it belongs in
//  the palette, and a typed list of its editable properties. One
//  declaration then feeds four consumers that would otherwise each
//  need a hand-written table kept in sync by hand:
//
//    - the Details panel builds widgets from the property types
//    - the level serialiser writes and reads declared properties
//    - the script graph exposes Get/Set nodes for them
//    - the Place Actors palette lists classes by category
//
//  Declaring it looks like this:
//
//      class PointLightComponent : public SceneComponent {
//          FORGE_OBJECT(PointLightComponent, SceneComponent)
//      public:
//          Color color{1, 1, 1};
//          float intensity = 10.0f;
//      };
//
//      // in the .cpp
//      FORGE_CLASS_BEGIN(PointLightComponent)
//          FORGE_DISPLAY("Point Light")
//          FORGE_CATEGORY("Lighting")
//          FORGE_PROP(color).category("Light")
//          FORGE_PROP(intensity).range(0, 100).category("Light")
//      FORGE_CLASS_END()
//
//  Properties bind through pointer-to-member, so they are type-checked
//  at compile time and never depend on offsetof against a polymorphic
//  layout.
// ============================================================
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include "forge/core/Json.hpp"
#include "forge/math/Math.hpp"

namespace forge {

class Object;
class ClassInfo;

// -----------------------------------------------------------------
//  Property values
// -----------------------------------------------------------------

enum class PropType { Bool, Int, Float, String, Vec3, Rotator, Color, Enum };

// What a String property actually refers to, which is how the details
// panel decides between a text box and an asset picker.
enum class RefKind { None, Mesh, Material, Texture, Sound, Script, Actor, Class };

struct PropertyValue {
    PropType type = PropType::Float;
    bool asBool = false;
    int asInt = 0;
    float asFloat = 0.0f;
    std::string asString;
    Vec3 asVec3;
    Rotator asRotator;
    Color asColor;

    static PropertyValue make(bool v) { PropertyValue p; p.type = PropType::Bool; p.asBool = v; return p; }
    static PropertyValue make(int v) { PropertyValue p; p.type = PropType::Int; p.asInt = v; return p; }
    static PropertyValue make(float v) { PropertyValue p; p.type = PropType::Float; p.asFloat = v; return p; }
    static PropertyValue make(const std::string& v) { PropertyValue p; p.type = PropType::String; p.asString = v; return p; }
    static PropertyValue make(const Vec3& v) { PropertyValue p; p.type = PropType::Vec3; p.asVec3 = v; return p; }
    static PropertyValue make(const Rotator& v) { PropertyValue p; p.type = PropType::Rotator; p.asRotator = v; return p; }
    static PropertyValue make(const Color& v) { PropertyValue p; p.type = PropType::Color; p.asColor = v; return p; }

    bool equals(const PropertyValue& o) const;
    Json toJson() const;
    static PropertyValue fromJson(const Json& j, PropType type);
    std::string toDisplayString() const;
};

// -----------------------------------------------------------------
//  Property
// -----------------------------------------------------------------

struct Property {
    std::string name;
    std::string display;
    std::string category = "Default";
    std::string tooltip;
    PropType type = PropType::Float;
    RefKind ref = RefKind::None;
    // For RefKind::Class, the base class a picker should filter to.
    std::string classFilter;
    float minValue = 0.0f;
    float maxValue = 0.0f;
    float step = 0.0f;
    bool hasRange = false;
    bool readOnly = false;
    bool hidden = false;           // reflected for scripts, absent from the panel
    bool transient = false;        // never written to disk
    bool multiline = false;
    std::vector<std::string> enumNames;

    std::function<PropertyValue(const Object*)> get;
    std::function<void(Object*, const PropertyValue&)> set;

    const std::string& label() const { return display.empty() ? name : display; }
};

// -----------------------------------------------------------------
//  ClassInfo
// -----------------------------------------------------------------

class ClassInfo {
public:
    const std::string& name() const { return name_; }
    const std::string& displayName() const { return display_.empty() ? name_ : display_; }
    const std::string& category() const { return category_; }
    const std::string& description() const { return description_; }
    const std::string& icon() const { return icon_; }
    const ClassInfo* super() const { return super_; }
    bool isAbstract() const { return !factory_; }
    bool placeable() const { return placeable_ && factory_; }

    // Own properties, in declaration order.
    const std::vector<Property>& properties() const { return properties_; }
    // Every property including inherited, base classes first. A subclass
    // that redeclares an inherited name overrides it in place, which is
    // how a Character narrows a default it got from Pawn.
    const std::vector<const Property*>& allProperties() const;
    const Property* findProperty(const std::string& name) const;

    bool isA(const ClassInfo* base) const;
    bool isA(const std::string& baseName) const;

    Object* construct() const { return factory_ ? factory_() : nullptr; }

    // The default value of a property *for this class*, which is not the
    // same as for the class that declared it: a subclass constructor may
    // narrow an inherited default, and serialisation has to compare
    // against the narrowed one or every instance writes it out again.
    //
    // Backed by a class default object built on first use. Lazily,
    // because constructing one runs a real constructor — an Actor's would
    // add components — and that must not happen during static init.
    const PropertyValue* defaultValue(const std::string& propName) const;
    const Object* defaultObject() const;

    // Registered subclasses, for palettes and class pickers.
    const std::vector<const ClassInfo*>& derived() const { return derived_; }
    std::vector<const ClassInfo*> allDerived(bool includeSelf, bool concreteOnly) const;

private:
    friend class ClassBuilder;
    friend class ClassRegistry;

    std::string name_, display_, category_ = "Default", description_, icon_;
    const ClassInfo* super_ = nullptr;
    std::function<Object*()> factory_;
    std::vector<Property> properties_;
    std::vector<const ClassInfo*> derived_;
    bool placeable_ = true;
    mutable std::vector<const Property*> allCache_;
    mutable bool allCacheValid_ = false;
    mutable std::unique_ptr<Object> cdo_;
    mutable bool cdoBuilt_ = false;
    mutable std::unordered_map<std::string, PropertyValue> defaults_;
};

// -----------------------------------------------------------------
//  Registry
// -----------------------------------------------------------------

class ClassRegistry {
public:
    static ClassRegistry& get();

    ClassInfo* add(std::unique_ptr<ClassInfo> info);
    const ClassInfo* find(const std::string& name) const;
    const std::vector<ClassInfo*>& all() const { return ordered_; }
    // Concrete classes descending from `baseName`, sorted for a stable
    // palette order between runs.
    std::vector<const ClassInfo*> derivedFrom(const std::string& baseName, bool concreteOnly = true) const;

private:
    std::unordered_map<std::string, std::unique_ptr<ClassInfo>> byName_;
    std::vector<ClassInfo*> ordered_;
};

// -----------------------------------------------------------------
//  Object
// -----------------------------------------------------------------

class Object {
public:
    virtual ~Object() = default;
    virtual const ClassInfo* getClass() const = 0;

    static ClassInfo* staticClass();

    const std::string& className() const { return getClass()->name(); }
    bool isA(const std::string& base) const { return getClass()->isA(base); }

    // Reflected access. `getProperty` returns false when the name is not
    // a property of this class, so callers can probe safely.
    bool getProperty(const std::string& name, PropertyValue& out) const;
    bool setProperty(const std::string& name, const PropertyValue& v);

    // Convenience typed setters used by scripts and the editor.
    bool setFloat(const std::string& name, float v);
    bool setInt(const std::string& name, int v);
    bool setBool(const std::string& name, bool v);
    bool setString(const std::string& name, const std::string& v);
    bool setVec3(const std::string& name, const Vec3& v);
    float getFloat(const std::string& name, float def = 0.0f) const;
    int getInt(const std::string& name, int def = 0) const;
    bool getBool(const std::string& name, bool def = false) const;
    std::string getString(const std::string& name, const std::string& def = "") const;
    Vec3 getVec3(const std::string& name, const Vec3& def = Vec3::Zero) const;

    // Write every property that differs from the class default. Keeping
    // defaults out of the file makes levels readable and lets a class
    // change a default later without rewriting saved levels.
    Json serializeProperties() const;
    void deserializeProperties(const Json& j);

    // Called after any reflected property changes — in the editor as well
    // as at spawn. This is the construction script: rebuild derived state
    // here and a slider drag updates the viewport live.
    virtual void onPropertyChanged(const std::string& name) { (void)name; }
};

// -----------------------------------------------------------------
//  Builder
// -----------------------------------------------------------------

class ClassBuilder {
public:
    ClassBuilder(const char* name, const ClassInfo* super, std::function<Object*()> factory);

    ClassBuilder& display(const char* v) { info_->display_ = v; return *this; }
    ClassBuilder& category(const char* v) { info_->category_ = v; return *this; }
    ClassBuilder& describe(const char* v) { info_->description_ = v; return *this; }
    ClassBuilder& icon(const char* v) { info_->icon_ = v; return *this; }
    ClassBuilder& notPlaceable() { info_->placeable_ = false; return *this; }

    // prop() adds a property; the modifiers that follow apply to it.
    template <typename C, typename T>
    ClassBuilder& prop(const char* name, T C::*member);

    ClassBuilder& tooltip(const char* v) { last().tooltip = v; return *this; }
    ClassBuilder& label(const char* v) { last().display = v; return *this; }
    ClassBuilder& cat(const char* v) { last().category = v; return *this; }
    ClassBuilder& range(float lo, float hi, float st = 0.0f) {
        Property& p = last();
        p.minValue = lo; p.maxValue = hi; p.hasRange = true;
        p.step = st > 0.0f ? st : (hi - lo) / 200.0f;
        return *this;
    }
    ClassBuilder& asset(RefKind kind) { last().ref = kind; return *this; }
    ClassBuilder& classRef(const char* base) { last().ref = RefKind::Class; last().classFilter = base; return *this; }
    ClassBuilder& options(std::initializer_list<const char*> names);
    ClassBuilder& readOnly() { last().readOnly = true; return *this; }
    ClassBuilder& hidden() { last().hidden = true; return *this; }
    ClassBuilder& transient() { last().transient = true; return *this; }
    ClassBuilder& multiline() { last().multiline = true; return *this; }

    ClassInfo* commit();

private:
    Property& last();
    std::unique_ptr<ClassInfo> owned_;
    ClassInfo* info_ = nullptr;
};

// -----------------------------------------------------------------
//  Member binding
// -----------------------------------------------------------------

namespace detail {

template <typename T> struct PropTraits;

#define FORGE_PROP_TRAIT(CType, Tag, Field)                                     \
    template <> struct PropTraits<CType> {                                      \
        static constexpr PropType kType = PropType::Tag;                        \
        static PropertyValue box(const CType& v) { return PropertyValue::make(v); } \
        static CType unbox(const PropertyValue& p) { return p.Field; }          \
    }

FORGE_PROP_TRAIT(bool, Bool, asBool);
FORGE_PROP_TRAIT(int, Int, asInt);
FORGE_PROP_TRAIT(float, Float, asFloat);
FORGE_PROP_TRAIT(std::string, String, asString);
FORGE_PROP_TRAIT(Vec3, Vec3, asVec3);
FORGE_PROP_TRAIT(Rotator, Rotator, asRotator);
FORGE_PROP_TRAIT(Color, Color, asColor);
#undef FORGE_PROP_TRAIT

// Scoped enums reflect as an Int with a name table supplied by options().
template <typename T>
struct EnumTraits {
    static constexpr PropType kType = PropType::Enum;
    static PropertyValue box(const T& v) { PropertyValue p; p.type = PropType::Enum; p.asInt = (int)v; return p; }
    static T unbox(const PropertyValue& p) { return (T)p.asInt; }
};

template <typename T>
using Traits = typename std::conditional<std::is_enum<T>::value, EnumTraits<T>, PropTraits<T>>::type;

// An abstract class gets no factory, which is exactly what makes
// ClassInfo::isAbstract() true and keeps it out of the palette. This has
// to be a template: `if constexpr` only discards the untaken branch
// inside one, and `new AbstractType()` would otherwise still be compiled.
template <typename T>
std::function<Object*()> makeFactory() {
    if constexpr (std::is_abstract<T>::value) return nullptr;
    else return [] { return static_cast<Object*>(new T()); };
}

} // namespace detail

template <typename C, typename T>
ClassBuilder& ClassBuilder::prop(const char* name, T C::*member) {
    using Tr = detail::Traits<T>;
    Property p;
    p.name = name;
    p.type = Tr::kType;
    p.get = [member](const Object* o) { return Tr::box(static_cast<const C*>(o)->*member); };
    p.set = [member](Object* o, const PropertyValue& v) { static_cast<C*>(o)->*member = Tr::unbox(v); };
    info_->properties_.push_back(std::move(p));
    return *this;
}

// -----------------------------------------------------------------
//  Declaration macros
// -----------------------------------------------------------------

#define FORGE_OBJECT(Class, SuperClass)                                        \
public:                                                                        \
    using Super = SuperClass;                                                  \
    using ThisClass = Class;                                                   \
    static ::forge::ClassInfo* staticClass();                                  \
    const ::forge::ClassInfo* getClass() const override { return Class::staticClass(); } \
private:

// The factory is omitted for abstract classes, which is what makes
// ClassInfo::isAbstract() true and keeps them out of the palette.
#define FORGE_CLASS_BEGIN(Class)                                               \
    ::forge::ClassInfo* Class::staticClass() {                                 \
        static ::forge::ClassInfo* info = [] {                                 \
            using ThisClass_ [[maybe_unused]] = Class;                         \
            ::forge::ClassBuilder b(#Class, Class::Super::staticClass(),       \
                                    ::forge::detail::makeFactory<Class>());     \
            b

#define FORGE_PROP(member) .prop(#member, &ThisClass_::member)

#define FORGE_CLASS_END()                                                      \
            ;                                                                  \
            return b.commit();                                                 \
        }();                                                                   \
        return info;                                                           \
    }

#define FORGE_DISPLAY(v)  .display(v)
#define FORGE_CATEGORY(v) .category(v)
#define FORGE_DESCRIBE(v) .describe(v)
#define FORGE_ICON(v)     .icon(v)

// Force registration at start-up so palettes and class pickers see the
// class without anything having constructed one first.
#define FORGE_REGISTER(Class)                                                  \
    namespace {                                                                \
        struct ForgeAutoReg_##Class {                                          \
            ForgeAutoReg_##Class() { Class::staticClass(); }                    \
        };                                                                     \
        static ForgeAutoReg_##Class g_forgeAutoReg_##Class;                     \
    }

} // namespace forge
