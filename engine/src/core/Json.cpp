#include "forge/core/Json.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace forge {

const std::string Json::kEmpty;
const Json Json::kNull;

size_t Json::size() const {
    if (type_ == Type::Array) return arr_.size();
    if (type_ == Type::Object) return obj_.size();
    return 0;
}

bool Json::has(const std::string& key) const {
    if (type_ != Type::Object) return false;
    for (auto& kv : obj_) if (kv.first == key) return true;
    return false;
}

const Json& Json::operator[](const std::string& key) const {
    if (type_ == Type::Object)
        for (auto& kv : obj_) if (kv.first == key) return kv.second;
    return kNull;
}

Json& Json::operator[](const std::string& key) {
    if (type_ != Type::Object) { type_ = Type::Object; arr_.clear(); }
    for (auto& kv : obj_) if (kv.first == key) return kv.second;
    obj_.emplace_back(key, Json());
    return obj_.back().second;
}

const Json& Json::operator[](size_t i) const {
    if (type_ == Type::Array && i < arr_.size()) return arr_[i];
    return kNull;
}

Json& Json::operator[](size_t i) {
    if (type_ != Type::Array) { type_ = Type::Array; obj_.clear(); }
    while (arr_.size() <= i) arr_.emplace_back();
    return arr_[i];
}

void Json::push(Json v) {
    if (type_ != Type::Array) { type_ = Type::Array; obj_.clear(); }
    arr_.push_back(std::move(v));
}

void Json::set(const std::string& key, Json v) { (*this)[key] = std::move(v); }

void Json::erase(const std::string& key) {
    for (size_t i = 0; i < obj_.size(); ++i)
        if (obj_[i].first == key) { obj_.erase(obj_.begin() + (long)i); return; }
}

Json Json::fromVec3(const Vec3& v) {
    Json j = Json::array();
    j.push(v.x); j.push(v.y); j.push(v.z);
    return j;
}
Json Json::fromRotator(const Rotator& r) {
    Json j = Json::array();
    j.push(r.pitch); j.push(r.yaw); j.push(r.roll);
    return j;
}
Json Json::fromColor(const Color& c) { return Json(c.toHex()); }

Vec3 Json::asVec3(const Vec3& def) const {
    if (type_ == Type::Array && arr_.size() >= 3)
        return {arr_[0].asFloat(), arr_[1].asFloat(), arr_[2].asFloat()};
    // Also accept {"x":..,"y":..,"z":..}, which is what a human hand-editing
    // a level is most likely to type.
    if (type_ == Type::Object && has("x"))
        return {(*this)["x"].asFloat(), (*this)["y"].asFloat(), (*this)["z"].asFloat()};
    return def;
}

Rotator Json::asRotator(const Rotator& def) const {
    if (type_ == Type::Array && arr_.size() >= 3)
        return {arr_[0].asFloat(), arr_[1].asFloat(), arr_[2].asFloat()};
    if (type_ == Type::Object && has("yaw"))
        return {(*this)["pitch"].asFloat(), (*this)["yaw"].asFloat(), (*this)["roll"].asFloat()};
    return def;
}

Color Json::asColor(const Color& def) const {
    if (type_ == Type::String) return Color::fromHex(str_);
    if (type_ == Type::Array && arr_.size() >= 3)
        return {arr_[0].asFloat(), arr_[1].asFloat(), arr_[2].asFloat(),
                arr_.size() > 3 ? arr_[3].asFloat(1.0f) : 1.0f};
    return def;
}

// -----------------------------------------------------------------
//  Writing
// -----------------------------------------------------------------

static void escapeTo(std::string& out, const std::string& s) {
    out += '"';
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if ((unsigned char)c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else out += c;
        }
    }
    out += '"';
}

static void numberTo(std::string& out, double v) {
    if (!std::isfinite(v)) { out += "0"; return; }
    // Integers print without a decimal point, and floats to a precision
    // that round-trips a float exactly without printing 17 digits of noise.
    if (v == (double)(long long)v && std::fabs(v) < 1e15) {
        out += std::to_string((long long)v);
        return;
    }
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%.9g", v);
    out += buf;
}

void Json::writeTo(std::string& out, int indent, int depth) const {
    const bool pretty = indent > 0;
    auto nl = [&](int d) {
        if (!pretty) return;
        out += '\n';
        out.append((size_t)(d * indent), ' ');
    };

    switch (type_) {
        case Type::Null:   out += "null"; break;
        case Type::Bool:   out += bool_ ? "true" : "false"; break;
        case Type::Number: numberTo(out, num_); break;
        case Type::String: escapeTo(out, str_); break;
        case Type::Array: {
            if (arr_.empty()) { out += "[]"; break; }
            // An array of numbers stays on one line: a Vec3 spread over
            // five lines makes a level file unreadable.
            bool scalarOnly = true;
            for (const Json& e : arr_)
                if (e.type_ == Type::Array || e.type_ == Type::Object) { scalarOnly = false; break; }
            out += '[';
            for (size_t i = 0; i < arr_.size(); ++i) {
                if (i) out += scalarOnly && pretty ? ", " : ",";
                if (!scalarOnly) nl(depth + 1);
                arr_[i].writeTo(out, indent, depth + 1);
            }
            if (!scalarOnly) nl(depth);
            out += ']';
            break;
        }
        case Type::Object: {
            if (obj_.empty()) { out += "{}"; break; }
            out += '{';
            for (size_t i = 0; i < obj_.size(); ++i) {
                if (i) out += ',';
                nl(depth + 1);
                escapeTo(out, obj_[i].first);
                out += pretty ? ": " : ":";
                obj_[i].second.writeTo(out, indent, depth + 1);
            }
            nl(depth);
            out += '}';
            break;
        }
    }
}

std::string Json::dump(int indent) const {
    std::string out;
    out.reserve(1024);
    writeTo(out, indent, 0);
    return out;
}

// -----------------------------------------------------------------
//  Parsing — a plain recursive-descent reader.
// -----------------------------------------------------------------

namespace {

struct Parser {
    const char* p;
    const char* end;
    std::string error;

    void skipWs() {
        while (p < end) {
            char c = *p;
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { ++p; continue; }
            // Line comments are not JSON, but every hand-edited config
            // file wants them and ignoring them costs three lines.
            if (c == '/' && p + 1 < end && p[1] == '/') {
                while (p < end && *p != '\n') ++p;
                continue;
            }
            if (c == '/' && p + 1 < end && p[1] == '*') {
                p += 2;
                while (p + 1 < end && !(*p == '*' && p[1] == '/')) ++p;
                p = std::min(p + 2, end);
                continue;
            }
            break;
        }
    }

    bool fail(const std::string& msg) {
        if (error.empty()) error = msg;
        return false;
    }

    bool parseValue(Json& out);

    bool parseString(std::string& out) {
        if (p >= end || *p != '"') return fail("expected string");
        ++p;
        while (p < end && *p != '"') {
            char c = *p++;
            if (c != '\\') { out += c; continue; }
            if (p >= end) return fail("unterminated escape");
            char e = *p++;
            switch (e) {
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'r': out += '\r'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'u': {
                    if (p + 4 > end) return fail("bad \\u escape");
                    unsigned cp = 0;
                    for (int i = 0; i < 4; ++i) {
                        char h = *p++;
                        cp <<= 4;
                        if (h >= '0' && h <= '9') cp |= (unsigned)(h - '0');
                        else if (h >= 'a' && h <= 'f') cp |= (unsigned)(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') cp |= (unsigned)(h - 'A' + 10);
                        else return fail("bad hex in \\u escape");
                    }
                    // Encode as UTF-8. Surrogate pairs are left as-is:
                    // the engine's own strings are ASCII, and mangling
                    // beats rejecting a file over a name with an emoji.
                    if (cp < 0x80) out += (char)cp;
                    else if (cp < 0x800) {
                        out += (char)(0xC0 | (cp >> 6));
                        out += (char)(0x80 | (cp & 0x3F));
                    } else {
                        out += (char)(0xE0 | (cp >> 12));
                        out += (char)(0x80 | ((cp >> 6) & 0x3F));
                        out += (char)(0x80 | (cp & 0x3F));
                    }
                    break;
                }
                default: out += e;
            }
        }
        if (p >= end) return fail("unterminated string");
        ++p;
        return true;
    }
};

bool Parser::parseValue(Json& out) {
    skipWs();
    if (p >= end) return fail("unexpected end of input");
    char c = *p;

    if (c == '{') {
        ++p;
        out = Json::object();
        skipWs();
        if (p < end && *p == '}') { ++p; return true; }
        while (true) {
            skipWs();
            std::string key;
            if (!parseString(key)) return false;
            skipWs();
            if (p >= end || *p != ':') return fail("expected ':' after key '" + key + "'");
            ++p;
            Json v;
            if (!parseValue(v)) return false;
            out.set(key, std::move(v));
            skipWs();
            if (p < end && *p == ',') { ++p; continue; }
            if (p < end && *p == '}') { ++p; return true; }
            return fail("expected ',' or '}' in object");
        }
    }

    if (c == '[') {
        ++p;
        out = Json::array();
        skipWs();
        if (p < end && *p == ']') { ++p; return true; }
        while (true) {
            Json v;
            if (!parseValue(v)) return false;
            out.push(std::move(v));
            skipWs();
            if (p < end && *p == ',') { ++p; continue; }
            if (p < end && *p == ']') { ++p; return true; }
            return fail("expected ',' or ']' in array");
        }
    }

    if (c == '"') {
        std::string s;
        if (!parseString(s)) return false;
        out = Json(std::move(s));
        return true;
    }

    if (end - p >= 4 && std::string(p, p + 4) == "true") { p += 4; out = Json(true); return true; }
    if (end - p >= 5 && std::string(p, p + 5) == "false") { p += 5; out = Json(false); return true; }
    if (end - p >= 4 && std::string(p, p + 4) == "null") { p += 4; out = Json(); return true; }

    if (c == '-' || c == '+' || (c >= '0' && c <= '9')) {
        char* stop = nullptr;
        double v = std::strtod(p, &stop);
        if (stop == p) return fail("bad number");
        p = stop;
        out = Json(v);
        return true;
    }

    return fail(std::string("unexpected character '") + c + "'");
}

} // namespace

Json Json::parse(const std::string& text, std::string* error) {
    Parser parser{text.data(), text.data() + text.size(), {}};
    Json out;
    if (!parser.parseValue(out)) {
        if (error) {
            size_t off = (size_t)(parser.p - text.data());
            size_t line = 1;
            for (size_t i = 0; i < off && i < text.size(); ++i) if (text[i] == '\n') ++line;
            *error = "line " + std::to_string(line) + ": " + parser.error;
        }
        return Json();
    }
    if (error) error->clear();
    return out;
}

Json Json::loadFile(const std::string& path, std::string* error) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        if (error) *error = "cannot open " + path;
        return Json();
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return parse(ss.str(), error);
}

bool Json::saveFile(const std::string& path, int indent) const {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f << dump(indent);
    f << '\n';
    return (bool)f;
}

} // namespace forge
