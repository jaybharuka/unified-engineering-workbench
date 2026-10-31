#include <atomic>
#include <cmath>
#include <set>
#include <stdexcept>

#include "batch.hpp"
#include "test_main.hpp"
#include "thread_pool.hpp"

using namespace wb;

namespace {
BatchConfig smallCfg(int cases = 300, int elements = 40) {
  BatchConfig c;
  c.cases = cases;
  c.elements = elements;
  return c;
}
}  // namespace

TEST(batch_generator_is_deterministic_and_in_range) {
  BatchConfig c = smallCfg(1000);
  auto a = generateCases(c), b = generateCases(c);
  CHECK(a.size() == 1000);
  for (size_t i = 0; i < a.size(); ++i) {
    CHECK(a[i].k == b[i].k && a[i].q == b[i].q);
    CHECK(a[i].k >= c.k_min && a[i].k <= c.k_max && a[i].k > 0);
    CHECK(a[i].q >= c.q_min && a[i].q <= c.q_max && a[i].q > 0);
  }
  c.seed += 1;
  auto d = generateCases(c);
  int different = 0;
  for (size_t i = 0; i < a.size(); ++i) different += (a[i].k != d[i].k);
  CHECK(different > 990);
}

TEST(batch_generator_golden_values_are_portable) {
  // Pins the fixed-seed sequence so a change of compiler/platform/RNG would be noticed.
  BatchConfig c;
  c.seed = 20260101ULL;
  c.cases = 3;
  auto v = generateCases(c);
  CHECK_NEAR(v[0].k, 94.8470036808003, 1e-9);
  CHECK_NEAR(v[0].q, 18124.6504808006, 1e-6);
  CHECK_NEAR(v[2].k, 54.37855088251936, 1e-9);
}

TEST(batch_cases_vary) {
  auto v = generateCases(smallCfg(200));
  std::set<double> ks;
  for (auto& s : v) ks.insert(s.k);
  CHECK(ks.size() > 190);
}

TEST(batch_serial_equals_individual_solves) {
  BatchConfig c = smallCfg(50, 25);
  auto cases = generateCases(c);
  std::vector<double> out(cases.size() * batchStride(c));
  CHECK(solveBatchSerial(c, cases, out.data(), BatchControl{}));
  for (size_t i = 0; i < cases.size(); ++i) {
    RodParams p = c.base; p.k = cases[i].k; p.q = cases[i].q;
    RodSolution s = solveRod(p, c.elements);
    for (int j = 0; j <= c.elements; ++j) CHECK(out[i * batchStride(c) + j] == s.T[j]);
  }
}

TEST(batch_parallel_matches_serial_for_1_2_3_4_workers) {
  BatchConfig c = smallCfg(777, 60);  // not a multiple of the chunk size
  auto cases = generateCases(c);
  std::vector<double> ref(cases.size() * batchStride(c)), out(ref.size());
  CHECK(solveBatchSerial(c, cases, ref.data(), BatchControl{}));
  for (int w : {1, 2, 3, 4}) {
    ThreadPool pool(w);
    std::fill(out.begin(), out.end(), -999.0);
    CHECK(solveBatchParallel(c, cases, out.data(), pool, BatchControl{}));
    double maxDiff = 0;
    for (size_t i = 0; i < ref.size(); ++i) maxDiff = std::max(maxDiff, std::fabs(out[i] - ref[i]));
    CHECK_MSG(maxDiff <= 1e-8, "workers=" + std::to_string(w));
  }
}

TEST(batch_progress_counter_reaches_total) {
  BatchConfig c = smallCfg(500, 20);
  auto cases = generateCases(c);
  std::vector<double> out(cases.size() * batchStride(c));
  std::atomic<std::uint64_t> done{0};
  BatchControl ctl;
  ctl.done = &done;
  CHECK(solveBatchSerial(c, cases, out.data(), ctl));
  CHECK(done == 500);
  done = 0;
  ThreadPool pool(3);
  CHECK(solveBatchParallel(c, cases, out.data(), pool, ctl));
  CHECK(done == 500);
}

TEST(batch_cancel_stops_cleanly) {
  BatchConfig c = smallCfg(2000, 50);
  auto cases = generateCases(c);
  std::vector<double> out(cases.size() * batchStride(c));
  std::atomic<bool> cancel{true};
  BatchControl ctl;
  ctl.cancel = &cancel;
  CHECK(!solveBatchSerial(c, cases, out.data(), ctl));
  ThreadPool pool(2);
  CHECK(!solveBatchParallel(c, cases, out.data(), pool, ctl));
  // the pool must remain usable after a cancelled run
  cancel = false;
  CHECK(solveBatchParallel(c, cases, out.data(), pool, ctl));
}

TEST(thread_pool_runs_every_worker_once_and_is_reusable) {
  ThreadPool pool(4);
  CHECK(pool.workers() == 4);
  for (int round = 0; round < 50; ++round) {
    std::atomic<int> mask{0}, calls{0};
    pool.runOnAll([&](int w) { mask |= (1 << w); ++calls; });
    CHECK(mask == 0xF);
    CHECK(calls == 4);
  }
}

TEST(thread_pool_propagates_exceptions_and_survives) {
  ThreadPool pool(3);
  CHECK_THROWS(pool.runOnAll([](int w) { if (w == 1) throw std::runtime_error("boom"); }));
  std::atomic<int> n{0};
  pool.runOnAll([&](int) { ++n; });
  CHECK(n == 3);
  CHECK_THROWS(ThreadPool(0));
}

TEST(batch_validation_rejects_bad_config) {
  BatchConfig c = smallCfg();
  CHECK(validateBatch(c).empty());
  BatchConfig b = c; b.cases = 0;            CHECK(!validateBatch(b).empty());
  b = c; b.cases = 2000000;                  CHECK(!validateBatch(b).empty());
  b = c; b.elements = 0;                     CHECK(!validateBatch(b).empty());
  b = c; b.k_min = 0;                        CHECK(!validateBatch(b).empty());
  b = c; b.k_min = -5;                       CHECK(!validateBatch(b).empty());
  b = c; b.k_max = b.k_min - 1;              CHECK(!validateBatch(b).empty());
  b = c; b.q_min = -1;                       CHECK(!validateBatch(b).empty());
  b = c; b.q_max = NAN;                      CHECK(!validateBatch(b).empty());
  b = c; b.cases = 1000000; b.elements = 1000; CHECK(CONTAINS(validateBatch(b), "memory"));
}

TEST(benchmark_reports_median_speedup_efficiency_and_agreement) {
  BenchConfig bc;
  bc.batch = smallCfg(400, 100);
  bc.warmup = 1;
  bc.repetitions = 3;
  bc.workers = {1, 2, 4};
  BenchReport r = runBenchmark(bc, nullptr, nullptr, nullptr);
  CHECK_MSG(r.completed, r.error);
  CHECK(r.rows.size() >= 2);
  CHECK(r.rows[0].implementation == "serial");
  CHECK(r.rows[0].speedup == 1.0);
  for (auto& row : r.rows) {
    CHECK(row.times_s.size() == 3);
    CHECK(row.median_s > 0);
    CHECK(row.agrees);
    CHECK(row.max_abs_diff_C <= 1e-8);
    CHECK(row.workers <= std::max(2, r.sys.logical_cores));  // never above logical CPUs (except 1-CPU demo)
    CHECK_NEAR(row.efficiency, row.speedup / row.workers, 1e-12);
  }
  CHECK(CONTAINS(benchCsv(r), "median_s"));
  CHECK(CONTAINS(benchTimesCsv(r), "repetition"));
}

TEST(benchmark_rejects_bad_parameters_and_can_be_cancelled) {
  BenchConfig bc;
  bc.batch = smallCfg();
  bc.repetitions = 0;
  CHECK(!runBenchmark(bc, nullptr, nullptr, nullptr).error.empty());
  bc.repetitions = 3;
  bc.warmup = 99;
  CHECK(!runBenchmark(bc, nullptr, nullptr, nullptr).error.empty());
  bc.warmup = 0;
  std::atomic<bool> cancel{true};
  BenchReport r = runBenchmark(bc, &cancel, nullptr, nullptr);
  CHECK(r.cancelled);
  CHECK(!r.completed);
}
