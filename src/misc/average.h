#pragma once

#include <stdint.h>

/**
 *
 */
typedef struct average {
  int64_t last_val;
  int slots[4];
  int slotptr;
  int last;
} average_t;

void average_fill(average_t *avg, int now, int64_t value);

int average_read(average_t *avg, int now);
