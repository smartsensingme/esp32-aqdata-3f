# esp_pr — proportional multi-resonant controller

Allocation-free C component for ESP-IDF, with portable host tests. One controller
has a common proportional gain and up to 16 parallel resonators. Independent
controllers can share an immutable prepared model; their dynamic states remain
separate. There is no task, lock, heap allocation, derivative or measurement
filter inside the component.

## Controller definition

For each branch, the continuous design is:

```
R_i(s) = 2 Kr_i wc_i s / (s² + 2 wc_i s + w_i²)
w_i = 2 pi frequency_hz
wc_i = 2 pi damping_hz
C(s) = Kp + sum R_i(s)
```

`Kr` is the branch gain at its central frequency. `damping_hz` is **wc/(2 pi)**;
the continuous branch's full -3 dB bandwidth is `2*damping_hz`. Damping must be
positive, so gain is finite: exact zero steady-state tracking error is not
promised. `Kr >= 0`; zero disables the contribution of that branch. Frequencies
must be strictly between zero and Nyquist. Float-rounded unstable/marginal
coefficient sets are rejected, including some extreme frequency/damping ratios.

Each branch uses two output-unit states:

```
q' = w p
p' = -w q - 2 wc p + 2 wc Kr e
branch_output = p
```

The linear update uses trapezoidal integration, prewarped separately at each
branch's frequency. Initialization evaluates transcendental functions in double
precision, then stores float coefficients. Update uses float arithmetic only.
It retains the previous error to account for both trapezoidal input endpoints.

## Anti-windup: shared post-sample tracking

The algorithm is explicitly split into a linear step and a correction step:

1. Advance all resonators using the current and previous error.
2. Compute `requested = Kp*error + sum(p_i)`.
3. Compute `applied = clamp(requested, output_min, output_max)`.
4. Return that applied output; correct the states for the **next** sample:

```
delta = applied - requested
alpha = 1 - exp(-Ts / Tt)
share_i = Kr_i / sum(Kr)
p_i += share_i * alpha * delta
q_i remains unchanged during this correction substep
```

There is only one tunable anti-windup parameter, `anti_windup_tracking_time_s`.
Zero disables correction. The distribution rule is fixed and documented, not a
set of additional per-branch tuning knobs. Zero-gain branches receive no
correction; if all gains are zero the controller is proportional only.
The weights sum to one, preventing multiplication of the total correction by
the number of branches. Oscillator rotation continues on every linear step.

The exponential is calculated once during preparation. It bounds the correction
fraction for very small Tt; this is **not** a proof of closed-loop stability or a
guarantee of recovery within Tt. This is a discrete tracking design, not a claim
of an exact discretization of a continuous saturated PR system. It only sees the
limits configured here: downstream limiting, slew constraints, feedforward or
coupled actuator limits must be accounted for before using this anti-windup
scheme. DAC quantization is not fed back.

## Usage

```c
#include "esp_pr.h"

static esp_pr_model_t model; // Internal RAM for cache-disabled operation.
static esp_pr_t phase_r, phase_s, phase_t;

void initialize(void) {
    const unsigned orders[] = {1, 3, 5, 7};
    const float kr[] = {60000, 6000, 10000, 13000};
    const float damping[] = {5, 5, 5, 5};
    esp_pr_resonator_config_t branches[4];
    ESP_ERROR_CHECK(esp_pr_make_harmonics(branches, 4, 60, orders, kr, damping));
    // Alternatively, fill branches directly with frequency_hz, kr, damping_hz.
    const esp_pr_config_t config = {
        .sample_rate_hz = 14400,
        .kp = 5,
        .output_min = -6,
        .output_max = 6,
        .anti_windup_tracking_time_s = 0.010f, // Provisional application value.
        .resonators = branches,
        .resonator_count = 4,
    };
    ESP_ERROR_CHECK(esp_pr_prepare(&model, &config));
    ESP_ERROR_CHECK(esp_pr_init(&phase_r, &model));
    ESP_ERROR_CHECK(esp_pr_init(&phase_s, &model));
    ESP_ERROR_CHECK(esp_pr_init(&phase_t, &model));
}
```

Call `esp_pr_update(&phase_r, reference, measurement, &output)` once per sample.
For an already computed error, use `reference=error, measurement=0`.
The component takes units chosen by the application; this application's output
unit is the conditioned bipolar voltage, not the DAC code.

Preparation copies the input configuration into coefficients; the input arrays
may leave scope. The prepared model must outlive its controllers and must not
be modified while in use. Reconfiguration requires stopping updates, preparing
the model and reinitializing all referencing controllers. Reset clears history
without changing the model; it is not a bumpless transfer operation.

A single owner must serialize each controller's updates and reset. Diagnostic
reads from a different task/core require caller synchronization. Diagnostics
report the branch contributions **before** anti-windup correction; `branches`
contains the corrected states. `esp_pr_get_diagnostics` is a task-context copy.
Zero-initialize objects before first use and call prepare/init before update.

Invalid/nonfinite arguments or intermediate overflow return an error without
changing controller state or the output destination. The caller must handle the
error; do not compile with `-ffast-math`, which invalidates finite checks. In this
application an update error resets the affected phase and commands 0 V. The DAC
conversion additionally maps nonfinite input to the midpoint.

`esp_pr_update` and reset are placed in IRAM on ESP targets. The model, controller,
output and task stack must all reside in internal RAM for cache-disabled use.
Runtime and memory are bounded by the configured branch count (the temporary
arrays and persistent storage reserve the maximum 16 branches). No ISR timing
or hardware deadline has been measured by the host tests.

## This project's integration

`main/app_control.c` preserves the recovered original tuning:

| Frequency | Kr | damping_hz |
|---|---:|---:|
| 60 Hz | 60000 | 5 |
| 180 Hz | 6000 | 5 |
| 300 Hz | 10000 | 5 |
| 420 Hz | 13000 | 5 |

Kp is 5 and sample rate is 14400 Hz. With anti-windup disabled, the generated
linear controller matches the original Octave biquad table within floating-point
precision. During saturation, enabling anti-windup intentionally changes the
subsequent state trajectory.

`idf.py menuconfig` → **Application PR controller** → anti-windup tracking time.
`CONFIG_APP_PR_TRACKING_TIME_US=10000` is the provisional 10 ms default.
Set it to zero for comparison with the previous output-clamped controller.
Physical-plant tuning and timing validation remain necessary; no plant transfer
function was supplied. The DAC mapping remains -6 V → 0, 0 V → 2048,
+6 V → 4095. The 0 V midpoint is rounded to the nearest code.

## Host validation

```sh
cmake -S components/esp_pr/tests -B /tmp/esp_pr_host
cmake --build /tmp/esp_pr_host
ctest --test-dir /tmp/esp_pr_host --output-on-failure
```

The tests check the historical coefficient table, analytical frequency response,
invalid inputs and overflow without partial state changes, independent phase
history, reset, harmonic construction, P-only operation, inactive branches,
unsaturated equivalence, saturation recovery and extreme tracking times. A second
host test compiles the real application adapter and DAC conversion against
minimal platform declarations and checks all 4096 DAC codes, bipolar endpoints
and per-phase fault recovery. It does not simulate peripheral or RTOS behavior.

The reference-table test uses double arithmetic; the component uses float. On
the development host the mixed-harmonic relative RMS difference was about
`7.6e-7`. Very high resonant gains make small DC leakage from float cancellation
visible: the constant-input test differs by about 0.8% of the **proportional DC
response**, while the tested nonzero-frequency responses differ by less than
0.001%. These are bounded numerical tests, not measurements of the real plant.
