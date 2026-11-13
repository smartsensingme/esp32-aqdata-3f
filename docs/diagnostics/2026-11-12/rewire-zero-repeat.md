# Repeated neutral test with GPIO7 grounded

User confirmed zero primary current and GPIO7 grounded. Repeated existing 84 s read-only neutral firmware with monitor reset. GPIO7 ignored by this diagnostic; no NVS writes. DAC codes 2048 throughout; loaded R offset 1.477462 V.

Twenty short captures: ADC mean average 1.497209 V (window means 1.487564..1.501416 V), AC RMS 14.578..18.634 mV. Final four long useful windows: ADC means 1.497013, 1.496929, 1.497030, 1.496281 V; AC RMS 15.972..16.083 mV. Normalized filtered mean candidate from rounded log residuals and FIR gain 1.00951398 is approximately 1.496813 V, final-four spread about 0.75 mV. Previous repeat candidate about 1.49657 V: agreement around 0.24 mV. No offset committed because this was a read-only comparison. ADC stopped at completion, DAC remains neutral; reset repeats same test.

Log: [monitor](rewire-zero-repeat-monitor.log).
