# Repeat after Hall sensor restored to core

User disclosed sensor had been outside core, restored it and reported primary0.89..0.90 A. Report reference updated0.895, same Kp0.2/Kr60=0/Ki10, offset1.495515 and limits. Build/flash passed. Neutral ADC mean1.749770 V, AC RMS15.502mV, filtered residual mean+256.673mV. Test aborted at boot17.320s (about12.90s after control start):100ms continuously at test limit. Last raw error-29.515mV, filtered+8.134mV, backlog1. Predicate includes PID branch saturation, not just total. No final10s active current statistics; instantaneous near-zero final filtered error is not a settled mean. No NVS or gain changes. ADC stopped, DACs2048. Sensor position during the preceding calibration is not established by this disclosure; do not assume it was outside for all previous tests/calibration. Reset repeats finite attempt.

[Monitor](sensor-restored-monitor.log), [flash](sensor-restored-flash.log).
