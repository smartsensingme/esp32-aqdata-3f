#pragma once
#include "esp_anti_windup.h"
#include "esp_pid.h"
#include "esp_pr.h"
#include <stdbool.h>

/* Application setup/telemetry adapter for esp_anti_windup and PR+I.
 * Instances contain self-referencing registrations: do not copy/move after
 * init. Serialized owner of PR+I and their common actuator tracking substep.
 * Model must outlive the instance, use +/-limit and select external tracking.
 * Results and tracking use public component APIs; this coordinator never
 * patches component state or model coefficients. */
typedef struct {
  esp_pr_t pr;
  esp_pid_t integral;
  float limit;
  bool integral_enabled;
  esp_anti_windup_t tracking;
  float pr_tracking_share;
  float integral_tracking_share;
  float requested_pr;
  float requested_integral;
  float requested;
  float applied;
  float saturation_error;
} app_parallel_t;

/* Full symmetric model bounds must equal +/-limit, in external tracking mode.
 * PID local tracking is also disabled. Common tracking time comes from
 * CONFIG_APP_PR_TRACKING_TIME_US (0 disables correction). ki is in
 * Vsoft/(Verror.s). No allocation. Initialize/reset before acquisition. */
esp_err_t app_parallel_init(app_parallel_t *controller,
                            const esp_pr_model_t *model, float ki, float limit);
esp_err_t app_parallel_reset(app_parallel_t *controller);
/* One update at exactly 14.4 kHz. Uses unsaturated branch requests, clamps
 * their sum once, and distributes one common correction to dynamic states.
 * Native per-component clamped diagnostics do not define the actuator command.
 * Both dynamic groups receive half the tracking correction when present;
 * a sole dynamic group receives all. Returns failure after resetting states. */
esp_err_t app_parallel_update(app_parallel_t *controller, float error,
                              float *output);
