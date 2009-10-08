#ifndef PERFTIMER_H__
#define PERFTIMER_H__

#include "showtime.h"

typedef struct perftimer {
  int avg;
  int peak;
  int64_t start;
  int logsec;
} perftimer_t;

static inline void perftimer_start(perftimer_t *pt)
{
  pt->start = showtime_get_ts();
}

static inline void perftimer_stop(perftimer_t *pt, const char *str)
{
  int64_t now = showtime_get_ts();
  int d = now - pt->start;
  int s;

  if(pt->avg == 0) {
    pt->avg = d;
  } else {
    pt->avg = (d + pt->avg * 15) / 16;
  }
  
  if(d > pt->peak)
    pt->peak = d;

  s = now / 1000000;

  if(s != pt->logsec) {
    TRACE(TRACE_DEBUG, "timer", "%s: avg:%d, peak:%d",
	  str, pt->avg, pt->peak);
    pt->logsec = s;
  }
}

#endif /* PERFTIMER_H__ */
