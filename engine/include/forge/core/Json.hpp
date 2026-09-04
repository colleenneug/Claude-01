// ============================================================
//  JSON — the engine's one serialisation format.
//
//  Levels, assets, script graphs and project settings are all JSON, so
//  every file the engine writes is diffable and hand-editable. This is
//  a complete parser and writer rather than a dependency, which keeps
//  the engine to a single build with no package manager.
// ============================================================
#pragma once

#include <initializer_list>
#include <map>
#include <string>
#include <vector>

#include "forge/math/Math.hpp"

namespace forge {

class Json {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Json() = default;
    Json(std::nullptr_t) {}
    Json(bool v) : type_(Type::Bool), bool_(v) {}
    Json(int v) : type_(Type::Number), num_((double)v) {}
    Json(float v) : type_(Type::Number), num_((double)v) {}
    Json(double v) : type_(Type::Number), num_(v) {}
    Json(const char* v) : type_(Type::String), str_(v) {}
    Json(std::string v) : type_(Type::String), str_(std::move(v)) {}

    static Json array() { Json j; j.type_ = Type::Array; return j; }
    static Json object() { Json j; j.type_ = Type::Object; return j; }

    Type type() const { return type_; }
    bool isNull() const { return type_ == Type::Null; }
    bool isBool() const { return type_ == Type::Bool; }
    bool isNumber() const { return type_ == Type::Number; }
    bool isString() const { return type_ == Type::String; }
    bool isArray() const { return type_ == Type::Array; }
    bool isObject() const { return type_ == Type::Object; }

    // Readers never throw: a missing or wrong-typed field yields the
    // fallback. Loading a level written by an older build should degrade,
    // not abort.
    bool asBool(bool def = false) const { return type_ == Type::Bool ? bool_ : def; }
    double asNumber(double def = 0.0) const { return type_ == Type::Number ? num_ : def; }
    float asFloat(float def = 0.0f) const { return type_ == Type::Number ? (float)num_ : def; }
    int asInt(int def = 0) const { return type_ == Type::Number ? (int)num_ : def; }
    const std::string& asString(const std::string& def = kEmpty) const { return type_ == Type::String ? str_ : def; }

    size_t size() const;
    bool has(const std::string& key) const;
    const Json& operator[](const std::string& key) const;
    Json& operator[](const std::string& key);
    const Json& operator[](size_t i) const;
    Json& operator[](size_t i);
    const Json& at(const std::string& key) const { return (*this)[key]; }

    void push(Json v);
    void set(const std::string& key, Json v);
    void erase(const std::string& key);
    // Insertion-ordered so a written file keeps the field order the code
    // used, which makes diffs between two saves readable.
    const std::vector<std::pair<std::string, Json>>& items() const { return obj_; }
    const std::vector<Json>& elements() const { return arr_; }

    // Convenience for the engine's own types.
    static Json fromVec3(const Vec3& v);
    static Json fromRotator(const Rotator& r);
    static Json fromColor(const Color& c);
    Vec3 asVec3(const Vec3& def = Vec3::Zero) const;
    Rotator asRotator(const Rotator& def = Rotator{}) const;
    Color asColor(const Color& def = Color{}) const;

    std::string dump(int indent = 2) const;
    static Json parse(const std::string& text, std::string* error = nullptr);
    static Json loadFile(const std::string& path, std::string* error = nullptr);
    bool saveFile(const std::string& path, int indent = 2) const;

private:
    void writeTo(std::string& out, int indent, int depth) const;

    Type type_ = Type::Null;
    bool bool_ = false;
    double num_ = 0.0;
    std::string str_;
    std::vector<Json> arr_;
    std::vector<std::pair<std::string, Json>> obj_;

    static const std::string kEmpty;
    static const Json kNull;
};

} // namespace forge
