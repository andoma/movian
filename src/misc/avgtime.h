#ifndef AVGTIME_H__
#define AVGTIME_H__

#include "showtime.h"
#include "prop.h"

typedef struct avgtime {
  int samples[10];
  int ptr;

  int start;

  int peak;
  int avg;
} avgtime_t;

static inline void avgtime_start(avgtime_t *a)
{
  a->start = showtime_get_ts();
}

static inline void avgtime_stop(avgtime_t *a, prop_t *avg, prop_t *peak)
{
  int64_t now = showtime_get_ts();
  int d = now - a->start;
  int i, sum;

  a->ptr++;
  if(a->ptr == 10)
    a->ptr = 0;
  
  a->samples[a->ptr] = d;
  
  if(d > a->peak)
    a->peak = d;
  
  for(sum = 0, i = 0; i < 10; i++)
    sum += a->samples[i];
  a->avg = sum / 10;

  if(avg != NULL)
    prop_set_int(avg, a->avg / 1000);

  if(peak != NULL)
    prop_set_int(peak, a->peak / 1000);
}

#endif /* AVGTIME_H__ */
