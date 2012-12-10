#include "showtime.h"
#include "alsa.h"

/**
 *
 */
int64_t
showtime_get_avtime(void)
{
  return showtime_get_ts();
}

const char *alsa_get_devicename(void)
{
  return "default";
}

