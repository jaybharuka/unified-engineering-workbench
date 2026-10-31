#include "batch.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <sstream>

#include "util.hpp"

namespace wb {
namespace {

// splitmix64: tiny, portable, fully specified => identical case lists on every platform/compiler.
struct SplitMix64 {
  std::uint64_t s;
  std::uint64_t next() {
    std::uint64_t z = (s += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
  }
  double uniform01() { return static_cast<double>(next() >> 11) * (1.0 / 9007199254740992.0); }  // [0,1)
};

RodParams caseParams(const BatchConfig& c, const CaseSpec& cs) {
  RodParams p = c.base;
  p.k = cs.k;
  p.q = cs.q;
  return p;
}

constexpr std::size_t kChunk = 32;

double elapsedSeconds(std::chrono::steady_clock::time_point a, std::chrono::steady_clock::time_point b) {
  return std::chrono::duration<double>(b - a).count();
}

double medianOf(std::vector<double> v) {
  std::sort(v.begin(), v.end());
  std::size_t n = v.size();
  if (n == 0) return 0.0;
  return (n % 2) ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

}  // namespace

std::string validateBatch(const BatchConfig& c) {
  if (c.cases < 1 || c.cases > 1000000) return "Case count must be between 1 and 1,000,000.";
  if (c.elements < 1 || c.elements > kMaxElements) return "Mesh size (elements per case) must be between 1 and " + std::to_string(kMaxElements) + ".";
  if (!std::isfinite(c.k_min) || !std::isfinite(c.k_max) || c.k_min <= 0.0 || c.k_max < c.k_min)
    return "Conductivity range must satisfy 0 < k_min <= k_max.";
  if (!std::isfinite(c.q_min) || !std::isfinite(c.q_max) || c.q_min <= 0.0 || c.q_max < c.q_min)
    return "Heat-generation range must satisfy 0 < q_min <= q_max.";
  std::uint64_t doubles = static_cast<std::uint64_t>(c.cases) * (static_cast<std::uint64_t>(c.elements) + 1);
  if (doubles > kMaxBatchDoubles)
    return "cases x (elements+1) = " + std::to_string(doubles) + " exceeds the memory cap of " + std::to_string(kMaxBatchDoubles) +
           ". Reduce the case count or mesh size.";
  RodParams p = c.base;
  p.k = c.k_min;
  p.q = c.q_min;
  return validateRod(p, c.elements);
}

std::vector<CaseSpec> generateCases(const BatchConfig& c) {
  SplitMix64 rng{c.seed};
  std::vector<CaseSpec> v(static_cast<std::size_t>(c.cases));
  for (auto& cs : v) {
    double u1 = rng.uniform01(), u2 = rng.uniform01();
    cs.k = c.k_min + u1 * (c.k_max - c.k_min);
    cs.q = c.q_min + u2 * (c.q_max - c.q_min);
  }
  return v;
}

bool solveBatchSerial(const BatchConfig& c, const std::vector<CaseSpec>& cases, double* out, const BatchControl& ctl) {
  RodWorkspace ws;
  const std::size_t stride = batchStride(c);
  for (std::size_t i = 0; i < cases.size(); ++i) {
    if ((i % kChunk) == 0 && ctl.cancel && ctl.cancel->load(std::memory_order_relaxed)) return false;
    solveRodInto(caseParams(c, cases[i]), c.elements, out + i * stride, ws);
    if (ctl.done && ((i + 1) % kChunk == 0 || i + 1 == cases.size()))
      ctl.done->fetch_add((i + 1) % kChunk == 0 ? kChunk : (i + 1) % kChunk, std::memory_order_relaxed);
  }
  return true;
}

bool solveBatchParallel(const BatchConfig& c, const std::vector<CaseSpec>& cases, double* out, ThreadPool& pool,
                        const BatchControl& ctl) {
  const std::size_t stride = batchStride(c);
  const std::size_t n = cases.size();
  std::atomic<std::size_t> next{0};
  std::atomic<bool> cancelled{false};
  pool.runOnAll([&](int /*worker*/) {
    RodWorkspace ws;  // per-worker scratch: no sharing, no locks in the hot loop
    while (true) {
      if (ctl.cancel && ctl.cancel->load(std::memory_order_relaxed)) { cancelled = true; return; }
      std::size_t begin = next.fetch_add(kChunk, std::memory_order_relaxed);
      if (begin >= n) return;
      std::size_t end = std::min(n, begin + kChunk);
      for (std::size_t i = begin; i < end; ++i)
        solveRodInto(caseParams(c, cases[i]), c.elements, out + i * stride, ws);  // each case writes its own slot => input order
      if (ctl.done) ctl.done->fetch_add(end - begin, std::memory_order_relaxed);
    }
  });
  return !cancelled.load();
}

BenchReport runBenchmark(const BenchConfig& cfg, const std::atomic<bool>* cancel, BenchProgress* progress,
                         const std::function<void(const std::string&)>& stageCb) {
  BenchReport rep;
  rep.config = cfg;
  rep.sys = querySysInfo();

  std::string err = validateBatch(cfg.batch);
  if (err.empty() && (cfg.warmup < 0 || cfg.warmup > 10)) err = "Warm-up runs must be between 0 and 10.";
  if (err.empty() && (cfg.repetitions < 1 || cfg.repetitions > 25)) err = "Repetitions must be between 1 and 25.";
  if (!err.empty()) { rep.error = err; return rep; }

  // Worker counts: requested list, positive, unique, capped at logical CPUs.
  const int logical = rep.sys.logical_cores;
  std::vector<int> ws;
  for (int w : cfg.workers) {
    if (w < 1) continue;
    int capped = std::min(w, logical);
    if (capped != w) rep.notes.push_back("Requested " + std::to_string(w) + " workers; capped at " + std::to_string(logical) + " logical CPUs.");
    if (std::find(ws.begin(), ws.end(), capped) == ws.end()) ws.push_back(capped);
  }
  if (std::find(ws.begin(), ws.end(), 1) == ws.end()) ws.push_back(1);
  std::sort(ws.begin(), ws.end());
  bool demoOnly = false;
  if (logical == 1) {
    ws = {1, 2};
    demoOnly = true;
    rep.notes.push_back("Only one logical CPU is available: the 2-worker pool is run to demonstrate the parallel code path, "
                        "but speedup cannot be assessed on this machine.");
  }
  rep.workers_used = ws;
  if (rep.sys.physical_cores < logical)
    rep.notes.push_back("Logical CPUs (" + std::to_string(logical) + ") exceed physical cores (" + std::to_string(rep.sys.physical_cores) +
                        "): workers beyond the physical core count share execution units (SMT), so efficiency drops.");

  const std::vector<CaseSpec> cases = generateCases(cfg.batch);
  const std::size_t total = cases.size() * batchStride(cfg.batch);
  std::vector<double> ref(total), out(total);

  const std::uint64_t passesPerConfig = static_cast<std::uint64_t>(cfg.warmup + cfg.repetitions);
  if (progress) {
    progress->done = 0;
    progress->total = passesPerConfig * (1 + ws.size()) * cases.size();
  }
  BatchControl ctl;
  ctl.cancel = cancel;
  ctl.done = progress ? &progress->done : nullptr;
  auto cancelled = [&] { return cancel && cancel->load(); };

  using clock = std::chrono::steady_clock;

  // ---- serial baseline (T1) ----
  if (stageCb) stageCb("serial baseline");
  BenchRow serial;
  serial.workers = 1;
  serial.implementation = "serial";
  for (int r = 0; r < cfg.warmup + cfg.repetitions; ++r) {
    auto t0 = clock::now();
    bool ok = solveBatchSerial(cfg.batch, cases, ref.data(), ctl);
    auto t1 = clock::now();
    if (!ok || cancelled()) { rep.cancelled = true; return rep; }
    if (r >= cfg.warmup) serial.times_s.push_back(elapsedSeconds(t0, t1));
  }
  serial.median_s = medianOf(serial.times_s);
  serial.speedup = 1.0;
  serial.efficiency = 1.0;
  serial.agrees = true;
  rep.rows.push_back(serial);
  double checksum = 0.0;
  for (double v : ref) checksum += v;
  rep.serial_checksum = checksum;

  // ---- pool runs ----
  for (int p : ws) {
    if (stageCb) stageCb("pool with " + std::to_string(p) + " worker(s)");
    ThreadPool pool(p);  // created outside the timed region
    BenchRow row;
    row.workers = p;
    row.implementation = "pool";
    row.oversubscribed = p > logical;
    for (int r = 0; r < cfg.warmup + cfg.repetitions; ++r) {
      auto t0 = clock::now();
      bool ok = solveBatchParallel(cfg.batch, cases, out.data(), pool, ctl);
      auto t1 = clock::now();
      if (!ok || cancelled()) { rep.cancelled = true; return rep; }
      if (r >= cfg.warmup) row.times_s.push_back(elapsedSeconds(t0, t1));
      // Agreement check is outside the timed region.
      double diff = 0.0;
      for (std::size_t i = 0; i < total; ++i) diff = std::max(diff, std::fabs(out[i] - ref[i]));
      row.max_abs_diff_C = std::max(row.max_abs_diff_C, diff);
    }
    row.median_s = medianOf(row.times_s);
    row.speedup = row.median_s > 0.0 ? serial.median_s / row.median_s : 0.0;
    row.efficiency = row.speedup / p;
    row.agrees = row.max_abs_diff_C <= 1e-8;
    rep.rows.push_back(row);
  }
  (void)demoOnly;
  rep.completed = true;
  return rep;
}

std::string benchCsv(const BenchReport& r) {
  std::ostringstream o;
  o << "implementation,workers,median_s,speedup_T1_over_Tp,efficiency,max_abs_diff_C,agrees_1e-8,oversubscribed\n";
  for (const auto& b : r.rows)
    o << b.implementation << ',' << b.workers << ',' << formatDouble(b.median_s, 8) << ',' << formatDouble(b.speedup, 6) << ','
      << formatDouble(b.efficiency, 6) << ',' << formatDouble(b.max_abs_diff_C, 6) << ',' << (b.agrees ? "yes" : "NO") << ','
      << (b.oversubscribed ? "yes" : "no") << '\n';
  return o.str();
}

std::string benchTimesCsv(const BenchReport& r) {
  std::ostringstream o;
  o << "implementation,workers,repetition,time_s\n";
  for (const auto& b : r.rows)
    for (std::size_t i = 0; i < b.times_s.size(); ++i)
      o << b.implementation << ',' << b.workers << ',' << (i + 1) << ',' << formatDouble(b.times_s[i], 8) << '\n';
  return o.str();
}

}  // namespace wb
