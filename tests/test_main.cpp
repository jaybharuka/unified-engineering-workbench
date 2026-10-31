#include "test_main.hpp"

namespace wbtest {
std::vector<Case>& registry() { static std::vector<Case> r; return r; }
int g_failures = 0;
std::string g_current;
}

int main(int argc, char** argv) {
  std::string filter = argc > 1 ? argv[1] : "";
  int run = 0, failedTests = 0;
  for (auto& c : wbtest::registry()) {
    if (!filter.empty() && c.name.find(filter) == std::string::npos) continue;
    wbtest::g_current = c.name;
    int before = wbtest::g_failures;
    try { c.fn(); } catch (const std::exception& e) { wbtest::fail(__FILE__, __LINE__, std::string("uncaught exception: ") + e.what()); }
    ++run;
    bool ok = wbtest::g_failures == before;
    if (!ok) ++failedTests;
    std::printf("[%s] %s\n", ok ? " OK " : "FAIL", c.name.c_str());
  }
  std::printf("\n%d test(s) run, %d failed, %d failed check(s)\n", run, failedTests, wbtest::g_failures);
  return wbtest::g_failures == 0 && run > 0 ? 0 : 1;
}
