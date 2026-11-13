# Retuning attempt after new zero

User authorized autonomous gain tuning to seek zero residual at reported external0.80 A. Retained offset1.495515 V, exact ADC clock, finite-test bounds. Reduced PR Kp2 to0.2, Kr60=100 to0 (isolate DC response), PID Ki20 to1. Test still aborted after100ms at limit, now boot12.86s versus4.95s previously. Baseline filtered error+164.008mV; last filtered error+210.006mV. No valid final10s active current estimate. Reducing gains slowed the approach to the limit without finding zero.

Measured existing bounded oneshot polarity test at fixed current: DAC2048->2304 yielded before1.6904/high1.7167/after1.6902 V, +26.37mV, threshold50.95mV. Larger step2048->2688 (within earlier finite DAC range) yielded before1.6898/high1.7185/after1.6921 V, +27.57mV, threshold46.57mV. Both inconclusive; positive response observed, but criterion not met. Larger command did not produce proportional change. Do not claim confirmed reversed polarity or a unique root cause; oneshot differs from normal DMA and current/time may drift. No further blind gain/limit increases, no NVS writes.

Final firmware changed back to read-only neutral mode CALIB_IS_ON0/TUNING_IS_ON1/TUNING_DC_TEST1/TUNING_HOLD_NEUTRAL1, reference0.80 metadata. Retained reduced gains0.2/0/1, no applied control in this mode. Polarity step restored128 for future explicit tests. Build/flash verification recorded separately. All DACs commanded2048; reset repeats neutral diagnostic, not active control.

[Reduced-gain log](tune-low-monitor.log), [256-code probe](retune-polarity-monitor.log), [640-code probe](retune-polarity640-monitor.log).
