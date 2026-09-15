# Guidance for agents modifying esp_fir

This file applies to `components/esp_fir` and all files below it. Read the
public header, implementation, `CMakeLists.txt`, README, and tests before
changing behavior or performance-sensitive code.

## Preserve the real-time contract

- Keep processing free of allocation, logging, locks, division, modulo,
  transcendental functions, and run-time validation.
- Keep processing functions in IRAM and document that models, coefficients,
  states, and histories must reside in internal RAM for cache-independent use.
- Preserve the caller-owned mirrored history and independent state per stream.
- Preserve the dedicated general, symmetric, and antisymmetric entry points so
  hard real-time callers can avoid run-time structure dispatch.
- Do not introduce a compiled-in maximum tap count.

## Optimization and debugging

The component's `-O3` option in `CMakeLists.txt` is intentional. The generic
15-tap symmetric kernel measured 758.88 cycles per three-channel frame at `-Og`
and 449.38 cycles at `-O3` on the target ESP32-S3. Keep `-O3` for normal builds
unless new target measurements justify a change.

For source-level debugging inside `esp_fir.c`, it is acceptable to change the
component option temporarily from `-O3` to `-Og`. `-Og` improves variable
inspection, breakpoints, and single-stepping but is not representative of the
production timing. Before finalizing work:

1. restore `-O3`;
2. run the portable host tests, including the legacy equivalence test;
3. build the ESP-IDF application;
4. confirm the active processing kernel remains in IRAM;
5. for hot-path changes, rerun `APP_FIR_BENCHMARK` on the target and compare
   numerical error as well as cycles.

Do not add `-ffast-math` without explicit numerical tests covering every
supported structure and application approval for its altered NaN, infinity,
signed-zero, reassociation, and rounding behavior.

## Validation expectations

Maintain coverage for general, symmetric, and antisymmetric filters; odd and
even tap counts; full and folded coefficients; zero and first-sample-primed
startup; reset; independent states; invalid configurations; and at least one
high-order filter. A performance change is incomplete when it reports cycle
improvement without confirming output equivalence.
