#include "app_diagnostics.h"

#if CONFIG_ESP_RT_DIAGNOSTICS_ENABLE
#include "esp_cpu.h"
#include "esp_log.h"
#include "esp_rt_diagnostics.h"
#include "esp_rt_diagnostics_reporter.h"
#include "freertos/task.h"
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
#if CONFIG_APP_DIAG_CAPTURE_SECONDS == 0
static esp_rt_diag_reporter_handle_t reporter;
#endif
#if CONFIG_APP_DIAG_CAPTURE_SECONDS > 0
static TaskHandle_t dump_task_handle;
static esp_rt_diag_snapshot_t capture_snapshot;
static int64_t capture_deadline_us, capture_end_us;
static bool capture_done;
#endif
static void (*adc_rate_callback)(void);

/* ISR-owned monotonic counters. Only the control task imports their unsigned
 * deltas into the component. No accumulator is shared across task/ISR owners.
 */
static DRAM_ATTR _Atomic uint32_t generated_frames;
static DRAM_ATTR _Atomic uint32_t pool_overflows;
#if CONFIG_APP_DIAG_CAPTURE_SECONDS == 0
static uint32_t previous_generated, previous_overflows;
#endif
static uint32_t start_cycles;
static bool backlog_active;

/* Single ISR producer, single control-task consumer. Release/acquire publishes
 * slots without sharing mutable entries. Drop newest when full; sequences keep
 * later timestamps aligned even after loss. esp_timer is a common clock across
 * cores; CPU cycle counters must not be subtracted across cores. */
#define TIMESTAMP_CAPACITY 64U
#define ANOMALY_CAPACITY 8U
#define INTERVAL_LOW_US 62U
#define INTERVAL_HIGH_US 77U
/* 62..77 us is a diagnostic screening band (~10%), not a timing guarantee. */
typedef struct {
  uint32_t sequence, time_us, interval_us;
} frame_stamp_t;
static DRAM_ATTR frame_stamp_t stamps[TIMESTAMP_CAPACITY];
static DRAM_ATTR _Atomic uint32_t stamp_write, stamp_read, stamp_drops;
static uint32_t consumed_sequence, previous_isr_us;
static bool have_isr;
static uint32_t stage_start, previous_dac_us;
static bool have_dac;
static frame_stamp_t current_stamp;
static uint32_t current_wake, current_pending;
static bool current_stamp_valid;
typedef struct {
  uint32_t sequence, time_us, pending, wake_us, isr_interval_us,
      dac_interval_us;
  uint32_t stage_cycles[APP_DIAG_STAGE_COUNT];
} anomaly_t;
static uint32_t current_stage_cycles[APP_DIAG_STAGE_COUNT];

typedef struct {
  uint32_t max_pending;
  uint32_t lifetime_max_pending;
  uint32_t max_execution_cycles;
  uint32_t lifetime_max_execution_cycles;
  uint32_t report_errors;
  uint32_t wake_max_us, wake_valid, wake_missing, timestamp_drops;
  uint32_t isr_min_us, isr_max_us, isr_outside;
  uint32_t dac_min_us, dac_max_us, dac_outside;
  uint32_t stage_max_cycles[APP_DIAG_STAGE_COUNT];
  uint32_t publish_max_cycles;
  uint32_t anomaly_count, anomaly_drops;
  anomaly_t anomalies[ANOMALY_CAPACITY];
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
           "us) deadline=%" PRIu32 "us wake=per-frame",
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
  ESP_LOGI(TAG,
           "timing us: wake_max=%" PRIu32 " valid=%" PRIu32 " missing=%" PRIu32
           " stamp_drops=%" PRIu32 " ISR=%" PRIu32 "..%" PRIu32
           " outside=%" PRIu32 " DAC_done=%" PRIu32 "..%" PRIu32
           " outside=%" PRIu32 " band=62..77",
           p->wake_max_us, p->wake_valid, p->wake_missing, p->timestamp_drops,
           p->isr_min_us, p->isr_max_us, p->isr_outside, p->dac_min_us,
           p->dac_max_us, p->dac_outside);
  ESP_LOGI(TAG,
           "stage maxima cycles: ADC=%" PRIu32 " compute=%" PRIu32
           " DAC=%" PRIu32 " publish=%" PRIu32
           " (previous publications) anomalies_dropped=%" PRIu32,
           p->stage_max_cycles[0], p->stage_max_cycles[1],
           p->stage_max_cycles[2], p->publish_max_cycles, p->anomaly_drops);
  for (uint32_t i = 0; i < p->anomaly_count; ++i) {
    const anomaly_t *a = &p->anomalies[i];
    ESP_LOGI(TAG,
             "anomaly seq=%" PRIu32 " t32_us=%" PRIu32 " pending=%" PRIu32
             " wake_us=%" PRIu32 " ISR_us=%" PRIu32 " DAC_us=%" PRIu32
             " stage_cycles=%" PRIu32 "/%" PRIu32 "/%" PRIu32,
             a->sequence, a->time_us, a->pending, a->wake_us,
             a->isr_interval_us, a->dac_interval_us, a->stage_cycles[0],
             a->stage_cycles[1], a->stage_cycles[2]);
  }
}

#if CONFIG_APP_DIAG_CAPTURE_SECONDS > 0
/* Single handoff after ADC stop: task notification publishes the immutable
 * snapshot/payload. No queue copy or formatting runs during acquisition. */
static void capture_dump_task(void *arg) {
  (void)arg;
  ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
  ESP_LOGI(TAG, "CAPTURE stopped duration_s=%d RMS=%s; DAC holds last value",
           CONFIG_APP_DIAG_CAPTURE_SECONDS,
#if CONFIG_APP_DIAG_DISABLE_RMS
           "off");
#else
           "on");
#endif
#if CONFIG_APP_RMS_LOG_ENABLE
  ESP_LOGI(TAG, "RMS console logging=on");
#else
  ESP_LOGI(TAG, "RMS console logging=off");
#endif
  report_window(&capture_snapshot, &payload, ESP_RT_DIAG_WINDOW_LOG_AFFECTED,
                NULL);
  ESP_LOGI(TAG, "CAPTURE COMPLETE");
  vTaskDelete(NULL);
}
#endif

void app_diagnostics_init(void (*report_adc_rate)(void)) {
#if CONFIG_APP_DIAG_CAPTURE_SECONDS > 0
  /* ADC stop deinitializes its clock registers. Do not inspect them from the
   * post-stop dump; frame rate is available in the captured statistics. */
  (void)report_adc_rate;
  adc_rate_callback = NULL;
  ESP_ERROR_CHECK(xTaskCreatePinnedToCore(capture_dump_task, "diag_dump", 4096,
                                          NULL, 4, &dump_task_handle,
                                          0) == pdPASS
                      ? ESP_OK
                      : ESP_ERR_NO_MEM);
#else
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
#endif
}

void app_diagnostics_start(void) {
  const esp_rt_diag_config_t config = {
      .expected_period_us = CONTROL_EXPECTED_PERIOD_US,
      .deadline_us = CONTROL_DEADLINE_US,
      .window_duration_us = 0, /* Component Kconfig, 5 s by default. */
      .event_count = APP_DIAG_EVENT_COUNT,
  };
  int64_t now = esp_timer_get_time();
  ESP_ERROR_CHECK(esp_rt_diag_init(&diagnostics, &config, now));
#if CONFIG_APP_DIAG_CAPTURE_SECONDS > 0
  capture_deadline_us =
      now + (int64_t)CONFIG_APP_DIAG_CAPTURE_SECONDS * 1000000;
#endif
}

void IRAM_ATTR app_diagnostics_frame_generated(void) {
  /* A single ISR writes each counter, so relaxed load/store avoids an atomic
   * read-modify-write retry loop while remaining race-free for the reader. */
  uint32_t value =
      atomic_load_explicit(&generated_frames, memory_order_relaxed);
  atomic_store_explicit(&generated_frames, value + 1U, memory_order_relaxed);
  uint32_t now = (uint32_t)esp_timer_get_time();
  frame_stamp_t stamp = {value + 1U, now, have_isr ? now - previous_isr_us : 0};
  previous_isr_us = now;
  have_isr = true;
  uint32_t write = atomic_load_explicit(&stamp_write, memory_order_relaxed);
  uint32_t read = atomic_load_explicit(&stamp_read, memory_order_acquire);
  if (write - read < TIMESTAMP_CAPACITY) {
    stamps[write % TIMESTAMP_CAPACITY] = stamp;
    atomic_store_explicit(&stamp_write, write + 1U, memory_order_release);
  } else {
    uint32_t drops = atomic_load_explicit(&stamp_drops, memory_order_relaxed);
    atomic_store_explicit(&stamp_drops, drops + 1U, memory_order_relaxed);
  }
}

void IRAM_ATTR app_diagnostics_pool_overflow(void) {
  uint32_t value = atomic_load_explicit(&pool_overflows, memory_order_relaxed);
  atomic_store_explicit(&pool_overflows, value + 1U, memory_order_relaxed);
}

void IRAM_ATTR app_diagnostics_cycle_begin(uint32_t pending_frames) {
  start_cycles = esp_cpu_get_cycle_count();
  stage_start = start_cycles;
  for (unsigned i = 0; i < APP_DIAG_STAGE_COUNT; ++i)
    current_stage_cycles[i] = 0;
  current_pending = pending_frames;
  ++consumed_sequence;
  current_stamp_valid = false;
  current_stamp = (frame_stamp_t){0};
  current_wake = 0;
  uint32_t read = atomic_load_explicit(&stamp_read, memory_order_relaxed);
  uint32_t write = atomic_load_explicit(&stamp_write, memory_order_acquire);
  if (read != write &&
      stamps[read % TIMESTAMP_CAPACITY].sequence == consumed_sequence) {
    current_stamp = stamps[read % TIMESTAMP_CAPACITY];
    atomic_store_explicit(&stamp_read, read + 1U, memory_order_release);
    current_stamp_valid = true;
  }
  int64_t now = esp_timer_get_time();
  if (current_stamp_valid) {
    current_wake = (uint32_t)now - current_stamp.time_us;
    payload.wake_valid++;
    esp_rt_diag_update_max(&payload.wake_max_us, current_wake);
    uint32_t dt = current_stamp.interval_us;
    if (dt) {
      if (!payload.isr_min_us || dt < payload.isr_min_us)
        payload.isr_min_us = dt;
      esp_rt_diag_update_max(&payload.isr_max_us, dt);
      if (dt < INTERVAL_LOW_US || dt > INTERVAL_HIGH_US)
        payload.isr_outside++;
    }
  } else
    payload.wake_missing++;
  /* Exactly one consumed event, even with backlog; never count backlog as loss.
   */
  esp_rt_diag_cycle_begin(&diagnostics, current_stamp.time_us,
                          current_stamp_valid ? 1U : 0U, now);
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

void IRAM_ATTR app_diagnostics_stage_end(app_diag_stage_t stage) {
  uint32_t cycles = esp_cpu_get_cycle_count();
  uint32_t elapsed = cycles - stage_start;
  stage_start = cycles;
  current_stage_cycles[stage] = elapsed;
  esp_rt_diag_update_max(&payload.stage_max_cycles[stage], elapsed);
  if (stage != APP_DIAG_DAC)
    return;

  uint32_t now = (uint32_t)esp_timer_get_time();
  uint32_t dt = have_dac ? now - previous_dac_us : 0;
  previous_dac_us = now;
  have_dac = true;
  bool outside = dt && (dt < INTERVAL_LOW_US || dt > INTERVAL_HIGH_US);
  if (dt) {
    if (!payload.dac_min_us || dt < payload.dac_min_us)
      payload.dac_min_us = dt;
    esp_rt_diag_update_max(&payload.dac_max_us, dt);
    if (outside)
      payload.dac_outside++;
  }
  if (outside || current_pending > 1U || current_wake > CONTROL_DEADLINE_US) {
    if (payload.anomaly_count < ANOMALY_CAPACITY) {
      anomaly_t *a = &payload.anomalies[payload.anomaly_count++];
      *a = (anomaly_t){.sequence = consumed_sequence,
                       .time_us = now,
                       .pending = current_pending,
                       .wake_us =
                           current_stamp_valid ? current_wake : UINT32_MAX,
                       .isr_interval_us = current_stamp.interval_us,
                       .dac_interval_us = dt};
      for (unsigned i = 0; i < APP_DIAG_STAGE_COUNT; ++i)
        a->stage_cycles[i] = current_stage_cycles[i];
    } else
      payload.anomaly_drops++;
  }
}

void IRAM_ATTR app_diagnostics_event(app_diag_event_t event) {
  esp_rt_diag_event(&diagnostics, event, 1);
}

/* Runs only once per window, after the measured cycle is closed. The component
 * copies/reset window statistics; the reporter copies them into a one-slot
 * overwrite queue. No formatting or allocation occurs on this path. Snapshot
 * and queue cost can affect the next frame and is observable as backlog. */
#if CONFIG_APP_DIAG_CAPTURE_SECONDS == 0
static void publish_window(int64_t now_us) {
  uint32_t publish_start = esp_cpu_get_cycle_count();
  payload.timestamp_drops =
      atomic_load_explicit(&stamp_drops, memory_order_relaxed);
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
  uint32_t lifetime_pending = payload.lifetime_max_pending;
  uint32_t lifetime_exec = payload.lifetime_max_execution_cycles;
  uint32_t errors = payload.report_errors;
  payload = (app_diag_payload_t){.lifetime_max_pending = lifetime_pending,
                                 .lifetime_max_execution_cycles = lifetime_exec,
                                 .report_errors = errors,
                                 .publish_max_cycles =
                                     esp_cpu_get_cycle_count() - publish_start};
}

#endif

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
#if CONFIG_APP_DIAG_CAPTURE_SECONDS > 0
  capture_end_us = end_us;
  capture_done = end_us >= capture_deadline_us;
#else
  if (esp_rt_diag_snapshot_due(&diagnostics, end_us))
    publish_window(end_us);
#endif
}

void app_diagnostics_idle(void) {
  const int64_t now_us = esp_timer_get_time();
#if CONFIG_APP_DIAG_CAPTURE_SECONDS > 0
  capture_end_us = now_us;
  capture_done = now_us >= capture_deadline_us;
#else
  if (esp_rt_diag_snapshot_due(&diagnostics, now_us))
    publish_window(now_us);
#endif
}

bool IRAM_ATTR app_diagnostics_capture_done(void) {
#if CONFIG_APP_DIAG_CAPTURE_SECONDS > 0
  return capture_done;
#else
  return false;
#endif
}

void app_diagnostics_capture_finish(void) {
#if CONFIG_APP_DIAG_CAPTURE_SECONDS > 0
  payload.timestamp_drops =
      atomic_load_explicit(&stamp_drops, memory_order_relaxed);
  esp_rt_diag_event(
      &diagnostics, APP_DIAG_GENERATED,
      atomic_load_explicit(&generated_frames, memory_order_relaxed));
  esp_rt_diag_event(
      &diagnostics, APP_DIAG_POOL_OVERFLOWS,
      atomic_load_explicit(&pool_overflows, memory_order_relaxed));
  /* Use the last measured cycle/timeout, excluding stop/cleanup overhead. */
  ESP_ERROR_CHECK(esp_rt_diag_take_snapshot(&diagnostics, capture_end_us,
                                            &capture_snapshot));
  xTaskNotifyGive(dump_task_handle);
#endif
}

#endif
