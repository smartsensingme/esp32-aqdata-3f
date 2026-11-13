# Reduced-gain retry after wiring adjustment

User adjusted wires and requested another attempt. Repeated finite R-only closed-loop configuration with unchanged Kp0.2, Kr60=0, Ki1, existing±2 Vsoft limits and NVS offset1.495515 V. Changed TUNING_HOLD_NEUTRAL1 to0, built and flashed. Before active stage, test stopped at boot420ms: ADC input outside test range. Last raw_error=-1.402515 V, giving calibrated ADC voltage0.093000 V, below0.1 V lower threshold. Filtered error-1.391014 V, backlog1. No valid active current result; controller did not reach closed-loop stage. DAC returned to2048, ADC stopped. No NVS writes, no gain/limit changes. User asked for simultaneous physical GPIO4-to-ESP32-GND voltage. Reset repeats finite attempt. Build/flash and git diff --check passed.

[Monitor](retry-wires-monitor.log), [flash](retry-wires-flash.log).
