// Tiny dependency-free test framework (keeps the build self-contained).
#pragma once
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace wbtest {
struct Case { std::string name; std::function<void()> fn; };
std::vector<Case>& registry();
struct Registrar { Registrar(const char* n, std::function<void()> f) { registry().push_back({n, std::move(f)}); } };
extern int g_failures;
extern std::string g_current;
inline void fail(const char* file, int line, const std::string& msg) {
  ++g_failures;
  std::printf("    FAIL %s:%d  [%s] %s\n", file, line, g_current.c_str(), msg.c_str());
}
}  // namespace wbtest

#define WB_CONCAT2(a, b) a##b
#define WB_CONCAT(a, b) WB_CONCAT2(a, b)
#define TEST(name)                                                              \
  static void WB_CONCAT(test_fn_, name)();                                      \
  static wbtest::Registrar WB_CONCAT(test_reg_, name)(#name, WB_CONCAT(test_fn_, name)); \
  static void WB_CONCAT(test_fn_, name)()

#define CHECK(cond) do { if (!(cond)) wbtest::fail(__FILE__, __LINE__, "CHECK(" #cond ")"); } while (0)
#define CHECK_MSG(cond, msg) do { if (!(cond)) wbtest::fail(__FILE__, __LINE__, std::string("CHECK(" #cond ") ") + (msg)); } while (0)
#define CHECK_NEAR(a, b, tol) do { double _a = (a), _b = (b), _t = (tol); \
  if (!(std::fabs(_a - _b) <= _t)) { char _buf[200]; std::snprintf(_buf, sizeof _buf, "CHECK_NEAR(" #a ", " #b ") got %.17g vs %.17g (tol %g)", _a, _b, _t); wbtest::fail(__FILE__, __LINE__, _buf); } } while (0)
#define CHECK_THROWS(expr) do { bool _t = false; try { expr; } catch (...) { _t = true; } if (!_t) wbtest::fail(__FILE__, __LINE__, "expected exception: " #expr); } while (0)
#define CONTAINS(hay, needle) ((hay).find(needle) != std::string::npos)
