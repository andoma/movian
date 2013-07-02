#include <time.h>
#include "time.h"

static int
leapyear(int year)
{
  return (year & 3) == 0 && (year % 100 != 0 || year % 400 == 0);
}


static const int mdays[2][12] = {
  {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334},
  {0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335}
};


/**
 *
 */
int
mktime_utc(time_t *tp, unsigned int year, unsigned int month, unsigned int day,
           unsigned int hour, unsigned int min, unsigned int sec)
{
  int i;

  if(year < 1970 || year > 2038 || month >= 12)
    return -1;

  i = 1970;
  if(year >= 2011) {
    sec += 1293840000;
    i = 2011;
  }

  for(; i < year; i++)
    sec += 86400 * (365 + leapyear(i));

  sec += mdays[leapyear(year)][month] * 86400;
  sec += 86400 * (day - 1) + hour * 3600 + min * 60;
  *tp = sec;
  return 0;
}
