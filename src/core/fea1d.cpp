#include "fea1d.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <stdexcept>

#include "util.hpp"

namespace wb {

void RodWorkspace::ensure(int n) {
  std::size_t m = static_cast<std::size_t>(n) + 1;
  if (diag.size() < m) {
    diag.resize(m);
    off.resize(m);
    rhs.resize(m);
    cprime.resize(m);
  }
}

std::string validateRod(const RodParams& p, int n) {
  auto bad = [](double v) { return !std::isfinite(v); };
  if (bad(p.L) || p.L <= 0.0) return "Length L must be a finite number > 0 (metres).";
  if (bad(p.A) || p.A <= 0.0) return "Cross-section area A must be a finite number > 0 (m^2).";
  if (bad(p.k) || p.k <= 0.0) return "Conductivity k must be a finite number > 0 (W/(m K)).";
  if (bad(p.q)) return "Heat generation q must be a finite number (W/m^3).";
  if (bad(p.T0) || bad(p.TL)) return "Boundary temperatures T0 and TL must be finite numbers (deg C).";
  if (p.T0 < -273.15 || p.TL < -273.15) return "Boundary temperatures must be above absolute zero (-273.15 deg C).";
  if (n < 1) return "Number of elements must be at least 1.";
  if (n > kMaxElements) return "Number of elements must not exceed " + std::to_string(kMaxElements) + ".";
  return std::string();
}

void solveRodInto(const RodParams& p, int n, double* T, RodWorkspace& ws) {
  ws.ensure(n);
  const double h = p.L / n;
  const double ke = p.k * p.A / h;          // element conductance
  const double fe = p.q * p.A * h / 2.0;    // consistent nodal load for uniform q

  const int nn = n + 1;
  for (int i = 0; i < nn; ++i) { ws.diag[i] = 0.0; ws.rhs[i] = 0.0; ws.off[i] = 0.0; }

  // Element-by-element assembly of a symmetric tridiagonal system.
  // off[e] holds K(e, e+1) = K(e+1, e).
  for (int e = 0; e < n; ++e) {
    ws.diag[e] += ke;
    ws.diag[e + 1] += ke;
    ws.off[e] += -ke;
    ws.rhs[e] += fe;
    ws.rhs[e + 1] += fe;
  }

  T[0] = p.T0;
  T[n] = p.TL;
  if (n == 1) return;  // no interior unknowns

  // Impose Dirichlet values: move K(i, 0)*T0 and K(i, n)*TL to the right-hand side.
  ws.rhs[1] -= ws.off[0] * p.T0;
  ws.rhs[n - 1] -= ws.off[n - 1] * p.TL;

  // Reduced system for interior nodes 1..n-1 : Thomas algorithm (SPD, diagonally dominant).
  const int m = n - 1;  // unknown count; unknown j corresponds to node j+1
  ws.cprime[0] = ws.off[1] / ws.diag[1];
  T[1] = ws.rhs[1] / ws.diag[1];
  for (int j = 1; j < m; ++j) {
    const int node = j + 1;
    const double lower = ws.off[node - 1];
    const double denom = ws.diag[node] - lower * ws.cprime[j - 1];
    if (j < m - 1) ws.cprime[j] = ws.off[node] / denom;
    T[node] = (ws.rhs[node] - lower * T[node - 1]) / denom;
  }
  for (int j = m - 2; j >= 0; --j) {
    const int node = j + 1;
    T[node] -= ws.cprime[j] * T[node + 1];
  }
}

RodSolution solveRod(const RodParams& p, int n) {
  std::string err = validateRod(p, n);
  if (!err.empty()) throw std::invalid_argument(err);
  RodSolution s;
  s.n_elements = n;
  s.h = p.L / n;
  s.x.resize(static_cast<std::size_t>(n) + 1);
  s.T.resize(static_cast<std::size_t>(n) + 1);
  for (int i = 0; i <= n; ++i) s.x[i] = p.L * i / n;
  RodWorkspace ws;
  solveRodInto(p, n, s.T.data(), ws);
  return s;
}

double exactTemperature(const RodParams& p, double x) {
  return p.T0 + (p.TL - p.T0) * x / p.L + (p.q / (2.0 * p.k)) * x * (p.L - x);
}

double feInterpolate(const RodSolution& s, double x) {
  const int n = s.n_elements;
  double xi = x / s.h;
  int e = static_cast<int>(std::floor(xi));
  e = std::max(0, std::min(n - 1, e));
  double t = (x - s.x[e]) / s.h;  // local coordinate in [0,1]
  return (1.0 - t) * s.T[e] + t * s.T[e + 1];
}

ErrorMetrics computeErrors(const RodParams& p, const RodSolution& s) {
  ErrorMetrics m;
  m.n_elements = s.n_elements;
  m.h = s.h;
  for (int i = 0; i <= s.n_elements; ++i)
    m.max_node_error = std::max(m.max_node_error, std::fabs(s.T[i] - exactTemperature(p, s.x[i])));
  for (int e = 0; e < s.n_elements; ++e) {
    const double xm = 0.5 * (s.x[e] + s.x[e + 1]);
    const double fe_mid = feInterpolate(s, xm);  // FE interpolation, not the exact curve
    m.max_midpoint_error = std::max(m.max_midpoint_error, std::fabs(fe_mid - exactTemperature(p, xm)));
  }
  return m;
}

std::vector<ConvergenceRow> convergenceStudy(const RodParams& p, const std::vector<int>& meshes) {
  std::vector<ConvergenceRow> rows;
  for (int n : meshes) {
    RodSolution s = solveRod(p, n);
    ConvergenceRow r;
    r.m = computeErrors(p, s);
    if (!rows.empty() && rows.back().m.max_midpoint_error > 0.0 && r.m.max_midpoint_error > 0.0) {
      r.observed_order = std::log(rows.back().m.max_midpoint_error / r.m.max_midpoint_error) /
                         std::log(rows.back().m.h / r.m.h);
    }
    rows.push_back(r);
  }
  return rows;
}

double maxInteriorResidual(const RodParams& p, const RodSolution& s) {
  const double ke = p.k * p.A / s.h;
  const double fe = p.q * p.A * s.h / 2.0;
  double worst = 0.0;
  for (int i = 1; i < s.n_elements; ++i) {
    // Row i of K T - f : (-ke) T[i-1] + 2 ke T[i] + (-ke) T[i+1] - 2 fe
    double r = -ke * s.T[i - 1] + 2.0 * ke * s.T[i] - ke * s.T[i + 1] - 2.0 * fe;
    worst = std::max(worst, std::fabs(r) / ke);
  }
  return worst;
}

std::string solutionCsv(const RodParams& p, const RodSolution& s) {
  std::ostringstream o;
  o << "x_m,T_fe_C,T_exact_C,abs_error_C\n";
  for (int i = 0; i <= s.n_elements; ++i) {
    double ex = exactTemperature(p, s.x[i]);
    o << formatDouble(s.x[i], 12) << ',' << formatDouble(s.T[i], 12) << ',' << formatDouble(ex, 12) << ','
      << formatDouble(std::fabs(s.T[i] - ex), 6) << '\n';
  }
  return o.str();
}

std::string convergenceCsv(const std::vector<ConvergenceRow>& rows) {
  std::ostringstream o;
  o << "n_elements,h_m,max_node_error_C,max_midpoint_error_C,observed_order\n";
  for (const auto& r : rows) {
    o << r.m.n_elements << ',' << formatDouble(r.m.h, 8) << ',' << formatDouble(r.m.max_node_error, 6) << ','
      << formatDouble(r.m.max_midpoint_error, 10) << ',' << (r.observed_order == 0.0 ? std::string("") : formatDouble(r.observed_order, 6))
      << '\n';
  }
  return o.str();
}

std::string midpointCsv(const RodParams& p, const RodSolution& s) {
  std::ostringstream o;
  o << "element,x_mid_m,T_fe_interp_C,T_exact_C,abs_error_C\n";
  for (int e = 0; e < s.n_elements; ++e) {
    double xm = 0.5 * (s.x[e] + s.x[e + 1]);
    double fe = feInterpolate(s, xm);
    double ex = exactTemperature(p, xm);
    o << e << ',' << formatDouble(xm, 12) << ',' << formatDouble(fe, 12) << ',' << formatDouble(ex, 12) << ','
      << formatDouble(std::fabs(fe - ex), 10) << '\n';
  }
  return o.str();
}

}  // namespace wb
