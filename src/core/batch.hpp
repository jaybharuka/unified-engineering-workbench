// Task D: batch of independent thermal solves, serial and CPU-parallel, plus benchmark harness.
#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "fea1d.hpp"
#include "sysinfo.hpp"
#include "thread_pool.hpp"

namespace wb {

struct BatchConfig {
  int cases = 20000;
  int elements = 500;
  std::uint64_t seed = 20260101ULL;
  double k_min = 1.0, k_max = 100.0;      // W/(m K), both > 0
  double q_min = 1.0e3, q_max = 1.0e5;    // W/m^3
  RodParams base;                          // L, A, T0, TL (k and q are overridden per case)
};

struct CaseSpec {
  double k = 0.0;
  double q = 0.0;
};

constexpr std::uint64_t kMaxBatchDoubles = 40000000ULL;  // cases * (elements+1) cap (320 MB per result set)

std::string validateBatch(const BatchConfig& c);
std::vector<CaseSpec> generateCases(const BatchConfig& c);  // deterministic for a given seed (portable RNG)

inline std::size_t batchStride(const BatchConfig& c) { return static_cast<std::size_t>(c.elements) + 1; }

struct BatchControl {
  const std::atomic<bool>* cancel = nullptr;   // optional
  std::atomic<std::uint64_t>* done = nullptr;  // optional progress counter (solves finished)
};

// Both return true when all cases were solved, false if cancelled (output then is partial and must be discarded).
// `out` must have cases * (elements+1) doubles. Case i is written at out[i*stride].
bool solveBatchSerial(const BatchConfig& c, const std::vector<CaseSpec>& cases, double* out, const BatchControl& ctl);
bool solveBatchParallel(const BatchConfig& c, const std::vector<CaseSpec>& cases, double* out, ThreadPool& pool,
                        const BatchControl& ctl);

struct BenchConfig {
  BatchConfig batch;
  std::vector<int> workers = {1, 2, 4};  // requested; capped at logical CPUs
  int warmup = 1;
  int repetitions = 5;
};

struct BenchRow {
  int workers = 1;
  std::string implementation;        // "serial" or "pool"
  std::vector<double> times_s;       // measured repetitions (wall-clock compute only)
  double median_s = 0.0;
  double speedup = 0.0;              // T1 / Tp, T1 = serial median
  double efficiency = 0.0;           // speedup / p
  double max_abs_diff_C = 0.0;       // vs serial reference, over all cases and nodes
  bool agrees = false;               // max_abs_diff <= 1e-8
  bool oversubscribed = false;       // workers > logical CPUs (single-CPU demo only)
};

struct BenchReport {
  bool completed = false;
  bool cancelled = false;
  std::string error;
  SysInfo sys;
  BenchConfig config;
  std::vector<int> workers_used;
  std::vector<std::string> notes;
  std::vector<BenchRow> rows;        // row 0 = serial (T1); then pool rows
  double serial_checksum = 0.0;
};

struct BenchProgress {
  std::atomic<std::uint64_t> done{0};
  std::atomic<std::uint64_t> total{0};
  std::string stage;                  // guarded by caller if shared across threads (see jobs.cpp)
};

// progress may be null. stageCb (optional) is invoked from the calling thread when a new stage begins.
BenchReport runBenchmark(const BenchConfig& cfg, const std::atomic<bool>* cancel, BenchProgress* progress,
                         const std::function<void(const std::string&)>& stageCb);

std::string benchCsv(const BenchReport& r);
std::string benchTimesCsv(const BenchReport& r);

}  // namespace wb
