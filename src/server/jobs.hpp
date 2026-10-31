// Background job control for long computations (benchmark). Keeps the HTTP threads responsive:
// the UI starts a job, polls status/progress, and may cancel it. A cancelled job produces no results.
#pragma once
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "batch.hpp"
#include "json.hpp"

namespace wbsrv {

class JobManager {
 public:
  ~JobManager();

  // Returns job id, or empty string with `error` set (invalid parameters / another job running).
  std::string startBenchmark(const wb::BenchConfig& cfg, std::string& error);
  bool status(const std::string& id, wb::Json& out);                    // false if unknown id
  bool cancel(const std::string& id);                                   // false if unknown id
  bool csv(const std::string& id, const std::string& kind, std::string& out, std::string& error);

 private:
  struct Job {
    std::string id;
    wb::BenchConfig cfg;
    std::thread worker;
    std::atomic<bool> cancel{false};
    wb::BenchProgress progress;
    std::mutex m;  // guards the fields below
    std::string state = "running";  // running | done | cancelled | error
    std::string stage = "starting";
    std::string error;
    wb::BenchReport report;
    double elapsed_s = 0.0;
  };

  void prune();  // keep memory bounded (<= 8 finished jobs)

  std::mutex m_;
  std::map<std::string, std::shared_ptr<Job>> jobs_;
  unsigned counter_ = 0;
};

wb::Json benchReportToJson(const wb::BenchReport& r);
wb::Json sysInfoToJson(const wb::SysInfo& s);

}  // namespace wbsrv
