// Task B (i): ASCII STL parsing and validation.
#pragma once
#include <cstddef>
#include <string>
#include <vector>

namespace wb {

struct Mesh {
  std::string name;
  std::vector<float> positions;  // 9 floats per triangle (3 vertices x xyz)
  double bbox_min[3] = {0, 0, 0};
  double bbox_max[3] = {0, 0, 0};
  double surface_area = 0.0;
  std::size_t degenerate_triangles = 0;
  std::size_t triangleCount() const { return positions.size() / 9; }
};

struct StlResult {
  bool ok = false;
  std::string error;                  // actionable message when !ok
  std::vector<std::string> warnings;  // non-fatal observations
  Mesh mesh;
};

constexpr std::size_t kMaxStlBytes = 256u * 1024u * 1024u;  // hard limit used by the app
constexpr std::size_t kMaxStlTriangles = 3000000;

// Parses ASCII STL content. Never throws; failure is reported through StlResult.
StlResult parseAsciiStl(const std::string& content, std::size_t maxTriangles = kMaxStlTriangles);
StlResult loadAsciiStlFile(const std::string& path, std::size_t maxBytes = kMaxStlBytes);

}  // namespace wb
