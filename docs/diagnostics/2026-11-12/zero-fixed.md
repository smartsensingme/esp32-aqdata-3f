# Stable recalibration after offset circuit problem corrected

User reported offset problem corrected and requested repeat; maintained previously confirmed zero primary current and GPIO7 grounded. Reset existing mode2 firmware, no source/build/flash/gain changes. Completed84s all DACs2048. Final-four filtered mean spread approximately1.02mV, below2mV gate. Firmware saved R offset1.538703 V to NVS after ADC stopped, retaining S0.354058 and T0.036351 V. Final filtered AC RMS approximately7.3mV. Stage residuals in the report are relative to the old1.495515 V loaded at boot; do not treat those+43..44mV as residuals against the newly saved offset. Test stopped, DACsneutral. Release GPIO7; reset with it low repeats calibration. No reboot verification performed. git diff --check passed.

[Monitor](zero-fixed-monitor.log).
