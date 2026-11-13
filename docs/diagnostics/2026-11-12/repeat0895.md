# Repeat with external current between0.89 and0.90 A

User requested repeat at newly reported0.89..0.90 A. Changed report-only reference0.80 to0.895 A, preserving Kp0.2/Kr60=0/Ki10, NVS R offset1.495515 V, finite4s neutral+60s control and existing limits. Build and flash passed. Test aborted boot12.770s (about8.35s after active stage start) due100ms continuously at limit. Existing predicate includes PID branch saturation as well as total saturation, so no claim that total was exactly±2 Vsoft. Neutral baseline ADC1.663202 V, raw AC16.593mV, filtered mean residual+169.280mV. At abort raw error+3.485mV, filtered+16.162mV, backlog1. Final10s active statistics never collected: no valid final Icomp_mean/Icomp_rms; baseline zero current is not a closed-loop result. ADC stopped, all DACs neutral2048, no NVS writes, no automatic gain/limit changes. Reset repeats finite attempt. git diff --check passed.

[Monitor](repeat0895-monitor.log), [flash](repeat0895-flash.log).
