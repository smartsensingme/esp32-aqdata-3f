# Closed loop with newly saved offset and external 0.80 A

User reported 0.80 A applied. Changed TUNING_HOLD_NEUTRAL=2 to 0 and report reference to 0.80 A; calibration disabled even with GPIO7 grounded. Preserved user gains PR Kp=2/Kr60=100, PID Kp=0/Ki=20/Kd=0 and total ±2 Vsoft limit (10% PR,90% PID). Build and flash passed. Boot loaded R offset 1.495515 V, confirming persistence from previous calibration.

After 4 s neutral, test aborted at boot time 4.950 s with '100 ms continuously at test limit'. The predicate also treats PID branch saturation as a limit, so this log alone does not prove the sum reached exactly ±2 Vsoft. Last raw error +0.209485 V (ADC about1.705 V), filtered error +0.214068 V, backlog1. Neutral useful baseline ADC mean1.656937 V, AC RMS15.181 mV, filtered mean error+162.960 mV. No final10s active statistics were collected; baseline Icomp=0 must not be reported as the closed-loop measured current. ADC stopped and all DACs returned to2048. No NVS writes or automatic limit/gain increases. Reset repeats finite closed-loop attempt. Existing polarity validation predates analog modifications; no claim that polarity is now wrong, but offset calibration alone does not validate the modified plant response.

Logs: [monitor](newzero080-monitor.log), [flash](newzero080-flash.log).
