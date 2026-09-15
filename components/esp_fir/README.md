# esp_fir

`esp_fir` is a scalar, real-coefficient FIR runtime component designed for
bounded-latency control loops. Coefficients may come from Parks-McClellan/Remez,
windowing, least squares, or any other design method.

The component provides separate kernels for general, symmetric, and
antisymmetric impulse responses. Symmetric filters use `ceil(N/2)`
multiplications and antisymmetric filters use `floor(N/2)`. The application can
call the matching kernel directly so no structure dispatch occurs in the hot
path.

No maximum order is compiled into the component. The caller owns the immutable
coefficient array and a `2*N`-float mirrored history for each signal stream.
That extra history copy makes the newest `N` samples contiguous and removes
division and modulo from processing. Models may be shared by multiple streams;
states and histories may not.

For cache-independent execution on ESP-IDF, keep the model, coefficients,
states, and histories in internal RAM. Processing functions are placed in IRAM.
Initialization and reset clear memory and are not real-time operations.

## Compiler optimization and debugging

The ESP-IDF component deliberately compiles its implementation with `-O3`
independently of the application's global optimization level. This is a
real-time performance requirement for the generic kernels, rather than a
correctness requirement. Unlike a fixed, hand-unrolled filter, a generic FIR
has run-time loop bounds, model indirection, and index calculations. Aggressive
optimization lets the compiler retain values in registers, simplify those
calculations, schedule floating-point instructions, and unroll loops when it is
profitable.

On an ESP32-S3 at 160 MHz, using the application's folded 15-tap symmetric FIR
on three channels, the same benchmark measured:

| Implementation | Cycles per three-channel frame | Time per frame |
|---|---:|---:|
| Hand-unrolled reference at `-Og` | 433.90 | 2.712 us |
| `esp_fir` at `-Og` | 758.88 | 4.743 us |
| `esp_fir` at `-O3` | 449.38 | 2.809 us |

These values depend on the target, toolchain, filter length, memory placement,
and surrounding workload. Measure again for each real-time application. In this
case, `-O3` reduced the generic implementation's overhead relative to the
reference from 74.90% to 3.57%.

The cost of `-O3` is primarily reduced source-level debug fidelity:

- local variables may be reported as `optimized out`;
- instructions may move across source lines, making single-stepping confusing;
- functions and loops may be inlined, transformed, or removed;
- breakpoints may stop at unexpected lines or be skipped;
- code size and IRAM use may increase;
- floating-point instruction selection may change rounding by a small amount.

The measured optimized result differed from the reference by at most one
single-precision ULP (`1.1920929e-07`). The component does not enable
`-ffast-math`; adding it would permit broader changes to floating-point
semantics and requires separate numerical validation.

When source-level debugging inside `esp_fir.c` is necessary, temporarily change
the component option in `CMakeLists.txt` from `-O3` to `-Og`, rebuild, and debug
that image. Restore `-O3` before evaluating timing or producing the final
firmware, then rerun the host tests, target build, numerical comparison, and
cycle benchmark. Code outside this component retains the application's normal
optimization level.

With folded coefficients, store the first member of each pair. An odd symmetric
filter stores its center coefficient last. An odd antisymmetric filter has an
implicit zero center. Full coefficient layouts are validated using the absolute
`symmetry_tolerance` supplied at preparation time.

`tests/` contains portable correctness tests and an equivalence comparison with
the original application-specific 15-tap filter. The application also offers a
boot-time ESP32 cycle benchmark under `APP_FIR_BENCHMARK`.
