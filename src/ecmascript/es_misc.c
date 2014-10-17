/*
 *  Showtime Mediacenter
 *  Copyright (C) 2007-2014 Lonelycoder AB
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

#include <unistd.h>
#include <assert.h>

#include "ecmascript.h"

#if ENABLE_WEBPOPUP
#include "ui/webpopup.h"
#endif

/**
 *
 */
static int
es_webpopup(duk_context *ctx)
{
  const char *url   = duk_safe_to_string(ctx, 0);
  const char *title = duk_safe_to_string(ctx, 1);
  const char *trap  = duk_safe_to_string(ctx, 2);

  duk_push_object(ctx);

#if ENABLE_WEBPOPUP
  webpopup_result_t *wr = webpopup_create(url, title, trap);

  const char *t;
  switch(wr->wr_resultcode) {
  case WEBPOPUP_TRAPPED_URL:
    t = "trapped";
    break;
  case WEBPOPUP_CLOSED_BY_USER:
    t = "userclose";
    break;
  case WEBPOPUP_LOAD_ERROR:
    t = "neterror";
    break;
  default:
    t = "error";
    break;
  }



  duk_push_string(ctx, t);
  duk_put_prop_string(ctx, -2, "result");

  if(wr->wr_trapped.url != NULL) {
    duk_push_string(ctx, wr->wr_trapped.url);
    duk_put_prop_string(ctx, -2, "trappedUrl");
  }

  duk_push_object(ctx);

  http_header_t *hh;
  LIST_FOREACH(hh, &wr->wr_trapped.qargs, hh_link) {
    duk_push_string(ctx, hh->hh_value);
    duk_put_prop_string(ctx, -2, hh->hh_key);
  }
  duk_put_prop_string(ctx, -2, "args");

  webpopup_result_free(wr);
#else

  duk_push_string(ctx, "unsupported");
  duk_put_prop_string(ctx, -2, "result");
#endif
  return 1;
}

/**
 * Showtime object exposed functions
 */
const duk_function_list_entry fnlist_Showtime_misc[] = {
  { "webpopup",              es_webpopup,      3 },
  { NULL, NULL, 0}
};
 
