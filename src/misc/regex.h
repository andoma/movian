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
#pragma once
#include "config.h"

#include "ext/minilibs/regexp.h"

typedef struct {
  Reprog *r;
  char *pat;
} hts_regex_t;

typedef struct {
  int rm_so;
  int rm_eo;
} hts_regmatch_t;

#define REG_EXTENDED 0

static __inline int hts_regcomp(hts_regex_t *r, const char *pat)
{
  const char *err;
  r->pat = strdup(pat);
  return !(r->r = myregcomp(pat, 0, &err));
}

static __inline int hts_regexec(hts_regex_t *r, const char *text,
                                int nmatches, hts_regmatch_t *matches, int v)
{
  Resub m;
  int i;
  printf("Comparing %s with pattern %s ... ", text ,r->pat);
  if(myregexec(r->r, text, &m, 0)) {
    printf("no match\n");
    return 1;
  }
  printf("MATCH\n");

  for(i = 0; i < m.nsub && i < nmatches; i++) {
    matches[i].rm_so = m.sub[i].sp - text;
    matches[i].rm_eo = m.sub[i].ep - text;
  }
  for(; i < nmatches; i++) {
    matches[i].rm_so = -1;
    matches[i].rm_eo = -1;
  }
  return 0;
}

static __inline void hts_regfree(hts_regex_t *r)
{
  myregfree(r->r);
  free(r->pat);
}

