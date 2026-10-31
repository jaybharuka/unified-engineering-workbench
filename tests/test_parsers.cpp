#include <cstdint>
#include <cstring>
#include <fstream>

#include "ansys_csv.hpp"
#include "samples.hpp"
#include "stl_parser.hpp"
#include "test_main.hpp"

using namespace wb;

namespace {
const char* kOneTriangle =
    "solid t\n facet normal 0 0 1\n  outer loop\n   vertex 0 0 0\n   vertex 1 0 0\n   vertex 0 1 0\n  endloop\n endfacet\nendsolid t\n";
const char* kHeader = "node_id,x_m,y_m,z_m,temperature_C\n";
}  // namespace

// ------------------------------- STL -------------------------------
TEST(stl_parses_single_triangle) {
  StlResult r = parseAsciiStl(kOneTriangle);
  CHECK(r.ok);
  CHECK(r.mesh.triangleCount() == 1);
  CHECK(r.mesh.name == "t");
  CHECK_NEAR(r.mesh.surface_area, 0.5, 1e-12);
  CHECK_NEAR(r.mesh.bbox_max[0], 1.0, 1e-12);
}

TEST(stl_generated_cylinder_counts) {
  StlResult r = parseAsciiStl(generateCylinderStl(1.0, 0.05, 24, 10));
  CHECK(r.ok);
  CHECK(r.mesh.triangleCount() == (size_t)(2 * 24 * 10 + 2 * 24));
  CHECK_NEAR(r.mesh.bbox_min[0], 0.0, 1e-6);
  CHECK_NEAR(r.mesh.bbox_max[0], 1.0, 1e-6);
  CHECK_NEAR(r.mesh.bbox_max[1], 0.05, 1e-6);
  CHECK(r.mesh.degenerate_triangles == 0);
}

TEST(stl_accepts_crlf_uppercase_scientific_and_bom) {
  std::string s = "\xEF\xBB\xBFSOLID T\r\nFACET NORMAL 0 0 1\r\nOUTER LOOP\r\nVERTEX 1e0 0.0E0 -0\r\nVERTEX 2 0 0\r\nVERTEX 0 3 0\r\nENDLOOP\r\nENDFACET\r\nENDSOLID T\r\n";
  StlResult r = parseAsciiStl(s);
  CHECK_MSG(r.ok, r.error);
  CHECK(r.mesh.triangleCount() == 1);
}

TEST(stl_rejects_empty_and_whitespace) {
  StlResult a = parseAsciiStl("");
  CHECK(!a.ok);
  CHECK(CONTAINS(a.error, "empty"));
  StlResult b = parseAsciiStl("  \n\t \n");
  CHECK(!b.ok);
  CHECK(CONTAINS(b.error, "whitespace"));
}

TEST(stl_explains_valid_syntax_with_no_facets) {
  StlResult r = parseAsciiStl("solid nothing\nendsolid nothing\n");
  CHECK(!r.ok);
  CHECK(CONTAINS(r.error, "no facets"));
}

TEST(stl_detects_binary) {
  std::string bin(80, 'x');
  bin.replace(0, 5, "solid");  // binary headers may start with 'solid'
  std::uint32_t n = 1;
  bin.append(reinterpret_cast<const char*>(&n), 4);
  bin.append(50, '\0');
  StlResult r = parseAsciiStl(bin);
  CHECK(!r.ok);
  CHECK(CONTAINS(r.error, "BINARY"));
}

TEST(stl_rejects_non_stl_text) {
  StlResult r = parseAsciiStl("hello world\n");
  CHECK(!r.ok);
  CHECK(CONTAINS(r.error, "solid"));
}

TEST(stl_rejects_nan_inf_and_garbage_numbers_with_line_number) {
  std::string base = kOneTriangle;
  for (const char* bad : {"nan", "inf", "-inf", "1e999", "0x10", "abc", "1.2.3"}) {
    std::string s = base;
    size_t p = s.find("vertex 1 0 0");
    s.replace(p, 12, std::string("vertex ") + bad + " 0 0");
    StlResult r = parseAsciiStl(s);
    CHECK_MSG(!r.ok, bad);
    CHECK_MSG(CONTAINS(r.error, "line 5"), r.error);
  }
}

TEST(stl_rejects_truncated_and_malformed_structure) {
  CHECK(!parseAsciiStl("solid t\n facet normal 0 0 1\n  outer loop\n   vertex 0 0 0\n").ok);                 // truncated
  CHECK(!parseAsciiStl("solid t\n facet normal 0 0 1\n outer loop\n vertex 0 0 0\n vertex 1 0 0\n endloop\n endfacet\nendsolid\n").ok);  // 2 vertices
  CHECK(!parseAsciiStl("solid t\n facet normal 0 0\n outer loop\n").ok);                                   // short normal
  std::string noEnd = "solid t\n facet normal 0 0 1\n outer loop\n vertex 0 0 0\n vertex 1 0 0\n vertex 0 1 0\n endloop\n endfacet\n";
  StlResult r = parseAsciiStl(noEnd);
  CHECK(!r.ok);
  CHECK(CONTAINS(r.error, "endsolid"));
  CHECK(!parseAsciiStl(std::string(kOneTriangle) + "garbage\n").ok);
}

TEST(stl_warns_on_degenerate_and_multiple_solids) {
  std::string s = std::string(kOneTriangle) +
                  "solid u\n facet normal 0 0 1\n outer loop\n vertex 0 0 0\n vertex 1 0 0\n vertex 2 0 0\n endloop\n endfacet\nendsolid u\n";
  StlResult r = parseAsciiStl(s);
  CHECK_MSG(r.ok, r.error);
  CHECK(r.mesh.triangleCount() == 2);
  CHECK(r.mesh.degenerate_triangles == 1);
  CHECK(r.warnings.size() == 2);
}

TEST(stl_enforces_triangle_limit) {
  StlResult r = parseAsciiStl(generateCylinderStl(1, 0.1, 10, 5), 10);
  CHECK(!r.ok);
  CHECK(CONTAINS(r.error, "limit"));
}

TEST(stl_file_errors) {
  StlResult a = loadAsciiStlFile("does/not/exist.stl");
  CHECK(!a.ok);
  CHECK(CONTAINS(a.error, "Cannot open"));
  StlResult b = loadAsciiStlFile("data/synthetic_rod.stl", 100);  // size cap
  CHECK(!b.ok);
  CHECK(CONTAINS(b.error, "limit"));
}

TEST(stl_shipped_sample_loads) {
  StlResult r = loadAsciiStlFile("data/synthetic_rod.stl");
  CHECK_MSG(r.ok, r.error);
  CHECK(r.mesh.triangleCount() > 100);
}

// ------------------------------- CSV -------------------------------
TEST(csv_parses_valid_rows) {
  CsvResult r = parseAnsysCsv(std::string(kHeader) + "1,0,0,0,100\n2,0.5,0.1,0.2,60.5\n3,1,0,0,20\n");
  CHECK_MSG(r.ok, r.error);
  CHECK(r.cloud.count() == 3);
  CHECK_NEAR(r.cloud.t_min, 20, 1e-12);
  CHECK_NEAR(r.cloud.t_max, 100, 1e-12);
  CHECK_NEAR(r.cloud.bbox_max[0], 1, 1e-12);
  CHECK(r.cloud.id[1] == 2);
}

TEST(csv_accepts_crlf_bom_case_and_spaces) {
  CsvResult r = parseAnsysCsv("\xEF\xBB\xBFNode_ID, X_M ,y_m,z_m,Temperature_C\r\n 1 , 0 ,0,0, 5 \r\n\r\n2,1,1,1,6\r\n");
  CHECK_MSG(r.ok, r.error);
  CHECK(r.cloud.count() == 2);
  CHECK(r.warnings.size() == 1);  // blank line
}

TEST(csv_rejects_empty_and_header_only) {
  CsvResult a = parseAnsysCsv("");
  CHECK(!a.ok);
  CHECK(CONTAINS(a.error, "empty"));
  CsvResult b = parseAnsysCsv(kHeader);
  CHECK(!b.ok);
  CHECK(CONTAINS(b.error, "no data rows"));
  CsvResult c = parseAnsysCsv("\n\n   \n");
  CHECK(!c.ok);
  CHECK(CONTAINS(c.error, "no header"));
}

TEST(csv_rejects_bad_headers) {
  CHECK(CONTAINS(parseAnsysCsv("node_id,x_m,y_m,temperature_C\n1,0,0,5\n").error, "5"));            // missing column
  CHECK(CONTAINS(parseAnsysCsv("node_id,y_m,x_m,z_m,temperature_C\n1,0,0,0,5\n").error, "column 2"));  // wrong order
  CHECK(CONTAINS(parseAnsysCsv("node_id;x_m;y_m;z_m;temperature_C\n1;0;0;0;5\n").error, "';'"));
  CHECK(!parseAnsysCsv("node_id,x_m,y_m,z_m,temperature_C,extra\n1,0,0,0,5,1\n").ok);
  CHECK(CONTAINS(parseAnsysCsv("node_id,x_m,y_m,z_m,temperature_K\n1,0,0,0,5\n").error, "temperature_C"));
}

TEST(csv_rejects_non_finite_and_malformed_values_with_line_numbers) {
  for (const char* bad : {"nan", "inf", "-inf", "NaN", "1e999", "0x1F", "abc", "", "1,2"}) {
    std::string row = std::string("1,0,0,0,") + bad + "\n";
    CsvResult r = parseAnsysCsv(std::string(kHeader) + row);
    CHECK_MSG(!r.ok, bad);
    CHECK_MSG(CONTAINS(r.error, "Line 2"), r.error);
  }
  CsvResult x = parseAnsysCsv(std::string(kHeader) + "1,nan,0,0,5\n");
  CHECK(!x.ok);
  CHECK(CONTAINS(x.error, "x_m"));
}

TEST(csv_rejects_bad_ids_and_duplicates) {
  CHECK(!parseAnsysCsv(std::string(kHeader) + "0,0,0,0,5\n").ok);
  CHECK(!parseAnsysCsv(std::string(kHeader) + "-3,0,0,0,5\n").ok);
  CHECK(!parseAnsysCsv(std::string(kHeader) + "1.5,0,0,0,5\n").ok);
  CHECK(!parseAnsysCsv(std::string(kHeader) + "99999999999,0,0,0,5\n").ok);
  CsvResult d = parseAnsysCsv(std::string(kHeader) + "1,0,0,0,5\n1,1,0,0,6\n");
  CHECK(!d.ok);
  CHECK(CONTAINS(d.error, "duplicate"));
  CHECK(CONTAINS(d.error, "Line 3"));
}

TEST(csv_rejects_below_absolute_zero_and_wrong_column_count) {
  CsvResult a = parseAnsysCsv(std::string(kHeader) + "1,0,0,0,-300\n");
  CHECK(!a.ok);
  CHECK(CONTAINS(a.error, "absolute zero"));
  CsvResult b = parseAnsysCsv(std::string(kHeader) + "1,0,0,5\n");
  CHECK(!b.ok);
  CHECK(CONTAINS(b.error, "expected 5"));
}

TEST(csv_counts_further_problems) {
  std::string s = kHeader;
  for (int i = 1; i <= 25; ++i) s += std::to_string(i) + ",0,0,0,nan\n";
  CsvResult r = parseAnsysCsv(s);
  CHECK(!r.ok);
  CHECK(r.problems.size() == kMaxReportedProblems);
  CHECK(CONTAINS(r.error, "+24 more"));
  CHECK(r.cloud.count() == 0);  // nothing partial leaks out
}

TEST(csv_row_limit) {
  CsvResult r = parseAnsysCsv(std::string(kHeader) + "1,0,0,0,5\n2,0,0,0,5\n3,0,0,0,5\n", 2);
  CHECK(!r.ok);
  CHECK(CONTAINS(r.error, "limit"));
}

TEST(csv_shipped_sample_loads_and_uses_exact_schema) {
  CsvResult r = loadAnsysCsvFile("data/synthetic_rod_temperatures.csv");
  CHECK_MSG(r.ok, r.error);
  CHECK(r.cloud.count() > 500);
  CHECK(r.cloud.t_min >= 19.0);
  std::ifstream f("data/synthetic_rod_temperatures.csv");
  std::string first;
  std::getline(f, first);
  CHECK(first == "node_id,x_m,y_m,z_m,temperature_C");
}

TEST(csv_generated_large_file_roundtrip) {
  CsvResult r = parseAnsysCsv(generateLargeCsv(50000));
  CHECK_MSG(r.ok, r.error);
  CHECK(r.cloud.count() == 50000);
}
