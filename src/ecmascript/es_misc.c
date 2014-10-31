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

#include "misc/str.h"
#include "keyring.h"
#include "notifications.h"

/**
 *
 */
static int
es_webpopup(duk_context *ctx)
{

  duk_push_object(ctx);

#if ENABLE_WEBPOPUP
  const char *url   = duk_safe_to_string(ctx, 0);
  const char *title = duk_safe_to_string(ctx, 1);
  const char *trap  = duk_safe_to_string(ctx, 2);

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
 *
 */
static int
es_entitydecode(duk_context *ctx)
{
  char *out = strdup(duk_safe_to_string(ctx, 0));
  html_entities_decode(out);
  duk_push_string(ctx, out);
  free(out);
  return 1;
}


/**
 *
 */
static int
es_queryStringSplit(duk_context *ctx)
{
  const char *str = duk_safe_to_string(ctx, 0);
  char *s0, *s;
  duk_push_object(ctx);

  s0 = s = strdup(str);

  while(s) {

    char *k = s;
    char *v = strchr(s, '=');
    if(v == NULL)
      break;

    *v++ = 0;

    if((s = strchr(v, '&')) != NULL)
      *s++ = 0;

    k = strdup(k);
    v = strdup(v);

    url_deescape(k);
    url_deescape(v);

    duk_push_string(ctx, v);
    duk_put_prop_string(ctx, -2, k);
    free(k);
    free(v);
  }
  free(s0);
  return 1;
}


/**
 *
 */
static int
es_escape(duk_context *ctx, int how)
{
  const char *str = duk_safe_to_string(ctx, 0);

  size_t len = url_escape(NULL, 0, str, how);
  char *r = malloc(len);
  url_escape(r, len, str, how);

  duk_push_lstring(ctx, r, len);
  free(r);
  return 1;
}

/**
 *
 */
static int
es_pathEscape(duk_context *ctx)
{
  return es_escape(ctx, URL_ESCAPE_PATH);
}


/**
 *
 */
static int
es_paramEscape(duk_context *ctx)
{
  return es_escape(ctx, URL_ESCAPE_PARAM);
}


/**
 *
 */
static int
es_getAuthCredentials(duk_context *ctx)
{
  char buf[256];
  char *username, *password;
  int r;

  const char *source = duk_safe_to_string(ctx, 0);
  const char *reason = duk_safe_to_string(ctx, 1);
  int query          = duk_to_boolean(ctx, 2);
  int forcetmp       = duk_to_boolean(ctx, 4);

  const char *id = duk_is_string(ctx, 3) ? duk_to_string(ctx, 3) : NULL;

  es_context_t *ec = es_get(ctx);

  snprintf(buf, sizeof(buf), "plugin-%s%s%s", ec->ec_id,
	   id ? "-" : "", id ?: "");

  int flags = 0;
  flags |= query    ? KEYRING_QUERY_USER : 0;
  flags |= forcetmp ? 0 : KEYRING_SHOW_REMEMBER_ME | KEYRING_REMEMBER_ME_SET;

  r = keyring_lookup(buf, &username, &password, NULL, NULL,
		     source, reason, flags);

  if(r == 1) {
    duk_push_false(ctx);
    return 1;
  }

  duk_push_object(ctx);

  if(r == -1) {

    duk_push_true(ctx);
    duk_put_prop_string(ctx, -2, "rejected");
  } else {

    duk_push_string(ctx, username);
    duk_put_prop_string(ctx, -2, "username");

    duk_push_string(ctx, password);
    duk_put_prop_string(ctx, -2, "password");
  }
  return 1;
}


/**
 *
 */
static int
es_message(duk_context *ctx)
{
  int r;

  const char *message = duk_to_string(ctx, 0);
  int ok     = duk_to_boolean(ctx, 1);
  int cancel = duk_to_boolean(ctx, 2);

  r = message_popup(message,
		    (ok     ? MESSAGE_POPUP_OK : 0) |
		    (cancel ? MESSAGE_POPUP_CANCEL : 0) |
		    MESSAGE_POPUP_RICH_TEXT, NULL);

  switch(r) {
  case MESSAGE_POPUP_OK:
    duk_push_true(ctx);
    break;
  case MESSAGE_POPUP_CANCEL:
    duk_push_false(ctx);
    break;
  default:
    duk_push_int(ctx, r);
    break;
  }
  return 1;
}


/**
 * Showtime object exposed functions
 */
const duk_function_list_entry fnlist_Showtime_misc[] = {
  { "webpopup",              es_webpopup,         3 },
  { "entityDecode",          es_entitydecode,     1 },
  { "queryStringSplit",      es_queryStringSplit, 1 },
  { "pathEscape",            es_pathEscape,       1 },
  { "paramEscape",           es_paramEscape,      1 },
  { "getAuthCredentials",    es_getAuthCredentials, 5 },
  { "message",               es_message, 3 },
  { NULL, NULL, 0}
};
 
