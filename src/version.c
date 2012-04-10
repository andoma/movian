#include "buildversion.h"
#include "config.h"

const char *htsversion=BUILD_VERSION;
const char *htsversion_full=BUILD_VERSION;

#include "showtime.h"
#include <stdio.h>

uint32_t
showtime_parse_version_int(const char *str)
{
  int major = 0;
  int minor = 0;
  int commit = 0;
  sscanf(str, "%d.%d.%d", &major, &minor, &commit);

  return
    major * 10000000 + 
    minor *   100000 +
    commit;
}

uint32_t
showtime_get_version_int(void) 
{
  return showtime_parse_version_int(BUILD_VERSION);
}

