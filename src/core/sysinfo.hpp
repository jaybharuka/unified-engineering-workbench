// Machine / build description reported with every performance measurement (Task D).
#pragma once
#include <cstdint>
#include <string>

namespace wb {

struct SysInfo {
  std::string cpu_name;
  int logical_cores = 1;
  int physical_cores = 1;
  bool physical_known = false;      // false => physical_cores is a fallback copy of logical_cores
  std::uint64_t ram_bytes = 0;
  std::string os;
  std::string compiler;
  std::string opt_flags;
  std::string build_type;
};

SysInfo querySysInfo();

}  // namespace wb
