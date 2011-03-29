/*
 *  Floating point conversion functions.
 *  Not accurate but should be enough for Showtime's needs
 *
 *  Copyright (C) 2011 Andreas Öman
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
 */

#include <stdio.h>
#include <math.h>
#include "dbl.h"


double
my_str2double(const char *str, const char **endp)
{
  double ret = 1.0f;
  int n = 0, m = 0, o = 0, e = 0;

  if(*str == '-') {
    ret = -1.0f;
    str++;
  }

  while(*str >= '0' && *str <= '9')
    n = n * 10 + *str++ - '0';

  if(*str != '.') {
    ret *= n;
  } else {

    str++;

    while(*str >= '0' && *str <= '9') {
      o = o * 10 + *str++ - '0';
      m--;
    }

    ret *= (n + pow(10, m) * o);
    
    if(*str == 'e' || *str == 'E') {
      int esign = 1;
      str++;
      
      if(*str == '+')
	str++;
      else if(*str == '-') {
	str++;
	esign = -1;
      }

      while(*str >= '0' && *str <= '9')
	e = e * 10 + *str++ - '0';
      ret *= pow(10, e * esign);
    }
  }

  if(endp != NULL)
    *endp = str;

  return ret;
}
