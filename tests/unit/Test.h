#pragma once

// Tiny self-registering test harness.

#include <cstdio>
#include <functional>
#include <string>
#include <type_traits>
#include <vector>

namespace kmtest {

struct Case {
    const char* name;
    std::function<void()> fn;
};

std::vector<Case>& Registry();
void Fail(const char* file, int line, const std::string& msg);

struct Registrar {
    Registrar(const char* name, std::function<void()> fn) { Registry().push_back({name, std::move(fn)}); }
};

}  // namespace kmtest

#define KM_CAT2(a, b) a##b
#define KM_CAT(a, b) KM_CAT2(a, b)
#define TEST(name)                                                                  \
    static void name();                                                             \
    static ::kmtest::Registrar KM_CAT(reg_, name)(#name, name);                     \
    static void name()

#define CHECK(cond)                                                                 \
    do {                                                                            \
        if (!(cond)) ::kmtest::Fail(__FILE__, __LINE__, "CHECK(" #cond ")");        \
    } while (0)

#define CHECK_EQ(a, b)                                                              \
    do {                                                                            \
        auto _va = (a);                                                             \
        auto _vb = (b);                                                             \
        if (!(_va == _vb))                                                          \
            ::kmtest::Fail(__FILE__, __LINE__,                                      \
                           std::string("CHECK_EQ(" #a ", " #b ")\n      got:      ") + \
                               ::kmtest::Show(_va) + "\n      expected: " + ::kmtest::Show(_vb)); \
    } while (0)

namespace kmtest {
inline std::string Show(const std::string& s) { return "\"" + s + "\""; }
inline std::string Show(const char* s) { return "\"" + std::string(s) + "\""; }
inline std::string Show(bool b) { return b ? "true" : "false"; }
template <typename T>
std::string Show(const T& v) {
    if constexpr (std::is_arithmetic_v<T>) return std::to_string(v);
    else if constexpr (std::is_enum_v<T>) return std::to_string(static_cast<long long>(v));
    else return "<value>";
}
}  // namespace kmtest
