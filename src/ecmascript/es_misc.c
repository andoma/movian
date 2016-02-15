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
#include <unistd.h>
#include <assert.h>

#include "ecmascript.h"

#if ENABLE_WEBPOPUP
#include "ui/webpopup.h"
#endif

#include "misc/str.h"
#include "keyring.h"
#include "notifications.h"
#include "blobcache.h"
#include "networking/http.h"
#include "networking/net.h"
#include "plugins.h"

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

  snprintf(buf, sizeof(buf), "plugin-%s%s%s", rstr_get(ec->ec_id),
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
 * This could be implemented in javscript instead
 */
static int
es_message(duk_context *ctx)
{
  int r;

  const char *message = duk_to_string(ctx, 0);
  int ok     = duk_to_boolean(ctx, 1);
  int cancel = duk_to_boolean(ctx, 2);

  if(!ok && !cancel)
    ok = 1; // We need to show one button at least

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
 * This could be implemented in javscript instead
 */
static int
es_textDialog(duk_context *ctx)
{
  int r;

  const char *message = duk_to_string(ctx, 0);
  int ok     = duk_to_boolean(ctx, 1);
  int cancel = duk_to_boolean(ctx, 2);

  char *input;

  r = text_dialog(message, &input,
		    (ok     ? MESSAGE_POPUP_OK : 0) |
		    (cancel ? MESSAGE_POPUP_CANCEL : 0) |
		    MESSAGE_POPUP_RICH_TEXT);

  if(r == 1) {
    duk_push_false(ctx);
    return 1;
  }

  duk_push_object(ctx);

  if(r == -1 || input == NULL) {
    duk_push_true(ctx);
    duk_put_prop_string(ctx, -2, "rejected");
  } else {
    duk_push_string(ctx, input);
    duk_put_prop_string(ctx, -2, "input");
  }
  return 1;
}


/**
 *
 */
static int
es_notify(duk_context *ctx)
{
  const char *text = duk_to_string(ctx, 0);
  unsigned int delay = duk_to_uint(ctx, 1);
  const char *icon = duk_get_string(ctx, 2);
  notify_add(NULL, NOTIFY_INFO, icon, delay, rstr_alloc("%s"), text);
  return 0;
}


/**
 *
 */
static int
es_cachePut(duk_context *ctx)
{
  const char *stash = duk_to_string(ctx, 0);
  const char *  key = duk_to_string(ctx, 1);
  duk_size_t bufsize;
  void *buf = duk_to_buffer(ctx, 2, &bufsize);
  int maxage = duk_get_uint(ctx, 3);
  buf_t *b = buf_create_and_copy(bufsize, buf);
  blobcache_put(key, stash, b, maxage, NULL, 0, 0);
  buf_release(b);
  return 0;
}


/**
 *
 */
static int
es_cacheGet(duk_context *ctx)
{
  const char *stash = duk_to_string(ctx, 0);
  const char *  key = duk_to_string(ctx, 1);
  buf_t *b = blobcache_get(key, stash, 0, NULL, NULL, NULL);
  if(b == NULL) {
    duk_push_null(ctx);
  } else {
    void *v = duk_push_fixed_buffer(ctx, buf_size(b));
    memcpy(v, buf_cstr(b), buf_size(b));
    buf_release(b);
  }
  return 1;
}


/**
 *
 */
static int
es_system_ip(duk_context *ctx)
{
    netif_t *ni = net_get_interfaces();
    if(ni == NULL)
      return 0;

    duk_push_sprintf(ctx, "%d.%d.%d.%d",
                     ni->ipv4_addr[0],
                     ni->ipv4_addr[1],
                     ni->ipv4_addr[2],
                     ni->ipv4_addr[3]);
    free(ni);

    return 1;
}

#if ENABLE_PLUGINS
/**
 *
 */
static int
es_select_view(duk_context *ctx)
{
  es_context_t *ec = es_get(ctx);
  const char *filename = duk_to_string(ctx, 0);
  plugin_select_view(rstr_get(ec->ec_id), filename);
  return 0;
}
#endif


/**
 * Showtime object exposed functions
 */
static const duk_function_list_entry fnlist_misc[] = {
  { "cachePut",              es_cachePut, 4},
  { "cacheGet",              es_cacheGet, 2},
  { "systemIpAddress",       es_system_ip, 0},
#if ENABLE_PLUGINS
  { "selectView",            es_select_view, 1},
#endif
  { NULL, NULL, 0}
};

ES_MODULE("misc", fnlist_misc);



static const duk_function_list_entry fnlist_popup[] = {
  { "webpopup",              es_webpopup,         3 },
  { "getAuthCredentials",    es_getAuthCredentials, 5 },
  { "message",               es_message, 3 },
  { "textDialog",            es_textDialog, 3 },
  { "notify",                es_notify, 3},
  { NULL, NULL, 0}
};

ES_MODULE("popup", fnlist_popup);
