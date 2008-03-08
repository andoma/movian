/*
 *  File access common functions
 *  Copyright (C) 2008 Andreas Öman
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

#define _GNU_SOURCE

#include <pthread.h>
#include <assert.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "showtime.h"
#include "fileaccess.h"

struct fa_protocol_list fileaccess_all_protocols;

/**
 *
 */
const char *
fa_resolve_proto(const char *url, fa_protocol_t **p)
{
  fa_protocol_t *fap;

  char proto[30];
  int n = 0;

  while(*url != ':' && *url>31 && n < sizeof(proto) - 1)
    proto[n++] = *url++;

  proto[n] = 0;

  if(url[0] != ':' || url[1] != '/' || url[2] != '/')
    return NULL;

  url += 3;

  LIST_FOREACH(fap, &fileaccess_all_protocols, fap_link)
    if(!strcmp(fap->fap_name, proto)) {
      *p = fap;
      return url;
    }
  return NULL;
}


/**
 *
 */
int
fileaccess_scandir(const char *url, fa_scandir_callback_t *cb, void *arg)
{
  fa_protocol_t *fap;

  if((url = fa_resolve_proto(url, &fap)) == NULL)
    return -1;

  return fap->fap_scan(url, cb, arg);
}



/**
 *
 */

#define INITPROTO(a)							      \
 {									      \
   extern  fa_protocol_t fa_protocol_ ## a;				      \
   LIST_INSERT_HEAD(&fileaccess_all_protocols, &fa_protocol_ ## a, fap_link); \
 }

void
fileaccess_init(void)
{
  INITPROTO(fs);
}
