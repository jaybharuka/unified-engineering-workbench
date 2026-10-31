// Task C: steady-state 1-D thermal finite-element solver (two-node linear elements).
//
// Governing equation (constant k, A; insulated lateral surface):
//     -d/dx( k A dT/dx ) = q A          on 0 < x < L
//     T(0) = T0,  T(L) = TL             (Dirichlet at both ends)
//
// Element matrices (element length h):
//     K_e = (k A / h) [ 1 -1; -1 1 ]    [W/K]
//     f_e = (q A h / 2) [ 1; 1 ]        [W]
// Prescribed temperatures are imposed by symmetric elimination: known nodal values are
// moved to the right-hand side and the reduced (interior) tridiagonal system is solved.
#pragma once
#include <string>
#include <vector>

namespace wb {

struct RodParams {
  double L = 1.0;      // length [m]
  double A = 1.0e-4;   // cross-section area [m^2]
  double k = 10.0;     // thermal conductivity [W/(m K)]
  double q = 1.0e4;    // volumetric heat generation [W/m^3]
  double T0 = 100.0;   // T(0) [deg C]
  double TL = 20.0;    // T(L) [deg C]
};

struct RodSolution {
  int n_elements = 0;
  double h = 0.0;
  std::vector<double> x;  // n+1 node coordinates [m]
  std::vector<double> T;  // n+1 nodal temperatures [deg C]
};

// Reusable scratch memory so batch runs do not allocate inside the hot loop.
struct RodWorkspace {
  std::vector<double> diag, off, rhs, cprime;
  void ensure(int n_elements);
};

constexpr int kMaxElements = 2000000;

// Returns an empty string if inputs are valid, otherwise an actionable message.
std::string validateRod(const RodParams& p, int n_elements);

// Core solver used by both the single solve and the batch (identical numerics).
// `T` must point to n_elements+1 doubles. Parameters must already be validated.
void solveRodInto(const RodParams& p, int n_elements, double* T, RodWorkspace& ws);

RodSolution solveRod(const RodParams& p, int n_elements);  // throws std::invalid_argument

double exactTemperature(const RodParams& p, double x);
// Piecewise-linear FE interpolation at x (uses the element containing x).
double feInterpolate(const RodSolution& s, double x);

struct ErrorMetrics {
  int n_elements = 0;
  double h = 0.0;
  double max_node_error = 0.0;      // max |T_h - T_exact| at nodes [deg C]
  double max_midpoint_error = 0.0;  // max |FE interpolation - T_exact| at element midpoints [deg C]
};
ErrorMetrics computeErrors(const RodParams& p, const RodSolution& s);

struct ConvergenceRow {
  ErrorMetrics m;
  double observed_order = 0.0;  // log2(e_prev/e_this) for midpoint error; 0 for first row
};
std::vector<ConvergenceRow> convergenceStudy(const RodParams& p, const std::vector<int>& meshes);

// Maximum interior residual |K T - f| / (kA/h) (diagnostic, dimension: deg C).
double maxInteriorResidual(const RodParams& p, const RodSolution& s);

// CSV exports (header row included). Units are in the column names.
std::string solutionCsv(const RodParams& p, const RodSolution& s);
std::string convergenceCsv(const std::vector<ConvergenceRow>& rows);
// Dense curve of FE interpolation vs exact on a fine grid, for plotting.
std::string midpointCsv(const RodParams& p, const RodSolution& s);

}  // namespace wb
