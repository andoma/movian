/*
 *  Showtime Mediacenter
 *  Copyright (C) 2007-2013 Lonelycoder AB
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *  This program is also available under a commercial proprietary license.
 *  For more information, contact andreas@lonelycoder.com
 */
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
