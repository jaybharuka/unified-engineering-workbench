#include <cmath>
#include <vector>

#include "fea1d.hpp"
#include "test_main.hpp"

using namespace wb;

namespace {
// Independent reference: dense global matrix + Gaussian elimination with partial pivoting.
std::vector<double> denseReference(const RodParams& p, int n) {
  const int m = n + 1;
  const double h = p.L / n, ke = p.k * p.A / h, fe = p.q * p.A * h / 2;
  std::vector<std::vector<double>> K(m, std::vector<double>(m, 0.0));
  std::vector<double> f(m, 0.0);
  for (int e = 0; e < n; ++e) {
    K[e][e] += ke; K[e + 1][e + 1] += ke; K[e][e + 1] -= ke; K[e + 1][e] -= ke;
    f[e] += fe; f[e + 1] += fe;
  }
  // Dirichlet by row replacement (a different strategy from the production code).
  for (int j = 0; j < m; ++j) { K[0][j] = 0; K[n][j] = 0; }
  K[0][0] = 1; f[0] = p.T0; K[n][n] = 1; f[n] = p.TL;
  for (int c = 0; c < m; ++c) {
    int piv = c;
    for (int r = c + 1; r < m; ++r) if (std::fabs(K[r][c]) > std::fabs(K[piv][c])) piv = r;
    std::swap(K[c], K[piv]); std::swap(f[c], f[piv]);
    for (int r = c + 1; r < m; ++r) {
      double fac = K[r][c] / K[c][c];
      for (int j = c; j < m; ++j) K[r][j] -= fac * K[c][j];
      f[r] -= fac * f[c];
    }
  }
  std::vector<double> T(m);
  for (int r = m - 1; r >= 0; --r) {
    double s = f[r];
    for (int j = r + 1; j < m; ++j) s -= K[r][j] * T[j];
    T[r] = s / K[r][r];
  }
  return T;
}
}  // namespace

TEST(fea_boundary_conditions_exact) {
  RodParams p;
  for (int n : {1, 2, 3, 10, 80}) {
    RodSolution s = solveRod(p, n);
    CHECK(s.T.front() == 100.0);
    CHECK(s.T.back() == 20.0);
    CHECK((int)s.T.size() == n + 1);
  }
}

TEST(fea_matches_dense_reference_solver) {
  RodParams p;
  for (int n : {2, 5, 10, 20, 40, 80}) {
    RodSolution s = solveRod(p, n);
    std::vector<double> ref = denseReference(p, n);
    for (int i = 0; i <= n; ++i) CHECK_NEAR(s.T[i], ref[i], 1e-9);
  }
}

TEST(fea_matches_dense_reference_random_params) {
  RodParams p;
  p.k = 37.5; p.q = 8.2e4; p.L = 2.5; p.A = 3e-4; p.T0 = 15; p.TL = 250;
  RodSolution s = solveRod(p, 33);
  std::vector<double> ref = denseReference(p, 33);
  for (int i = 0; i <= 33; ++i) CHECK_NEAR(s.T[i], ref[i], 1e-8);
}

TEST(fea_midpoint_error_equals_q_h2_over_8k) {
  // For this problem the FE solution is nodally exact, so the midpoint error of the linear
  // interpolant is exactly q h^2 / (8 k) = 125 h^2.
  RodParams p;
  const int meshes[4] = {10, 20, 40, 80};
  const double expected[4] = {1.25, 0.3125, 0.078125, 0.01953125};
  for (int i = 0; i < 4; ++i) {
    RodSolution s = solveRod(p, meshes[i]);
    ErrorMetrics m = computeErrors(p, s);
    CHECK_NEAR(m.max_midpoint_error, expected[i], 1e-9);
    CHECK(m.max_node_error < 1e-9);  // nodes are (numerically) exact, which is why midpoints are required
  }
}

TEST(fea_convergence_order_is_two) {
  RodParams p;
  auto rows = convergenceStudy(p, {10, 20, 40, 80});
  CHECK(rows.size() == 4);
  CHECK(rows[0].observed_order == 0.0);
  for (size_t i = 1; i < rows.size(); ++i) {
    CHECK_NEAR(rows[i].observed_order, 2.0, 1e-6);
    CHECK(rows[i].m.max_midpoint_error < rows[i - 1].m.max_midpoint_error);
  }
}

TEST(fea_zero_generation_is_linear) {
  RodParams p;
  p.q = 0.0;
  RodSolution s = solveRod(p, 7);
  for (int i = 0; i <= 7; ++i) CHECK_NEAR(s.T[i], 100.0 - 80.0 * s.x[i], 1e-10);
  CHECK(computeErrors(p, s).max_midpoint_error < 1e-10);
}

TEST(fea_single_element_has_no_interior_unknowns) {
  RodParams p;
  RodSolution s = solveRod(p, 1);
  CHECK(s.T.size() == 2);
  // midpoint of the linear interpolant is 60; exact is 60 + 125 => error 125 * h^2 with h=1
  CHECK_NEAR(computeErrors(p, s).max_midpoint_error, 125.0, 1e-9);
}

TEST(fea_residual_is_tiny) {
  RodParams p;
  RodSolution s = solveRod(p, 40);
  CHECK(maxInteriorResidual(p, s) < 1e-9);
}

TEST(fea_exact_solution_values) {
  RodParams p;
  CHECK_NEAR(exactTemperature(p, 0.0), 100.0, 1e-12);
  CHECK_NEAR(exactTemperature(p, 1.0), 20.0, 1e-12);
  CHECK_NEAR(exactTemperature(p, 0.5), 100.0 - 40.0 + 500.0 * 0.25, 1e-12);  // = 185
}

TEST(fea_interpolation_reproduces_nodes_and_midpoint_average) {
  RodParams p;
  RodSolution s = solveRod(p, 10);
  for (int i = 0; i <= 10; ++i) CHECK_NEAR(feInterpolate(s, s.x[i]), s.T[i], 1e-10);
  CHECK_NEAR(feInterpolate(s, 0.05), 0.5 * (s.T[0] + s.T[1]), 1e-12);
}

TEST(fea_validation_rejects_bad_input) {
  RodParams p;
  CHECK(validateRod(p, 10).empty());
  CHECK(!validateRod(p, 0).empty());
  CHECK(!validateRod(p, -5).empty());
  CHECK(!validateRod(p, kMaxElements + 1).empty());
  RodParams b = p; b.k = 0;       CHECK(!validateRod(b, 10).empty());
  b = p; b.k = -1;                CHECK(!validateRod(b, 10).empty());
  b = p; b.A = 0;                 CHECK(!validateRod(b, 10).empty());
  b = p; b.L = -1;                CHECK(!validateRod(b, 10).empty());
  b = p; b.q = NAN;               CHECK(!validateRod(b, 10).empty());
  b = p; b.T0 = INFINITY;         CHECK(!validateRod(b, 10).empty());
  b = p; b.TL = -300;             CHECK(!validateRod(b, 10).empty());
  CHECK_THROWS(solveRod(b, 10));
}

TEST(fea_csv_exports_have_expected_shape) {
  RodParams p;
  RodSolution s = solveRod(p, 10);
  std::string csv = solutionCsv(p, s);
  CHECK(CONTAINS(csv, "x_m,T_fe_C,T_exact_C,abs_error_C"));
  size_t lines = 0;
  for (char ch : csv) if (ch == '\n') ++lines;
  CHECK(lines == 12);  // header + 11 nodes
  std::string conv = convergenceCsv(convergenceStudy(p, {10, 20}));
  CHECK(CONTAINS(conv, "max_midpoint_error_C"));
  std::string mid = midpointCsv(p, s);
  CHECK(CONTAINS(mid, "x_mid_m"));
}
