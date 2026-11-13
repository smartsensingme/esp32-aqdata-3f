# Successful identical repeat with sensor in core

User requested repeat. Monitor reset same firmware, no source/build/flash/gain/limit/NVS changes. Kp0.2/Kr60=0/Ki10, offset1.495515 V, reference metadata0.895 A from user0.89..0.90 A report. Completed4s neutral+60s control. Final10s estimated current mean0.984055 A, total RMS0.984058 A (+9.95% vs0.895 A; +9.34..10.57% vs0.90..0.89 A). Filtered residual mean-0.143mV, AC RMS6.937mV, ADC mean1.495374 V/RMS1.495456 V. Command peak1.7633 Vsoft; no PR/PID clipping. Neutral ADC mean1.912715 V, significantly different from prior1.749770 V; unchanged configuration does not establish identical analog conditions. All DACs returned2048, ADC/control stopped. No NVS writes. Reset repeats finite test.

[Monitor](sensor-restored-repeat-monitor.log).
