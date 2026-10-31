#include "hardware.hpp"

#include <cmath>

namespace wb {

const char* deviceTypeName(DeviceType t) {
  switch (t) {
    case DeviceType::Mcu: return "microcontroller";
    case DeviceType::Fpga: return "fpga";
    case DeviceType::JtagAdapter: return "jtag_adapter";
  }
  return "unknown";
}

namespace {

// Deterministic pseudo-waveform so tests and documentation are reproducible.
double wave(unsigned long seq, double period) { return std::sin(static_cast<double>(seq) * 6.283185307179586 / period); }

class MockMcu : public IDevice {
 public:
  MockMcu() {
    info_.id = "mcu0";
    info_.type = DeviceType::Mcu;
    info_.name = "Simulated 3.3 V Cortex-M microcontroller board (example: ST Nucleo-class)";
    info_.host_link = "USB virtual COM port (UART bridge on the board), 115200 8N1 assumed";
    info_.connector = "USB Micro-B/Type-C to host; 2x ST morpho/Arduino headers for I/O";
    info_.compute_capable = true;
    info_.capabilities = {"gpio", "uart", "adc", "spi", "i2c", "swd/jtag-debug-pins"};
    info_.voltage_domains = {{"VDD / GPIO", 3.3}, {"USB VBUS", 5.0}};
  }
  const DeviceInfo& info() const override { return info_; }
  Json telemetry(unsigned long seq) const override {
    Json j = Json::object();
    j["type"] = "mcu_telemetry";
    j["simulated"] = true;
    j["seq"] = seq;
    j["uptime_ms"] = seq * 1000UL;
    j["vdd_V"] = 3.3;
    j["core_temp_C"] = 34.0 + 1.5 * wave(seq, 20);
    j["adc_ch0_V"] = 1.65 + 0.5 * wave(seq, 10);
    j["gpio_state"] = "0x0000A5A5";
    return j;
  }

 private:
  DeviceInfo info_;
};

class MockFpga : public IDevice {
 public:
  MockFpga() {
    info_.id = "fpga0";
    info_.type = DeviceType::Fpga;
    info_.name = "Simulated Xilinx-class FPGA development board (example: Artix-7 board)";
    info_.host_link = "USB (on-board JTAG/UART bridge) assumed; programming path NOT exercised in this prototype";
    info_.connector = "USB; Pmod 3.3 V I/O headers; JTAG access via on-board bridge or external header";
    info_.compute_capable = true;
    info_.capabilities = {"pmod-io (3.3 V)", "clock-status", "config-status (simulated)", "jtag-target"};
    info_.voltage_domains = {{"VCCINT (core)", 1.0}, {"VCCAUX", 1.8}, {"VCCO bank I/O", 3.3}};
  }
  const DeviceInfo& info() const override { return info_; }
  Json telemetry(unsigned long seq) const override {
    Json j = Json::object();
    j["type"] = "fpga_telemetry";
    j["simulated"] = true;
    j["seq"] = seq;
    j["config_done"] = true;
    j["die_temp_C"] = 42.0 + 2.0 * wave(seq, 30);
    j["vccint_V"] = 1.0 + 0.005 * wave(seq, 7);
    j["clock_locked"] = true;
    return j;
  }

 private:
  DeviceInfo info_;
};

class MockJtag : public IDevice {
 public:
  MockJtag() {
    info_.id = "jtag0";
    info_.type = DeviceType::JtagAdapter;
    info_.name = "Simulated JTAG debug adapter (example: FT2232-class USB-JTAG pod)";
    info_.host_link = "USB to host; JTAG/SWD signals to target (adapter owns TCK/TMS/TDI, target owns TDO/VTref)";
    info_.connector = "Target-side debug header (e.g. 10-pin Cortex Debug or 14-pin FPGA JTAG) - NOT shared between boards";
    info_.compute_capable = false;  // a debug interface, not a general compute board
    info_.capabilities = {"jtag-tap-access", "vtref-sense", "tck-clock-select", "srst/trst-control"};
    info_.voltage_domains = {{"Adapter logic (follows VTref)", 3.3}, {"USB VBUS", 5.0}};
  }
  const DeviceInfo& info() const override { return info_; }
  Json telemetry(unsigned long seq) const override {
    Json j = Json::object();
    j["type"] = "jtag_status";
    j["simulated"] = true;
    j["seq"] = seq;
    j["vtref_V"] = 3.3;
    j["tck_hz"] = 1000000;
    j["target_attached"] = true;
    Json chain = Json::array();
    chain.push("SIM-IDCODE-0001");
    j["chain"] = chain;
    return j;
  }

 private:
  DeviceInfo info_;
};

}  // namespace

DeviceManager::DeviceManager() {
  auto add = [&](std::unique_ptr<IDevice> d) {
    std::string id = d->info().id;
    Slot s;
    s.dev = std::move(d);
    slots_.emplace(id, std::move(s));
    order_.push_back(id);
  };
  add(std::make_unique<MockMcu>());
  add(std::make_unique<MockFpga>());
  add(std::make_unique<MockJtag>());
}

DeviceManager::Slot& DeviceManager::slot(const std::string& id) {
  auto it = slots_.find(id);
  if (it == slots_.end())
    throw DeviceError("UNKNOWN_DEVICE", "No device with id '" + id + "'.", false, "Run discovery and use one of the listed ids (mcu0, fpga0, jtag0).");
  return it->second;
}

void DeviceManager::log(const std::string& msg) {
  events_.push_back("#" + std::to_string(++eventCounter_) + " [SIMULATED] " + msg);
  if (events_.size() > 50) events_.erase(events_.begin());
}

Json DeviceManager::describe(const Slot& s) const {
  const DeviceInfo& i = s.dev->info();
  Json j = Json::object();
  j["id"] = i.id;
  j["type"] = deviceTypeName(i.type);
  j["name"] = i.name;
  j["simulated"] = true;
  j["state"] = s.state == ConnState::Connected ? "connected" : "disconnected";
  j["compute_capable"] = i.compute_capable;
  j["host_link"] = i.host_link;
  j["connector"] = i.connector;
  Json caps = Json::array();
  for (const auto& c : i.capabilities) caps.push(c);
  j["capabilities"] = caps;
  Json vd = Json::array();
  for (const auto& d : i.voltage_domains) {
    Json o = Json::object();
    o["name"] = d.name;
    o["volts"] = d.volts;
    vd.push(o);
  }
  j["voltage_domains"] = vd;
  j["armed_timeouts"] = s.pendingTimeouts;
  return j;
}

Json DeviceManager::discover() {
  std::lock_guard<std::mutex> lk(m_);
  log("discovery scan");
  Json arr = Json::array();
  for (const auto& id : order_) arr.push(describe(slots_.at(id)));
  Json j = Json::object();
  j["simulated"] = true;
  j["devices"] = arr;
  return j;
}

Json DeviceManager::connect(const std::string& id) {
  std::lock_guard<std::mutex> lk(m_);
  Slot& s = slot(id);
  if (s.pendingTimeouts > 0) {
    --s.pendingTimeouts;
    log(id + ": connect TIMEOUT (injected fault)");
    throw DeviceError("TIMEOUT", "Simulated timeout: device '" + id + "' did not answer the connect request within 500 ms.", true,
                      "Recoverable. Check the (simulated) cable/power and press Retry; the next attempt succeeds.");
  }
  if (s.state == ConnState::Connected) {
    log(id + ": already connected");
    return describe(s);
  }
  s.state = ConnState::Connected;
  s.sequence = 0;
  log(id + ": connected");
  return describe(s);
}

Json DeviceManager::disconnect(const std::string& id) {
  std::lock_guard<std::mutex> lk(m_);
  Slot& s = slot(id);
  s.state = ConnState::Disconnected;
  log(id + ": disconnected");
  return describe(s);
}

Json DeviceManager::telemetry(const std::string& id) {
  std::lock_guard<std::mutex> lk(m_);
  Slot& s = slot(id);
  if (s.state != ConnState::Connected)
    throw DeviceError("NOT_CONNECTED", "Device '" + id + "' is not connected.", true, "Press Connect first, then request telemetry.");
  if (s.pendingTimeouts > 0) {
    --s.pendingTimeouts;
    s.state = ConnState::Disconnected;  // link considered lost after a timeout; recovery = reconnect
    log(id + ": telemetry TIMEOUT (injected fault); link dropped");
    throw DeviceError("TIMEOUT", "Simulated timeout: no telemetry from '" + id + "' within 500 ms. The link was dropped.", true,
                      "Recoverable. Press Connect again, then request telemetry.");
  }
  Json m = s.dev->telemetry(s.sequence++);
  log(id + ": telemetry seq " + std::to_string(s.sequence - 1));
  return m;
}

Json DeviceManager::armFault(const std::string& id, const std::string& kind, int count) {
  std::lock_guard<std::mutex> lk(m_);
  Slot& s = slot(id);
  if (kind != "timeout")
    throw DeviceError("BAD_FAULT", "Unsupported fault kind '" + kind + "'. Only 'timeout' is simulated.", false, "Use kind \"timeout\".");
  if (count < 1 || count > 5)
    throw DeviceError("BAD_FAULT", "Fault count must be between 1 and 5.", false, "Use a count from 1 to 5.");
  s.pendingTimeouts = count;
  log(id + ": armed " + std::to_string(count) + " timeout fault(s)");
  return describe(s);
}

Json DeviceManager::eventLog() {
  std::lock_guard<std::mutex> lk(m_);
  Json a = Json::array();
  for (const auto& e : events_) a.push(e);
  return a;
}

}  // namespace wb
