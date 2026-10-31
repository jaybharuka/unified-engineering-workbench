#include "hardware.hpp"
#include "test_main.hpp"

using namespace wb;

namespace {
const Json* dev(const Json& list, size_t i) { return &list.find("devices")->at(i); }
}

TEST(hw_discovery_reports_three_labelled_simulated_devices) {
  DeviceManager m;
  Json d = m.discover();
  CHECK(d.find("simulated")->asBool());
  CHECK(d.find("devices")->size() == 3);
  CHECK(dev(d, 0)->find("type")->asString() == "microcontroller");
  CHECK(dev(d, 1)->find("type")->asString() == "fpga");
  CHECK(dev(d, 2)->find("type")->asString() == "jtag_adapter");
  for (size_t i = 0; i < 3; ++i) {
    CHECK(dev(d, i)->find("simulated")->asBool());
    CHECK(dev(d, i)->find("capabilities")->size() > 0);
    CHECK(dev(d, i)->find("state")->asString() == "disconnected");
  }
}

TEST(hw_jtag_adapter_is_not_a_compute_board) {
  DeviceManager m;
  Json d = m.discover();
  CHECK(dev(d, 0)->find("compute_capable")->asBool());
  CHECK(dev(d, 1)->find("compute_capable")->asBool());
  CHECK(!dev(d, 2)->find("compute_capable")->asBool());
}

TEST(hw_connect_telemetry_disconnect_for_all_types) {
  DeviceManager m;
  for (const char* id : {"mcu0", "fpga0", "jtag0"}) {
    Json c = m.connect(id);
    CHECK(c.find("state")->asString() == "connected");
    Json t0 = m.telemetry(id), t1 = m.telemetry(id);
    CHECK(t0.find("simulated")->asBool());
    CHECK(t0.find("seq")->asNumber() == 0);
    CHECK(t1.find("seq")->asNumber() == 1);
    CHECK(t0.find("type")->isString());
    CHECK(m.disconnect(id).find("state")->asString() == "disconnected");
  }
}

TEST(hw_telemetry_is_type_specific_and_deterministic) {
  DeviceManager a, b;
  a.connect("mcu0"); b.connect("mcu0");
  CHECK(a.telemetry("mcu0").dump() == b.telemetry("mcu0").dump());
  a.connect("fpga0");
  CHECK(a.telemetry("fpga0").find("type")->asString() == "fpga_telemetry");
  a.connect("jtag0");
  CHECK(a.telemetry("jtag0").find("type")->asString() == "jtag_status");
}

TEST(hw_telemetry_requires_connection) {
  DeviceManager m;
  try { m.telemetry("mcu0"); CHECK(false); }
  catch (const DeviceError& e) { CHECK(e.code() == "NOT_CONNECTED"); CHECK(e.recoverable()); }
}

TEST(hw_unknown_device_is_not_recoverable) {
  DeviceManager m;
  try { m.connect("nope"); CHECK(false); }
  catch (const DeviceError& e) { CHECK(e.code() == "UNKNOWN_DEVICE"); CHECK(!e.recoverable()); CHECK(!e.hint().empty()); }
}

TEST(hw_connect_timeout_then_retry_succeeds) {
  DeviceManager m;
  m.armFault("fpga0", "timeout", 1);
  try { m.connect("fpga0"); CHECK(false); }
  catch (const DeviceError& e) { CHECK(e.code() == "TIMEOUT"); CHECK(e.recoverable()); CHECK(!e.hint().empty()); }
  CHECK(m.connect("fpga0").find("state")->asString() == "connected");  // recovery
  CHECK(m.telemetry("fpga0").find("seq")->asNumber() == 0);
}

TEST(hw_telemetry_timeout_drops_link_and_reconnect_recovers) {
  DeviceManager m;
  m.connect("mcu0");
  m.telemetry("mcu0");
  m.armFault("mcu0", "timeout", 1);
  try { m.telemetry("mcu0"); CHECK(false); } catch (const DeviceError& e) { CHECK(e.code() == "TIMEOUT"); }
  try { m.telemetry("mcu0"); CHECK(false); } catch (const DeviceError& e) { CHECK(e.code() == "NOT_CONNECTED"); }
  m.connect("mcu0");
  CHECK(m.telemetry("mcu0").find("type")->asString() == "mcu_telemetry");
}

TEST(hw_multiple_faults_and_bad_fault_requests) {
  DeviceManager m;
  m.armFault("jtag0", "timeout", 2);
  for (int i = 0; i < 2; ++i) { try { m.connect("jtag0"); CHECK(false); } catch (const DeviceError&) {} }
  CHECK(m.connect("jtag0").find("state")->asString() == "connected");
  CHECK_THROWS(m.armFault("jtag0", "explode", 1));
  CHECK_THROWS(m.armFault("jtag0", "timeout", 0));
  CHECK_THROWS(m.armFault("jtag0", "timeout", 99));
  CHECK_THROWS(m.armFault("zzz", "timeout", 1));
}

TEST(hw_event_log_is_bounded_and_labelled) {
  DeviceManager m;
  for (int i = 0; i < 80; ++i) m.discover();
  Json log = m.eventLog();
  CHECK(log.size() == 50);
  CHECK(CONTAINS(log.at(0).asString(), "SIMULATED"));
}
