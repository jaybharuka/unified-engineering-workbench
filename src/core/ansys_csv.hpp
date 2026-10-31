// Task B (ii): CSV of nodal temperatures exported from ANSYS.
// Required header (exactly these five columns, in this order):
//     node_id,x_m,y_m,z_m,temperature_C
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace wb {

struct NodeCloud {
  std::vector<std::int32_t> id;
  std::vector<double> x, y, z, T;
  double bbox_min[3] = {0, 0, 0};
  double bbox_max[3] = {0, 0, 0};
  double t_min = 0.0, t_max = 0.0;
  std::size_t count() const { return id.size(); }
};

struct CsvResult {
  bool ok = false;
  std::string error;                  // first problem, with line number (plus count of further ones)
  std::vector<std::string> problems;  // up to kMaxReportedProblems detailed messages
  std::vector<std::string> warnings;
  NodeCloud cloud;
};

constexpr std::size_t kMaxCsvBytes = 256u * 1024u * 1024u;
constexpr std::size_t kMaxCsvRows = 5000000;
constexpr std::size_t kMaxReportedProblems = 10;

CsvResult parseAnsysCsv(const std::string& content, std::size_t maxRows = kMaxCsvRows);
CsvResult loadAnsysCsvFile(const std::string& path, std::size_t maxBytes = kMaxCsvBytes);

}  // namespace wb
