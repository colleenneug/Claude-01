#include "forge/core/Object.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace forge {

// -----------------------------------------------------------------
//  PropertyValue
// -----------------------------------------------------------------

bool PropertyValue::equals(const PropertyValue& o) const {
    if (type != o.type) return false;
    switch (type) {
        case PropType::Bool:    return asBool == o.asBool;
        case PropType::Int:
        case PropType::Enum:    return asInt == o.asInt;
        case PropType::Float:   return nearlyEqual(asFloat, o.asFloat, 1e-6f);
        case PropType::String:  return asString == o.asString;
        case PropType::Vec3:    return (asVec3 - o.asVec3).isNearlyZero(1e-6f);
        case PropType::Rotator: return nearlyEqual(asRotator.pitch, o.asRotator.pitch, 1e-4f) &&
                                       nearlyEqual(asRotator.yaw, o.asRotator.yaw, 1e-4f) &&
                                       nearlyEqual(asRotator.roll, o.asRotator.roll, 1e-4f);
        case PropType::Color:   return nearlyEqual(asColor.r, o.asColor.r, 1e-4f) &&
                                       nearlyEqual(asColor.g, o.asColor.g, 1e-4f) &&
                                       nearlyEqual(asColor.b, o.asColor.b, 1e-4f) &&
                                       nearlyEqual(asColor.a, o.asColor.a, 1e-4f);
    }
    return false;
}

Json PropertyValue::toJson() const {
    switch (type) {
        case PropType::Bool:    return Json(asBool);
        case PropType::Int:
        case PropType::Enum:    return Json(asInt);
        case PropType::Float:   return Json(asFloat);
        case PropType::String:  return Json(asString);
        case PropType::Vec3:    return Json::fromVec3(asVec3);
        case PropType::Rotator: return Json::fromRotator(asRotator);
        case PropType::Color:   return Json::fromColor(asColor);
    }
    return Json();
}

PropertyValue PropertyValue::fromJson(const Json& j, PropType type) {
    PropertyValue p;
    p.type = type;
    switch (type) {
        case PropType::Bool:    p.asBool = j.asBool(); break;
        case PropType::Int:
        case PropType::Enum:    p.asInt = j.asInt(); break;
        case PropType::Float:   p.asFloat = j.asFloat(); break;
        case PropType::String:  p.asString = j.asString(); break;
        case PropType::Vec3:    p.asVec3 = j.asVec3(); break;
        case PropType::Rotator: p.asRotator = j.asRotator(); break;
        case PropType::Color:   p.asColor = j.asColor(); break;
    }
    return p;
}

std::string PropertyValue::toDisplayString() const {
    char buf[128];
    switch (type) {
        case PropType::Bool: return asBool ? "true" : "false";
        case PropType::Int:
        case PropType::Enum: return std::to_string(asInt);
        case PropType::Float:
            std::snprintf(buf, sizeof(buf), "%.3f", asFloat);
            return buf;
        case PropType::String: return asString;
        case PropType::Vec3:
            std::snprintf(buf, sizeof(buf), "%.2f, %.2f, %.2f", asVec3.x, asVec3.y, asVec3.z);
            return buf;
        case PropType::Rotator:
            std::snprintf(buf, sizeof(buf), "%.1f, %.1f, %.1f", asRotator.pitch, asRotator.yaw, asRotator.roll);
            return buf;
        case PropType::Color: return asColor.toHex();
    }
    return {};
}

// -----------------------------------------------------------------
//  Property
// -----------------------------------------------------------------

const std::string& Property::label() const {
    if (!display.empty()) return display;
    if (!derivedLabel_.empty()) return derivedLabel_;
    // Split camelCase and snake_case into words and capitalise each.
    std::string out;
    for (size_t i = 0; i < name.size(); ++i) {
        const char c = name[i];
        if (c == '_' || c == '-') { out += ' '; continue; }
        const bool boundary = i > 0 && std::isupper((unsigned char)c) &&
                              !std::isupper((unsigned char)name[i - 1]);
        if (boundary) out += ' ';
        out += (i == 0 || out.back() == ' ') ? (char)std::toupper((unsigned char)c) : c;
    }
    derivedLabel_ = out.empty() ? name : out;
    return derivedLabel_;
}

// -----------------------------------------------------------------
//  ClassInfo
// -----------------------------------------------------------------

const std::vector<const Property*>& ClassInfo::allProperties() const {
    if (allCacheValid_) return allCache_;
    allCache_.clear();
    // Walk to the root first so base-class properties come out on top,
    // which is the order the details panel wants to draw them in.
    std::vector<const ClassInfo*> chain;
    for (const ClassInfo* c = this; c; c = c->super_) chain.push_back(c);
    std::reverse(chain.begin(), chain.end());

    for (const ClassInfo* c : chain) {
        for (const Property& p : c->properties_) {
            auto it = std::find_if(allCache_.begin(), allCache_.end(),
                                   [&](const Property* e) { return e->name == p.name; });
            if (it != allCache_.end()) *it = &p;   // subclass override, in place
            else allCache_.push_back(&p);
        }
    }
    allCacheValid_ = true;
    return allCache_;
}

const Property* ClassInfo::findProperty(const std::string& name) const {
    for (const Property* p : allProperties())
        if (p->name == name) return p;
    return nullptr;
}

const Object* ClassInfo::defaultObject() const {
    if (!cdoBuilt_) {
        cdoBuilt_ = true;
        if (factory_) {
            cdo_.reset(factory_());
            if (cdo_) {
                for (const Property* p : allProperties())
                    if (p->get) defaults_[p->name] = p->get(cdo_.get());
            }
        }
    }
    return cdo_.get();
}

const PropertyValue* ClassInfo::defaultValue(const std::string& propName) const {
    defaultObject();
    auto it = defaults_.find(propName);
    return it == defaults_.end() ? nullptr : &it->second;
}

bool ClassInfo::isA(const ClassInfo* base) const {
    for (const ClassInfo* c = this; c; c = c->super_)
        if (c == base) return true;
    return false;
}

bool ClassInfo::isA(const std::string& baseName) const {
    for (const ClassInfo* c = this; c; c = c->super_)
        if (c->name_ == baseName) return true;
    return false;
}

std::vector<const ClassInfo*> ClassInfo::allDerived(bool includeSelf, bool concreteOnly) const {
    std::vector<const ClassInfo*> out;
    std::function<void(const ClassInfo*)> walk = [&](const ClassInfo* c) {
        if ((c != this || includeSelf) && (!concreteOnly || !c->isAbstract())) out.push_back(c);
        for (const ClassInfo* d : c->derived_) walk(d);
    };
    walk(this);
    return out;
}

// -----------------------------------------------------------------
//  ClassRegistry
// -----------------------------------------------------------------

ClassRegistry& ClassRegistry::get() {
    static ClassRegistry registry;
    return registry;
}

ClassInfo* ClassRegistry::add(std::unique_ptr<ClassInfo> info) {
    const std::string name = info->name_;
    auto existing = byName_.find(name);
    if (existing != byName_.end()) {
        // Two classes with one name would make level files ambiguous.
        std::fprintf(stderr, "[forge] duplicate class registration: %s\n", name.c_str());
        return existing->second.get();
    }
    ClassInfo* raw = info.get();
    byName_[name] = std::move(info);
    ordered_.push_back(raw);
    if (raw->super_) const_cast<ClassInfo*>(raw->super_)->derived_.push_back(raw);
    return raw;
}

const ClassInfo* ClassRegistry::find(const std::string& name) const {
    auto it = byName_.find(name);
    return it == byName_.end() ? nullptr : it->second.get();
}

std::vector<const ClassInfo*> ClassRegistry::derivedFrom(const std::string& baseName, bool concreteOnly) const {
    const ClassInfo* base = find(baseName);
    if (!base) return {};
    std::vector<const ClassInfo*> out = base->allDerived(true, concreteOnly);
    std::sort(out.begin(), out.end(), [](const ClassInfo* a, const ClassInfo* b) {
        if (a->category() != b->category()) return a->category() < b->category();
        return a->displayName() < b->displayName();
    });
    return out;
}

// -----------------------------------------------------------------
//  ClassBuilder
// -----------------------------------------------------------------

ClassBuilder::ClassBuilder(const char* name, const ClassInfo* super, std::function<Object*()> factory)
    : owned_(new ClassInfo()) {
    info_ = owned_.get();
    info_->name_ = name;
    info_->super_ = super;
    info_->factory_ = std::move(factory);
    if (super) {
        // Inherit the category so a subclass lands beside its parent in
        // the palette unless it says otherwise.
        info_->category_ = super->category();
        info_->icon_ = super->icon();
    }
}

Property& ClassBuilder::last() {
    static Property dummy;
    if (info_->properties_.empty()) {
        std::fprintf(stderr, "[forge] property modifier before any prop() on %s\n", info_->name_.c_str());
        return dummy;
    }
    return info_->properties_.back();
}

ClassBuilder& ClassBuilder::options(std::initializer_list<const char*> names) {
    Property& p = last();
    p.enumNames.assign(names.begin(), names.end());
    if (p.type == PropType::Int) p.type = PropType::Enum;
    return *this;
}

ClassInfo* ClassBuilder::commit() {
    // Defaults are not snapshotted here: the class default object they
    // come from is built on first use instead, once static
    // initialisation is over and constructing one is safe.
    return ClassRegistry::get().add(std::move(owned_));
}

// -----------------------------------------------------------------
//  Object
// -----------------------------------------------------------------

ClassInfo* Object::staticClass() {
    static ClassInfo* info = [] {
        ClassBuilder b("Object", nullptr, nullptr);
        b.category("Core").describe("Root of every reflected class.").notPlaceable();
        return b.commit();
    }();
    return info;
}

bool Object::getProperty(const std::string& name, PropertyValue& out) const {
    const Property* p = getClass()->findProperty(name);
    if (!p || !p->get) return false;
    out = p->get(this);
    return true;
}

bool Object::setProperty(const std::string& name, const PropertyValue& v) {
    const Property* p = getClass()->findProperty(name);
    if (!p || !p->set || p->readOnly) return false;
    // Coerce rather than reject: a script that hands an int to a float
    // property, or a level file written before a type changed, should
    // still land the value.
    PropertyValue coerced = v;
    if (v.type != p->type) {
        coerced.type = p->type;
        switch (p->type) {
            case PropType::Bool:
                coerced.asBool = v.type == PropType::Float ? v.asFloat != 0.0f
                               : v.type == PropType::Int || v.type == PropType::Enum ? v.asInt != 0
                               : v.type == PropType::String ? (v.asString == "true" || v.asString == "1")
                               : v.asBool;
                break;
            case PropType::Int:
            case PropType::Enum:
                coerced.asInt = v.type == PropType::Float ? (int)std::lround(v.asFloat)
                              : v.type == PropType::Bool ? (v.asBool ? 1 : 0)
                              : v.type == PropType::String ? std::atoi(v.asString.c_str())
                              : v.asInt;
                break;
            case PropType::Float:
                coerced.asFloat = v.type == PropType::Int || v.type == PropType::Enum ? (float)v.asInt
                                : v.type == PropType::Bool ? (v.asBool ? 1.0f : 0.0f)
                                : v.type == PropType::String ? (float)std::atof(v.asString.c_str())
                                : v.asFloat;
                break;
            case PropType::String:
                coerced.asString = v.toDisplayString();
                break;
            case PropType::Vec3:
                if (v.type == PropType::Rotator)
                    coerced.asVec3 = {v.asRotator.pitch, v.asRotator.yaw, v.asRotator.roll};
                else if (v.type == PropType::Float) coerced.asVec3 = Vec3(v.asFloat);
                break;
            case PropType::Rotator:
                if (v.type == PropType::Vec3) coerced.asRotator = {v.asVec3.x, v.asVec3.y, v.asVec3.z};
                break;
            case PropType::Color:
                if (v.type == PropType::String) coerced.asColor = Color::fromHex(v.asString);
                else if (v.type == PropType::Vec3) coerced.asColor = {v.asVec3.x, v.asVec3.y, v.asVec3.z, 1.0f};
                break;
        }
    }
    if (p->hasRange && p->type == PropType::Float)
        coerced.asFloat = clampf(coerced.asFloat, p->minValue, p->maxValue);
    if (p->hasRange && (p->type == PropType::Int || p->type == PropType::Enum))
        coerced.asInt = (int)clampf((float)coerced.asInt, p->minValue, p->maxValue);

    p->set(this, coerced);
    onPropertyChanged(name);
    return true;
}

bool Object::setFloat(const std::string& n, float v) { return setProperty(n, PropertyValue::make(v)); }
bool Object::setInt(const std::string& n, int v) { return setProperty(n, PropertyValue::make(v)); }
bool Object::setBool(const std::string& n, bool v) { return setProperty(n, PropertyValue::make(v)); }
bool Object::setString(const std::string& n, const std::string& v) { return setProperty(n, PropertyValue::make(v)); }
bool Object::setVec3(const std::string& n, const Vec3& v) { return setProperty(n, PropertyValue::make(v)); }

float Object::getFloat(const std::string& n, float def) const {
    PropertyValue v;
    if (!getProperty(n, v)) return def;
    if (v.type == PropType::Int || v.type == PropType::Enum) return (float)v.asInt;
    if (v.type == PropType::Bool) return v.asBool ? 1.0f : 0.0f;
    return v.asFloat;
}
int Object::getInt(const std::string& n, int def) const {
    PropertyValue v;
    if (!getProperty(n, v)) return def;
    if (v.type == PropType::Float) return (int)std::lround(v.asFloat);
    if (v.type == PropType::Bool) return v.asBool ? 1 : 0;
    return v.asInt;
}
bool Object::getBool(const std::string& n, bool def) const {
    PropertyValue v;
    if (!getProperty(n, v)) return def;
    if (v.type == PropType::Float) return v.asFloat != 0.0f;
    if (v.type == PropType::Int || v.type == PropType::Enum) return v.asInt != 0;
    return v.asBool;
}
std::string Object::getString(const std::string& n, const std::string& def) const {
    PropertyValue v;
    if (!getProperty(n, v)) return def;
    return v.type == PropType::String ? v.asString : v.toDisplayString();
}
Vec3 Object::getVec3(const std::string& n, const Vec3& def) const {
    PropertyValue v;
    if (!getProperty(n, v)) return def;
    if (v.type == PropType::Rotator) return {v.asRotator.pitch, v.asRotator.yaw, v.asRotator.roll};
    return v.asVec3;
}

Json Object::serializeProperties() const {
    Json out = Json::object();
    for (const Property* p : getClass()->allProperties()) {
        if (p->transient || !p->get) continue;
        PropertyValue v = p->get(this);
        const PropertyValue* def = getClass()->defaultValue(p->name);
        if (def && v.equals(*def)) continue;
        out.set(p->name, v.toJson());
    }
    return out;
}

void Object::deserializeProperties(const Json& j) {
    if (!j.isObject()) return;
    for (const Property* p : getClass()->allProperties()) {
        if (p->transient || !p->set) continue;
        if (!j.has(p->name)) continue;
        p->set(this, PropertyValue::fromJson(j[p->name], p->type));
    }
    // One notification after the whole batch: a construction script that
    // rebuilds a mesh should not run once per property on load.
    onPropertyChanged(std::string());
}

} // namespace forge
