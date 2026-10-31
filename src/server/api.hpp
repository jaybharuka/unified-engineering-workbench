// HTTP/JSON transport layer. Contains no numerical code: every route validates input, calls the
// wb_core library and serialises the result.
#pragma once
#include <atomic>
#include <chrono>
#include <functional>
#include <string>

#include "hardware.hpp"
#include "httplib.h"
#include "jobs.hpp"

namespace wbsrv {

struct ApiContext {
  std::string webDir;
  std::string dataDir;
  int port = 0;
  JobManager jobs;
  wb::DeviceManager devices;
  std::chrono::steady_clock::time_point processStart = std::chrono::steady_clock::now();
  double readyMs = 0.0;            // process start -> listening
  double launchToReadyMs = -1.0;   // Launch.exe start -> listening (only when started by the launcher)
  std::atomic<long long> lastHeartbeatMs{0};  // ms since processStart (0 = never)
  std::atomic<long long> closingAtMs{0};      // ms since processStart when the UI said it is closing (0 = not closing)
  std::function<void()> requestShutdown;
};

void registerRoutes(httplib::Server& svr, ApiContext& ctx);

}  // namespace wbsrv
