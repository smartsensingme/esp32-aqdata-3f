#include "app_control.h"
#include <assert.h>
#include <math.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>

static atomic_bool stop;

static void check_rms(void) {
  float r = -1.0f, s = -1.0f, t = -1.0f;
  assert(app_control_calc_rms(&r, &s, &t));
  assert(fabsf(r - 80.0f) < 0.002f);
  assert(fabsf(s - (80.0f / 4095.0f)) < 0.002f);
  assert(fabsf(t - 80.0f) < 0.002f);
}

static void *producer(void *unused) {
  (void)unused;
  while (!atomic_load_explicit(&stop, memory_order_relaxed))
    app_control_store_telemetry(0, 2048, 4095);
  return NULL;
}

int main(void) {
  float untouched = -1.0f;
  assert(!app_control_calc_rms(&untouched, NULL, NULL));
  assert(untouched == -1.0f);
  assert(!app_control_store_telemetry(0, 0, 0));

  assert(app_control_request_capture());
  assert(!app_control_request_capture());
  for (unsigned i = 0; i < RMS_BUFFER_SIZE - 1; ++i)
    assert(!app_control_store_telemetry(0, 2048, 4095));
  assert(!app_control_calc_rms(NULL, NULL, NULL));
  assert(app_control_store_telemetry(0, 2048, 4095));
  assert(!app_control_request_capture());

  /* A ready buffer must remain intact until the consumer releases it. */
  for (unsigned i = 0; i < RMS_BUFFER_SIZE; ++i)
    assert(!app_control_store_telemetry(2048, 0, 2048));
  check_rms();
  assert(!app_control_calc_rms(NULL, NULL, NULL));

  /* Exercise request/completion handoffs with concurrent producer/consumer. */
  pthread_t thread;
  assert(pthread_create(&thread, NULL, producer, NULL) == 0);
  for (unsigned capture = 0; capture < 500; ++capture) {
    assert(app_control_request_capture());
    float r, s, t;
    while (!app_control_calc_rms(&r, &s, &t))
      sched_yield();
    assert(fabsf(r - 80.0f) < 0.002f);
    assert(fabsf(s - (80.0f / 4095.0f)) < 0.002f);
    assert(fabsf(t - 80.0f) < 0.002f);
  }
  atomic_store_explicit(&stop, true, memory_order_relaxed);
  assert(pthread_join(thread, NULL) == 0);
  return 0;
}
