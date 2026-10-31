#include <cmath>

#include "json.hpp"
#include "test_main.hpp"

using namespace wb;

TEST(json_roundtrip_and_types) {
  Json j = Json::parse(R"({"a":1.5,"b":[1,2,3],"c":{"d":"x\ny"},"e":true,"f":null})");
  CHECK(j.find("a")->asNumber() == 1.5);
  CHECK(j.find("b")->size() == 3);
  CHECK(j.find("c")->find("d")->asString() == "x\ny");
  CHECK(j.find("e")->asBool());
  CHECK(j.find("f")->isNull());
  Json k = Json::parse(j.dump());
  CHECK(k.dump() == j.dump());
}

TEST(json_rejects_malformed) {
  CHECK_THROWS(Json::parse(""));
  CHECK_THROWS(Json::parse("{"));
  CHECK_THROWS(Json::parse("{\"a\":}"));
  CHECK_THROWS(Json::parse("[1,2,"));
  CHECK_THROWS(Json::parse("{\"a\":1} extra"));
  CHECK_THROWS(Json::parse("\"unterminated"));
  CHECK_THROWS(Json::parse("{\"a\":1e999}"));
  std::string deep(200, '[');
  CHECK_THROWS(Json::parse(deep));
}

TEST(json_non_finite_numbers_serialize_as_null) {
  Json j = Json::object();
  j["nan"] = NAN;
  j["inf"] = INFINITY;
  CHECK(j.dump() == "{\"nan\":null,\"inf\":null}");
}

TEST(json_escapes_control_characters) {
  Json j("a\"b\\c\t\x01");
  CHECK(j.dump() == "\"a\\\"b\\\\c\\t\\u0001\"");
}
