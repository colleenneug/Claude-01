// A test harness small enough to not be a dependency: register cases with
// a macro, run them all, report failures with file and line.
#pragma once

#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace forge_test {

struct Case { std::string name; std::function<void()> fn; };
inline std::vector<Case>& cases() { static std::vector<Case> c; return c; }
inline int& failures() { static int f = 0; return f; }
inline int& checks() { static int c = 0; return c; }
inline std::string& current() { static std::string s; return s; }

struct Registrar {
    Registrar(const char* name, std::function<void()> fn) { cases().push_back({name, std::move(fn)}); }
};

inline void fail(const char* file, int line, const std::string& msg) {
    ++failures();
    std::printf("  FAIL %s:%d  %s\n", file, line, msg.c_str());
}

inline bool nearly(double a, double b, double eps) { return std::fabs(a - b) <= eps; }

inline int runAll(const char* suite) {
    std::printf("== %s ==\n", suite);
    for (auto& c : cases()) {
        current() = c.name;
        int before = failures();
        c.fn();
        std::printf("%s %s\n", failures() == before ? "  ok  " : "  ERR ", c.name.c_str());
    }
    std::printf("-- %d checks, %d failure(s)\n", checks(), failures());
    return failures() == 0 ? 0 : 1;
}

} // namespace forge_test

#define TEST(name)                                                            \
    static void name();                                                       \
    static ::forge_test::Registrar reg_##name(#name, name);                   \
    static void name()

#define CHECK(expr)                                                           \
    do {                                                                      \
        ++::forge_test::checks();                                             \
        if (!(expr)) ::forge_test::fail(__FILE__, __LINE__, "expected " #expr); \
    } while (0)

#define CHECK_NEAR(a, b, eps)                                                 \
    do {                                                                      \
        ++::forge_test::checks();                                             \
        double va = (double)(a), vb = (double)(b);                            \
        if (!::forge_test::nearly(va, vb, eps))                               \
            ::forge_test::fail(__FILE__, __LINE__,                            \
                std::string(#a " == " #b " (") + std::to_string(va) + " vs " + std::to_string(vb) + ")"); \
    } while (0)

#define CHECK_VEC(a, b, eps)                                                  \
    do {                                                                      \
        ++::forge_test::checks();                                             \
        auto _a = (a); auto _b = (b);                                         \
        if (!::forge_test::nearly(_a.x, _b.x, eps) ||                         \
            !::forge_test::nearly(_a.y, _b.y, eps) ||                         \
            !::forge_test::nearly(_a.z, _b.z, eps))                           \
            ::forge_test::fail(__FILE__, __LINE__,                            \
                std::string(#a " == " #b " (") + std::to_string(_a.x) + "," + std::to_string(_a.y) + "," + \
                std::to_string(_a.z) + " vs " + std::to_string(_b.x) + "," + std::to_string(_b.y) + "," + \
                std::to_string(_b.z) + ")");                                  \
    } while (0)
