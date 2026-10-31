// Generators for the synthetic example files in data/ and for stress-test files (file-size limits).
#pragma once
#include <cstddef>
#include <string>

namespace wb {

// Self-created ASCII STL of a cylinder (axis along +x) : length_m, radius_m, `segments` around, `slices` along.
std::string generateCylinderStl(double length_m, double radius_m, int segments, int slices);

// SYNTHETIC nodal-temperature CSV on a cylinder-shaped node set in the ANSYS export schema.
// T = exact 1-D rod solution + small synthetic radial term. NOT an actual ANSYS export.
std::string generateSyntheticRodCsv(double length_m, double radius_m, int slices, int rings, int angular);

// Large files for documenting tested size limits.
std::string generateLargeStl(std::size_t triangles);
std::string generateLargeCsv(std::size_t rows);

}  // namespace wb
