// Task E: SIMULATED device adapters (microcontroller, FPGA, JTAG debug adapter).
// Nothing here talks to real hardware. Every message carries "simulated": true.
#pragma once
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

#include "json.hpp"

namespace wb {

enum class DeviceType { Mcu, Fpga, JtagAdapter };
const char* deviceTypeName(DeviceType t);

enum class ConnState { Disconnected, Connected };

// Error with a machine-readable code and a flag telling the UI whether "retry" is meaningful.
class DeviceError : public std::runtime_error {
 public:
  DeviceError(std::string code, std::string message, bool recoverable, std::string hint)
      : std::runtime_error(std::move(message)), code_(std::move(code)), recoverable_(recoverable), hint_(std::move(hint)) {}
  const std::string& code() const { return code_; }
  bool recoverable() const { return recoverable_; }
  const std::string& hint() const { return hint_; }

 private:
  std::string code_;
  bool recoverable_;
  std::string hint_;
};

struct VoltageDomain {
  std::string name;
  double volts;
};

struct DeviceInfo {
  std::string id;
  DeviceType type;
  std::string name;               // example board assumed for the design (see docs/EMBEDDED_DESIGN.md)
  std::string host_link;
  std::string connector;
  bool compute_capable;           // false for the JTAG adapter: it is a debug interface, not a compute board
  std::vector<std::string> capabilities;
  std::vector<VoltageDomain> voltage_domains;
};

class IDevice {
 public:
  virtual ~IDevice() = default;
  virtual const DeviceInfo& info() const = 0;
  virtual Json telemetry(unsigned long sequence) const = 0;  // one telemetry message (deterministic)
};

class DeviceManager {
 public:
  DeviceManager();

  Json discover();                               // list of devices + states + capabilities
  Json connect(const std::string& id);           // throws DeviceError
  Json disconnect(const std::string& id);        // throws DeviceError
  Json telemetry(const std::string& id);         // throws DeviceError
  Json armFault(const std::string& id, const std::string& kind, int count);  // kind: "timeout"
  Json eventLog();                               // recent events (bounded)

 private:
  struct Slot {
    std::unique_ptr<IDevice> dev;
    ConnState state = ConnState::Disconnected;
    int pendingTimeouts = 0;
    unsigned long sequence = 0;
  };
  Slot& slot(const std::string& id);  // throws UNKNOWN_DEVICE
  Json describe(const Slot& s) const;
  void log(const std::string& msg);

  std::mutex m_;
  std::map<std::string, Slot> slots_;
  std::vector<std::string> order_;
  std::vector<std::string> events_;
  unsigned long eventCounter_ = 0;
};

}  // namespace wb
