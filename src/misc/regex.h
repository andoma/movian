#pragma once
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

#include "config.h"

#include "ext/trex/trex.h"

typedef struct {
  TRex *r;
} hts_regex_t;

typedef struct {
  int rm_so;
  int rm_eo;
} hts_regmatch_t;

#define REG_EXTENDED 0

static __inline int hts_regcomp(hts_regex_t *r, const char *pat)
{
  const char *err;
  return !(r->r = trex_compile(pat, &err));
}

static __inline int hts_regexec(hts_regex_t *r, const char *text,
                                int nmatches, hts_regmatch_t *matches, int v)
{
  int i;
  TRexMatch m;
  if(trex_match(r->r, text) == TRex_False)
    return 1;
  
  for(i = 0; i < nmatches; i++) {
    if(trex_getsubexp(r->r, i, &m) == TRex_False) {
      matches[i].rm_so = -1;
      matches[i].rm_eo = -1;
    } else {
      matches[i].rm_so = m.begin - text;
      matches[i].rm_eo = matches[i].rm_so + m.len;
    }
  }
  return 0;
}

static __inline void hts_regfree(hts_regex_t *r)
{
  trex_free(r->r);
}

