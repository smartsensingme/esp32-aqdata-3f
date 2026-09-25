# esp_pr — proportional multi-resonant controller

Allocation-free C component for ESP-IDF, with portable host tests. One controller
has a common proportional gain and up to 16 parallel resonators. Independent
controllers can share an immutable prepared model; their dynamic states remain
separate. There is no task, lock, heap allocation, derivative or measurement
filter inside the component.

## Installation

The component supports ESP-IDF 6.0 or newer. In a consuming application's
`main/idf_component.yml`, declare the direct dependency with the public HTTPS
URL and a tested release tag:

```yaml
dependencies:
  esp_pr:
    git: https://github.com/smartsensingme/esp_pr.git
    version: v0.1.0
```

The manifest above belongs to the consuming application. This repository's
root `idf_component.yml` belongs to `esp_pr` itself and declares its ESP-IDF
compatibility. The component has no third-party component dependencies.

For a manual or offline installation, copy or clone this repository into the
application's `components/esp_pr` directory. If an existing Component Manager
manifest selects a generated `managed_components/esp_pr`, either remove that
dependency for the local build or configure an explicit local override. Setting
`IDF_COMPONENT_MANAGER=0` disables Component Manager processing for a build,
but then every required component must be available locally.

Add `esp_pr` to the consuming component's build requirements:

```cmake
idf_component_register(SRCS "control.c" REQUIRES esp_pr)
```

## Architecture and lifecycle

The component separates immutable tuning from mutable stream state:

```text
branch descriptions ──> prepare model ──> initialize controller(s)
                                                   │
                                                   ├──> update once per sample
                                                   ├──> copy diagnostics
                                                   └──> reset state
```

`esp_pr_prepare()` copies every required tuning value into discrete
coefficients, so the input configuration and resonator array may leave scope.
The resulting model must remain immutable and outlive every controller that
references it. Each phase or signal stream needs a separate `esp_pr_t`; sharing
one instance would mix its previous error, resonator states, and diagnostics.

Reconfiguration is an application-level transition: stop updates, prepare the
model, and reinitialize all controllers that refer to it. Reset clears dynamic
state and diagnostics while retaining the model. Neither operation provides a
bumpless transfer between tunings.

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

A complete sample-loop call with an explicit fallback is:

```c
float command;
esp_err_t err = esp_pr_update(&phase_r, reference, measurement, &command);
if (err != ESP_OK) {
    (void)esp_pr_reset(&phase_r);
    command = 0.0f; // Application-selected safe command.
}
apply_command(command);
```

Preparation copies the input configuration into coefficients; the input arrays
may leave scope. The prepared model must outlive its controllers and must not
be modified while in use. Reconfiguration requires stopping updates, preparing
the model and reinitializing all referencing controllers. Reset clears history
without changing the model; it is not a bumpless transfer operation.

A single owner must serialize each controller's updates and reset. Diagnostic
reads from a different task/core require caller synchronization. Diagnostics
report the branch contributions **before** anti-windup correction; `branches`
contains the corrected states. `esp_pr_get_diagnostics` is a task-context copy.
Call prepare/init before update; successful initialization sets the complete
destination object, so prior zero-initialization is optional.

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

## API behavior and error handling

| Function | Called from | Work and state transition | Errors |
|---|---|---|---|
| `esp_pr_make_harmonics` | Application initialization | Builds 1–16 branch descriptions atomically | `ESP_ERR_INVALID_ARG` for invalid pointers/counts or generated branch values |
| `esp_pr_prepare` | Application initialization | Validates, discretizes, checks poles, and commits an immutable model | `ESP_ERR_INVALID_ARG`; model unchanged |
| `esp_pr_init` | Application initialization | Binds a prepared model and zeroes one controller | `ESP_ERR_INVALID_ARG`; controller unchanged |
| `esp_pr_update` | Periodic owner task | Advances one sample, saturates output, applies post-sample tracking, commits diagnostics | Invalid state/input/arithmetic; controller and output unchanged |
| `esp_pr_reset` | Application recovery/lifecycle | Clears all states and diagnostics | `ESP_ERR_INVALID_STATE` for invalid controller |
| `esp_pr_get_diagnostics` | Application task context | Copies the latest committed diagnostics | Invalid state or null destination |

The component has no Kconfig symbols and no external component dependencies.
Consumers list `esp_pr` in `REQUIRES` or `PRIV_REQUIRES`. Configuration is
provided entirely through `esp_pr_config_t`; application Kconfig values may be
converted into those fields before preparation.

The component does not check whether `output` aliases controller/model memory;
that documented precondition belongs to the caller. It does check null pointers,
nonfinite inputs, overflowed intermediate values, and model state. Do not use
`-ffast-math`, because its finite-value assumptions undermine this recovery
contract.

## Host validation

```sh
cmake -S components/esp_pr/tests -B /tmp/esp_pr_host
cmake --build /tmp/esp_pr_host
ctest --test-dir /tmp/esp_pr_host --output-on-failure
```

The tests check the historical coefficient table, analytical frequency response,
invalid inputs and overflow without partial state changes, independent stream
history, reset, harmonic construction, P-only operation, inactive branches,
unsaturated equivalence, saturation recovery and extreme tracking times. They
do not simulate peripheral, plant, actuator, or RTOS behavior.

The reference-table test uses double arithmetic; the component uses float. On
the development host the mixed-harmonic relative RMS difference was about
`7.6e-7`. Very high resonant gains make small DC leakage from float cancellation
visible: the constant-input test differs by about 0.8% of the **proportional DC
response**, while the tested nonzero-frequency responses differ by less than
0.001%. These are bounded numerical tests, not measurements of the real plant.

## ESP-IDF example

`examples/basic` is a standalone ESP-IDF application that uses the repository
root as a local component, prepares four harmonic branches, initializes one
controller, and performs one update:

```sh
idf.py -C examples/basic set-target esp32s3
idf.py -C examples/basic build
```

The generated `sdkconfig`, `dependencies.lock`, and build directory are ignored.
Applications should choose gains, limits, tracking time, update rate, and fault
handling from their actual plant and actuator requirements.

## Troubleshooting

- `esp_pr_prepare()` rejects a branch near Nyquist: reduce its center frequency
  or increase the sample rate; every frequency must be strictly below Fs/2.
- A very narrow or extreme branch is rejected despite valid-looking continuous
  parameters: the float-rounded discrete poles failed the strict Jury checks.
  Adjust frequency/damping or validate a higher-precision design separately.
- Output remains clipped after the overload disappears: inspect
  `unsaturated_output`, `saturation_error`, and branch states; verify the
  downstream actuator uses the same limits and tune the tracking time on the
  physical plant.
- A zero-gain branch does nothing: this is intentional; it receives zero input
  gain and zero anti-windup correction share.
- Diagnostics appear inconsistent across cores: copy them under caller-provided
  synchronization or publish a snapshot from the controller's owner task.
- A numerical update fails: retain or select a safe actuator command, reset or
  reinitialize that controller according to the application's recovery policy,
  and investigate nonfinite measurements or state growth.

## License

This component is available under the MIT License. See `LICENSE`.
