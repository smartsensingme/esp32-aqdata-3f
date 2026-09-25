# esp_fir — real FIR filters for deterministic loops

`esp_fir` is an allocation-free C component for scalar, real-coefficient finite
impulse response filters in ESP-IDF. It supports arbitrary, symmetric, and
antisymmetric impulse responses, including odd and even tap counts and filters
whose coefficients are prepared at run time. There is no compiled-in maximum
order.

The component executes the FIR only. It does not design coefficients, resample
signals, allocate memory, create tasks, synchronize callers, log, or decide how
an application handles nonfinite samples. Coefficients may come from
Parks-McClellan/Remez, windows, least squares, or another design method.

## Architecture and lifecycle

The API separates three kinds of storage:

1. The application-owned coefficient array remains immutable and alive.
2. `esp_fir_prepare()` creates one immutable `esp_fir_model_t` that refers to
   those coefficients and may be shared by any number of streams.
3. Each stream receives its own `esp_fir_t` and caller-owned history array.

The history holds `2*N` floats for an N-tap filter. Every new sample is written
twice into a mirrored circular buffer, which always exposes the newest N samples
as one contiguous span. This costs one extra write and N extra stored floats but
removes division, modulo, shifting, and wraparound branches from convolution.

The lifecycle is:

```text
coefficient array ──> prepare model ──> initialize stream(s) ──> process samples
                                              │                       │
                                              └──────── reset <───────┘
```

Preparation validates and computes metadata but does not copy coefficients.
Initialization clears the stream history. Reset clears history again without
changing the model. Stop processing before preparing a shared model again or
resetting one of its streams.

## Structures and coefficient layouts

| Structure | Required relation | Full count | Folded count | Multiplications/sample |
|---|---|---:|---:|---:|
| `ESP_FIR_GENERAL` | none | N | unsupported | N |
| `ESP_FIR_SYMMETRIC` | `h[k] = h[N-1-k]` | N | `ceil(N/2)` | `ceil(N/2)` |
| `ESP_FIR_ANTISYMMETRIC` | `h[k] = -h[N-1-k]` | N | `floor(N/2)` | `floor(N/2)` |

Folded arrays store the coefficient belonging to the first member of each
mirrored pair. An odd symmetric array appends its center coefficient. An odd
antisymmetric filter has an implicit zero center and does not store it.

For example, the symmetric response `{a, b, c, b, a}` has N=5 and folded array
`{a, b, c}`. The antisymmetric response `{a, b, 0, -b, -a}` has folded array
`{a, b}`. With FULL storage, `symmetry_tolerance` is the maximum absolute
difference accepted while checking each pair; folded storage needs no pair
validation.

## Startup behavior and DC gain

`ESP_FIR_STARTUP_ZERO` treats samples before startup as zero. Its first outputs
contain the usual FIR fill transient.

`ESP_FIR_STARTUP_PRIME_FIRST_SAMPLE` treats the entire earlier history as equal
to the first input. It is useful when the signal already exists before software
startup and an artificial transition from zero is undesirable. Preparation
computes the full coefficient sum and stores it as `model.dc_gain`; the first
result is `first_sample * dc_gain`. This value is derived automatically and is
not an additional application gain or a normalization factor.

## Complete symmetric-filter example

```c
#include "esp_check.h"
#include "esp_fir.h"

enum { TAPS = 5, HISTORY_FLOATS = 2 * TAPS };

static const float coefficients[] = {0.1f, 0.2f, 0.4f};
static esp_fir_model_t model;
static esp_fir_t channel_a;
static esp_fir_t channel_b;
static float history_a[HISTORY_FLOATS];
static float history_b[HISTORY_FLOATS];

void filters_init(void) {
    const esp_fir_config_t config = {
        .coefficients = coefficients,
        .coefficient_count = 3,
        .tap_count = TAPS,
        .structure = ESP_FIR_SYMMETRIC,
        .coefficient_layout = ESP_FIR_COEFFICIENTS_FOLDED,
        .startup = ESP_FIR_STARTUP_PRIME_FIRST_SAMPLE,
        .symmetry_tolerance = 0.0f,
    };

    ESP_ERROR_CHECK(esp_fir_prepare(&model, &config));
    ESP_ERROR_CHECK(esp_fir_init(&channel_a, &model, history_a,
                                 HISTORY_FLOATS));
    ESP_ERROR_CHECK(esp_fir_init(&channel_b, &model, history_b,
                                 HISTORY_FLOATS));
}

float filter_channel_a(float sample) {
    return esp_fir_process_symmetric(&channel_a, sample);
}
```

The model and coefficient array are shared; the states and histories are not.
Use `esp_fir_history_size(TAPS)` when allocating a history dynamically outside
the real-time phase. For arbitrary coefficients, select GENERAL/FULL and call
`esp_fir_process_general()`. The dispatcher `esp_fir_process()` is convenient
when the structure is not known at the call site.

## API behavior and errors

| Function | Context and effect | Failure behavior |
|---|---|---|
| `esp_fir_history_size` | Pure sizing helper; returns `2*N` floats | Returns zero for N=0 or size overflow |
| `esp_fir_prepare` | O(N), validates configuration and prepares a model | `ESP_ERR_INVALID_ARG`; destination unchanged |
| `esp_fir_init` | O(N), binds model/history and clears required storage | Error; destination state unchanged |
| `esp_fir_reset` | O(N), clears history and restores startup policy | `ESP_ERR_INVALID_STATE` for invalid state |
| `esp_fir_process` | O(N), dispatches by prepared structure | Preconditions unchecked |
| structure-specific process functions | O(N), direct real-time kernels | Preconditions unchecked |

All coefficients and input samples are `float`. Preparation rejects nonfinite
coefficients. Processing assumes a successfully initialized instance, a finite
sample, a matching structure-specific entry point, and valid storage; violating
those preconditions has no defined recovery path inside the component. Validate
or replace bad samples in the application before entering the hot path.

## Concurrency, memory, and real-time use

One serialized owner must process and reset each `esp_fir_t`. Sharing a prepared
model is safe while it remains immutable. The component provides no locks or
atomic snapshot mechanism.

The processing functions and their insertion helper are placed in IRAM on ESP
targets. For cache-independent execution, the selected function's complete call
path, model, coefficient array, state, history, and task stack must reside in
internal memory. `IRAM_ATTR` alone does not establish that complete property;
verify the linked ELF/map for the target configuration.

Processing performs no allocation, logging, locking, division, modulo, or
transcendental math. Work grows linearly with tap count, so the application must
measure its chosen order, structure, memory placement, compiler, and competing
load against the loop deadline. Host correctness tests do not establish a
worst-case target execution time.

## Compiler optimization and debugging

The component deliberately compiles `esp_fir.c` with `-O3`, independently of
the application's global optimization level. Generic kernels retain run-time
loop bounds and indexing that the compiler must optimize for a tight loop.

During migration from the former application-specific filter, an ESP32-S3 at
160 MHz using the application's folded 15-tap symmetric FIR on three channels
measured:

| Implementation | Cycles per three-channel frame | Time per frame |
|---|---:|---:|
| Hand-unrolled reference at `-Og` | 433.90 | 2.712 us |
| `esp_fir` at `-Og` | 758.88 | 4.743 us |
| `esp_fir` at `-O3` | 449.38 | 2.809 us |

These values are measurements, not guaranteed bounds. In that case `-O3`
reduced generic overhead relative to the reference from 74.90% to 3.57%.

With `-O3`, local variables may appear as `optimized out`; instructions may
move between source lines; loops and functions may be transformed; breakpoints
may behave unexpectedly; and code/IRAM size may increase. Floating-point
instruction selection may also change the last bits. The measured optimized
result differed from the reference by at most one single-precision ULP
(`1.1920929e-07`). The component does not enable `-ffast-math`.

For source-level debugging, temporarily change the component option in
`CMakeLists.txt` from `-O3` to `-Og`. Restore `-O3` before performance validation
or production builds and rerun correctness and target timing measurements.

## This project's integration

`main/app_fir.c` prepares one folded symmetric 15-tap model designed for 14.4
kHz operation. Three independent instances filter the R, S, and T phase errors.
The control task calls `esp_fir_process_symmetric()` directly once per phase per
frame, avoiding dispatcher overhead. The coefficient sum is approximately
1.009513983 and is used automatically by first-sample priming.

The earlier `main/fir_pmr.c` and `main/fir_pmr.h` implementation and its
application comparison benchmark were removed after numerical and target timing
validation. Applications should use this component API; there is no compatibility
wrapper for the removed private functions.

## Building and testing

As an in-tree ESP-IDF component, consumers add `esp_fir` to their component's
`REQUIRES` or `PRIV_REQUIRES`; there are no Kconfig options or external component
dependencies. The forced `-O3` option is local to the `esp_fir` target.

Portable tests run on the host:

```sh
cmake -S components/esp_fir/tests -B /tmp/esp_fir_host
cmake --build /tmp/esp_fir_host
ctest --test-dir /tmp/esp_fir_host --output-on-failure
```

They compare general, symmetric, and antisymmetric kernels against independent
direct convolution for odd/even and full/folded cases; cover both startup modes,
reset, invalid configuration, independent states; and exercise a 511-tap filter.

## Troubleshooting

- `esp_fir_prepare()` rejects a folded general filter: use FULL coefficients or
  declare and verify a symmetric/antisymmetric response.
- It rejects FULL symmetric coefficients: increase `symmetry_tolerance` only to
  cover known coefficient quantization, or correct the coefficients.
- `esp_fir_init()` reports `ESP_ERR_INVALID_SIZE`: allocate at least
  `esp_fir_history_size(tap_count)` float elements for that stream.
- Startup output looks biased: inspect the coefficient sum and choose the
  startup policy deliberately; priming preserves the filter's DC gain.
- Timing changes unexpectedly: confirm `-O3`, internal-memory placement, the
  direct kernel call, target clock, and linked map before comparing measurements.
