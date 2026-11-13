# INVALID — repeat with intermittently controlled oven load

**Excluded from measurement comparisons and gain tuning by user request.**
The user identified the load as an oven that stops drawing current when it
reaches its temperature target. The user suspects an electronic relay, but
its switching implementation was not confirmed. Both powered attempts below
are invalid as stable-current commissioning runs. Retain logs for traceability;
do not use the0.470 A reading to calibrate or assess closed-loop accuracy.
This disclosure does not establish the cause of the ADC out-of-range indications
or USB disconnection. Firmware, gains and NVS were not changed by exclusion.

User confirmed source had been off during previous low-input attempt and authorized repeat. Reset unchanged shared PR+I firmware. First powered attempt aborted at boot441ms before active stage: ADC input outside test range, raw error+1.584296V relative to loaded offset1.538703V -> calibrated indication3.122999V. Filtered error+2.132493V, backlog1. Asked external meter reading immediately at stop; user answered0.470A. No active current statistics, no NVS/gain/limit changes.

Repeated once to distinguish startup settling, no code changes. Second attempt aborted boot4751ms (about0.32s after active stage start) for ADC input outside test range. Log then recorded USB read failure Errno6 Device not configured, so final raw ADC/statistics were not received. Firmware logged that it returned DACs neutral and stopped control; physical state after USB disconnect/reconnection was not independently verified. Port subsequently reappeared; no additional automatic reset was performed. Asked final external meter reading immediately, user subsequently requested discarding this test because the oven cycles its load. Do not assert transient, clipping, wiring or brownout as a proven cause. Source-on confirmation and primary current do not establish actual GPIO4 voltage.

[First powered attempt](shared048-powered-monitor.log), [settling repeat](shared047-settled-monitor.log).
