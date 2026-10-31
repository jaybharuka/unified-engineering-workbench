// wb_cli: command-line front end to the same C++ core. Used to regenerate everything in results/ and data/.
//   wb_cli all --out results          regenerate every raw CSV and report text
//   wb_cli gen-samples --out data     write the synthetic STL and CSV samples
//   wb_cli gen-large --dir tests/tmp [--triangles N --rows N]   write large files for the file-size limit tests
//   wb_cli limits                     parse generated large files and print timings (documented in docs/)
//   wb_cli bench [--cases N --elements N --seed N --reps N --warmup N --workers 1,2,4 --out dir]
//   wb_cli sysinfo
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <sstream>
#include <string>

#include "ansys_csv.hpp"
#include "backprop.hpp"
#include "batch.hpp"
#include "fea1d.hpp"
#include "samples.hpp"
#include "stl_parser.hpp"
#include "sysinfo.hpp"
#include "util.hpp"

using namespace wb;

namespace {

std::map<std::string, std::string> parseArgs(int argc, char** argv, int first) {
  std::map<std::string, std::string> a;
  for (int i = first; i < argc; ++i) {
    std::string k = argv[i];
    if (k.rfind("--", 0) == 0) {
      std::string v = (i + 1 < argc && std::string(argv[i + 1]).rfind("--", 0) != 0) ? argv[++i] : "1";
      a[k.substr(2)] = v;
    }
  }
  return a;
}

std::string get(const std::map<std::string, std::string>& a, const char* k, const std::string& d) {
  auto it = a.find(k);
  return it == a.end() ? d : it->second;
}

bool save(const std::string& dir, const std::string& name, const std::string& content) {
  std::string err;
  std::string path = dir + "/" + name;
  if (!writeTextFile(path, content, err)) {
    std::fprintf(stderr, "error: %s\n", err.c_str());
    return false;
  }
  std::printf("wrote %s (%zu bytes)\n", path.c_str(), content.size());
  return true;
}

std::vector<int> parseList(const std::string& s) {
  std::vector<int> v;
  std::stringstream ss(s);
  std::string t;
  while (std::getline(ss, t, ',')) v.push_back(std::atoi(t.c_str()));
  return v;
}

std::string sysInfoText(const SysInfo& s) {
  std::ostringstream o;
  o << "cpu: " << s.cpu_name << "\n"
    << "logical_cores: " << s.logical_cores << "\n"
    << "physical_cores: " << s.physical_cores << (s.physical_known ? "" : " (unknown; assumed equal to logical)") << "\n"
    << "ram_gib: " << formatDouble(static_cast<double>(s.ram_bytes) / (1024.0 * 1024.0 * 1024.0), 4) << "\n"
    << "os: " << s.os << "\n"
    << "compiler: " << s.compiler << "\n"
    << "build_type: " << s.build_type << "\n"
    << "flags: " << s.opt_flags << "\n";
  return o.str();
}

int doGenSamples(const std::string& dir) {
  bool ok = save(dir, "synthetic_rod.stl", generateCylinderStl(1.0, 0.05, 24, 20));
  ok &= save(dir, "synthetic_rod_temperatures.csv", generateSyntheticRodCsv(1.0, 0.05, 40, 3, 12));
  return ok ? 0 : 1;
}

int doBench(const std::map<std::string, std::string>& a) {
  BenchConfig bc;
  bc.batch.cases = std::atoi(get(a, "cases", "20000").c_str());
  bc.batch.elements = std::atoi(get(a, "elements", "500").c_str());
  bc.batch.seed = std::strtoull(get(a, "seed", "20260101").c_str(), nullptr, 10);
  bc.repetitions = std::atoi(get(a, "reps", "5").c_str());
  bc.warmup = std::atoi(get(a, "warmup", "1").c_str());
  bc.workers = parseList(get(a, "workers", "1,2,4"));
  BenchProgress prog;
  BenchReport r = runBenchmark(bc, nullptr, &prog, [](const std::string& s) { std::printf("stage: %s\n", s.c_str()); });
  if (!r.error.empty()) { std::fprintf(stderr, "error: %s\n", r.error.c_str()); return 1; }
  std::printf("%s", sysInfoText(r.sys).c_str());
  std::printf("workload: %d cases x %d elements, seed %llu, warm-up %d, repetitions %d\n", bc.batch.cases, bc.batch.elements,
              static_cast<unsigned long long>(bc.batch.seed), bc.warmup, bc.repetitions);
  std::printf("%s", benchCsv(r).c_str());
  for (const auto& n : r.notes) std::printf("note: %s\n", n.c_str());
  std::string out = get(a, "out", "");
  if (!out.empty()) {
    std::ostringstream meta;
    meta << sysInfoText(r.sys) << "workload_cases: " << bc.batch.cases << "\nworkload_elements: " << bc.batch.elements
         << "\nseed: " << bc.batch.seed << "\nwarmup_runs: " << bc.warmup << "\nmeasured_repetitions: " << bc.repetitions
         << "\nworkers_measured: ";
    for (int w : r.workers_used) meta << w << ' ';
    meta << "\nserial_checksum: " << formatDouble(r.serial_checksum, 17) << "\n";
    for (const auto& n : r.notes) meta << "note: " << n << "\n";
    save(out, "performance_summary.csv", benchCsv(r));
    save(out, "performance_repetitions.csv", benchTimesCsv(r));
    save(out, "performance_environment.txt", meta.str());
  }
  return 0;
}

int doAll(const std::string& out) {
  RodParams p;
  bool ok = true;
  // Task C
  std::vector<int> meshes = {10, 20, 40, 80};
  for (int n : meshes) {
    RodSolution s = solveRod(p, n);
    ok &= save(out, "thermal_solution_N" + std::to_string(n) + ".csv", solutionCsv(p, s));
    ok &= save(out, "thermal_midpoints_N" + std::to_string(n) + ".csv", midpointCsv(p, s));
  }
  auto rows = convergenceStudy(p, meshes);
  ok &= save(out, "thermal_convergence.csv", convergenceCsv(rows));
  // Task F
  BpParams bp;
  BpGradCheck chk = bpCheck(bp, 1.0, 2.0, 1e-6, 1e-5);
  BpTrace tr = bpTrain(bp, 1.0, 2.0, 0.1, 100);
  ok &= save(out, "backprop_loss.csv", bpLossCsv(tr));
  std::string rep = bpReportText(1.0, 2.0, 0.1, bp, chk, tr);
  ok &= save(out, "backprop_report.txt", rep);
  std::printf("%s", rep.c_str());
  if (!chk.pass) { std::fprintf(stderr, "gradient check FAILED\n"); return 1; }
  // Task D
  std::map<std::string, std::string> a;
  a["out"] = out;
  if (doBench(a) != 0) return 1;
  return ok ? 0 : 1;
}

int doGenLarge(const std::string& dir, std::size_t stlTris, std::size_t csvRows) {
  bool ok = save(dir, "large_" + std::to_string(stlTris) + "_triangles.stl", generateLargeStl(stlTris));
  ok &= save(dir, "large_" + std::to_string(csvRows) + "_nodes.csv", generateLargeCsv(csvRows));
  return ok ? 0 : 1;
}

int doLimits() {
  struct Item { const char* name; std::size_t n; bool stl; };
  const Item items[] = {{"STL", 10000, true}, {"STL", 100000, true}, {"STL", 500000, true}, {"STL", 1000000, true},
                        {"CSV", 100000, false}, {"CSV", 1000000, false}, {"CSV", 3000000, false}};
  std::printf("kind,records,file_MiB,parse_seconds,result\n");
  for (const auto& it : items) {
    std::string content = it.stl ? generateLargeStl(it.n) : generateLargeCsv(it.n);
    auto t0 = std::chrono::steady_clock::now();
    bool ok;
    std::string msg;
    if (it.stl) { StlResult r = parseAsciiStl(content); ok = r.ok; msg = r.error; }
    else { CsvResult r = parseAnsysCsv(content); ok = r.ok; msg = r.error; }
    double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("%s,%zu,%.1f,%.3f,%s\n", it.name, it.n, static_cast<double>(content.size()) / (1024.0 * 1024.0), sec, ok ? "ok" : msg.c_str());
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::printf("usage: wb_cli all|gen-samples|limits|bench|sysinfo [--out dir] ...\n");
    return 2;
  }
  std::string cmd = argv[1];
  auto a = parseArgs(argc, argv, 2);
  try {
    if (cmd == "all") return doAll(get(a, "out", "results"));
    if (cmd == "gen-samples") return doGenSamples(get(a, "out", "data"));
    if (cmd == "bench") return doBench(a);
    if (cmd == "limits") return doLimits();
    if (cmd == "gen-large")
      return doGenLarge(get(a, "dir", "tests/tmp"), static_cast<std::size_t>(std::atoll(get(a, "triangles", "100000").c_str())),
                        static_cast<std::size_t>(std::atoll(get(a, "rows", "1000000").c_str())));
    if (cmd == "sysinfo") { std::printf("%s", sysInfoText(querySysInfo()).c_str()); return 0; }
  } catch (const std::exception& e) {
    std::fprintf(stderr, "error: %s\n", e.what());
    return 1;
  }
  std::fprintf(stderr, "unknown command '%s'\n", cmd.c_str());
  return 2;
}
