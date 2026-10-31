#include "backprop.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>

#include "util.hpp"

namespace wb {

BpForward bpForward(const BpParams& p, double x, double y) {
  BpForward f;
  f.z = p.w * x + p.b;
  f.h = std::tanh(f.z);
  f.y_hat = p.v * f.h + p.c;
  f.error = f.y_hat - y;
  f.loss = 0.5 * f.error * f.error;
  return f;
}

// Chain rule, written out:
//   J = 0.5 e^2,               e = y_hat - y            dJ/de     = e
//   y_hat = v h + c                                      dJ/dv     = e h,   dJ/dc = e,   dJ/dh = e v
//   h = tanh(z),               dh/dz = 1 - h^2           dJ/dz     = e v (1 - h^2)
//   z = w x + b                                          dJ/dw     = dJ/dz x,  dJ/db = dJ/dz
BpGrad bpAnalyticGrad(const BpParams& p, double x, double y) {
  BpForward f = bpForward(p, x, y);
  BpGrad g;
  g.dc = f.error;
  g.dv = f.error * f.h;
  const double dh = f.error * p.v;
  const double dz = dh * (1.0 - f.h * f.h);
  g.dw = dz * x;
  g.db = dz;
  return g;
}

BpGrad bpNumericGrad(const BpParams& p, double x, double y, double eps) {
  auto lossAt = [&](BpParams q) { return bpForward(q, x, y).loss; };
  BpGrad g;
  BpParams a = p, b = p;
  a.w += eps; b.w -= eps; g.dw = (lossAt(a) - lossAt(b)) / (2 * eps);
  a = p; b = p;
  a.b += eps; b.b -= eps; g.db = (lossAt(a) - lossAt(b)) / (2 * eps);
  a = p; b = p;
  a.v += eps; b.v -= eps; g.dv = (lossAt(a) - lossAt(b)) / (2 * eps);
  a = p; b = p;
  a.c += eps; b.c -= eps; g.dc = (lossAt(a) - lossAt(b)) / (2 * eps);
  return g;
}

BpParams bpStep(const BpParams& p, const BpGrad& g, double lr) {
  BpParams n;  // all four gradients were computed from the ORIGINAL p => simultaneous update
  n.w = p.w - lr * g.dw;
  n.b = p.b - lr * g.db;
  n.v = p.v - lr * g.dv;
  n.c = p.c - lr * g.dc;
  return n;
}

BpGradCheck bpCheck(const BpParams& p, double x, double y, double eps, double tol) {
  BpGradCheck c;
  c.analytic = bpAnalyticGrad(p, x, y);
  c.numeric = bpNumericGrad(p, x, y, eps);
  c.abs_err_w = std::fabs(c.analytic.dw - c.numeric.dw);
  c.abs_err_b = std::fabs(c.analytic.db - c.numeric.db);
  c.abs_err_v = std::fabs(c.analytic.dv - c.numeric.dv);
  c.abs_err_c = std::fabs(c.analytic.dc - c.numeric.dc);
  c.max_abs_err = std::max(std::max(c.abs_err_w, c.abs_err_b), std::max(c.abs_err_v, c.abs_err_c));
  c.pass = c.max_abs_err <= tol;
  return c;
}

BpTrace bpTrain(const BpParams& start, double x, double y, double lr, int steps) {
  BpTrace t;
  BpParams p = start;
  t.params.push_back(p);
  t.loss.push_back(bpForward(p, x, y).loss);
  for (int i = 0; i < steps + 1; ++i) {  // +1 : the first update is the hand-calculated one
    p = bpStep(p, bpAnalyticGrad(p, x, y), lr);
    t.params.push_back(p);
    t.loss.push_back(bpForward(p, x, y).loss);
  }
  return t;
}

std::string bpLossCsv(const BpTrace& t) {
  std::ostringstream o;
  o << "step,loss,w,b,v,c\n";
  for (std::size_t i = 0; i < t.loss.size(); ++i)
    o << i << ',' << formatDouble(t.loss[i], 12) << ',' << formatDouble(t.params[i].w, 12) << ',' << formatDouble(t.params[i].b, 12)
      << ',' << formatDouble(t.params[i].v, 12) << ',' << formatDouble(t.params[i].c, 12) << '\n';
  return o.str();
}

std::string bpReportText(double x, double y, double lr, const BpParams& s, const BpGradCheck& k, const BpTrace& t) {
  char buf[256];
  std::ostringstream o;
  BpForward f = bpForward(s, x, y);
  std::snprintf(buf, sizeof buf, "x=%g  y=%g  lr=%g  start: w=%g b=%g v=%g c=%g\n", x, y, lr, s.w, s.b, s.v, s.c);
  o << buf;
  std::snprintf(buf, sizeof buf, "forward: z=%.10f  h=tanh(z)=%.10f  y_hat=%.10f  error=%.10f  J=%.10f\n", f.z, f.h, f.y_hat, f.error, f.loss);
  o << buf;
  std::snprintf(buf, sizeof buf, "analytic:  dJ/dw=%.10f  dJ/db=%.10f  dJ/dv=%.10f  dJ/dc=%.10f\n", k.analytic.dw, k.analytic.db, k.analytic.dv, k.analytic.dc);
  o << buf;
  std::snprintf(buf, sizeof buf, "numeric :  dJ/dw=%.10f  dJ/db=%.10f  dJ/dv=%.10f  dJ/dc=%.10f\n", k.numeric.dw, k.numeric.db, k.numeric.dv, k.numeric.dc);
  o << buf;
  std::snprintf(buf, sizeof buf, "abs error: w=%.3e b=%.3e v=%.3e c=%.3e  max=%.3e  (tolerance 1e-5) -> %s\n", k.abs_err_w, k.abs_err_b,
                k.abs_err_v, k.abs_err_c, k.max_abs_err, k.pass ? "PASS" : "FAIL");
  o << buf;
  const BpParams& p1 = t.params.at(1);
  std::snprintf(buf, sizeof buf, "after 1 update: w=%.10f b=%.10f v=%.10f c=%.10f  J=%.10f\n", p1.w, p1.b, p1.v, p1.c, t.loss.at(1));
  o << buf;
  std::snprintf(buf, sizeof buf, "after %zu updates: J=%.10f (initial J=%.10f)\n", t.loss.size() - 1, t.loss.back(), t.loss.front());
  o << buf;
  return o.str();
}

}  // namespace wb
