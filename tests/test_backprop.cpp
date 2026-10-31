#include "backprop.hpp"
#include "test_main.hpp"

using namespace wb;

// Reference numbers computed independently with Python's math module (see docs/backprop_worked_solution.md).
TEST(backprop_forward_pass_values) {
  BpParams p;
  BpForward f = bpForward(p, 1.0, 2.0);
  CHECK_NEAR(f.z, 0.5, 1e-15);
  CHECK_NEAR(f.h, 0.46211715726000974, 1e-14);
  CHECK_NEAR(f.y_hat, 0.46211715726000974, 1e-14);
  CHECK_NEAR(f.error, -1.5378828427399902, 1e-13);
  CHECK_NEAR(f.loss, 1.1825418189970167, 1e-13);
}

TEST(backprop_analytic_gradients) {
  BpParams p;
  BpGrad g = bpAnalyticGrad(p, 1.0, 2.0);
  CHECK_NEAR(g.dw, -1.2094644752400612, 1e-13);
  CHECK_NEAR(g.db, -1.2094644752400612, 1e-13);
  CHECK_NEAR(g.dv, -0.7106820474859469, 1e-13);
  CHECK_NEAR(g.dc, -1.5378828427399902, 1e-13);
}

TEST(backprop_central_difference_check_passes) {
  BpParams p;
  BpGradCheck c = bpCheck(p, 1.0, 2.0, 1e-6, 1e-5);
  CHECK(c.pass);
  CHECK(c.max_abs_err <= 1e-5);
  CHECK(c.abs_err_w <= 1e-5 && c.abs_err_b <= 1e-5 && c.abs_err_v <= 1e-5 && c.abs_err_c <= 1e-5);
}

TEST(backprop_gradient_check_detects_a_wrong_gradient) {
  // Negative test: a deliberately broken gradient (forgetting the (1 - h^2) factor) must FAIL the check.
  BpParams p;
  BpForward f = bpForward(p, 1.0, 2.0);
  double wrong_dw = f.error * p.v * 1.0;  // missing (1 - h^2)
  BpGrad num = bpNumericGrad(p, 1.0, 2.0);
  CHECK(std::fabs(wrong_dw - num.dw) > 1e-2);
}

TEST(backprop_gradient_check_at_other_points) {
  BpParams q; q.w = -0.7; q.b = 0.3; q.v = 1.9; q.c = -0.4;
  CHECK(bpCheck(q, 0.8, -1.0).pass);
  CHECK(bpCheck(q, 2.5, 0.0).pass);
}

TEST(backprop_simultaneous_update) {
  BpParams p;
  BpGrad g = bpAnalyticGrad(p, 1.0, 2.0);
  BpParams n = bpStep(p, g, 0.1);
  CHECK_NEAR(n.w, 0.6209464475240061, 1e-13);
  CHECK_NEAR(n.b, 0.12094644752400613, 1e-13);
  CHECK_NEAR(n.v, 1.0710682047485947, 1e-13);
  CHECK_NEAR(n.c, 0.15378828427399904, 1e-13);
}

TEST(backprop_training_trace) {
  BpParams p;
  BpTrace t = bpTrain(p, 1.0, 2.0, 0.1, 100);
  CHECK(t.loss.size() == 102);               // initial + 1 hand-calculated update + 100 subsequent
  CHECK_NEAR(t.loss[0], 1.1825418189970167, 1e-12);
  CHECK_NEAR(t.loss[1], 0.6857737650448423, 1e-12);
  for (size_t i = 1; i < t.loss.size(); ++i) CHECK(t.loss[i] <= t.loss[i - 1] + 1e-15);  // monotone decrease
  CHECK(t.loss.back() < 1e-15);
  CHECK_NEAR(t.params.back().w, 0.9037292855095282, 1e-9);
}

TEST(backprop_report_and_csv_text) {
  BpParams p;
  BpTrace t = bpTrain(p, 1.0, 2.0, 0.1, 100);
  CHECK(CONTAINS(bpLossCsv(t), "step,loss,w,b,v,c"));
  CHECK(CONTAINS(bpReportText(1, 2, 0.1, p, bpCheck(p, 1, 2), t), "PASS"));
}
