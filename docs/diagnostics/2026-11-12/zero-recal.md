# Recalibration requested after current removed

User explicitly confirmed zero current and GPIO7 grounded. Changed finite test to TUNING_HOLD_NEUTRAL2 and reference0, preserving gains0.2/0/10 and existing offset. Build/flash passed. Ran two84s neutral captures, all DACs2048. Neither saved: first final-four filtered means spread~8.89mV, second~16.21mV, exceeding2mV gate. First raw final stage means end~1.43056V; second raw stage means fell1.421867 to1.379955V, with AC RMS~16mV and filtered AC~7.5mV. Low-frequency drift dominates calibration failure; no claim of thermal/magnetic/electronic root cause. NVS R remains1.495515V, S0.354058/T0.036351. ADC stopped at end, DACsneutral, control stopped. Mode2 persists: resetting with GPIO7 low requests calibration again; do not apply primary current before releasing GPIO7 and changing mode. No relaxed stability threshold or forced writes.

[First capture](zero-recal-monitor.log), [second capture](zero-recal-repeat-monitor.log).
