#include <time.h>

#include "showtime.h"
#include "audio2/alsa.h"


int64_t
showtime_get_avtime(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec * 1000000LL + ts.tv_nsec / 1000;
}

const char *alsa_get_devicename(void)
{
  return "hw:1,0";
}
