idf_component_register(SRCS "esp_fir.c" INCLUDE_DIRS "include")

# Keep the FIR hot path optimized even when the application uses a debug or
# size-oriented build. The component contains no code that depends on debug
# optimization semantics, and its portable tests cover the optimized kernels.
target_compile_options(${COMPONENT_LIB} PRIVATE -O3)
