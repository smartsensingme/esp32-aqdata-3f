# Return to reported0.80 A

User restored primary current0.80 A and requested repeat. Changed report-only reference0.895 to0.80, retained Kp0.2/Kr60=0/Ki10, NVS offset1.495515 V, bounds and finite test. Build/flash passed. Abort at boot9.510s, about5.09s after active stage start:100ms continuously at test limit. Last raw error-12.515mV, filtered+15.446mV, backlog1. Neutral baseline ADC1.637283 V, AC RMS15.912mV, filtered residual mean+143.116mV. No final10s active current estimate. All DACs2048, ADC stopped, no NVS writes. Prior successful0.80 A test therefore was not reproduced by restoring the reported primary current alone. No unique cause inferred. Reset repeats finite test.

[Monitor](return080-monitor.log), [flash](return080-flash.log).
