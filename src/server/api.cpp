#include "api.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <thread>

#include "ansys_csv.hpp"
#include "backprop.hpp"
#include "batch.hpp"
#include "fea1d.hpp"
#include "stl_parser.hpp"
#include "sysinfo.hpp"
#include "util.hpp"

namespace fs = std::filesystem;
using wb::Json;

namespace wbsrv {
namespace {

constexpr const char* kVersion = "1.0.0";
constexpr std::size_t kMaxDisplayTriangles = 250000;
constexpr std::size_t kMaxDisplayPoints = 1000000;

void sendJson(httplib::Response& res, int status, const Json& j) {
  res.status = status;
  res.set_content(j.dump(), "application/json");
}

void sendError(httplib::Response& res, int status, const std::string& message, const std::string& code = "BAD_REQUEST",
               const std::string& hint = "", const Json* extra = nullptr) {
  Json j = Json::object();
  j["error"] = message;
  j["code"] = code;
  if (!hint.empty()) j["hint"] = hint;
  if (extra) j["details"] = *extra;
  sendJson(res, status, j);
}

bool readJson(const httplib::Request& req, httplib::Response& res, Json& out) {
  if (req.body.empty()) { out = Json::object(); return true; }
  try {
    out = Json::parse(req.body);
  } catch (const wb::JsonParseError& e) {
    sendError(res, 400, std::string("The request body is not valid JSON. ") + e.what(), "BAD_JSON");
    return false;
  }
  if (!out.isObject()) { sendError(res, 400, "The request body must be a JSON object.", "BAD_JSON"); return false; }
  return true;
}

// Reads typed fields from a JSON object and remembers the first problem.
class Fields {
 public:
  explicit Fields(const Json& j) : j_(j) {}
  double num(const char* key, double def) {
    const Json* v = j_.find(key);
    if (!v || v->isNull()) return def;
    if (!v->isNumber()) { fail(std::string("Field '") + key + "' must be a number."); return def; }
    return v->asNumber();
  }
  int integer(const char* key, int def, double lo, double hi) {
    const Json* v = j_.find(key);
    if (!v || v->isNull()) return def;
    if (!v->isNumber() || v->asNumber() != std::floor(v->asNumber()) || v->asNumber() < lo || v->asNumber() > hi) {
      fail(std::string("Field '") + key + "' must be a whole number between " + wb::formatDouble(lo, 12) + " and " + wb::formatDouble(hi, 12) + ".");
      return def;
    }
    return static_cast<int>(v->asNumber());
  }
  std::string str(const char* key, const std::string& def) {
    const Json* v = j_.find(key);
    if (!v || v->isNull()) return def;
    if (!v->isString()) { fail(std::string("Field '") + key + "' must be text."); return def; }
    return v->asString();
  }
  std::vector<int> intList(const char* key, const std::vector<int>& def, std::size_t maxCount, double lo, double hi) {
    const Json* v = j_.find(key);
    if (!v || v->isNull()) return def;
    if (!v->isArray() || v->size() == 0 || v->size() > maxCount) {
      fail(std::string("Field '") + key + "' must be a list of 1 to " + std::to_string(maxCount) + " whole numbers.");
      return def;
    }
    std::vector<int> out;
    for (std::size_t i = 0; i < v->size(); ++i) {
      const Json& e = v->at(i);
      if (!e.isNumber() || e.asNumber() != std::floor(e.asNumber()) || e.asNumber() < lo || e.asNumber() > hi) {
        fail(std::string("Every entry of '") + key + "' must be a whole number between " + wb::formatDouble(lo, 12) + " and " + wb::formatDouble(hi, 12) + ".");
        return def;
      }
      out.push_back(static_cast<int>(e.asNumber()));
    }
    return out;
  }
  bool ok() const { return err_.empty(); }
  const std::string& error() const { return err_; }

 private:
  void fail(const std::string& m) { if (err_.empty()) err_ = m; }
  const Json& j_;
  std::string err_;
};

wb::RodParams readRod(Fields& f) {
  wb::RodParams p;
  p.L = f.num("L", p.L);
  p.A = f.num("A", p.A);
  p.k = f.num("k", p.k);
  p.q = f.num("q", p.q);
  p.T0 = f.num("T0", p.T0);
  p.TL = f.num("TL", p.TL);
  return p;
}

Json rodToJson(const wb::RodParams& p) {
  Json j = Json::object();
  j["L_m"] = p.L; j["A_m2"] = p.A; j["k_W_per_mK"] = p.k; j["q_W_per_m3"] = p.q; j["T0_C"] = p.T0; j["TL_C"] = p.TL;
  return j;
}

Json vec3(const double* v) { return Json::numberArray({v[0], v[1], v[2]}); }

Json strList(const std::vector<std::string>& v) {
  Json a = Json::array();
  for (const auto& s : v) a.push(s);
  return a;
}

Json meshToJson(const wb::StlResult& r, double parseMs) {
  const wb::Mesh& m = r.mesh;
  Json j = Json::object();
  j["name"] = m.name;
  j["triangles"] = m.triangleCount();
  j["vertices"] = m.triangleCount() * 3;
  j["bbox_min_m"] = vec3(m.bbox_min);
  j["bbox_max_m"] = vec3(m.bbox_max);
  j["surface_area_m2"] = m.surface_area;
  j["degenerate_triangles"] = m.degenerate_triangles;
  j["parse_ms"] = parseMs;
  j["warnings"] = strList(r.warnings);
  j["units_note"] = "STL has no unit field; coordinates are displayed as-is and assumed to be metres.";
  std::size_t tris = m.triangleCount();
  std::size_t stride = tris > kMaxDisplayTriangles ? (tris + kMaxDisplayTriangles - 1) / kMaxDisplayTriangles : 1;
  if (stride == 1) {
    j["positions_b64"] = wb::base64Encode(m.positions.data(), m.positions.size() * sizeof(float));
    j["display_triangles"] = tris;
  } else {
    std::vector<float> sub;
    sub.reserve(kMaxDisplayTriangles * 9 + 9);
    for (std::size_t t = 0; t < tris; t += stride) sub.insert(sub.end(), m.positions.begin() + t * 9, m.positions.begin() + t * 9 + 9);
    j["positions_b64"] = wb::base64Encode(sub.data(), sub.size() * sizeof(float));
    j["display_triangles"] = sub.size() / 9;
    j["warnings"].push("Display is decimated to every " + std::to_string(stride) + "th triangle (" + std::to_string(sub.size() / 9) +
                       " of " + std::to_string(tris) + ") to keep the viewer responsive. Statistics use the full mesh.");
  }
  return j;
}

Json cloudToJson(const wb::CsvResult& r, double parseMs) {
  const wb::NodeCloud& c = r.cloud;
  Json j = Json::object();
  const std::size_t n = c.count();
  j["nodes"] = n;
  j["bbox_min_m"] = vec3(c.bbox_min);
  j["bbox_max_m"] = vec3(c.bbox_max);
  j["t_min_C"] = c.t_min;
  j["t_max_C"] = c.t_max;
  j["units"] = "x,y,z in metres; temperature in degrees Celsius";
  j["parse_ms"] = parseMs;
  j["warnings"] = strList(r.warnings);
  std::size_t stride = n > kMaxDisplayPoints ? (n + kMaxDisplayPoints - 1) / kMaxDisplayPoints : 1;
  std::vector<float> xyz, T;
  std::vector<std::int32_t> ids;
  for (std::size_t i = 0; i < n; i += stride) {
    xyz.push_back(static_cast<float>(c.x[i]));
    xyz.push_back(static_cast<float>(c.y[i]));
    xyz.push_back(static_cast<float>(c.z[i]));
    T.push_back(static_cast<float>(c.T[i]));
    ids.push_back(c.id[i]);
  }
  j["display_nodes"] = ids.size();
  if (stride > 1)
    j["warnings"].push("Display is decimated to every " + std::to_string(stride) + "th node (" + std::to_string(ids.size()) + " of " +
                       std::to_string(n) + "). Statistics use all nodes.");
  j["xyz_b64"] = wb::base64Encode(xyz.data(), xyz.size() * sizeof(float));
  j["t_b64"] = wb::base64Encode(T.data(), T.size() * sizeof(float));
  j["id_b64"] = wb::base64Encode(ids.data(), ids.size() * sizeof(std::int32_t));
  return j;
}

double msSince(std::chrono::steady_clock::time_point t) {
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count();
}

bool isSafeFileName(const std::string& n) {
  if (n.empty() || n.size() > 128) return false;
  for (char c : n)
    if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '.')) return false;
  return n.find("..") == std::string::npos;
}

std::string extLower(const std::string& n) {
  auto p = n.rfind('.');
  if (p == std::string::npos) return "";
  std::string e = n.substr(p);
  for (char& c : e) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return e;
}

Json thermalResponse(const wb::RodParams& p, const std::vector<int>& meshes) {
  Json out = Json::object();
  out["parameters"] = rodToJson(p);
  out["equation"] = "-d/dx(k A dT/dx) = q A ;  T_exact(x) = T0 + (TL-T0) x/L + q/(2k) x (L-x)";
  Json fine = Json::object();
  std::vector<double> fx, ft;
  for (int i = 0; i <= 200; ++i) {
    double x = p.L * i / 200.0;
    fx.push_back(x);
    ft.push_back(wb::exactTemperature(p, x));
  }
  fine["x_m"] = Json::numberArray(fx);
  fine["T_exact_C"] = Json::numberArray(ft);
  out["exact_curve"] = fine;

  Json runs = Json::array();
  for (int n : meshes) {
    wb::RodSolution s = wb::solveRod(p, n);
    wb::ErrorMetrics m = wb::computeErrors(p, s);
    Json r = Json::object();
    r["n_elements"] = n;
    r["h_m"] = s.h;
    r["x_m"] = Json::numberArray(s.x);
    r["T_fe_C"] = Json::numberArray(s.T);
    std::vector<double> ex(s.x.size()), mx, mfe, mex;
    for (std::size_t i = 0; i < s.x.size(); ++i) ex[i] = wb::exactTemperature(p, s.x[i]);
    for (int e = 0; e < n; ++e) {
      double xm = 0.5 * (s.x[e] + s.x[e + 1]);
      mx.push_back(xm);
      mfe.push_back(wb::feInterpolate(s, xm));
      mex.push_back(wb::exactTemperature(p, xm));
    }
    r["T_exact_nodes_C"] = Json::numberArray(ex);
    r["mid_x_m"] = Json::numberArray(mx);
    r["mid_T_fe_C"] = Json::numberArray(mfe);
    r["mid_T_exact_C"] = Json::numberArray(mex);
    r["max_node_error_C"] = m.max_node_error;
    r["max_midpoint_error_C"] = m.max_midpoint_error;
    r["max_residual_C"] = wb::maxInteriorResidual(p, s);
    runs.push(r);
  }
  out["runs"] = runs;
  Json conv = Json::array();
  for (const auto& row : wb::convergenceStudy(p, meshes)) {
    Json c = Json::object();
    c["n_elements"] = row.m.n_elements;
    c["h_m"] = row.m.h;
    c["max_node_error_C"] = row.m.max_node_error;
    c["max_midpoint_error_C"] = row.m.max_midpoint_error;
    c["observed_order"] = row.observed_order;
    conv.push(c);
  }
  out["convergence"] = conv;
  return out;
}

int statusForDeviceError(const wb::DeviceError& e) {
  if (e.code() == "TIMEOUT") return 504;
  if (e.code() == "NOT_CONNECTED") return 409;
  if (e.code() == "UNKNOWN_DEVICE") return 404;
  return 400;
}

}  // namespace

void registerRoutes(httplib::Server& svr, ApiContext& ctx) {
  // ---------- security: loopback only, DNS-rebinding and cross-site POST protection ----------
  svr.set_pre_routing_handler([&ctx](const httplib::Request& req, httplib::Response& res) {
    std::string host = req.get_header_value("Host");
    std::string p = std::to_string(ctx.port);
    bool hostOk = host == "127.0.0.1:" + p || host == "localhost:" + p;
    if (!hostOk) {
      sendError(res, 403, "Unexpected Host header.", "FORBIDDEN");
      return httplib::Server::HandlerResponse::Handled;
    }
    if (req.method == "POST" && req.get_header_value("X-WB-Client") != "1") {
      sendError(res, 403, "Missing X-WB-Client header (cross-site request blocked).", "FORBIDDEN");
      return httplib::Server::HandlerResponse::Handled;
    }
    return httplib::Server::HandlerResponse::Unhandled;
  });

  svr.set_exception_handler([](const httplib::Request&, httplib::Response& res, std::exception_ptr ep) {
    std::string msg = "unknown error";
    try { if (ep) std::rethrow_exception(ep); } catch (const std::exception& e) { msg = e.what(); } catch (...) {}
    sendError(res, 500, "Internal error: " + msg, "INTERNAL");
  });

  // ---------- lifecycle ----------
  svr.Get("/api/health", [&ctx](const httplib::Request&, httplib::Response& res) {
    Json j = Json::object();
    j["status"] = "ok";
    j["version"] = kVersion;
    j["ready_ms_since_process_start"] = ctx.readyMs;
    if (ctx.launchToReadyMs >= 0) j["launch_to_ready_ms"] = ctx.launchToReadyMs;
    j["uptime_s"] = msSince(ctx.processStart) / 1000.0;
    sendJson(res, 200, j);
  });
  svr.Post("/api/heartbeat", [&ctx](const httplib::Request&, httplib::Response& res) {
    ctx.lastHeartbeatMs = std::max<long long>(1, static_cast<long long>(msSince(ctx.processStart)));
    ctx.closingAtMs = 0;
    sendJson(res, 200, Json::object());
  });
  svr.Post("/api/closing", [&ctx](const httplib::Request&, httplib::Response& res) {
    ctx.closingAtMs = std::max<long long>(1, static_cast<long long>(msSince(ctx.processStart)));
    sendJson(res, 200, Json::object());
  });
  svr.Post("/api/shutdown", [&ctx](const httplib::Request&, httplib::Response& res) {
    sendJson(res, 200, Json::object());
    if (ctx.requestShutdown) std::thread([&ctx] { ctx.requestShutdown(); }).detach();
  });
  svr.Get("/api/sysinfo", [](const httplib::Request&, httplib::Response& res) { sendJson(res, 200, sysInfoToJson(wb::querySysInfo())); });

  // ---------- samples and file parsing (Task B) ----------
  svr.Get("/api/samples", [&ctx](const httplib::Request&, httplib::Response& res) {
    Json stl = Json::array(), csv = Json::array();
    std::error_code ec;
    for (auto& e : fs::directory_iterator(ctx.dataDir, ec)) {
      if (!e.is_regular_file(ec)) continue;
      std::string name = e.path().filename().string();
      std::string ext = extLower(name);
      Json o = Json::object();
      o["name"] = name;
      o["bytes"] = static_cast<double>(e.file_size(ec));
      if (ext == ".stl") stl.push(o);
      else if (ext == ".csv") csv.push(o);
    }
    Json j = Json::object();
    j["stl"] = stl;
    j["csv"] = csv;
    sendJson(res, 200, j);
  });

  svr.Get("/api/geometry/sample", [&ctx](const httplib::Request& req, httplib::Response& res) {
    std::string name = req.get_param_value("name");
    if (!isSafeFileName(name) || extLower(name) != ".stl") { sendError(res, 400, "Invalid sample name.", "BAD_REQUEST"); return; }
    auto t0 = std::chrono::steady_clock::now();
    wb::StlResult r = wb::loadAsciiStlFile((fs::path(ctx.dataDir) / name).string());
    if (!r.ok) { sendError(res, 422, r.error, "INVALID_STL"); return; }
    sendJson(res, 200, meshToJson(r, msSince(t0)));
  });
  svr.Post("/api/geometry/parse", [](const httplib::Request& req, httplib::Response& res) {
    auto t0 = std::chrono::steady_clock::now();
    wb::StlResult r = wb::parseAsciiStl(req.body);
    if (!r.ok) {
      sendError(res, 422, r.error, "INVALID_STL", "Only ASCII STL ('solid ... facet normal ... vertex ...') is supported.");
      return;
    }
    sendJson(res, 200, meshToJson(r, msSince(t0)));
  });

  svr.Get("/api/results/sample", [&ctx](const httplib::Request& req, httplib::Response& res) {
    std::string name = req.get_param_value("name");
    if (!isSafeFileName(name) || extLower(name) != ".csv") { sendError(res, 400, "Invalid sample name.", "BAD_REQUEST"); return; }
    auto t0 = std::chrono::steady_clock::now();
    wb::CsvResult r = wb::loadAnsysCsvFile((fs::path(ctx.dataDir) / name).string());
    if (!r.ok) {
      Json d = strList(r.problems);
      sendError(res, 422, r.error, "INVALID_CSV", "", &d);
      return;
    }
    sendJson(res, 200, cloudToJson(r, msSince(t0)));
  });
  svr.Post("/api/results/parse", [](const httplib::Request& req, httplib::Response& res) {
    auto t0 = std::chrono::steady_clock::now();
    wb::CsvResult r = wb::parseAnsysCsv(req.body);
    if (!r.ok) {
      Json d = strList(r.problems);
      sendError(res, 422, r.error, "INVALID_CSV", "Required header: node_id,x_m,y_m,z_m,temperature_C (metres and degrees Celsius).", &d);
      return;
    }
    sendJson(res, 200, cloudToJson(r, msSince(t0)));
  });

  // ---------- thermal FEA (Task C) ----------
  svr.Post("/api/thermal/solve", [](const httplib::Request& req, httplib::Response& res) {
    Json body;
    if (!readJson(req, res, body)) return;
    Fields f(body);
    wb::RodParams p = readRod(f);
    std::vector<int> meshes = f.intList("meshes", {10, 20, 40, 80}, 8, 1, 20000);
    if (!f.ok()) { sendError(res, 400, f.error(), "INVALID_PARAMETER"); return; }
    for (int n : meshes) {
      std::string e = wb::validateRod(p, n);
      if (!e.empty()) { sendError(res, 400, e, "INVALID_PARAMETER"); return; }
    }
    sendJson(res, 200, thermalResponse(p, meshes));
  });
  svr.Post("/api/thermal/export", [](const httplib::Request& req, httplib::Response& res) {
    Json body;
    if (!readJson(req, res, body)) return;
    Fields f(body);
    wb::RodParams p = readRod(f);
    std::vector<int> meshes = f.intList("meshes", {10, 20, 40, 80}, 8, 1, 20000);
    std::string kind = f.str("kind", "convergence");
    int n = f.integer("n", meshes.front(), 1, 20000);
    if (!f.ok()) { sendError(res, 400, f.error(), "INVALID_PARAMETER"); return; }
    for (int m : meshes) {
      std::string e = wb::validateRod(p, m);
      if (!e.empty()) { sendError(res, 400, e, "INVALID_PARAMETER"); return; }
    }
    std::string csv, name;
    if (kind == "convergence") { csv = wb::convergenceCsv(wb::convergenceStudy(p, meshes)); name = "thermal_convergence.csv"; }
    else if (kind == "solution" || kind == "midpoints") {
      std::string e = wb::validateRod(p, n);
      if (!e.empty()) { sendError(res, 400, e, "INVALID_PARAMETER"); return; }
      wb::RodSolution s = wb::solveRod(p, n);
      csv = kind == "solution" ? wb::solutionCsv(p, s) : wb::midpointCsv(p, s);
      name = std::string(kind == "solution" ? "thermal_solution_N" : "thermal_midpoints_N") + std::to_string(n) + ".csv";
    } else { sendError(res, 400, "Field 'kind' must be convergence, solution or midpoints.", "INVALID_PARAMETER"); return; }
    res.set_header("Content-Disposition", "attachment; filename=\"" + name + "\"");
    res.set_content(csv, "text/csv");
  });

  // ---------- performance (Task D) ----------
  svr.Post("/api/jobs/benchmark", [&ctx](const httplib::Request& req, httplib::Response& res) {
    Json body;
    if (!readJson(req, res, body)) return;
    Fields f(body);
    wb::BenchConfig bc;
    bc.batch.cases = f.integer("cases", bc.batch.cases, 1, 1000000);
    bc.batch.elements = f.integer("elements", bc.batch.elements, 1, wb::kMaxElements);
    bc.batch.seed = static_cast<std::uint64_t>(f.integer("seed", static_cast<int>(bc.batch.seed % 2000000000ULL), 0, 2147483647));
    bc.batch.k_min = f.num("k_min", bc.batch.k_min);
    bc.batch.k_max = f.num("k_max", bc.batch.k_max);
    bc.batch.q_min = f.num("q_min", bc.batch.q_min);
    bc.batch.q_max = f.num("q_max", bc.batch.q_max);
    bc.warmup = f.integer("warmup", bc.warmup, 0, 10);
    bc.repetitions = f.integer("repetitions", bc.repetitions, 1, 25);
    bc.workers = f.intList("workers", bc.workers, 8, 1, 64);
    if (!f.ok()) { sendError(res, 400, f.error(), "INVALID_PARAMETER"); return; }
    std::string err;
    std::string id = ctx.jobs.startBenchmark(bc, err);
    if (id.empty()) { sendError(res, 400, err, "INVALID_PARAMETER"); return; }
    Json j = Json::object();
    j["job_id"] = id;
    sendJson(res, 202, j);
  });
  svr.Get(R"(/api/jobs/([A-Za-z0-9_]+))", [&ctx](const httplib::Request& req, httplib::Response& res) {
    Json j;
    if (!ctx.jobs.status(req.matches[1], j)) { sendError(res, 404, "Unknown job id.", "NOT_FOUND"); return; }
    sendJson(res, 200, j);
  });
  svr.Post(R"(/api/jobs/([A-Za-z0-9_]+)/cancel)", [&ctx](const httplib::Request& req, httplib::Response& res) {
    if (!ctx.jobs.cancel(req.matches[1])) { sendError(res, 404, "Unknown job id.", "NOT_FOUND"); return; }
    sendJson(res, 200, Json::object());
  });
  svr.Get(R"(/api/jobs/([A-Za-z0-9_]+)/csv)", [&ctx](const httplib::Request& req, httplib::Response& res) {
    std::string kind = req.get_param_value("kind"), out, err;
    if (!ctx.jobs.csv(req.matches[1], kind, out, err)) { sendError(res, 409, err, "NOT_READY"); return; }
    res.set_header("Content-Disposition", std::string("attachment; filename=\"") + (kind == "repetitions" ? "performance_repetitions.csv" : "performance_summary.csv") + "\"");
    res.set_content(out, "text/csv");
  });

  // ---------- embedded mocks (Task E) ----------
  auto hwCall = [&ctx](httplib::Response& res, const std::function<Json()>& fn) {
    try {
      sendJson(res, 200, fn());
    } catch (const wb::DeviceError& e) {
      Json j = Json::object();
      j["error"] = std::string(e.what());
      j["code"] = e.code();
      j["recoverable"] = e.recoverable();
      j["hint"] = e.hint();
      j["simulated"] = true;
      sendJson(res, statusForDeviceError(e), j);
    }
  };
  svr.Get("/api/hw/devices", [&ctx, hwCall](const httplib::Request&, httplib::Response& res) { hwCall(res, [&] { return ctx.devices.discover(); }); });
  svr.Get("/api/hw/events", [&ctx, hwCall](const httplib::Request&, httplib::Response& res) { hwCall(res, [&] { return ctx.devices.eventLog(); }); });
  svr.Post("/api/hw/connect", [&ctx, hwCall](const httplib::Request& req, httplib::Response& res) {
    Json b; if (!readJson(req, res, b)) return; Fields f(b); std::string id = f.str("id", "");
    if (!f.ok()) { sendError(res, 400, f.error()); return; }
    hwCall(res, [&] { return ctx.devices.connect(id); });
  });
  svr.Post("/api/hw/disconnect", [&ctx, hwCall](const httplib::Request& req, httplib::Response& res) {
    Json b; if (!readJson(req, res, b)) return; Fields f(b); std::string id = f.str("id", "");
    if (!f.ok()) { sendError(res, 400, f.error()); return; }
    hwCall(res, [&] { return ctx.devices.disconnect(id); });
  });
  svr.Post("/api/hw/telemetry", [&ctx, hwCall](const httplib::Request& req, httplib::Response& res) {
    Json b; if (!readJson(req, res, b)) return; Fields f(b); std::string id = f.str("id", "");
    if (!f.ok()) { sendError(res, 400, f.error()); return; }
    hwCall(res, [&] { return ctx.devices.telemetry(id); });
  });
  svr.Post("/api/hw/fault", [&ctx, hwCall](const httplib::Request& req, httplib::Response& res) {
    Json b; if (!readJson(req, res, b)) return; Fields f(b);
    std::string id = f.str("id", ""), kind = f.str("kind", "timeout");
    int count = f.integer("count", 1, 1, 5);
    if (!f.ok()) { sendError(res, 400, f.error()); return; }
    hwCall(res, [&] { return ctx.devices.armFault(id, kind, count); });
  });

  // ---------- backpropagation (Task F) ----------
  svr.Post("/api/backprop/run", [](const httplib::Request& req, httplib::Response& res) {
    Json body;
    if (!readJson(req, res, body)) return;
    Fields f(body);
    double x = f.num("x", 1.0), y = f.num("y", 2.0), lr = f.num("lr", 0.1);
    wb::BpParams p;
    p.w = f.num("w", p.w); p.b = f.num("b", p.b); p.v = f.num("v", p.v); p.c = f.num("c", p.c);
    int steps = f.integer("steps", 100, 1, 10000);
    if (!f.ok()) { sendError(res, 400, f.error(), "INVALID_PARAMETER"); return; }
    for (double v : {x, y, p.w, p.b, p.v, p.c})
      if (!std::isfinite(v)) { sendError(res, 400, "All inputs must be finite numbers.", "INVALID_PARAMETER"); return; }
    if (!(lr > 0.0 && lr <= 10.0)) { sendError(res, 400, "Learning rate must be in (0, 10].", "INVALID_PARAMETER"); return; }
    wb::BpForward fw = wb::bpForward(p, x, y);
    wb::BpGradCheck chk = wb::bpCheck(p, x, y, 1e-6, 1e-5);
    wb::BpTrace tr = wb::bpTrain(p, x, y, lr, steps);
    Json j = Json::object();
    Json fwd = Json::object();
    fwd["z"] = fw.z; fwd["h"] = fw.h; fwd["y_hat"] = fw.y_hat; fwd["error"] = fw.error; fwd["loss"] = fw.loss;
    j["forward"] = fwd;
    auto g = [](const wb::BpGrad& v) { Json o = Json::object(); o["dJ_dw"] = v.dw; o["dJ_db"] = v.db; o["dJ_dv"] = v.dv; o["dJ_dc"] = v.dc; return o; };
    j["analytic_gradient"] = g(chk.analytic);
    j["numeric_gradient"] = g(chk.numeric);
    Json errs = Json::object();
    errs["w"] = chk.abs_err_w; errs["b"] = chk.abs_err_b; errs["v"] = chk.abs_err_v; errs["c"] = chk.abs_err_c; errs["max"] = chk.max_abs_err;
    j["abs_error"] = errs;
    j["gradient_check_pass"] = chk.pass;
    j["tolerance"] = 1e-5;
    j["epsilon"] = 1e-6;
    const wb::BpParams& u = tr.params.at(1);
    Json up = Json::object();
    up["w"] = u.w; up["b"] = u.b; up["v"] = u.v; up["c"] = u.c; up["loss"] = tr.loss.at(1);
    j["after_one_update"] = up;
    j["loss"] = Json::numberArray(tr.loss);
    j["note"] = "loss[0] = initial, loss[1] = after the single hand-calculated update, loss[2..] = subsequent steps";
    sendJson(res, 200, j);
  });

  // ---------- static web UI ----------
  if (!svr.set_mount_point("/", ctx.webDir)) {
    svr.Get("/", [](const httplib::Request&, httplib::Response& res) {
      res.set_content("<h1>Web folder not found</h1><p>The 'web' folder must sit next to the program. Re-extract the ZIP completely.</p>", "text/html");
    });
  }
  svr.set_error_handler([](const httplib::Request& req, httplib::Response& res) {
    if (res.status == 404 && req.path.rfind("/api/", 0) == 0) { sendError(res, 404, "Unknown API route.", "NOT_FOUND"); return; }
    if (res.body.empty() && res.status >= 400) sendError(res, res.status, "Request failed.", "ERROR");
  });
}

}  // namespace wbsrv
