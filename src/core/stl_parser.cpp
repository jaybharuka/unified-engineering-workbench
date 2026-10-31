#include "stl_parser.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>

#include "util.hpp"

namespace wb {
namespace {

struct Token {
  const char* b = nullptr;
  std::size_t n = 0;
  std::size_t line = 0;
  bool end() const { return b == nullptr; }
};

class Tokenizer {
 public:
  Tokenizer(const char* p, std::size_t n) : p_(p), e_(p + n) {}
  Token next() {
    while (p_ < e_ && std::isspace(static_cast<unsigned char>(*p_))) {
      if (*p_ == '\n') ++line_;
      ++p_;
    }
    if (p_ >= e_) return Token{};
    const char* s = p_;
    while (p_ < e_ && !std::isspace(static_cast<unsigned char>(*p_))) ++p_;
    return Token{s, static_cast<std::size_t>(p_ - s), line_};
  }
  // Rest of the current line (for the solid name).
  std::string restOfLine() {
    const char* s = p_;
    while (p_ < e_ && *p_ != '\n') ++p_;
    std::string r(s, p_);
    while (!r.empty() && (r.back() == '\r' || std::isspace(static_cast<unsigned char>(r.back())))) r.pop_back();
    std::size_t i = 0;
    while (i < r.size() && std::isspace(static_cast<unsigned char>(r[i]))) ++i;
    return r.substr(i);
  }
  std::size_t line() const { return line_; }

 private:
  const char* p_;
  const char* e_;
  std::size_t line_ = 1;
};

bool iequals(const Token& t, const char* w) {
  std::size_t n = std::strlen(w);
  if (t.n != n) return false;
  for (std::size_t i = 0; i < n; ++i)
    if (std::tolower(static_cast<unsigned char>(t.b[i])) != w[i]) return false;
  return true;
}

std::string show(const Token& t) {
  if (t.end()) return "end of file";
  std::string s(t.b, std::min<std::size_t>(t.n, 24));
  return "'" + s + "'";
}

std::string at(const Token& t, std::size_t fallbackLine) {
  return "line " + std::to_string(t.end() ? fallbackLine : t.line);
}

// Strict decimal float parse of a whole token. Rejects hex floats, "nan", "inf".
bool parseNumber(const Token& t, double& out, std::string& why) {
  if (t.end()) { why = "missing number"; return false; }
  char buf[64];
  if (t.n >= sizeof buf) { why = "number too long"; return false; }
  for (std::size_t i = 0; i < t.n; ++i) {
    char c = t.b[i];
    if (!(std::isdigit(static_cast<unsigned char>(c)) || c == '+' || c == '-' || c == '.' || c == 'e' || c == 'E')) {
      why = "'" + std::string(t.b, t.n) + "' is not a finite decimal number";
      return false;
    }
    buf[i] = c;
  }
  buf[t.n] = '\0';
  char* endp = nullptr;
  out = std::strtod(buf, &endp);
  if (endp == buf || *endp != '\0') { why = "'" + std::string(buf) + "' is not a valid number"; return false; }
  if (!std::isfinite(out)) { why = "'" + std::string(buf) + "' is not finite"; return false; }
  if (std::fabs(out) > 3.0e38) { why = "'" + std::string(buf) + "' is out of range for 32-bit coordinates"; return false; }
  return true;
}

StlResult fail(std::string msg) {
  StlResult r;
  r.ok = false;
  r.error = std::move(msg);
  return r;
}

bool looksLikeBinaryStl(const std::string& s) {
  if (s.size() < 84) return false;
  std::uint32_t n = 0;
  std::memcpy(&n, s.data() + 80, 4);  // little-endian on all supported targets
  return n > 0 && s.size() == 84u + 50u * static_cast<std::size_t>(n);
}

}  // namespace

StlResult parseAsciiStl(const std::string& content, std::size_t maxTriangles) {
  // ---- whole-file checks ----
  if (content.empty()) return fail("The file is empty (0 bytes). Choose an ASCII STL file that contains at least one facet.");
  {
    bool allSpace = std::all_of(content.begin(), content.end(), [](unsigned char c) { return std::isspace(c) != 0; });
    if (allSpace) return fail("The file contains only whitespace. Choose an ASCII STL file that contains at least one facet.");
  }
  if (looksLikeBinaryStl(content))
    return fail("This looks like a BINARY STL (size matches the binary layout). Only ASCII STL is supported - re-export as ASCII.");
  if (content.find('\0') != std::string::npos)
    return fail("The file contains NUL bytes, so it is not ASCII STL. Re-export the model as ASCII STL.");

  const char* data = content.data();
  std::size_t size = content.size();
  if (size >= 3 && static_cast<unsigned char>(data[0]) == 0xEF && static_cast<unsigned char>(data[1]) == 0xBB &&
      static_cast<unsigned char>(data[2]) == 0xBF) {  // UTF-8 BOM
    data += 3;
    size -= 3;
  }

  Tokenizer tk(data, size);
  StlResult res;
  Token t = tk.next();
  if (!iequals(t, "solid"))
    return fail("Line " + std::to_string(t.end() ? 1 : t.line) + ": expected the keyword 'solid' but found " + show(t) +
                ". The file is not ASCII STL.");

  bool firstSolid = true;
  std::size_t solids = 0;
  std::vector<float>& pos = res.mesh.positions;

  while (!t.end()) {
    // t == 'solid'
    ++solids;
    std::string nm = tk.restOfLine();
    if (firstSolid) { res.mesh.name = nm; firstSolid = false; }
    t = tk.next();
    while (iequals(t, "facet")) {
      if (pos.size() / 9 >= maxTriangles)
        return fail("More than " + std::to_string(maxTriangles) + " triangles. The limit protects memory; use a coarser mesh.");
      Token w = tk.next();
      if (!iequals(w, "normal")) return fail(at(w, tk.line()) + ": expected 'normal' after 'facet' but found " + show(w) + ".");
      double nrm[3];
      std::string why;
      for (int i = 0; i < 3; ++i) {
        Token v = tk.next();
        if (!parseNumber(v, nrm[i], why)) return fail(at(v, tk.line()) + ": bad facet normal component - " + why + ".");
      }
      w = tk.next();
      if (!iequals(w, "outer")) return fail(at(w, tk.line()) + ": expected 'outer loop' but found " + show(w) + ".");
      w = tk.next();
      if (!iequals(w, "loop")) return fail(at(w, tk.line()) + ": expected 'loop' after 'outer' but found " + show(w) + ".");
      double vtx[3][3];
      for (int k = 0; k < 3; ++k) {
        w = tk.next();
        if (!iequals(w, "vertex"))
          return fail(at(w, tk.line()) + ": expected 'vertex' (3 per facet) but found " + show(w) +
                      ". The facet is incomplete or the file is truncated.");
        for (int i = 0; i < 3; ++i) {
          Token v = tk.next();
          if (!parseNumber(v, vtx[k][i], why)) return fail(at(v, tk.line()) + ": bad vertex coordinate - " + why + ".");
        }
      }
      w = tk.next();
      if (!iequals(w, "endloop")) return fail(at(w, tk.line()) + ": expected 'endloop' but found " + show(w) + ". A facet must have exactly 3 vertices.");
      w = tk.next();
      if (!iequals(w, "endfacet")) return fail(at(w, tk.line()) + ": expected 'endfacet' but found " + show(w) + ".");
      for (int k = 0; k < 3; ++k)
        for (int i = 0; i < 3; ++i) pos.push_back(static_cast<float>(vtx[k][i]));
      t = tk.next();
    }
    if (!iequals(t, "endsolid"))
      return fail(at(t, tk.line()) + ": expected 'facet' or 'endsolid' but found " + show(t) +
                  (t.end() ? ". The file is truncated (missing 'endsolid')." : "."));
    tk.restOfLine();  // optional name after endsolid
    t = tk.next();
    if (!t.end() && !iequals(t, "solid"))
      return fail(at(t, tk.line()) + ": unexpected content " + show(t) + " after 'endsolid'.");
  }

  if (pos.empty())
    return fail("The file is valid STL syntax but contains no facets (0 triangles), so there is nothing to display.");
  if (solids > 1) res.warnings.push_back(std::to_string(solids) + " solids found; they were merged into one mesh.");

  // ---- statistics ----
  Mesh& m = res.mesh;
  for (int i = 0; i < 3; ++i) { m.bbox_min[i] = 1e300; m.bbox_max[i] = -1e300; }
  const std::size_t tris = pos.size() / 9;
  for (std::size_t ti = 0; ti < tris; ++ti) {
    const float* p = &pos[ti * 9];
    for (int v = 0; v < 3; ++v)
      for (int i = 0; i < 3; ++i) {
        m.bbox_min[i] = std::min<double>(m.bbox_min[i], p[v * 3 + i]);
        m.bbox_max[i] = std::max<double>(m.bbox_max[i], p[v * 3 + i]);
      }
    double ax = p[3] - p[0], ay = p[4] - p[1], az = p[5] - p[2];
    double bx = p[6] - p[0], by = p[7] - p[1], bz = p[8] - p[2];
    double cx = ay * bz - az * by, cy = az * bx - ax * bz, cz = ax * by - ay * bx;
    double area = 0.5 * std::sqrt(cx * cx + cy * cy + cz * cz);
    if (area <= 0.0) ++m.degenerate_triangles;
    m.surface_area += area;
  }
  if (m.degenerate_triangles > 0)
    res.warnings.push_back(std::to_string(m.degenerate_triangles) + " zero-area (degenerate) triangle(s) found.");
  res.ok = true;
  return res;
}

StlResult loadAsciiStlFile(const std::string& path, std::size_t maxBytes) {
  std::string content, err;
  if (!readTextFile(path, maxBytes, content, err)) return fail(err);
  return parseAsciiStl(content);
}

}  // namespace wb
