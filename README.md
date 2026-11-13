# ESP32-S3 High-Performance Digital Control Architecture

*Read this in other languages: [Português](README.pt-br.md)*

An ultra-low latency, bare-metal optimized digital control architecture for the ESP32-S3. This project demonstrates how to implement a **timing-sensitive Proportional-Resonant (PR) controller** for 3-phase grid systems (e.g., inverters), coupled with a fixed-rate 60 Hz sine-wave lookup table and USB console telemetry.

## Core Architecture

The ADC DMA interrupt and control owner task run on Core 1. RMS processing and diagnostic presentation run on Core 0. Affinity reduces competition, but interrupts, shared kernel locks and flash/cache behavior can still introduce jitter; the measured results are not a hard real-time guarantee.

- **Core 1:** Processes one three-conversion ADC frame at a nominal 14.4 kHz, runs FIR/PR, and writes the DAC. In demo mode, a 240-entry LUT supplies three 60 Hz sine outputs. The dedicated SPI bus remains reserved; DAC hardware waits have bounded polling and checked errors.
- **Core 0:** Captures RMS through an atomic buffer handoff and polls for completion once per tick. RMS logs and diagnostic presentation run here. Wi-Fi/MQTT support has been removed.

## Key Features

- **Continuous ADC DMA Triggering**: Releases the control task from ADC DMA frame callbacks. Sampling time and callback/task execution time are distinct.
- **Exact 60 Hz LUT Cadence**: The ADC controller is configured for 14.4kHz per phase, yielding exactly 240 samples per 60Hz cycle. A 240-sample LUT advances by one entry per frame, with 80- and 160-sample offsets for the other phases.
- **Bare-Metal SPI DAC Driver**: Uses direct SPI/GPIO register writes for the MCP4922 with exclusive bus ownership, bounded hardware-busy waits and error handling. Measured stage costs and limitations are recorded in the diagnostic reports.
- **Proportional-Resonant (PR) Controller**: Reusable damped multi-resonant `esp_pr` component with a shared proportional gain, per-branch tuning, output limits and configurable anti-windup. See [API, equations and tests](components/esp_pr/README.md).
- **Real-Time FIR**: Reusable `esp_fir` component for arbitrary real FIR filters, with dedicated symmetric and antisymmetric kernels, caller-owned mirrored histories, IRAM processing, and no allocation or modulo in the sample path. The application calls the symmetric kernel directly for its 15-tap filter.
- **Real-Time Diagnostics**: `esp_rt_diagnostics` accumulates timings, overruns, and ADC events in the control task. `esp_rt_diagnostics_reporter` moves snapshots to a Core 0 task and classifies windows that may have been affected by reporter logging.

## Project Structure

* `managed_components/esp_mcp4922/`: Git-managed MCP4922 driver, pinned in `main/idf_component.yml`; source changes belong to its independent repository.
* `components/esp_pr/`: Reusable multi-resonant PR controller and host tests.
* `components/esp_fir/`: Reusable general, symmetric, and antisymmetric FIR kernels and host tests.
* `main/app_control.c`: Application tuning, direct `esp_pr` integration, DAC conversion, and RMS calculations.
* `main/app_fir.c`: FIR coefficients and the three phase filter histories.
* `main/app_diagnostics.c`: Control-loop diagnostics, ADC events, and asynchronous Core 0 presentation.
* `main/main.c`: The orchestrator. Configures the dual-core architecture, ADC DMA, and runs the high-speed critical section.

## Control-loop diagnostics

Every five-second window reports window and lifetime counters. `pool_lost` is confirmed ADC pool loss; `backlog` is recoverable accumulated work; `read_errors` means a notification did not yield a complete frame; and `cycle_overruns` compares CPU cycles with the exact 1/14400 s budget. Component microsecond deadlines are rounded up to 70 us, while cycle accounting preserves the approximately 69.44 us boundary.

`quiet` and `log-affected` describe possible interference from reporter output; neither proves absence of jitter. A sequence-tagged timestamp FIFO measures callback-to-task latency, and stage/interval summaries help locate delays. These measurements do not directly observe ADC conversion instants or the physical LDAC edge.

The checked-in configuration uses `-O2`, FIR `-O3`, supported FreeRTOS/SPI/ring-buffer IRAM options, continuous demo generation, RMS processing and logs. CPU frequency remains 160 MHz with PM disabled, as required by the DAC cycle-counter timeout contract. A true DAC transfer error stops acquisition and suspends the owner without releasing a possibly active SPI transaction; reset is required.


## Hardware Setup

* **ADC Input (ADC1)**: GPIO 4 (Phase R), GPIO 5 (Phase S), GPIO 6 (Phase T)
* **DAC SPI (SPI2)**: MOSI (GPIO 11), SCK (GPIO 12), LDAC (GPIO 14), CS1 (GPIO 10), CS2 (GPIO 9)

## How to Build and Run

1. Clone the project with submodules (`git clone --recurse-submodules https://github.com/smartsensingme/esp32-aqdata-3f.git`), or run `git submodule update --init --recursive` in an existing checkout.
2. Activate your ESP-IDF v6.0 environment (`. export.sh`).
3. Build and flash:
   ```bash
   idf.py build flash monitor
   ```

---
![SmartSensing.me Logo](https://smartsensing.me/ssme-logo.png)

## 📝 Description

This project is part of the **SmartSensing.me** ecosystem and goes beyond basic examples found on the internet. Here, we apply the real fundamentals of high-performance instrumentation and embedded systems engineering.

Unlike superficial content aimed only at clicks, this repository delivers:
- **Originality:** Original implementations based on nearly 30 years of academic experience.
- **Technical Density:** Professional use of the ESP-IDF framework and FreeRTOS.
- **Didactics:** Documented and structured code for those seeking real technical evolution.

> "We transform signals from the physical world into digital intelligence, without shortcuts."

---

## 🛠️ Technologies
- **Hardware Target:** ESP32-S3
- **Framework:** ESP-IDF v5.x / v6.x
- **Language:** C / C++
- **Simulation:** LTSpice (Sensor Modeling)

---

## 👤 About the Author

**José Alexandre de França** *Adjunct Professor at the Electrical Engineering Department of UEL*

Electrical Engineer with nearly three decades of experience in undergraduate and graduate teaching. PhD in Electrical Engineering, researcher in electronic instrumentation and embedded systems developer. SmartSensing.me is my commitment to raising the level of technological education in Brazil.

- 🌐 **Website:** [smartsensing.me](https://smartsensing.me)
- 📧 **E-mail:** [info@smartsensing.me](mailto:info@smartsensing.me)
- 📺 **YouTube:** [@smartsensingme](https://youtube.com/@smartsensingme)
- 📸 **Instagram:** [@smartsensing.me](https://instagram.com/smartsensing.me)

---

## 📄 License

This project is licensed under the MIT License.

## Reusable PR component

`components/esp_pr` contains the controller and portable host tests.
`main/app_control.c` configures the shared model and independent phase states.
The initial anti-windup tracking time is 10 ms, configurable under
**Application control** in menuconfig; zero disables it. Physical-plant
tuning and control-loop timing with anti-windup still require bench validation.
See [component documentation](components/esp_pr/README.md).

## Startup offset calibration

ADC offsets default to +1.65 V per phase. An active-low GPIO7 at startup
requests calibration and NVS persistence before the control loop starts. All
DAC channels start at midpoint (2048). See the [calibration procedure](docs/offset-calibration.md)
for wiring, zero-error conditions, configuration and failure handling.

## Controller commissioning

All three phases now share the provisional gains in `main/app_control.h`.
Only `TEST_IS_ON` (three-phase sines) and `CALIB_IS_ON` (polarity check) select
build modes. Both zero run all three closed loops continuously. DC/RMS reports
run every 10 s; detailed R ADC/FIR reports run every 60 s. See
[current operation](docs/operation-modes.md).

The current controller adds an I-only `esp_pid` branch in parallel with the PR.
See [PR/PID integration and commissioning](docs/pr-pid-integration.md) for
coordinated anti-windup of the summed command, AC/DC statistics, measured
1.985 kΩ current scale and remaining hardware-validation limits.

Reports now separate mean, total RMS and AC RMS, including DC signals. Offset
calibration uses a trimmed mean after DC-normalized FIR, not total RMS.
See [RMS and offset calibration](docs/rms-and-offset.md).
