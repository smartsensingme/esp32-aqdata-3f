# Closed loop after stable recalibration, corrected reference0.905 A

User requested closed loop initially reporting0.95 A, then corrected to0.905 A during active run. Firmware flashed with report-only reference0.95; it does not enter control, so ongoing run remained valid. Source metadata corrected to0.905 after run, not reflashed; binary still logs0.95 and its percent comparison must be disregarded. Preserve raw log. No gain/limit changes: Kp0.2/Kr60=0/Ki10, finite4s neutral+60s active. Loaded new R offset1.538703 V from NVS, no calibration with current present.

Completed without branch clipping. Final10s estimated compensation mean0.865036 A, total RMS0.865039 A. Against corrected0.905 A: -39.964mA, -4.41591%. ADC mean1.538588 V, RMS1.538674 V, AC RMS16.259mV. Filtered residual mean-0.117mV, AC RMS7.330mV. Command peak1.5471 Vsoft. Neutral baseline ADC1.826782 V. All DACs returned2048, ADC/control stopped. Offset unchanged. Build/flash and git diff --check passed; source-only reference correction changes reporting, not actuation. Reset repeats finite test with binary metadata0.95 until reflashed.

[Monitor](newzero0905-monitor.log), [flash](newzero0905-flash.log).
