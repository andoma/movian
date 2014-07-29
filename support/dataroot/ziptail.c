#include <stdio.h>
#include "arch/threads.h"
#include "showtime.h"
#include "fileaccess/fileaccess.h"

static int initialized;
static char buf[256];
static HTS_MUTEX_DECL(mtx);

const char *showtime_dataroot(void)
{
  if(!initialized) {
    hts_mutex_lock(&mtx);
    snprintf(buf, sizeof(buf), "zip://%s", gconf.binary);
    fa_reference(buf);
    hts_mutex_unlock(&mtx);
    initialized = 1;
  }
  return buf;
}
