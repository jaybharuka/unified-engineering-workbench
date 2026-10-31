#include "jobs.hpp"

#include <chrono>

namespace wbsrv {

using wb::Json;

Json sysInfoToJson(const wb::SysInfo& s) {
  Json j = Json::object();
  j["cpu"] = s.cpu_name;
  j["logical_cores"] = s.logical_cores;
  j["physical_cores"] = s.physical_cores;
  j["physical_cores_known"] = s.physical_known;
  j["ram_bytes"] = s.ram_bytes;
  j["ram_gib"] = static_cast<double>(s.ram_bytes) / (1024.0 * 1024.0 * 1024.0);
  j["os"] = s.os;
  j["compiler"] = s.compiler;
  j["optimisation_flags"] = s.opt_flags;
  j["build_type"] = s.build_type;
  return j;
}

Json benchReportToJson(const wb::BenchReport& r) {
  Json j = Json::object();
  j["system"] = sysInfoToJson(r.sys);
  Json cfg = Json::object();
  cfg["cases"] = r.config.batch.cases;
  cfg["elements"] = r.config.batch.elements;
  cfg["seed"] = static_cast<double>(r.config.batch.seed);
  cfg["warmup_runs"] = r.config.warmup;
  cfg["repetitions"] = r.config.repetitions;
  cfg["k_range_W_per_mK"] = Json::numberArray({r.config.batch.k_min, r.config.batch.k_max});
  cfg["q_range_W_per_m3"] = Json::numberArray({r.config.batch.q_min, r.config.batch.q_max});
  j["config"] = cfg;
  Json rows = Json::array();
  for (const auto& b : r.rows) {
    Json o = Json::object();
    o["implementation"] = b.implementation;
    o["workers"] = b.workers;
    o["times_s"] = Json::numberArray(b.times_s);
    o["median_s"] = b.median_s;
    o["speedup"] = b.speedup;
    o["efficiency"] = b.efficiency;
    o["max_abs_diff_C"] = b.max_abs_diff_C;
    o["agrees"] = b.agrees;
    o["oversubscribed"] = b.oversubscribed;
    rows.push(o);
  }
  j["rows"] = rows;
  Json notes = Json::array();
  for (const auto& n : r.notes) notes.push(n);
  j["notes"] = notes;
  return j;
}

JobManager::~JobManager() {
  std::lock_guard<std::mutex> lk(m_);
  for (auto& kv : jobs_) kv.second->cancel = true;
  for (auto& kv : jobs_)
    if (kv.second->worker.joinable()) kv.second->worker.join();
}

void JobManager::prune() {
  // m_ held by caller
  std::size_t finished = 0;
  for (auto& kv : jobs_) {
    std::lock_guard<std::mutex> lk(kv.second->m);
    if (kv.second->state != "running") ++finished;
  }
  while (finished > 8) {
    for (auto it = jobs_.begin(); it != jobs_.end(); ++it) {
      bool fin;
      {
        std::lock_guard<std::mutex> lk(it->second->m);
        fin = it->second->state != "running";
      }
      if (fin) {
        if (it->second->worker.joinable()) it->second->worker.join();
        jobs_.erase(it);
        --finished;
        break;
      }
    }
  }
}

std::string JobManager::startBenchmark(const wb::BenchConfig& cfg, std::string& error) {
  std::string v = wb::validateBatch(cfg.batch);
  if (v.empty() && (cfg.warmup < 0 || cfg.warmup > 10)) v = "Warm-up runs must be between 0 and 10.";
  if (v.empty() && (cfg.repetitions < 1 || cfg.repetitions > 25)) v = "Repetitions must be between 1 and 25.";
  if (!v.empty()) { error = v; return std::string(); }

  std::lock_guard<std::mutex> lk(m_);
  for (auto& kv : jobs_) {
    std::lock_guard<std::mutex> jl(kv.second->m);
    if (kv.second->state == "running") {
      error = "A benchmark is already running. Wait for it to finish or cancel it first (running two at once would distort the timings).";
      return std::string();
    }
  }
  prune();
  auto job = std::make_shared<Job>();
  job->id = "job" + std::to_string(++counter_);
  job->cfg = cfg;
  Job* raw = job.get();
  job->worker = std::thread([raw] {
    auto t0 = std::chrono::steady_clock::now();
    try {
      wb::BenchReport rep = wb::runBenchmark(raw->cfg, &raw->cancel, &raw->progress, [raw](const std::string& s) {
        std::lock_guard<std::mutex> lk(raw->m);
        raw->stage = s;
      });
      std::lock_guard<std::mutex> lk(raw->m);
      raw->elapsed_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
      if (!rep.error.empty()) { raw->state = "error"; raw->error = rep.error; }
      else if (rep.cancelled) { raw->state = "cancelled"; raw->stage = "cancelled; no results were kept"; }
      else { raw->state = "done"; raw->stage = "finished"; raw->report = std::move(rep); }
    } catch (const std::exception& e) {
      std::lock_guard<std::mutex> lk(raw->m);
      raw->state = "error";
      raw->error = std::string("Benchmark failed: ") + e.what();
    }
  });
  std::string id = job->id;
  jobs_[id] = job;
  return id;
}

bool JobManager::status(const std::string& id, Json& out) {
  std::shared_ptr<Job> job;
  {
    std::lock_guard<std::mutex> lk(m_);
    auto it = jobs_.find(id);
    if (it == jobs_.end()) return false;
    job = it->second;
  }
  std::lock_guard<std::mutex> lk(job->m);
  out = Json::object();
  out["id"] = job->id;
  out["state"] = job->state;
  out["stage"] = job->stage;
  double done = static_cast<double>(job->progress.done.load()), total = static_cast<double>(job->progress.total.load());
  Json p = Json::object();
  p["done"] = done;
  p["total"] = total;
  p["fraction"] = total > 0 ? done / total : 0.0;
  out["progress"] = p;
  if (!job->error.empty()) out["error"] = job->error;
  if (job->state == "done") {
    out["report"] = benchReportToJson(job->report);
    out["elapsed_s"] = job->elapsed_s;
  }
  return true;
}

bool JobManager::cancel(const std::string& id) {
  std::lock_guard<std::mutex> lk(m_);
  auto it = jobs_.find(id);
  if (it == jobs_.end()) return false;
  it->second->cancel = true;
  return true;
}

bool JobManager::csv(const std::string& id, const std::string& kind, std::string& out, std::string& error) {
  std::shared_ptr<Job> job;
  {
    std::lock_guard<std::mutex> lk(m_);
    auto it = jobs_.find(id);
    if (it == jobs_.end()) { error = "Unknown job id."; return false; }
    job = it->second;
  }
  std::lock_guard<std::mutex> lk(job->m);
  if (job->state != "done") { error = "The job has not finished successfully, so there is nothing to export."; return false; }
  if (kind == "repetitions") out = wb::benchTimesCsv(job->report);
  else out = wb::benchCsv(job->report);
  return true;
}

}  // namespace wbsrv
