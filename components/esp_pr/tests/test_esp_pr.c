#include "esp_pr.h"
#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static const double pi = 3.14159265358979323846;
static esp_pr_resonator_config_t branches[] = {
    {60, 60000, 5}, {180, 6000, 5}, {300, 10000, 5}, {420, 13000, 5}};
static esp_pr_config_t config(float tt) {
  return (esp_pr_config_t){14400, 5, -6, 6, tt, branches, 4};
}
static void validation(void) {
  esp_pr_model_t m = {0};
  esp_pr_config_t c = config(.01f);
  assert(esp_pr_prepare(&m, &c) == ESP_OK);
  esp_pr_model_t old = m;
  c.sample_rate_hz = NAN;
  assert(esp_pr_prepare(&m, &c) != ESP_OK);
  assert(memcmp(&m, &old, sizeof m) == 0);
  c = config(.01f);
  c.output_max = c.output_min;
  assert(esp_pr_prepare(&m, &c) != ESP_OK);
  c = config(.01f);
  c.resonator_count = ESP_PR_MAX_RESONATORS + 1;
  assert(esp_pr_prepare(&m, &c) != ESP_OK);
  c = config(-1);
  assert(esp_pr_prepare(&m, &c) != ESP_OK);
  esp_pr_resonator_config_t invalid = {7200, 1, 5};
  c = config(0);
  c.resonators = &invalid;
  c.resonator_count = 1;
  assert(esp_pr_prepare(&m, &c) != ESP_OK);
  invalid = (esp_pr_resonator_config_t){60, -1, 5};
  assert(esp_pr_prepare(&m, &c) != ESP_OK);
  invalid = (esp_pr_resonator_config_t){60, 1, 0};
  assert(esp_pr_prepare(&m, &c) != ESP_OK);
  esp_pr_t a, b;
  assert(esp_pr_init(&a, &old) == ESP_OK);
  assert(esp_pr_init(&b, &old) == ESP_OK);
  float out = 123;
  assert(esp_pr_update(&a, .1f, 0, &out) == ESP_OK);
  esp_pr_t saved = a;
  out = 123;
  assert(esp_pr_update(&a, NAN, 0, &out) != ESP_OK);
  assert(esp_pr_update(&a, FLT_MAX, -FLT_MAX, &out) != ESP_OK);
  assert(esp_pr_update(&a, FLT_MAX, 0, &out) != ESP_OK);
  assert(out == 123 && memcmp(&a, &saved, sizeof a) == 0);
  assert(esp_pr_update(&b, 0, 0, &out) == ESP_OK && out == 0);
  assert(esp_pr_reset(&a) == ESP_OK);
  assert(esp_pr_update(&a, 0, 0, &out) == ESP_OK && out == 0);
  unsigned orders[] = {1, 3, 5, 7};
  float gains[] = {60000, 6000, 10000, 13000}, damping[] = {5, 5, 5, 5};
  esp_pr_resonator_config_t h[4];
  assert(esp_pr_make_harmonics(h, 4, 60, orders, gains, damping) == ESP_OK);
  assert(memcmp(h, branches, sizeof h) == 0);
  orders[3] = 0;
  assert(esp_pr_make_harmonics(h, 4, 60, orders, gains, damping) != ESP_OK);
  assert(memcmp(h, branches, sizeof h) == 0);
  c = config(.01f);
  c.resonator_count = 0;
  c.resonators = NULL;
  assert(esp_pr_prepare(&m, &c) == ESP_OK);
  assert(esp_pr_init(&a, &m) == ESP_OK);
  assert(esp_pr_update(&a, 2, 0, &out) == ESP_OK && out == 6);
  assert(esp_pr_update(&a, -2, 0, &out) == ESP_OK && out == -6);
  puts("PASS validation, atomic errors, reset, independent states, harmonic "
       "helper, P-only");
}

static void legacy_equivalence(void) {
  const double b0[] = {130.599849414, 13.0480787197, 21.7071428583,
                       28.1420636624};
  const double a1[] = {-1.99496281341, -1.98949871552, -1.9785854357,
                       -1.96225276556};
  const double a2[] = {.995646671686, .995650640427, .995658571428,
                       .995670451744};
  double y1[4] = {0}, y2[4] = {0}, e1 = 0, e2 = 0, err2 = 0, ref2 = 0;
  esp_pr_config_t c = config(0);
  c.output_min = -1e9f;
  c.output_max = 1e9f;
  esp_pr_model_t m;
  esp_pr_t p;
  assert(esp_pr_prepare(&m, &c) == ESP_OK);
  assert(esp_pr_init(&p, &m) == ESP_OK);
  for (int k = 0; k < 144000; ++k) {
    float e = (float)(.001 * (sin(2 * pi * 60 * k / 14400) +
                              .2 * sin(2 * pi * 180 * k / 14400) +
                              .3 * sin(2 * pi * 300 * k / 14400) +
                              .1 * sin(2 * pi * 420 * k / 14400)));
    if (k < 100)
      e += .001f;
    double reference = 5 * e;
    for (int i = 0; i < 4; ++i) {
      double y = b0[i] * (e - e2) - a1[i] * y1[i] - a2[i] * y2[i];
      y2[i] = y1[i];
      y1[i] = y;
      reference += y;
    }
    e2 = e1;
    e1 = e;
    float out;
    assert(esp_pr_update(&p, e, 0, &out) == ESP_OK);
    err2 += (out - reference) * (out - reference);
    ref2 += reference * reference;
  }
  double relative = sqrt(err2 / ref2);
  printf("Legacy coefficient-table relative RMS error: %.8g\n", relative);
  assert(relative < .001);
}

static void frequency_response(void) {
  const double frequencies[] = {0, 30, 60, 63, 120, 180, 300, 420, 1000};
  for (size_t f = 0; f < sizeof frequencies / sizeof *frequencies; ++f) {
    double frequency = frequencies[f];
    esp_pr_config_t c = config(0);
    c.output_min = -1e9f;
    c.output_max = 1e9f;
    esp_pr_model_t m;
    esp_pr_t p;
    assert(esp_pr_prepare(&m, &c) == ESP_OK);
    assert(esp_pr_init(&p, &m) == ESP_OK);
    double re = 5, im = 0;
    for (int i = 0; i < 4; ++i) {
      double w = 2 * pi * branches[i].frequency_hz,
             wc = 2 * pi * branches[i].damping_hz;
      double kappa = w / tan(w / (2 * 14400));
      double omega = kappa * tan(pi * frequency / 14400);
      double den_re = w * w - omega * omega, den_im = 2 * wc * omega;
      double num_im = branches[i].kr * den_im;
      double den = den_re * den_re + den_im * den_im;
      re += num_im * den_im / den;
      im += num_im * den_re / den;
    }
    double err2 = 0, ref2 = 0;
    for (int k = 0; k < 43200; ++k) {
      double angle = 2 * pi * frequency * k / 14400;
      float out;
      assert(esp_pr_update(&p, (float)(.001 * cos(angle)), 0, &out) == ESP_OK);
      if (k >= 28800) {
        double reference = .001 * (re * cos(angle) - im * sin(angle));
        err2 += (out - reference) * (out - reference);
        ref2 += reference * reference;
      }
    }
    double relative = sqrt(err2 / ref2);
    printf("Response %4g Hz: relative RMS error %.8g\n", frequency, relative);
    /* At DC the resonant signal cancels against a much larger stored
     * quadrature state. Bound float leakage relative to Kp separately. */
    assert(relative < (frequency == 0 ? .01 : .003));
  }
}

static double saturation_case(float tt, double *peak_state) {
  esp_pr_config_t c = config(tt);
  esp_pr_model_t m;
  esp_pr_t p;
  assert(esp_pr_prepare(&m, &c) == ESP_OK);
  assert(esp_pr_init(&p, &m) == ESP_OK);
  double tail_energy = 0;
  *peak_state = 0;
  for (int k = 0; k < 57600; ++k) {
    float error = k < 43200 ? (float)(.1 * sin(2 * pi * 60 * k / 14400)) : 0;
    float out;
    assert(esp_pr_update(&p, error, 0, &out) == ESP_OK);
    assert(out >= -6 && out <= 6);
    if (k >= 43200 && k < 44640)
      tail_energy += out * out;
    double sum = 0;
    for (size_t i = 0; i < 4; ++i)
      sum += hypot(p.branches[i].q, p.branches[i].p);
    if (sum > *peak_state)
      *peak_state = sum;
    assert(isfinite(sum));
  }
  return tail_energy;
}
static void linear_and_disabled_branch(void) {
  esp_pr_config_t c = config(0);
  esp_pr_model_t off, on;
  esp_pr_t a, b;
  assert(esp_pr_prepare(&off, &c) == ESP_OK);
  c.anti_windup_tracking_time_s = .01f;
  assert(esp_pr_prepare(&on, &c) == ESP_OK);
  assert(esp_pr_init(&a, &off) == ESP_OK);
  assert(esp_pr_init(&b, &on) == ESP_OK);
  for (int k = 0; k < 14400; ++k) {
    float x = (float)(1e-6 * sin(2 * pi * 60 * k / 14400)), y0, y1;
    assert(esp_pr_update(&a, x, 0, &y0) == ESP_OK);
    assert(esp_pr_update(&b, x, 0, &y1) == ESP_OK);
    assert(!a.diagnostics.saturated && !b.diagnostics.saturated && y0 == y1);
  }
  esp_pr_resonator_config_t zero = {60, 0, 5};
  c.resonators = &zero;
  c.resonator_count = 1;
  assert(esp_pr_prepare(&on, &c) == ESP_OK);
  assert(esp_pr_init(&b, &on) == ESP_OK);
  for (int k = 0; k < 1000; ++k) {
    float y;
    assert(esp_pr_update(&b, k % 2 ? 10 : -10, 0, &y) == ESP_OK);
    assert(b.branches[0].q == 0 && b.branches[0].p == 0);
  }
  esp_pr_diagnostics_t d;
  assert(esp_pr_get_diagnostics(&b, &d) == ESP_OK);
  assert(d.saturated && d.output == 6 && d.proportional == 50);
  puts("PASS identical unsaturated response, zero-gain branch and diagnostics");
}

static void anti_windup(void) {
  double peak_off, peak_on;
  double off = saturation_case(0, &peak_off),
         on = saturation_case(.01f, &peak_on);
  printf("Saturation: peak state %.3f -> %.3f; first 100ms release energy %.3f "
         "-> %.3f\n",
         peak_off, peak_on, off, on);
  assert(peak_on < peak_off && on < off);
  double unused;
  saturation_case(1e-9f, &unused);
  saturation_case(10, &unused);
  esp_pr_config_t c = config(.01f);
  esp_pr_model_t m;
  esp_pr_t p;
  assert(esp_pr_prepare(&m, &c) == ESP_OK);
  assert(esp_pr_init(&p, &m) == ESP_OK);
  float out;
  assert(esp_pr_update(&p, .1f, 0, &out) == ESP_OK);
  double before = 0, after = 0;
  for (int i = 0; i < 4; ++i) {
    before += p.diagnostics.resonant_output[i];
    after += p.branches[i].p;
  }
  assert(fabs((after - before) -
              m.correction_alpha * p.diagnostics.saturation_error) < 1e-5);
  puts("PASS anti-windup shared correction, saturation/release and "
       "tracking-time extremes");
}
int main(void) {
  validation();
  legacy_equivalence();
  frequency_response();
  linear_and_disabled_branch();
  anti_windup();
  puts("All esp_pr host tests passed.");
  return 0;
}
