/*
 *  Showtime mediacenter
 *  Copyright (C) 2007-2013 Andreas Öman
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

#include "showtime.h"
#include "webpopup.h"
#include "misc/str.h"

void
webpopup_finalize_result(webpopup_result_t *wr)
{
  if(wr->wr_trapped.url != NULL) {
    char hn[256];
    char path[4096];

    TRACE(TRACE_DEBUG, "webpopup", "Trapped URL: %s", wr->wr_trapped.url);

    url_split(NULL, 0, NULL, 0, hn, sizeof(hn),
              &wr->wr_trapped.port, path, sizeof(path), 
              wr->wr_trapped.url);

    wr->wr_trapped.hostname = strdup(hn);
    wr->wr_trapped.path     = strdup(path);
    

    char *k = strchr(wr->wr_trapped.path, '?');
    while(k) {
      *k++ = 0;
      
      char *v = strchr(k, '=');
      int kl, vl;

      char *n = strchr(k, '&');

      if(v == NULL) {
        if(n == NULL) {
          kl = strlen(k);
        } else {
          kl = n - k;
        }
      } else {
        kl = v - k;
        v++;
        if(n == NULL) {
          vl = strlen(v);
        } else {
          vl = n - v;
        }
      }

      http_header_t *hh = malloc(sizeof(http_header_t));
      hh->hh_key = malloc(kl+1);
      memcpy(hh->hh_key, k, kl);
      hh->hh_key[kl] = 0;

      if(v != NULL) {
        hh->hh_value = malloc(vl+1);
        memcpy(hh->hh_value, v, vl);
        hh->hh_value[vl] = 0;
      }
      k = n;
      LIST_INSERT_HEAD(&wr->wr_trapped.qargs, hh, hh_link);
    }
  }
}

/**
 *
 */
void
webpopup_result_free(webpopup_result_t *wr)
{
  free(wr->wr_trapped.url);
  free(wr->wr_trapped.hostname);
  free(wr->wr_trapped.path);
  http_headers_free(&wr->wr_trapped.qargs);
  free(wr);
}

