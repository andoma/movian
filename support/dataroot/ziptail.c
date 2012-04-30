#include <stdio.h>
#include "arch/threads.h"
#include "showtime.h"
#include "fileaccess/fileaccess.h"

static int initialized;
static char buf[256];
static hts_mutex_t mtx;
extern char *showtime_bin;

static void __attribute__((constructor)) showtime_dataroot_init(void)
{
  hts_mutex_init(&mtx);
}

const char *showtime_dataroot(void)
{
  if(!initialized) {
    hts_mutex_lock(&mtx);
    snprintf(buf, sizeof(buf), "zip://%s", showtime_bin);
    fa_reference(buf);
    hts_mutex_unlock(&mtx);
    initialized = 1;
  }
  return buf;
}
