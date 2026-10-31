#include "sysinfo.hpp"

#include <algorithm>
#include <fstream>
#include <set>
#include <sstream>
#include <thread>
#include <utility>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#ifndef WB_FLAGS_REPORT
#define WB_FLAGS_REPORT "unknown"
#endif
#ifndef WB_BUILD_TYPE
#define WB_BUILD_TYPE "unknown"
#endif

namespace wb {

static std::string compilerString() {
#if defined(__clang__)
  return std::string("clang ") + __clang_version__;
#elif defined(__GNUC__)
  return std::string("GCC ") + __VERSION__;
#elif defined(_MSC_VER)
  return "MSVC " + std::to_string(_MSC_VER);
#else
  return "unknown compiler";
#endif
}

SysInfo querySysInfo() {
  SysInfo s;
  s.compiler = compilerString();
  s.opt_flags = WB_FLAGS_REPORT;
  s.build_type = WB_BUILD_TYPE;
  unsigned hc = std::thread::hardware_concurrency();
  s.logical_cores = hc ? static_cast<int>(hc) : 1;
  s.physical_cores = s.logical_cores;

#ifdef _WIN32
  s.os = "Windows";
  MEMORYSTATUSEX ms;
  ms.dwLength = sizeof ms;
  if (GlobalMemoryStatusEx(&ms)) s.ram_bytes = ms.ullTotalPhys;

  DWORD len = 0;
  GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &len);
  if (len > 0) {
    std::string buf(len, '\0');
    auto* info = reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(&buf[0]);
    if (GetLogicalProcessorInformationEx(RelationProcessorCore, info, &len)) {
      int cores = 0;
      char* p = &buf[0];
      while (p < &buf[0] + len) {
        auto* e = reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(p);
        if (e->Relationship == RelationProcessorCore) ++cores;
        if (e->Size == 0) break;
        p += e->Size;
      }
      if (cores > 0) { s.physical_cores = cores; s.physical_known = true; }
    }
  }
  HKEY key;
  if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", 0, KEY_READ, &key) == ERROR_SUCCESS) {
    char name[256];
    DWORD sz = sizeof name, type = 0;
    if (RegQueryValueExA(key, "ProcessorNameString", nullptr, &type, reinterpret_cast<LPBYTE>(name), &sz) == ERROR_SUCCESS && type == REG_SZ) {
      name[sizeof name - 1] = '\0';
      s.cpu_name = name;
    }
    RegCloseKey(key);
  }
#else
  s.os = "Linux";
  {
    std::ifstream f("/proc/meminfo");
    std::string key;
    unsigned long long kb = 0;
    std::string unit;
    while (f >> key >> kb >> unit) {
      if (key == "MemTotal:") { s.ram_bytes = kb * 1024ULL; break; }
    }
  }
  {
    std::ifstream f("/proc/cpuinfo");
    std::string line;
    std::set<std::pair<std::string, std::string>> cores;
    std::string phys, core;
    while (std::getline(f, line)) {
      auto colon = line.find(':');
      if (colon == std::string::npos) continue;
      std::string k = line.substr(0, colon), v = line.substr(colon + 1);
      while (!k.empty() && (k.back() == ' ' || k.back() == '\t')) k.pop_back();
      if (!v.empty() && v[0] == ' ') v.erase(0, 1);
      if (k == "model name" && s.cpu_name.empty()) s.cpu_name = v;
      else if (k == "physical id") phys = v;
      else if (k == "core id") { core = v; cores.insert({phys, core}); }
    }
    if (!cores.empty()) { s.physical_cores = static_cast<int>(cores.size()); s.physical_known = true; }
  }
#endif
  if (s.cpu_name.empty()) s.cpu_name = "unknown CPU";
  s.physical_cores = std::min(s.physical_cores, s.logical_cores);
  return s;
}

}  // namespace wb
