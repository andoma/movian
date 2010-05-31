#include "lastfm.h"
#include "opensubtitles.h"
#include "api.h"

void
api_init(void)
{
  lastfm_init();
  opensub_init();
}
