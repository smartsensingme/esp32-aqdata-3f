#include "app_diagnostics.h"

#if CONFIG_ESP_RT_DIAGNOSTICS_ENABLE
#include "esp_cpu.h"
#include "esp_log.h"
#include "esp_rt_diagnostics.h"
#include "esp_rt_diagnostics_reporter.h"
#include <inttypes.h>
#include <stdatomic.h>

#define CONTROL_PERIOD_CYCLES (configCPU_CLOCK_HZ / APP_CONTROL_FRAME_RATE_HZ)
/* Component metadata uses integer microseconds. Round the descriptive period
 * to nearest, and the deadline upward so rounding alone cannot create an
 * overrun. The CPU-cycle event retains the precise 1/14400 s budget. */
#define CONTROL_EXPECTED_PERIOD_US                                             \
  ((1000000U + APP_CONTROL_FRAME_RATE_HZ / 2U) / APP_CONTROL_FRAME_RATE_HZ)
#define CONTROL_DEADLINE_US                                                    \
  ((1000000U + APP_CONTROL_FRAME_RATE_HZ - 1U) / APP_CONTROL_FRAME_RATE_HZ)

_Static_assert(APP_DIAG_EVENT_COUNT <= ESP_RT_DIAG_MAX_EVENTS,
               "Application events must fit the component");
_Static_assert(__atomic_always_lock_free(sizeof(uint32_t), 0),
               "ISR counters must use lock-free 32-bit atomics");

static const char *TAG = "control_diag";
static DRAM_ATTR esp_rt_diag_t diagnostics;
static esp_rt_diag_reporter_handle_t reporter;
static void (*adc_rate_callback)(void);

/* ISR-owned monotonic counters. Only the control task imports their unsigned
 * deltas into the component. No accumulator is shared across task/ISR owners.
 */
static DRAM_ATTR _Atomic uint32_t generated_frames;
static DRAM_ATTR _Atomic uint32_t pool_overflows;
static uint32_t previous_generated, previous_overflows;
static uint32_t start_cycles;
static bool backlog_active;

typedef struct {
  uint32_t max_pending;
  uint32_t lifetime_max_pending;
  uint32_t max_execution_cycles;
  uint32_t lifetime_max_execution_cycles;
  uint32_t report_errors;
} app_diag_payload_t;
static app_diag_payload_t payload;

/* Presentation runs only in the reporter task on Core 0. This callback uses
 * copied data; it never inspects the accumulator being changed on Core 1. */
static void report_window(const esp_rt_diag_snapshot_t *s, const void *data,
                          esp_rt_diag_window_class_t window_class,
                          void *context) {
  (void)context;
  const app_diag_payload_t *p = data;
  if (adc_rate_callback) {
    adc_rate_callback();
    adc_rate_callback = NULL;
  }
  ESP_LOGI(TAG,
           "window=%" PRIu64 " [%s] duration=%" PRIu32 "us attempts=%" PRIu32
           " rate=%.1f/%.1fHz processed=%.1fHz deadline_overruns=%" PRIu32
           "/%" PRIu32 " (%.4f%%) exec_max=%" PRIu32 "us (boot=%" PRIu32
           "us) deadline=%" PRIu32 "us wake=unmeasured",
           s->window_id, esp_rt_diag_window_class_name(window_class),
           s->window_duration_us, s->cycles,
           (double)esp_rt_diag_cycle_rate_hz(s),
           (double)APP_CONTROL_FRAME_RATE_HZ,
           (double)esp_rt_diag_event_rate_hz(s, APP_DIAG_PROCESSED),
           s->deadline_overruns, s->cycles,
           (double)esp_rt_diag_deadline_overrun_percent(s),
           s->max_processing_time_us, s->lifetime_max_processing_time_us,
           s->deadline_us);
  ESP_LOGI(TAG,
           "window=%" PRIu64 " delta(total): generated=%" PRIu32 "(%" PRIu32
           ") processed=%" PRIu32 "(%" PRIu32 ") backlog=%" PRIu32 "(%" PRIu32
           ") queued_iterations=%" PRIu32 " max_pending=%" PRIu32
           "(boot=%" PRIu32 ")",
           s->window_id, s->event_counts[APP_DIAG_GENERATED],
           s->total_event_counts[APP_DIAG_GENERATED],
           s->event_counts[APP_DIAG_PROCESSED],
           s->total_event_counts[APP_DIAG_PROCESSED],
           s->event_counts[APP_DIAG_BACKLOG_EPISODES],
           s->total_event_counts[APP_DIAG_BACKLOG_EPISODES],
           s->event_counts[APP_DIAG_BACKLOG_ITERATIONS], p->max_pending,
           p->lifetime_max_pending);
  ESP_LOGI(TAG,
           "window=%" PRIu64 " delta(total): read_errors=%" PRIu32 "(%" PRIu32
           ") partial_reads=%" PRIu32 "(%" PRIu32 ") pool_lost=%" PRIu32
           "(%" PRIu32 ") cycle_overruns=%" PRIu32 "/%" PRIu32 " (%" PRIu32
           " total) exec_max=%" PRIu32 " cycles(boot=%" PRIu32
           ") budget=%u cycles report_errors=%" PRIu32,
           s->window_id, s->event_counts[APP_DIAG_READ_ERRORS],
           s->total_event_counts[APP_DIAG_READ_ERRORS],
           s->event_counts[APP_DIAG_PARTIAL_READS],
           s->total_event_counts[APP_DIAG_PARTIAL_READS],
           s->event_counts[APP_DIAG_POOL_OVERFLOWS],
           s->total_event_counts[APP_DIAG_POOL_OVERFLOWS],
           s->event_counts[APP_DIAG_EXEC_OVERRUNS], s->cycles,
           s->total_event_counts[APP_DIAG_EXEC_OVERRUNS],
           p->max_execution_cycles, p->lifetime_max_execution_cycles,
           (unsigned)CONTROL_PERIOD_CYCLES, p->report_errors);
}

void app_diagnostics_init(void (*report_adc_rate)(void)) {
  adc_rate_callback = report_adc_rate;
  const esp_rt_diag_reporter_config_t config = {
      /* Custom labels avoid presenting missed_events=0 as proof of no loss:
       * this application's queued frames are not coalesced periodic events. */
      .report_format = ESP_RT_DIAG_REPORT_SILENT,
      .application_payload_size = sizeof(payload),
      .report_callback = report_window,
      .task_stack_size = 4096,
      .task_priority = 4,
      .task_core = 0,
  };
  ESP_ERROR_CHECK(esp_rt_diag_reporter_create(&config, &reporter));
}

void app_diagnostics_start(void) {
  const esp_rt_diag_config_t config = {
      .expected_period_us = CONTROL_EXPECTED_PERIOD_US,
      .deadline_us = CONTROL_DEADLINE_US,
      .window_duration_us = 0, /* Component Kconfig, 5 s by default. */
      .event_count = APP_DIAG_EVENT_COUNT,
  };
  ESP_ERROR_CHECK(
      esp_rt_diag_init(&diagnostics, &config, esp_timer_get_time()));
}

void IRAM_ATTR app_diagnostics_frame_generated(void) {
  /* A single ISR writes each counter, so relaxed load/store avoids an atomic
   * read-modify-write retry loop while remaining race-free for the reader. */
  uint32_t value =
      atomic_load_explicit(&generated_frames, memory_order_relaxed);
  atomic_store_explicit(&generated_frames, value + 1U, memory_order_relaxed);
}

void IRAM_ATTR app_diagnostics_pool_overflow(void) {
  uint32_t value = atomic_load_explicit(&pool_overflows, memory_order_relaxed);
  atomic_store_explicit(&pool_overflows, value + 1U, memory_order_relaxed);
}

void IRAM_ATTR app_diagnostics_cycle_begin(uint32_t pending_frames) {
  start_cycles = esp_cpu_get_cycle_count();
  /* pdFALSE drains exactly one event. Passing pending_frames would count the
   * same backlog repeatedly. No per-frame timestamp FIFO exists, so wake time
   * is explicitly unavailable, including when another ISR races this wake. */
  esp_rt_diag_cycle_begin(&diagnostics, 0, 0, esp_timer_get_time());
  esp_rt_diag_update_max(&payload.max_pending, pending_frames);
  esp_rt_diag_update_max(&payload.lifetime_max_pending, pending_frames);
  if (pending_frames > 1U) {
    esp_rt_diag_event(&diagnostics, APP_DIAG_BACKLOG_ITERATIONS, 1);
    if (!backlog_active) {
      esp_rt_diag_event(&diagnostics, APP_DIAG_BACKLOG_EPISODES, 1);
    }
    backlog_active = true;
  } else {
    backlog_active = false;
  }
}

void IRAM_ATTR app_diagnostics_event(app_diag_event_t event) {
  esp_rt_diag_event(&diagnostics, event, 1);
}

/* Runs only once per window, after the measured cycle is closed. The component
 * copies/reset window statistics; the reporter copies them into a one-slot
 * overwrite queue. No formatting or allocation occurs on this path. Snapshot
 * and queue cost can affect the next frame and is observable as backlog. */
static void publish_window(int64_t now_us) {
  uint32_t generated =
      atomic_load_explicit(&generated_frames, memory_order_relaxed);
  uint32_t overflows =
      atomic_load_explicit(&pool_overflows, memory_order_relaxed);
  esp_rt_diag_event(&diagnostics, APP_DIAG_GENERATED,
                    generated - previous_generated);
  esp_rt_diag_event(&diagnostics, APP_DIAG_POOL_OVERFLOWS,
                    overflows - previous_overflows);
  previous_generated = generated;
  previous_overflows = overflows;

  esp_rt_diag_snapshot_t snapshot;
  if (esp_rt_diag_take_snapshot(&diagnostics, now_us, &snapshot) != ESP_OK) {
    payload.report_errors++;
    return;
  }
  if (esp_rt_diag_reporter_publish(reporter, &snapshot, &payload) != ESP_OK) {
    payload.report_errors++;
  }
  payload.max_pending = 0;
  payload.max_execution_cycles = 0;
}

void IRAM_ATTR app_diagnostics_cycle_end(bool processed) {
  const uint32_t elapsed_cycles = esp_cpu_get_cycle_count() - start_cycles;
  const int64_t end_us = esp_rt_diag_cycle_end_now(&diagnostics);
  if (processed)
    esp_rt_diag_event(&diagnostics, APP_DIAG_PROCESSED, 1);
  esp_rt_diag_update_max(&payload.max_execution_cycles, elapsed_cycles);
  esp_rt_diag_update_max(&payload.lifetime_max_execution_cycles,
                         elapsed_cycles);
  if (elapsed_cycles > CONTROL_PERIOD_CYCLES)
    esp_rt_diag_event(&diagnostics, APP_DIAG_EXEC_OVERRUNS, 1);
  if (esp_rt_diag_snapshot_due(&diagnostics, end_us))
    publish_window(end_us);
}
void app_diagnostics_idle(void) {
  const int64_t now_us = esp_timer_get_time();
  if (esp_rt_diag_snapshot_due(&diagnostics, now_us))
    publish_window(now_us);
}
#endif
