# AC commissioning with external True RMS reference

User applied an AC appliance load and initially reported0.560 A True RMS, later corrected to0.549 A during final Kr60=14 run. References are report-only, never a current setpoint. Last flashed binary still reports0.560; final corrected comparison is+2.1153% for0.560613 A estimate. Source metadata updated0.549 after measurement, without another hardware run; no actuation changes. Do not retroactively apply0.549 to earlier runs whose simultaneous reference was not confirmed.

## Implementation

Added app_tuning_init_ac: same4s neutral+60s R-only closed-loop test, final10s stats, total±2 Vsoft bound. PR gets90% and integral10% in AC, retaining default10%/90% for DC/normal initializers. app_control_init_with_allocation sets symmetric PR bounds at startup, app_parallel_init validates them and assigns the remaining limit to integral. No runtime allocation/state migration. ADC guard0.1..3.0 V and existing aborts preserved; PR clipping reported separately from existing total/PID stop condition.

AC comparison uses Icomp_rms, not abs(Icomp_mean); logs also show nominal60/180/300/420 Hz residual correlations. AC residual is signal plus noise, not a noise-only statistic. Correlations assume60 Hz and do not measure actual mains frequency or all harmonics. Statistics accumulated in existing bounded internal per-stage storage, no formatted logging in periodic task. Existing filtered RMS/mean calculations unchanged. Gain macros added for180/300/420 and connected to existing PR harmonic preparation.

Initial connection failed twice; user reset/reconnected, flash then worked. R offset1.538703 V loaded throughout, NVS not changed. Analog input neutral mean during AC was~1.35 V, ~−188mV from saved zero: Ki2 reached the small integral budget and test aborted. Ki disabled for subsequent AC isolation. This does not solve DC zero error or validate simultaneous AC/DC control.

## Sequential results

| Kp | Kr60 | Kr180/300/420 | Ki | Current RMS A | Residual AC RMS mV | PR clipping | Result |
|---:|---:|---|---:|---:|---:|---:|---|
|0.2|10|0/0/0|2|not measured|—|—|integral limit abort|
|0.2|10|0/0/0|0|0.490746|149.863|0%|completed|
|0.2|50|0/0/0|0|0.755296|189.120|37.262%|rejected as clipped|
|0.2|20|0/0/0|0|not measured|—|—|ADC sample0 V, aborted|
|0.2|10|0/0/0|0|0.488673|148.523|0%|repeat completed|
|0.2|10|3/2/1|0|0.484915|144.937|0%|completed|
|1|10|3/2/1|0|0.509536|139.494|0%|completed|
|1|12|3/2/1|0|0.537695|137.267|0%|completed|
|1|14|3/2/1|0|0.560613|135.785|0%|completed, provisional final|

Final: mean estimated current−0.087073 A, AC-only current RMS~0.553810 A (total RMS includes this apparent DC command); ADC mean1.386958 V/RMS1.393585 V/AC135.740mV; filtered residual mean−153.181mV, total RMS204.700mV. Nominal residual correlations60=9.884,180=3.652,300=0.127,420=0.104 mV RMS. Command peak1.6951 Vsoft, below PR1.8; no branch clipping. Large remaining AC energy is not explained by these four correlations alone. Do not call it exclusively harmonics or noise without further measurement. Similar current RMS to external reference is not proof of accurate compensation or optimal tuning. Source/DC gain calibration and mechanical conditions require validation, particularly zero with final assembly and no primary current. No claim that the single0V sample was caused by gain rather than hardware.

Final firmware CALIB_IS_ON0, TEST_IS_ON0, TUNING_IS_ON1, TUNING_DC_TEST1, TUNING_AC_TEST1, TUNING_HOLD_NEUTRAL0; quiet SPI/default ADC timing disabled. Gains Kp1/Kr60=14/Kr180=3/Kr300=2/Kr420=1/Ki0. Ended finite test, ADC/control stopped, all DACs2048. Reset repeats AC test. GPIO7 ignored in this mode. Wi-Fi/MQTT remain off.

Host parallel-control and bounded-tuning suites passed, including AC90/10 allocation and RMS comparison vs signed mean. DC convergence scenarios explicitly enable Ki2 in the test even when the selected AC profile has Ki0; no claim that disabled integration converges in DC. New allocation validation tests reject PR bounds exceeding total limit. Build/flash passed per run after connection restored. Final diagnostic windows showed no ADC read/pool losses, observed execution maximum41us; observed maxima are not worst-case bounds. git diff --check passed. No commits or pushes.

Logs: [first AC attempt](ac10-monitor.log), [PR10](ac-pr10-monitor.log), [PR50](ac-pr50-monitor.log), [PR20](ac-pr20-monitor.log), [PR10 repeat](ac-pr10-repeat-monitor.log), [harmonic branches](ac-harm-monitor.log), [Kp1](ac-kp1-monitor.log), [Kr12](ac-kr12-monitor.log), [Kr14](ac-kr14-monitor.log).
