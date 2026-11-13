# Closed-loop DC test at reported 0.80 A

User reported primary current increased from 0.79 to 0.80 A, external GPIO4 DC reading 1.76 V, then requested closed-loop measurement. Current reference is report metadata, not controller setpoint.

Built and flashed with CALIB_IS_ON=0, TUNING_IS_ON=1, TUNING_DC_TEST=1, TUNING_HOLD_NEUTRAL=0, ADC_QUIET_SPI=0, ADC_USE_IDF_TIMING=0, TEST_IS_ON=0. Restored exact 14,400 frame/s clock and SPI actuation. R only; unchanged PR Kp=1/Kr60=10 and PID Kp=0/Ki=2/Kd=0. Loaded R offset 1.578633 V; no NVS writes. Rs=1985 ohms, nominal 2000 turns, references 3.36 V.

Completed 4 s neutral plus 60 s closed loop, then ADC stopped and DAC returned to neutral. Final 10 s: command-derived primary compensation mean 0.543746 A, total RMS 0.543788 A, -32.03% against reported 0.80 A. Residual mean -0.088 mV, AC RMS 5.760 mV; ADC mean 1.578546 V, total RMS 1.578596 V. No PR/PID clipping. Neutral baseline ADC mean 1.708988 V, RMS 1.709042 V. Displayed diagnostics: no backlog, ADC errors, pool loss, cycle/deadline overruns; maximum observed task execution 39 us, not a worst-case guarantee.

The loop nulls the loaded offset, but this does not independently validate the physical current scale or the zero. Do not adjust gains or turns just to force agreement with 0.80 A. Source and flashed firmware retain finite closed-loop mode; reset repeats the test. This supersedes previous notes describing the current source as read-only quiet-SPI/default-clock diagnostics.

Logs: [monitor](closed080-monitor.log), [flash](closed080-flash.log).
