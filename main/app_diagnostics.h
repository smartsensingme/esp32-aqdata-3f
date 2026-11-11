#pragma once

#include "sdkconfig.h"
#include <stdbool.h>
#include <stdint.h>

/* Must match the ADC frame rate and the PR model. */
#define APP_CONTROL_FRAME_RATE_HZ 14400U

typedef enum {
  APP_DIAG_GENERATED,
  APP_DIAG_PROCESSED,
  APP_DIAG_BACKLOG_EPISODES,
  APP_DIAG_READ_ERRORS,
  APP_DIAG_PARTIAL_READS,
  APP_DIAG_POOL_OVERFLOWS,
  APP_DIAG_EXEC_OVERRUNS,
  APP_DIAG_BACKLOG_ITERATIONS,
  APP_DIAG_EVENT_COUNT
} app_diag_event_t;

typedef enum {
  APP_DIAG_ADC,
  APP_DIAG_COMPUTE,
  APP_DIAG_DAC,
  APP_DIAG_STAGE_COUNT
} app_diag_stage_t;

#if CONFIG_ESP_RT_DIAGNOSTICS_ENABLE
/* Create the Core 0 reporter before starting the control task. The optional
 * callback runs once in continuous reporter context to report ADC clocks.
 * Finite mode skips it because the ADC is already stopped at dump time. */
void app_diagnostics_init(void (*report_adc_rate)(void));
/* Control-task owner only; call immediately before starting the ADC. */
void app_diagnostics_start(void);
/* ISR callbacks only; each counter has exactly one writer. */
void app_diagnostics_frame_generated(void);
void app_diagnostics_pool_overflow(void);
/* Control-task only. Consume exactly ONE frame per begin/end pair. The pending
 * count describes queue depth, not coalesced/lost notifications. */
void app_diagnostics_cycle_begin(uint32_t pending_frames);
/* End a sequential ADC/compute/DAC stage; DAC also marks write completion. */
void app_diagnostics_stage_end(app_diag_stage_t stage);
void app_diagnostics_event(app_diag_event_t event);
void app_diagnostics_cycle_end(bool processed);
/* Publish a due window on a notification timeout, without inventing a cycle. */
void app_diagnostics_idle(void);
/* Control-task completion check is in IRAM; continuous mode returns false.
 * Stop ADC before finish; finish transfers ownership to the Core 0 dump task.
 */
bool app_diagnostics_capture_done(void);
void app_diagnostics_capture_finish(void);
#else
static inline void app_diagnostics_init(void (*report_adc_rate)(void)) {
  (void)report_adc_rate;
}
static inline void app_diagnostics_start(void) {}
static inline void app_diagnostics_frame_generated(void) {}
static inline void app_diagnostics_pool_overflow(void) {}
static inline void app_diagnostics_cycle_begin(uint32_t pending_frames) {
  (void)pending_frames;
}
static inline void app_diagnostics_stage_end(app_diag_stage_t stage) {
  (void)stage;
}
static inline void app_diagnostics_event(app_diag_event_t event) {
  (void)event;
}
static inline void app_diagnostics_cycle_end(bool processed) {
  (void)processed;
}
#endif
