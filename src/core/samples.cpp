#include "samples.hpp"

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <sstream>

#include "fea1d.hpp"
#include "util.hpp"

namespace wb {
namespace {

const double kPi = 3.14159265358979323846;

void facet(std::string& out, const double a[3], const double b[3], const double c[3]) {
  double ux = b[0] - a[0], uy = b[1] - a[1], uz = b[2] - a[2];
  double vx = c[0] - a[0], vy = c[1] - a[1], vz = c[2] - a[2];
  double nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
  double len = std::sqrt(nx * nx + ny * ny + nz * nz);
  if (len > 0) { nx /= len; ny /= len; nz /= len; }
  char buf[512];
  std::snprintf(buf, sizeof buf,
                "  facet normal %.6e %.6e %.6e\n    outer loop\n      vertex %.6e %.6e %.6e\n      vertex %.6e %.6e %.6e\n      vertex %.6e %.6e %.6e\n    endloop\n  endfacet\n",
                nx, ny, nz, a[0], a[1], a[2], b[0], b[1], b[2], c[0], c[1], c[2]);
  out += buf;
}

}  // namespace

std::string generateCylinderStl(double L, double R, int seg, int slices) {
  std::string out = "solid synthetic_cylinder_rod\n";
  for (int s = 0; s < slices; ++s) {
    double x0 = L * s / slices, x1 = L * (s + 1) / slices;
    for (int i = 0; i < seg; ++i) {
      double a0 = 2 * kPi * i / seg, a1 = 2 * kPi * (i + 1) / seg;
      double p00[3] = {x0, R * std::cos(a0), R * std::sin(a0)};
      double p01[3] = {x0, R * std::cos(a1), R * std::sin(a1)};
      double p10[3] = {x1, R * std::cos(a0), R * std::sin(a0)};
      double p11[3] = {x1, R * std::cos(a1), R * std::sin(a1)};
      facet(out, p00, p10, p11);  // outward normals (counter-clockwise seen from outside)
      facet(out, p00, p11, p01);
    }
  }
  double c0[3] = {0, 0, 0}, c1[3] = {L, 0, 0};
  for (int i = 0; i < seg; ++i) {
    double a0 = 2 * kPi * i / seg, a1 = 2 * kPi * (i + 1) / seg;
    double q0[3] = {0, R * std::cos(a0), R * std::sin(a0)}, q1[3] = {0, R * std::cos(a1), R * std::sin(a1)};
    double r0[3] = {L, R * std::cos(a0), R * std::sin(a0)}, r1[3] = {L, R * std::cos(a1), R * std::sin(a1)};
    facet(out, c0, q1, q0);  // end cap at x = 0 (normal -x)
    facet(out, c1, r0, r1);  // end cap at x = L (normal +x)
  }
  out += "endsolid synthetic_cylinder_rod\n";
  return out;
}

std::string generateSyntheticRodCsv(double L, double R, int slices, int rings, int angular) {
  RodParams p;  // Task C parameters, so the colours show the same physics
  std::ostringstream o;
  o << "node_id,x_m,y_m,z_m,temperature_C\n";
  int id = 1;
  for (int s = 0; s <= slices; ++s) {
    double x = L * s / slices;
    double base = exactTemperature(p, x);
    for (int r = 0; r <= rings; ++r) {
      double rad = R * r / rings;
      int na = (r == 0) ? 1 : angular;
      for (int a = 0; a < na; ++a) {
        double ang = 2 * kPi * a / angular;
        double y = rad * std::cos(ang), z = rad * std::sin(ang);
        double t = base + 5.0 * (rad / R) * (rad / R);  // synthetic radial rise toward the surface
        o << id++ << ',' << formatDouble(x, 8) << ',' << formatDouble(y, 8) << ',' << formatDouble(z, 8) << ',' << formatDouble(t, 8) << '\n';
      }
    }
  }
  return o.str();
}

std::string generateLargeStl(std::size_t triangles) {
  // A strip of triangles; geometry is irrelevant, size is the point.
  std::string out = "solid large_test\n";
  out.reserve(triangles * 260);
  for (std::size_t i = 0; i < triangles; ++i) {
    double x = static_cast<double>(i % 1000) * 0.01, y = static_cast<double>(i / 1000) * 0.01;
    double a[3] = {x, y, 0}, b[3] = {x + 0.01, y, 0}, c[3] = {x, y + 0.01, 0.001 * static_cast<double>(i % 7)};
    facet(out, a, b, c);
  }
  out += "endsolid large_test\n";
  return out;
}

std::string generateLargeCsv(std::size_t rows) {
  std::string out = "node_id,x_m,y_m,z_m,temperature_C\n";
  out.reserve(rows * 50);
  char buf[160];
  for (std::size_t i = 0; i < rows; ++i) {
    double x = static_cast<double>(i % 1000) * 0.001, y = static_cast<double>((i / 1000) % 1000) * 0.001, z = static_cast<double>(i / 1000000) * 0.001;
    std::snprintf(buf, sizeof buf, "%zu,%.6f,%.6f,%.6f,%.4f\n", i + 1, x, y, z, 20.0 + 80.0 * x);
    out += buf;
  }
  return out;
}

}  // namespace wb
