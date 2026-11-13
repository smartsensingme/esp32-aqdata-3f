# Further tuning outside the60Hz branch

User asked whether only60Hz gain was being adjusted and requested continued tuning. Confirmed previous last two changes affected60Hz, while180/300/420 were enabled earlier with3/2/1. Added nominal even-harmonic residual correlations120/240/360, with a host test injecting120Hz and checking expected7.071mV RMS plus rejection at240/360. No changes to NVS or total actuator limit. Reference metadata0.549 A.

| Kp | Kr60 | Kr120 | Kr180/300/420 | Current total RMS A | Current mean A | Residual AC RMS mV | PR clipping |
|---:|---:|---:|---|---:|---:|---:|---:|
|1|14|0|3/2/1|0.561891|-0.089018|135.702|0%|
|1|14|3|3/2/1|0.579038|-0.078744|120.770|12.117%|
|1|14|1|3/2/1|0.575567|-0.089101|131.100|4.012%|
|0.5|14|1|3/2/1|0.563355|-0.049748|135.379|0%|

Higher120 gain decreased residual AC but caused clipping, so not accepted as a full-range valid result. Reduced Kp recovered margin, final command peak1.7105Vsoft below PR1.8. Final filtered residual mean-173.461mV, AC135.379mV, total220.037mV: not near-zero error. Final nominal correlations60=66.395,120=114.362,180=27.441,240=8.017,300=0.703,360=1.788,420=3.715mV RMS. Correlation values vary between runs and assume exact nominal frequency; not a measured frequency spectrum or unique attribution of all AC energy. Compared with earlier no120 profile, no large reduction of residual AC remained after avoiding clipping. All runs ended with DACs2048 and control/ADC stopped. Offset1.538703V unchanged.

Final provisional source/flashed profile Kp0.5, Kr60=14, Kr120=1, Kr180=3, Kr300=2, Kr420=1, Ki0. Five configurable resonant slots now in PR model; default/normal initializer still uses10% PR allocation, AC initializer90%. Finite AC test only, not validated normal/DC operation. Reset repeats finite AC test.

User identified meter as Minipa ET-2507 without suffix and automatic, later described sine plus continuous line on display. Exact original manual was not located; the accessible official ET-2507B manual is a different revision and must not be treated as proof of original ET-2507 mode. Do not infer AC+DC sensing from Auto or display description alone. Current comparison remains ambiguous until instrument mode is established. Final firmware total RMS0.563355 A gives+2.61475% vs0.549. AC-only derived sqrt(total^2-mean^2)=0.561154 A gives+2.21387%, conditional on external meter measuring AC-only. User has not confirmed instrument mode; keep both interpretations explicit. No physical wiring changes requested.

Host parallel and bounded tuning suites passed, including even-harmonic reporting and AC/DC allocation tests. Firmware builds/flashes passed, git diff --check passed. Last hardware log records one deadline overrun; observed timing maxima are not worst-case bounds. See raw log for authoritative counters.

[Even-harmonic baseline](ac-even-monitor.log), [Kr120=3](ac-120-monitor.log), [Kr120=1](ac-120-low-monitor.log), [Kp0.5](ac-120-final-monitor.log).
