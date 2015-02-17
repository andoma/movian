/*
 *  Copyright (C) 2007-2015 Lonelycoder AB
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
#include "showtime.h"
#include "longpress.h"

int
longpress_periodic(lphelper_t *lph, int64_t now)
{
 if(lph->down != 1)
    return 0;

  if(lph->expire > now)
    return 0;

  lph->down = 2;
  return 1;
}

void
longpress_down(lphelper_t *lph)
{
  if(lph->down)
    return;
  lph->expire = arch_get_ts() + LP_TIMEOUT;
  lph->down = 1;
}

int
longpress_up(lphelper_t *lph)
{
  int r = lph->expire > arch_get_ts() && lph->down != 2;
  lph->down = 0;
  return r;
}
