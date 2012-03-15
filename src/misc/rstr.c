/*
 *  Referenced strings
 *  Copyright (C) 2009 Andreas Öman
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

#include <string.h>
#include "rstr.h"

#ifdef USE_RSTR

rstr_t *
rstr_alloc(const char *in)
{
  if(in == NULL)
    return NULL;
  size_t l = strlen(in);
  rstr_t *rs = malloc(sizeof(rstr_t) + l + 1);
  rs->refcnt = 1;
  memcpy(rs->str, in, l + 1);
  return rs;
}

rstr_t *
rstr_allocl(const char *in, size_t len)
{
  rstr_t *rs = malloc(sizeof(rstr_t) + len + 1);
  rs->refcnt = 1;
  if(in != NULL)
    memcpy(rs->str, in, len);
  rs->str[len] = 0;
  return rs;
}

rstr_t *
rstr_spn(rstr_t *s, const char *set)
{
  size_t len = strlen(rstr_get(s));
  size_t l = strcspn(rstr_get(s), set);
  if(l == len)
    return rstr_dup(s);
  return rstr_allocl(rstr_get(s), l);
}


#else

rstr_t *
rstr_dup(rstr_t *in)
{
  return in ? strdup(in) : NULL;
}

void
rstr_release(rstr_t *x)
{
  free(x);
}


rstr_t *
rstr_alloc(const char *in)
{
  if(in)
    return strdup(in);
  else
    return NULL;
}

rstr_t *
rstr_allocl(const char *in, size_t len)
{
  char *r = malloc(len + 1);

  if(in)
    memcpy(r, in, len);
  r[len] = 0;
  return r;
}


#endif
