#include "app_tuning.h"
#include "app_control.h"
#include "app_signal_stats.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include <math.h>
#include <stdint.h>

#define RATE 14400U
#define SETTLE_FRAMES (2U * RATE)
#define LIMIT_V (CONFIG_APP_CONTROL_LIMIT_MV / 1000.0f)
#define STAGES 9U

static const char *TAG = "tuning";
static const float kps[STAGES] = {0, 0.5f, 1, 2, 4, 1, 1, 1, 1};
static const float krs[STAGES] = {0, 0, 0, 0, 0, 1, 2, 5, 10};
static DRAM_ATTR esp_pr_model_t models[STAGES];
static DRAM_ATTR esp_pr_t controllers[STAGES];

typedef struct {
  unsigned n, clipped, pr_clipped, pid_clipped;
  float error_sum, error_sq, raw_sum, raw_sq, command_sq, command_peak;
  float error_sum_correction, error_sq_correction, raw_sum_correction,
      raw_sq_correction;
  float sine_sum, cosine_sum;
  float harmonic_sine[3], harmonic_cosine[3];
  float even_sine[3], even_cosine[3];
  float current_sum, current_sq;
  float current_sum_correction, current_sq_correction;
} stats_t;
static DRAM_ATTR stats_t stats[STAGES];
static unsigned stage, frame;
static unsigned saturation_window_frames, saturation_window_hits, saturated_windows;
static bool stopped, validation, dc_test, hold_neutral, ac_test;
static float dc_offset, dc_reference, last_raw, last_filtered;
static unsigned last_backlog;
#define DC_LIMIT_V CONTROL_OUTPUT_MAX_V
static unsigned stage_count;
static const char *reason;
static float sine_phase, cosine_phase, sine_increment, cosine_increment;

void app_tuning_init(bool validate) {
  if (validate)
    app_control_init();
  validation = validate;
  dc_test = false;
  ac_test = false;
  hold_neutral = false;
  stage_count = validation ? 2 : STAGES;
  stage = frame = 0;
  saturation_window_frames = saturation_window_hits = saturated_windows = 0;
  stopped = false;
  last_raw = last_filtered = 0.0f;
  last_backlog = 0;
  reason = "completed all stages";
  sine_phase = 0.0f;
  cosine_phase = 1.0f;
  sine_increment = sinf(2.0f * (float)M_PI * 60.0f / RATE);
  cosine_increment = cosf(2.0f * (float)M_PI * 60.0f / RATE);
  for (unsigned i = 0; i < STAGES; ++i)
    stats[i] = (stats_t){0};
  for (unsigned i = 0; i < stage_count; ++i) {
    const float kp = validation ? (i ? APP_CONTROL_KP : 0.0f) : kps[i];
    const float kr = validation ? (i ? APP_CONTROL_KR60 : 0.0f) : krs[i];
    const esp_pr_resonator_config_t branch = {
        .frequency_hz = 60.0f, .kr = kr, .damping_hz = 5.0f};
    const esp_pr_config_t config = {.sample_rate_hz = RATE,
                                    .kp = kp,
                                    .output_min = -LIMIT_V,
                                    .output_max = LIMIT_V,
                                    .anti_windup_tracking_time_s = 0.01f,
                                    .resonators = &branch,
                                    .resonator_count = kr > 0 ? 1 : 0};
    ESP_ERROR_CHECK(esp_pr_prepare(&models[i], &config));
    ESP_ERROR_CHECK(esp_pr_init(&controllers[i], &models[i]));
  }
  ESP_LOGW(TAG,
           "Finite R-only tuning: %s; discard first 2 s/stage; bounded output, "
           "no NVS writes",
           validation ? "8 s neutral + 60 s selected gains" : "9 x 8 s sweep");
}

void app_tuning_init_dc(float adc_offset, float reference_current) {
  ESP_ERROR_CHECK(isfinite(reference_current) && reference_current >= 0
                      ? ESP_OK
                      : ESP_ERR_INVALID_ARG);
  dc_reference = reference_current;
  app_tuning_init(true);
  dc_test = true;
  dc_offset = adc_offset;
  app_control_init_with_limit(DC_LIMIT_V);
  ESP_LOGW(TAG,
           "CURRENT TEST: R only, PR Kp=%.3g/Kr60=%.3g + PID Kp=0/Ki=%.3g/Kd=0; 4 s "
           "neutral + 60 s closed loop; expected external |I|=%.3f A",
           (double)APP_CONTROL_KP, (double)APP_CONTROL_KR60,
           (double)APP_CONTROL_KI, (double)dc_reference);
  ESP_LOGW(TAG, "Full DAC range: +/-6 Vsoft -> codes 0..4095; sustained rail saturation abort after %u one-second windows (>=10%% rail samples/window)", APP_TUNING_SATURATION_SECONDS);
}

void app_tuning_init_ac(float adc_offset, float reference_rms) {
  app_tuning_init_dc(adc_offset, reference_rms);
  ac_test = true;
  app_control_init_with_limit(DC_LIMIT_V);
  ESP_LOGW(TAG, "AC TEST: shared full DAC range; coordinated PR+I tracking; Ki=%.3g; reference is report-only", (double)APP_CONTROL_KI);
  ESP_LOGW(TAG, "Harmonic gains 60/120/180/300/420 Hz: %.3g / %.3g / %.3g / %.3g / %.3g",
           (double)APP_CONTROL_KR60, (double)APP_CONTROL_KR120, (double)APP_CONTROL_KR180,
           (double)APP_CONTROL_KR300, (double)APP_CONTROL_KR420);
}

void app_tuning_init_neutral(float adc_offset, float reference_current) {
  app_tuning_init_dc(adc_offset, reference_current);
  hold_neutral = true;
  stage_count = STAGES;
  ESP_LOGW(TAG, "NEUTRAL ONLY: 84 s / 9 windows; all DACs fixed at 2048; "
                "controllers discarded");
}

bool IRAM_ATTR app_tuning_done(void) { return stopped; }

void IRAM_ATTR app_tuning_abort(void) {
  stopped = true;
  reason = "ADC read failure";
}

float IRAM_ATTR app_tuning_step(float raw_error, float filtered_error,
                                unsigned backlog) {
  if (stopped)
    return 0.0f;
  last_raw = raw_error;
  last_filtered = filtered_error;
  last_backlog = backlog;
  if (!isfinite(raw_error) || !isfinite(filtered_error)) {
    stopped = true;
    reason = "nonfinite ADC/control value";
    return 0.0f;
  }
  if (backlog > 4) {
    stopped = true;
    reason = "ADC backlog >4";
    return 0.0f;
  }
  if (dc_test ? (raw_error + dc_offset < 0.1f || raw_error + dc_offset > 3.0f)
              : fabsf(raw_error) > 0.3f) {
    stopped = true;
    reason = "ADC input outside test range";
    return 0.0f;
  }
  float command = 0.0f;
  const esp_err_t update =
      validation && (stage == 1 || hold_neutral)
          ? app_parallel_update(&pr_fase_r, filtered_error, &command)
          : esp_pr_update(&controllers[stage], filtered_error, 0.0f, &command);
  if (update != ESP_OK || !isfinite(command)) {
    stopped = true;
    reason = "PR numerical failure";
    return 0.0f;
  }
  if (hold_neutral)
    command = 0.0f;
  const uint16_t command_code = app_control_voltage_to_dac(command);
  const bool limited = !hold_neutral &&
                       (command_code == 0 || command_code == DAC_MAX_CODE);
  const bool pid_limited = !hold_neutral && validation && stage == 1 &&
                          pr_fase_r.integral_enabled &&
                          fabsf(pr_fase_r.requested_integral) >=
                              pr_fase_r.integral.config.output_max * 0.999f;
  const bool pr_limited = !hold_neutral && validation && stage == 1 &&
                         fabsf(pr_fase_r.requested_pr) >=
                             pr_fase_r.pr.model->output_max * 0.999f;
  // Sample-based timing at the fixed 14.4 kHz rate. Rail duty, not an
  // instantaneous peak or an individual branch budget, determines the stop.
  if (!hold_neutral) {
    saturation_window_hits += limited;
    if (++saturation_window_frames == RATE) {
      saturated_windows = saturation_window_hits >= RATE / 10
                              ? saturated_windows + 1 : 0;
      saturation_window_frames = saturation_window_hits = 0;
      if (saturated_windows >= APP_TUNING_SATURATION_SECONDS) {
        stopped = true;
        reason = "sustained DAC rail saturation: >=10% per 1 s window for 5 s";
        return 0.0f;
      }
    }
  }

  const unsigned sample_start = dc_test && !hold_neutral && stage == 1
                                    ? (APP_DC_ACTIVE_SECONDS - 10U) * RATE
                                    : SETTLE_FRAMES;
  if (frame >= sample_start) {
    stats_t *s = &stats[stage];
    ++s->n;
    s->clipped += limited;
    s->pr_clipped += pr_limited;
    s->pid_clipped += pid_limited;
    app_compensated_add(&s->error_sum, &s->error_sum_correction,
                        filtered_error);
    app_compensated_add(&s->error_sq, &s->error_sq_correction,
                        filtered_error * filtered_error);
    app_compensated_add(&s->raw_sum, &s->raw_sum_correction, raw_error);
    app_compensated_add(&s->raw_sq, &s->raw_sq_correction,
                        raw_error * raw_error);
    s->command_sq += command * command;
    const float current =
        app_control_dac_to_primary_current(command_code);
    // Compensated sums avoid float drift over long DC windows (which could
    // otherwise make the reported RMS slightly smaller than the mean).
    const float y = current - s->current_sum_correction;
    const float sum = s->current_sum + y;
    s->current_sum_correction = (sum - s->current_sum) - y;
    s->current_sum = sum;
    const float y_sq = current * current - s->current_sq_correction;
    const float sum_sq = s->current_sq + y_sq;
    s->current_sq_correction = (sum_sq - s->current_sq) - y_sq;
    s->current_sq = sum_sq;
    s->command_peak = fmaxf(s->command_peak, fabsf(command));
    s->sine_sum += filtered_error * sine_phase;
    s->cosine_sum += filtered_error * cosine_phase;
    if (ac_test) {
      // Nominal 60 Hz correlations at odd harmonics, no trig/allocation here.
      const float c2 = cosine_phase * cosine_phase - sine_phase * sine_phase;
      const float s2 = 2.0f * sine_phase * cosine_phase;
      float es = s2, ec = c2;
      for (unsigned h = 0; h < 3; ++h) {
        s->even_sine[h] += filtered_error * es;
        s->even_cosine[h] += filtered_error * ec;
        const float ns = es * c2 + ec * s2;
        ec = ec * c2 - es * s2;
        es = ns;
      }
      float hs = sine_phase, hc = cosine_phase;
      for (unsigned h = 0; h < 3; ++h) {
        const float ns = hs * c2 + hc * s2;
        hc = hc * c2 - hs * s2;
        hs = ns;
        s->harmonic_sine[h] += filtered_error * hs;
        s->harmonic_cosine[h] += filtered_error * hc;
      }
    }
  }
  const float next_sine =
      sine_phase * cosine_increment + cosine_phase * sine_increment;
  cosine_phase = cosine_phase * cosine_increment - sine_phase * sine_increment;
  sine_phase = next_sine;

  const unsigned stage_frames =
      hold_neutral ? (stage == 0 ? 4U : 10U) * RATE
      : dc_test    ? (stage == 0 ? 4U : APP_DC_ACTIVE_SECONDS) * RATE
                   : (validation && stage == 1 ? 60U : 8U) * RATE;
  if (++frame == stage_frames) {
    frame = 0;
    saturation_window_frames = saturation_window_hits = saturated_windows = 0;
    sine_phase = 0.0f;
    cosine_phase = 1.0f;
    if (++stage == stage_count) {
      stopped = true;
      return 0.0f;
    }
  }
  return command;
}

void app_tuning_report(void) {
  ESP_LOGI(TAG, "TUNING STOPPED: %s; DAC neutral, control stopped", reason);
  ESP_LOGI(TAG, "Last raw_error=%.6f filtered_error=%.6f V backlog=%u%s",
           last_raw, last_filtered, last_backlog,
           dc_test ? " (ADC voltage=raw_error+loaded offset)" : "");
  for (unsigned i = 0; i < stage_count; ++i) {
    const stats_t *s = &stats[i];
    if (!s->n)
      continue;
    const float n = (float)s->n;
    const float rms = sqrtf(s->error_sq / n);
    const float mean = s->error_sum / n;
    const float ac = sqrtf(fmaxf(0.0f, s->error_sq / n - mean * mean));
    if (dc_test) {
      const float raw_mean = s->raw_sum / n;
      const float noise =
          sqrtf(fmaxf(0.0f, s->raw_sq / n - raw_mean * raw_mean));
      const float adc_mean = dc_offset + raw_mean;
      ESP_LOGI(TAG,
               "ADC: media=%.6f RMS=%.6f V; ruido_RMS=%.3f mV; "
               "erro_filtrado_RMS=%.3f mV",
               adc_mean, hypotf(adc_mean, noise), noise * 1000, rms * 1000);
      ESP_LOGI(TAG,
               "%s stage=%u n=%u Icomp_mean=%+.6f Icomp_rms=%.6f A; "
               "residual_mean=%+.3f residual_ac=%.3f mV; command_peak=%.4f "
               "Vsoft; limited=%.3f%%",
               ac_test ? "AC" : "DC", i, s->n, s->current_sum / n, sqrtf(s->current_sq / n),
               mean * 1000, ac * 1000, s->command_peak,
               100.0f * s->clipped / n);
      ESP_LOGI(TAG,
               "%s branch limits: PR %.3f%% / PID %.3f%% of useful samples",
               ac_test ? "AC" : "DC", 100.0f * s->pr_clipped / n, 100.0f * s->pid_clipped / n);
      if (ac_test) {
        const float current_mean = s->current_sum / n;
        const float current_ac = sqrtf(fmaxf(0.0f, s->current_sq / n - current_mean * current_mean));
        ESP_LOGI(TAG, "Current components: DC_mean=%+.6f AC_RMS=%.6f total_RMS=%.6f A", current_mean, current_ac, sqrtf(s->current_sq / n));
        const float fundamental = 1.41421356f * hypotf(s->sine_sum, s->cosine_sum) / n;
        ESP_LOGI(TAG, "AC residual: total_RMS=%.3f mV; fundamental60_RMS=%.3f mV", rms * 1000, fundamental * 1000);
        ESP_LOGI(TAG, "AC residual nominal even harmonics: 120=%.3f 240=%.3f 360=%.3f mV RMS",
                 1414.21356f * hypotf(s->even_sine[0], s->even_cosine[0]) / n,
                 1414.21356f * hypotf(s->even_sine[1], s->even_cosine[1]) / n,
                 1414.21356f * hypotf(s->even_sine[2], s->even_cosine[2]) / n);
        ESP_LOGI(TAG, "AC residual nominal harmonics: 180=%.3f 300=%.3f 420=%.3f mV RMS",
                 1414.21356f * hypotf(s->harmonic_sine[0], s->harmonic_cosine[0]) / n,
                 1414.21356f * hypotf(s->harmonic_sine[1], s->harmonic_cosine[1]) / n,
                 1414.21356f * hypotf(s->harmonic_sine[2], s->harmonic_cosine[2]) / n);
      }
      if (i == 1 && !hold_neutral && dc_reference > 0.0f) {
        ESP_LOGI(TAG,
                 "Reference comparison (external current %.3f A): "
                 "%.2f%% magnitude difference; residual check=%s. Estimate "
                 "depends on Rs, references, turns and zero calibration",
                 dc_reference,
                 100.0f * ((ac_test ? sqrtf(s->current_sq / n) : fabsf(s->current_sum / n)) / dc_reference - 1.0f),
                 (ac_test ? rms < 0.005f : fabsf(mean) < 0.005f) ? "yes" : "no");
      }
      if (i == 1 && !hold_neutral && dc_reference == 0.0f) {
        ESP_LOGI(TAG,
                 "ZERO CURRENT CHECK: apparent compensation=%+.6f A; do not "
                 "recalibrate with primary current present",
                 s->current_sum / n);
      }
      continue;
    }
    const float harmonic = 1.41421356f * hypotf(s->sine_sum, s->cosine_sum) / n;
    ESP_LOGI(TAG,
             "stage=%u kp=%.2f kr60=%.2f n=%u mean=%.3f rms=%.3f ac=%.3f "
             "raw_rms=%.3f e60=%.3f mV; u_rms=%.4f u_peak=%.4f Vsoft; "
             "clipped=%.3f%%",
             i, validation ? (i ? APP_CONTROL_KP : 0.0f) : kps[i],
             validation ? (i ? APP_CONTROL_KR60 : 0.0f) : krs[i], s->n,
             mean * 1000, rms * 1000, ac * 1000, sqrtf(s->raw_sq / n) * 1000,
             harmonic * 1000, sqrtf(s->command_sq / n), s->command_peak,
             100.0f * s->clipped / n);
  }
}

bool app_tuning_zero_offset(float inverse_fir_gain, float *offset) {
  if (!offset || !hold_neutral || dc_reference != 0.0f || !stopped ||
      stage != stage_count || !isfinite(inverse_fir_gain) ||
      inverse_fir_gain <= 0)
    return false;
  float sum = 0, minimum = INFINITY, maximum = -INFINITY;
  unsigned n = 0;
  for (unsigned i = stage_count - 4; i < stage_count; ++i) {
    if (!stats[i].n)
      return false;
    const float mean =
        dc_offset + inverse_fir_gain * stats[i].error_sum / stats[i].n;
    minimum = fminf(minimum, mean);
    maximum = fmaxf(maximum, mean);
    sum += stats[i].error_sum;
    n += stats[i].n;
  }
  // Commissioning quality gate, not a guaranteed accuracy bound.
  if (!n || maximum - minimum > 0.002f)
    return false;
  *offset = dc_offset + inverse_fir_gain * sum / n;
  return isfinite(*offset) && *offset > 0.1f && *offset < 3.0f;
}
