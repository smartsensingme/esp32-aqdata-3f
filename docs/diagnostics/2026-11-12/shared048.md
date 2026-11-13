# Requested repeat near0.48 A

User requested unchanged repeat and final instrument reading immediately at stop. Reset existing shared-tracking firmware via stop-detecting capture helper; no source/build/flash/gain/NVS changes. Firmware aborted at boot631ms before active stage: ADC input outside test range, last raw error-1.441703V relative to loaded offset1.538703V, giving ADC0.097000V. Filtered error-1.411470V, backlog1. No closed-loop current estimate or active statistics. All DACs neutral2048, ADC stopped. Asked for instrument reading immediately after capture returned; user answered0.482A. This external primary reading does not verify analog sensor-supply state or GPIO4 voltage. No cause asserted without physical confirmation. Reference compiled in binary still0.559A from previous run; it did not enter actuation or cause abort. NVS preserved.

[Monitor](shared048-monitor.log).
