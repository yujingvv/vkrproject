#include "cycle_counter.h"

static uint64_t g_total = 0;

void cycles_reset(void) { g_total = 0; }
void cycles_add(uint32_t n) { g_total += (uint64_t)n; }
uint64_t cycles_total(void) { return g_total; }
