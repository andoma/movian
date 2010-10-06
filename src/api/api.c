#include "config.h"

#include "lastfm.h"
#include "opensubtitles.h"
#include "httpcontrol.h"
#include "api.h"

void
api_init(void)
{
  lastfm_init();
  opensub_init();
#if ENABLE_HTTPSERVER
  httpcontrol_init();
#endif
}
