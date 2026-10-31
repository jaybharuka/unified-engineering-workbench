# Task E: embedded-system integration design (SIMULATED prototype)

**Everything in the Hardware view is simulated.** No USB, serial, JTAG or FPGA bitstream operation is performed; every message carries `"simulated": true`. The example boards are assumptions used to make the design concrete, not tested hardware.

## Block diagram
`web/assets/embedded_block_diagram.svg` (also shown in the Hardware view):

Host PC (Windows 11, workbench UI + C++ core) <-USB-> powered, current-limited USB hub (5 V) <-USB-> {MCU board, FPGA board, JTAG adapter}.
JTAG adapter -> target selector (one target at a time, manual) -> MCU debug header (SWD/JTAG) **or** FPGA JTAG header. Optional level shifter only on the MCU<->FPGA user link if domains differ.

## Interface / pin-voltage table (assumed example boards)
| Item | Microcontroller board | FPGA board | JTAG debug adapter |
|---|---|---|---|
| Example (assumption) | 3.3 V Cortex-M board (Nucleo-class) | Artix-7-class dev board | FT2232-class USB-JTAG pod |
| Role | Compute / firmware target | Programmable logic target | **Debug interface only** |
| Host link | USB, virtual COM (UART bridge), 115200 8N1 assumed | USB (on-board JTAG/UART bridge) assumed | USB (D+/D-, VBUS 5 V) |
| Power in | USB VBUS 5 V -> on-board 3.3 V regulator | USB or barrel 5 V -> on-board rails | USB VBUS 5 V (powers the adapter only) |
| Voltage domains | VDD/GPIO 3.3 V | VCCINT 1.0 V, VCCAUX 1.8 V, VCCO (I/O banks) 3.3 V | Logic follows target VTref (1.8-3.3 V class); never sources VTref |
| I/O connector | Arduino/morpho headers: GPIO, UART, SPI, I2C, ADC | Pmod/headers, bank VCCO 3.3 V | none (signals go to debug header) |
| Debug connector | 10-pin Cortex Debug (SWD/JTAG) | 14-pin FPGA JTAG header (family pinout) | Target-side header, **different per target** |
| JTAG signal ownership | Target: TDO, VTref (input to adapter), nSRST (open-drain) | Target: TDO, VREF | **Adapter drives** TCK, TMS, TDI, (n)TRST, (n)SRST; **reads** TDO, VTref |
| Ground | Common ground through the USB shield and the debug-header GND pins (several pins); no extra jumper | same | same; connect GND before signals |
| Level shifting | none (3.3 V native) | none on 3.3 V Pmod | internal buffers track VTref; external shifter only for a 1.8 V or 5 V target |

Pin numbers are intentionally **not** invented here: each real board has its own pinout and must be taken from its datasheet/schematic before wiring. The design states the *signal ownership and voltage domains*, which are board-independent, and leaves pin numbers as `[from board datasheet]`.

## Why a JTAG adapter is not an interchangeable compute board
* It runs no user application; it is a protocol bridge that drives a **target's** TAP (TCK/TMS/TDI) and reads TDO.
* Its signal levels are not its own: it senses the target's VTref and adapts. A wrong VTref or an unbuffered 3.3 V adapter on a 1.8 V FPGA JTAG bank can damage the target.
* Connectors differ (10-pin Cortex vs 14-pin FPGA vs 20-pin ARM), as do protocols (SWD vs JTAG) and pin ownership. A compute board has its own MCU/FPGA, power rails and firmware; an adapter has none of the target's resources. Capability reporting reflects this (`compute_capable = false`).
* So the software discovers devices **by type and capability** and never assumes a shared connector, voltage or protocol.

## Software prototype (C++ mocks, `src/core/hardware.*`)
`DeviceManager` provides discovery, capability reporting, connect/disconnect and one telemetry message per device (`mcu0`, `fpga0`, `jtag0`), plus a bounded event log. A **recoverable failure** can be injected from the UI ("Inject timeout"): the next connect or telemetry call returns `TIMEOUT` (`recoverable: true`, with a hint); telemetry timeouts drop the link; recovery is reconnect. Screenshots: `docs/screenshots/08_hardware_timeout.png`, `09_hardware_recovered.png`. Unit tests: `tests/test_hardware.cpp` (10 tests).

## Safe integration plan (for a future real build)
1. Paper review: datasheets and schematics for the exact boards; fill in the pin numbers and VTref range; check absolute maximum ratings.
2. Power: bench supply or powered hub with per-port current limit; connect one device at a time; measure rails with a meter before attaching signals.
3. Ground first: common ground, then JTAG/UART signals; never feed 5 V into 3.3 V pins; add series resistors (e.g. 100 ohm) and, if needed, a level shifter.
4. Read-only first: read IDCODE over JTAG and do UART loopback before any write.
5. Only then: flash a known-good image to the MCU, program the FPGA, and keep ESD precautions and a documented rollback.
6. Add timeouts, retries with backoff and a "safe disconnect" in the host software (already mirrored in the mocks).

## Limitations
Simulated only; no real driver stack (WinUSB/FTDI/serial); no pin-level verification; example boards are assumptions; telemetry values are deterministic fake numbers; only the `timeout` fault is modelled; no firmware or bitstream handling.
