// wb_server: local HTTP server hosting the web UI and the JSON API on 127.0.0.1 only.
//   wb_server [--port N] [--port-file path] [--root dir] [--auto-exit] [--launch-epoch-ms N]
// --port 0 (default) picks a free port. --auto-exit makes the server quit when the UI window closes
// (heartbeat watchdog), which is how Launch.exe avoids leaving background processes behind.
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <thread>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include "api.hpp"
#include "util.hpp"

namespace fs = std::filesystem;

namespace {

fs::path exeDirectory(const char* argv0) {
#ifdef _WIN32
  wchar_t buf[MAX_PATH * 4];
  DWORD n = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(sizeof buf / sizeof buf[0]));
  if (n > 0 && n < sizeof buf / sizeof buf[0]) return fs::path(buf).parent_path();
#else
  std::error_code ec;
  fs::path p = fs::read_symlink("/proc/self/exe", ec);
  if (!ec) return p.parent_path();
#endif
  return fs::absolute(fs::path(argv0)).parent_path();
}

// Finds a directory <name> in the exe folder or one of its parents (so it works both from the
// packaged layout and from a build tree).
fs::path findDir(fs::path start, const char* name) {
  for (int i = 0; i < 4; ++i) {
    fs::path c = start / name;
    std::error_code ec;
    if (fs::is_directory(c, ec)) return c;
    if (!start.has_parent_path() || start.parent_path() == start) break;
    start = start.parent_path();
  }
  return fs::path();
}

long long nowEpochMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

}  // namespace

int main(int argc, char** argv) {
  int port = 0;
  std::string portFile, rootOverride;
  bool autoExit = false;
  long long launchEpochMs = 0;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto next = [&]() -> std::string { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
    if (a == "--port") port = std::atoi(next().c_str());
    else if (a == "--port-file") portFile = next();
    else if (a == "--root") rootOverride = next();
    else if (a == "--auto-exit") autoExit = true;
    else if (a == "--launch-epoch-ms") launchEpochMs = std::atoll(next().c_str());
    else if (a == "--help" || a == "-h") {
      std::printf("usage: wb_server [--port N] [--port-file path] [--root dir] [--auto-exit] [--launch-epoch-ms N]\n");
      return 0;
    } else {
      std::fprintf(stderr, "unknown option '%s'\n", a.c_str());
      return 2;
    }
  }
  if (port < 0 || port > 65535) { std::fprintf(stderr, "invalid port\n"); return 2; }

  fs::path exeDir = exeDirectory(argv[0]);
  fs::path web = rootOverride.empty() ? findDir(exeDir, "web") : fs::path(rootOverride) / "web";
  fs::path data = rootOverride.empty() ? findDir(exeDir, "data") : fs::path(rootOverride) / "data";
  if (web.empty()) { std::fprintf(stderr, "error: cannot find the 'web' folder next to the program. Re-extract the whole ZIP.\n"); return 3; }

  wbsrv::ApiContext ctx;
  ctx.webDir = web.string();
  ctx.dataDir = data.empty() ? (exeDir / "data").string() : data.string();

  httplib::Server svr;
  svr.set_payload_max_length(300u * 1024u * 1024u);
  svr.set_read_timeout(60, 0);
  svr.set_write_timeout(60, 0);
  svr.new_task_queue = [] { return new httplib::ThreadPool(16); };

  ctx.requestShutdown = [&svr] { svr.stop(); };

  if (port == 0) {
    port = svr.bind_to_any_port("127.0.0.1");
    if (port < 0) { std::fprintf(stderr, "error: cannot open a local port\n"); return 4; }
  } else if (!svr.bind_to_port("127.0.0.1", port)) {
    std::fprintf(stderr, "error: port %d is already in use. Close the other program or use --port 0.\n", port);
    return 4;
  }
  ctx.port = port;
  wbsrv::registerRoutes(svr, ctx);

  ctx.readyMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - ctx.processStart).count();
  if (launchEpochMs > 0) ctx.launchToReadyMs = static_cast<double>(nowEpochMs() - launchEpochMs);

  if (!portFile.empty()) {
    std::string err;
    if (!wb::writeTextFile(portFile, std::to_string(port), err)) std::fprintf(stderr, "warning: %s\n", err.c_str());
  }
  std::printf("Workbench server listening on http://127.0.0.1:%d/  (web: %s)\n", port, ctx.webDir.c_str());
  std::fflush(stdout);

  // Watchdog: exit when the UI window is gone (only with --auto-exit).
  std::atomic<bool> stopWatch{false};
  std::thread watchdog;
  if (autoExit) {
    watchdog = std::thread([&] {
      using namespace std::chrono;
      while (!stopWatch) {
        std::this_thread::sleep_for(milliseconds(500));
        long long now = static_cast<long long>(duration<double, std::milli>(steady_clock::now() - ctx.processStart).count());
        long long last = ctx.lastHeartbeatMs.load(), closing = ctx.closingAtMs.load();
        bool quit = false;
        if (closing > 0 && now - closing > 4000) quit = true;              // window closed, no reload followed
        else if (last > 0 && now - last > 180000) quit = true;             // no heartbeat for 3 minutes
        else if (last == 0 && now > 300000) quit = true;                   // UI never loaded
        if (quit) { svr.stop(); return; }
      }
    });
  }

  bool ok = svr.listen_after_bind();
  stopWatch = true;
  if (watchdog.joinable()) watchdog.join();
  return ok ? 0 : 5;
}
