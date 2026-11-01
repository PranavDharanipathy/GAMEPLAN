// Tiny dependency-free test harness (keeps the core buildable on every platform with no fetches).
#pragma once
#include <cmath>
#include <cstdio>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace gptest {
struct Case { const char* name; std::function<void()> fn; };
inline std::vector<Case>& registry() { static std::vector<Case> r; return r; }
struct Reg { Reg(const char* n, std::function<void()> f) { registry().push_back({n, std::move(f)}); } };
struct Failure { std::string msg; };
inline std::string root() { return GP_SOURCE_ROOT; }
}  // namespace gptest

#define GP_CAT2(a, b) a##b
#define GP_CAT(a, b) GP_CAT2(a, b)
#define TEST(name) static void name(); static gptest::Reg GP_CAT(reg_, name)(#name, name); static void name()
#define CHECK(cond) do { if (!(cond)) { std::ostringstream _s; _s << __FILE__ << ":" << __LINE__ << " CHECK(" #cond ") failed"; throw gptest::Failure{_s.str()}; } } while (0)
#define CHECK_NEAR(a, b, tol) do { double _a = (a), _b = (b); if (!(std::fabs(_a - _b) <= (tol))) { std::ostringstream _s; _s << __FILE__ << ":" << __LINE__ << " CHECK_NEAR(" #a ", " #b ") " << _a << " vs " << _b << " tol " << (tol); throw gptest::Failure{_s.str()}; } } while (0)
#define CHECK_LT(a, b) do { double _a = (a), _b = (b); if (!(_a < _b)) { std::ostringstream _s; _s << __FILE__ << ":" << __LINE__ << " CHECK_LT(" #a ", " #b ") " << _a << " !< " << _b; throw gptest::Failure{_s.str()}; } } while (0)
#define CHECK_GT(a, b) CHECK_LT(b, a)
