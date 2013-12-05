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

