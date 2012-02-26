#include "config.h"

#include "opensubtitles.h"
#include "httpcontrol.h"
#include "api.h"
#include "tmdb.h"

void
api_init(void)
{
  opensub_init();
#if ENABLE_HTTPSERVER
  httpcontrol_init();
#endif
  tmdb_init();
}
