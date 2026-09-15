# ESP32-S3 High-Performance Digital Control Architecture

*Read this in other languages: [Português](README.pt-br.md)*

An ultra-low latency, bare-metal optimized digital control architecture for the ESP32-S3. This project demonstrates how to implement a **hard real-time Proportional-Resonant (PR) controller** for 3-phase grid systems (e.g., inverters), coupled with a fixed-rate 60 Hz sine-wave lookup table and isolated Wi-Fi/MQTT telemetry.

## Core Architecture

To guarantee strict determinism (microsecond latency) without triggering RTOS watchdogs or stalling under heavy network loads, the system leverages asymmetric multicore processing:

- **Core 1 (Real-Time Control Loop):** Completely isolated from the network stack. Driven by the hardware ADC Continuous Mode (via DMA) at 14.4kHz per phase. Executes the Proportional-Resonant (PR) math, advances a 240-sample sine LUT, and writes directly to the physical SPI hardware FIFO (`mcp4922_ll_write_channels`). Total loop time with anti-windup must be measured on hardware.
- **Core 0 (Telemetry & Networking):** Runs the FreeRTOS Wi-Fi, TLS, and MQTT stacks. Calculates RMS currents over 3-cycle windows in the background and publishes JSON payloads securely (MQTTS) without stealing CPU cycles from the control loop.

## Key Features

- **Continuous ADC DMA Triggering**: Replaces traditional `gptimer` interrupts with hardware DMA callbacks, ensuring the control loop is perfectly synchronized with physical analog sampling.
- **Exact 60 Hz LUT Cadence**: The ADC controller is configured for 14.4kHz per phase, yielding exactly 240 samples per 60Hz cycle. A 240-sample LUT advances by one entry per frame, with 80- and 160-sample offsets for the other phases.
- **Bare-Metal SPI DAC Driver**: Bypasses the ESP-IDF abstraction layer for the MCP4922 12-bit Dual DAC. Eliminates FreeRTOS locks and context checks, reducing transmission latency from >100us to **~3us**.
- **Proportional-Resonant (PR) Controller**: Reusable damped multi-resonant `esp_pr` component with a shared proportional gain, per-branch tuning, output limits and configurable anti-windup. See [API, equations and tests](components/esp_pr/README.md).
- **Real-Time FIR**: Reusable `esp_fir` component for arbitrary real FIR filters, with dedicated symmetric and antisymmetric kernels, caller-owned mirrored histories, IRAM processing, and no allocation or modulo in the sample path. The application calls the symmetric kernel directly for its 15-tap filter.
- **Wi-Fi Power Management & MQTTS**: Wi-Fi Tx power is attenuated (`WIFI_DBPOWER`) and Power Save is disabled to prevent hardware brownouts and RF cross-talk from corrupting the sensitive analog measurements.
- **Real-Time Diagnostics**: `esp_rt_diagnostics` accumulates timings, overruns, and ADC events in the control task. `esp_rt_diagnostics_reporter` moves snapshots to a Core 0 task and classifies windows that may have been affected by reporter logging.

## Project Structure

* `components/esp_mcp4922/`: The core driver component containing the high-level and bare-metal (`ll`) implementations for the MCP4922.
* `components/esp_pr/`: Reusable multi-resonant PR controller and host tests.
* `components/esp_fir/`: Reusable general, symmetric, and antisymmetric FIR kernels and host tests.
* `main/app_control.c`: Application tuning, direct `esp_pr` integration, DAC conversion, and RMS calculations.
* `main/app_fir.c`: FIR coefficients, three phase histories, and the optional target cycle benchmark.
* `main/app_diagnostics.c`: Control-loop diagnostics, ADC events, and asynchronous Core 0 presentation.
* `main/main.c`: The orchestrator. Configures the dual-core architecture, ADC DMA, Wi-Fi/MQTT connections, and runs the high-speed critical section.
* `main/secrets.txt`: (User created) Contains Wi-Fi and MQTT credentials excluded from version control.

## Control-loop diagnostics

Every five-second window reports window and lifetime counters. `pool_lost` is confirmed ADC pool loss; `backlog` is recoverable accumulated work; `read_errors` means a notification did not yield a complete frame; and `cycle_overruns` compares CPU cycles with the exact 1/14400 s budget. Component microsecond deadlines are rounded up to 70 us, while cycle accounting preserves the approximately 69.44 us boundary.

Use `quiet` windows as the normal-operation baseline. `log-affected` windows may include disturbance from earlier reporter output. ISR-to-task latency is explicitly `unmeasured`: this application drains one notification at a time and has no timestamp queue for accumulated frames, so assigning the newest timestamp would be ambiguous.

## FIR comparison

`APP_FIR_BENCHMARK` runs once at boot, before the ADC and control task start. It
feeds identical deterministic samples to three instances of the original
hand-unrolled filter and three instances of `esp_fir_process_symmetric()`. The
log reports cycles per call and per three-phase frame, equivalent time, the
relative timing difference, and the maximum numerical difference. The result is
repeated with the first diagnostics report so it is not lost among boot logs. Disable the
option after collecting results to let the linker discard the legacy filter and
benchmark storage. The normal diagnostics then measure the complete 69.44 us
control loop with the new FIR under real workload.

## Hardware Setup

* **ADC Input (ADC1)**: GPIO 4 (Phase R), GPIO 5 (Phase S), GPIO 6 (Phase T)
* **DAC SPI (SPI2)**: MOSI (GPIO 11), SCK (GPIO 12), LDAC (GPIO 14), CS1 (GPIO 10), CS2 (GPIO 9)

## How to Build and Run

1. Clone the project.
2. Create `main/secrets.txt` with your credentials based on `secrets.example.txt`:
   ```c
   #define WIFI_PASS "your_wifi_password"
   #define MQTT_PASS "your_mqtt_password"
   ```
3. Activate your ESP-IDF v5/v6 environment (`. export.sh`).
4. Build and flash:
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
- **Hardware Target:** ESP32 / ESP32-S3
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

This project is licensed under the MIT License. See the [LICENSE](LICENSE) file for details.

## Reusable PR component

`components/esp_pr` contains the controller and portable host tests.
`main/app_control.c` configures the shared model and independent phase states.
The initial anti-windup tracking time is 10 ms, configurable under
**Application PR controller** in menuconfig; zero disables it. Physical-plant
tuning and control-loop timing with anti-windup still require bench validation.
See [component documentation](components/esp_pr/README.md).
