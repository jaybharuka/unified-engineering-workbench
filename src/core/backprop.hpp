// Task F: handwritten backpropagation for  h = tanh(w x + b),  y_hat = v h + c,  J = 0.5 (y_hat - y)^2.
// No automatic differentiation: the chain rule is written out explicitly.
#pragma once
#include <string>
#include <vector>

namespace wb {

struct BpParams {
  double w = 0.5, b = 0.0, v = 1.0, c = 0.0;
};

struct BpForward {
  double z = 0, h = 0, y_hat = 0, error = 0, loss = 0;
};

struct BpGrad {
  double dw = 0, db = 0, dv = 0, dc = 0;
};

BpForward bpForward(const BpParams& p, double x, double y);
BpGrad bpAnalyticGrad(const BpParams& p, double x, double y);
BpGrad bpNumericGrad(const BpParams& p, double x, double y, double eps = 1e-6);  // central differences
BpParams bpStep(const BpParams& p, const BpGrad& g, double lr);                    // simultaneous update

struct BpGradCheck {
  BpGrad analytic, numeric;
  double abs_err_w = 0, abs_err_b = 0, abs_err_v = 0, abs_err_c = 0;
  double max_abs_err = 0;
  bool pass = false;  // max error <= tolerance
};
BpGradCheck bpCheck(const BpParams& p, double x, double y, double eps = 1e-6, double tol = 1e-5);

struct BpTrace {
  // index 0 = initial parameters; index 1 = after the single update (the hand-calculated one);
  // indices 2..steps+1 = the "100 subsequent" steps.
  std::vector<BpParams> params;
  std::vector<double> loss;
};
BpTrace bpTrain(const BpParams& start, double x, double y, double lr, int subsequent_steps);

std::string bpLossCsv(const BpTrace& t);
std::string bpReportText(double x, double y, double lr, const BpParams& start, const BpGradCheck& chk, const BpTrace& t);

}  // namespace wb
