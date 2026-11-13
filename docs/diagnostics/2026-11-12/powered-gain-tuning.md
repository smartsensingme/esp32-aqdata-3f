# Gain tuning with analog supplies restored

User disclosed analog supplies had been off during previous0.093 V attempt, then confirmed they were on and requested renewed autonomous tuning. This explains the previous low-input abort; do not retroactively claim every earlier response anomaly was caused by the same supply condition.

Retained R NVS offset1.495515 V, reported primary0.80 A, R only, total±2 Vsoft limits with PR10%/PID90%, exact14.4kHz DMA, finite4s neutral+60s closed loop. Kp0.2, Kr60=0 to isolate DC integral response. No NVS writes.

| Ki | Final10s estimated current mean/RMS A | Filtered residual mean/AC RMS mV | Neutral ADC mean V |
|---:|---:|---:|---:|
|1|0.866172 /0.866173|+0.366 /8.069|1.678518|
|2|0.872719 /0.872720|+0.355 /8.025|1.681754|
|10|0.881470 /0.881491|-0.285 /8.017|1.686335|

All three completed with zero reported branch clipping; each returned DACs to2048. Final Ki10 command peak1.5940 Vsoft, final ADC mean1.495234 V/RMS1.495329 V. Difference vs nominal0.80 A +10.18%; low mean residual does not establish current calibration. Neutral ADC baseline changed during successive tests, so changing estimated current cannot be attributed solely to gain; actual external current was not rechecked during each test. No claim of optimal gains or AC tuning. Mean residual is near zero compared with8mV AC; cannot promise instantaneous zero.

Source/flashed gains now Kp0.2, Kr60=0, Ki10. CALIB_IS_ON0/TUNING_IS_ON1/TUNING_DC_TEST1/TUNING_HOLD_NEUTRAL0. Finite mode remains: reset repeats4+60s attempt, then neutral. GPIO7 ignored; no recalibration with applied current. Host parallel-control suite and firmware build/flash passed for Ki2 and10. git diff --check passed. Logs include observed timing diagnostics; no worst-case timing guarantee.

[Ki1](powered-retune-monitor.log), [Ki2](powered-ki2-monitor.log), [Ki10](powered-ki10-monitor.log).
