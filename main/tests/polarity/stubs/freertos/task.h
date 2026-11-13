#include "fakes.h"
typedef unsigned TickType_t;
TickType_t xTaskGetTickCount(void);
int xTaskDelayUntil(TickType_t *, TickType_t);
