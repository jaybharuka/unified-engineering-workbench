#include "ansys_csv.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <unordered_set>

#include "util.hpp"

namespace wb {
namespace {

const char* kExpectedHeader[5] = {"node_id", "x_m", "y_m", "z_m", "temperature_C"};
const char* kHeaderText = "node_id,x_m,y_m,z_m,temperature_C";

std::string trim(const std::string& s) {
  std::size_t a = 0, b = s.size();
  while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
  while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
  std::string r = s.substr(a, b - a);
  if (r.size() >= 2 && r.front() == '"' && r.back() == '"') r = r.substr(1, r.size() - 2);
  return r;
}

std::string lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

std::vector<std::string> splitComma(const std::string& line) {
  std::vector<std::string> f;
  std::size_t start = 0;
  while (true) {
    std::size_t p = line.find(',', start);
    if (p == std::string::npos) { f.push_back(trim(line.substr(start))); break; }
    f.push_back(trim(line.substr(start, p - start)));
    start = p + 1;
  }
  return f;
}

bool parseFinite(const std::string& s, double& out, std::string& why) {
  if (s.empty()) { why = "value is empty"; return false; }
  for (char c : s)
    if (!(std::isdigit(static_cast<unsigned char>(c)) || c == '+' || c == '-' || c == '.' || c == 'e' || c == 'E')) {
      why = "'" + s + "' is not a finite decimal number (nan, inf, text and hex are rejected)";
      return false;
    }
  char* endp = nullptr;
  out = std::strtod(s.c_str(), &endp);
  if (endp == s.c_str() || *endp != '\0') { why = "'" + s + "' is not a valid number"; return false; }
  if (!std::isfinite(out)) { why = "'" + s + "' is not finite"; return false; }
  return true;
}

CsvResult failOnly(const std::string& msg) {
  CsvResult r;
  r.ok = false;
  r.error = msg;
  r.problems.push_back(msg);
  return r;
}

}  // namespace

CsvResult parseAnsysCsv(const std::string& content, std::size_t maxRows) {
  if (content.empty()) return failOnly("The file is empty (0 bytes). Export nodal temperatures with the header '" + std::string(kHeaderText) + "'.");

  CsvResult res;
  std::size_t pos = 0;
  std::size_t lineNo = 0;
  bool haveHeader = false;
  std::size_t dataRows = 0, blankLines = 0, extraProblems = 0;
  std::unordered_set<std::int32_t> seen;
  NodeCloud& c = res.cloud;

  auto addProblem = [&](const std::string& m) {
    if (res.problems.size() < kMaxReportedProblems) res.problems.push_back(m);
    else ++extraProblems;
  };

  // Skip UTF-8 BOM.
  if (content.size() >= 3 && static_cast<unsigned char>(content[0]) == 0xEF && static_cast<unsigned char>(content[1]) == 0xBB &&
      static_cast<unsigned char>(content[2]) == 0xBF)
    pos = 3;

  while (pos <= content.size() && pos < content.size()) {
    std::size_t nl = content.find('\n', pos);
    std::string line = content.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
    pos = (nl == std::string::npos) ? content.size() : nl + 1;
    ++lineNo;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (trim(line).empty()) { ++blankLines; continue; }

    std::vector<std::string> f = splitComma(line);

    if (!haveHeader) {
      if (f.size() == 1 && line.find(';') != std::string::npos)
        return failOnly("Line " + std::to_string(lineNo) + ": the file uses ';' separators. Export with commas: " + kHeaderText);
      if (f.size() != 5)
        return failOnly("Line " + std::to_string(lineNo) + ": the header has " + std::to_string(f.size()) + " column(s) but exactly 5 are required: " + kHeaderText);
      for (int i = 0; i < 5; ++i)
        if (lower(f[i]) != lower(kExpectedHeader[i]))
          return failOnly("Line " + std::to_string(lineNo) + ": header column " + std::to_string(i + 1) + " is '" + f[i] +
                          "' but must be '" + kExpectedHeader[i] + "'. Required header: " + kHeaderText);
      haveHeader = true;
      continue;
    }

    if (dataRows >= maxRows)
      return failOnly("More than " + std::to_string(maxRows) + " data rows. The limit protects memory; split the export.");
    ++dataRows;

    if (f.size() != 5) {
      addProblem("Line " + std::to_string(lineNo) + ": expected 5 comma-separated values but found " + std::to_string(f.size()) + ".");
      continue;
    }
    std::string why;
    double vals[4];
    bool rowOk = true;

    // node_id: positive integer that fits in 32 bits
    {
      const std::string& s = f[0];
      bool digits = !s.empty() && std::all_of(s.begin(), s.end(), [](unsigned char ch) { return std::isdigit(ch) != 0; });
      if (!digits || s.size() > 10) {
        addProblem("Line " + std::to_string(lineNo) + ": node_id '" + s + "' must be a positive integer.");
        rowOk = false;
      } else {
        long long id = std::atoll(s.c_str());
        if (id < 1 || id > 2147483647LL) {
          addProblem("Line " + std::to_string(lineNo) + ": node_id " + s + " is outside 1..2147483647.");
          rowOk = false;
        } else if (!seen.insert(static_cast<std::int32_t>(id)).second) {
          addProblem("Line " + std::to_string(lineNo) + ": duplicate node_id " + s + ".");
          rowOk = false;
        } else {
          vals[0] = static_cast<double>(id);
        }
      }
    }
    static const char* names[4] = {"", "x_m", "y_m", "z_m"};
    for (int i = 1; i <= 3 && rowOk; ++i) {
      if (!parseFinite(f[i], vals[i], why)) {
        addProblem("Line " + std::to_string(lineNo) + ", column " + names[i] + ": " + why + ".");
        rowOk = false;
      }
    }
    double temp = 0.0;
    if (rowOk) {
      if (!parseFinite(f[4], temp, why)) {
        addProblem("Line " + std::to_string(lineNo) + ", column temperature_C: " + why + ".");
        rowOk = false;
      } else if (temp < -273.15) {
        addProblem("Line " + std::to_string(lineNo) + ": temperature_C " + f[4] + " is below absolute zero (-273.15). Check units (the column must be deg C).");
        rowOk = false;
      }
    }
    if (rowOk) {
      c.id.push_back(static_cast<std::int32_t>(vals[0]));
      c.x.push_back(vals[1]);
      c.y.push_back(vals[2]);
      c.z.push_back(vals[3]);
      c.T.push_back(temp);
    }
  }

  if (!haveHeader)
    return failOnly("The file contains no header row. The first non-empty line must be: " + std::string(kHeaderText));
  if (dataRows == 0)
    return failOnly("The file has a valid header but no data rows, so there are no nodes to display.");
  if (!res.problems.empty()) {
    res.ok = false;
    res.error = res.problems.front();
    if (res.problems.size() > 1 || extraProblems > 0)
      res.error += " (+" + std::to_string(res.problems.size() - 1 + extraProblems) + " more problem(s); fix the file and retry)";
    res.cloud = NodeCloud();
    return res;
  }

  if (blankLines > 0) res.warnings.push_back(std::to_string(blankLines) + " blank line(s) ignored.");

  const std::size_t n = c.count();
  for (int i = 0; i < 3; ++i) { c.bbox_min[i] = 1e300; c.bbox_max[i] = -1e300; }
  c.t_min = 1e300;
  c.t_max = -1e300;
  for (std::size_t i = 0; i < n; ++i) {
    const double p[3] = {c.x[i], c.y[i], c.z[i]};
    for (int k = 0; k < 3; ++k) {
      c.bbox_min[k] = std::min(c.bbox_min[k], p[k]);
      c.bbox_max[k] = std::max(c.bbox_max[k], p[k]);
    }
    c.t_min = std::min(c.t_min, c.T[i]);
    c.t_max = std::max(c.t_max, c.T[i]);
  }
  if (c.t_max == c.t_min) res.warnings.push_back("All nodes have the same temperature; the colour scale is a single value.");
  res.ok = true;
  return res;
}

CsvResult loadAnsysCsvFile(const std::string& path, std::size_t maxBytes) {
  std::string content, err;
  if (!readTextFile(path, maxBytes, content, err)) return failOnly(err);
  return parseAnsysCsv(content);
}

}  // namespace wb
