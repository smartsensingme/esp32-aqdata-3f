# Neutral ADC test after breadboard rewiring

Repeated existing 84 s neutral-only firmware via monitor reset; no build, flash, gain changes or NVS writes. All DAC codes fixed at 2048; exact 14,400 frame/s acquisition, normal per-frame SPI writes. Offset R loaded 1.477462 V. User previously reported scope GPIO4 1.60 V after rewiring; not reconfirmed simultaneously during this run.

Twenty 100 ms snapshots: ADC window means 1.487798..1.501643 V, average 1.495973 V; AC RMS 14.749..17.086 mV. FIR DC-normalized AC RMS 6.827..8.892 mV. Full useful stage windows ADC means 1.493550..1.496799 V, AC RMS 16.090..16.361 mV. Last four stage filtered mean spread about 0.827 mV, candidate filtered offset about 1.49657 V (derived from rounded printed residual means and FIR DC gain 1.00951398); not committed. Substantially less AC than previous pre-rewire run (raw stage AC RMS 112..409 mV), but remaining disagreement with earlier scope 1.60 V is not resolved by this test. Capture finished; ADC stopped, DAC remains neutral, control stopped. Reset repeats neutral test.

Log: [monitor](rewire-zero-monitor.log).
