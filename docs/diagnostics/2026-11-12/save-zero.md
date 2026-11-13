# Stable neutral offset saved to NVS

User confirmed GPIO7 remained grounded and requested saving the zero, retaining the zero-primary-current condition from previous tests. Changed only TUNING_HOLD_NEUTRAL=1 to 2, built and flashed existing 84 s neutral calibration. GPIO7 request recognized. Exact ADC timing and per-frame SPI writes retained, all DACs fixed at 2048. Last-four normalized filtered mean spread approximately 1.16 mV, below 2 mV criterion. After ADC stopped, firmware reported Calibration saved (DMA) R/S/T: 1.495515 / 0.354058 / 0.036351 V. Only R updated, S/T retained. Filtered AC RMS approximately 7.5 mV in final windows. No reboot verification performed, avoiding another calibration while GPIO7 remains grounded. Firmware remains in mode 2: GPIO7 low on reset repeats the calibration; release GPIO7 after completion. ADC/control stopped, DAC neutral. Build, flash and git diff --check passed.

Logs: [monitor](save-zero-monitor.log), [flash](save-zero-flash.log).
