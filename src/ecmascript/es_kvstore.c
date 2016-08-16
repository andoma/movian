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
#include <assert.h>

#include "ecmascript.h"
#include "db/kvstore.h"
#include "misc/rstr.h"


/**
 *
 */
static int
es_kvstore_get_domain(duk_context *ctx, int validx)
{
  const char *domain = duk_to_string(ctx, validx);
  if(!strcmp(domain, "plugin"))
    return KVSTORE_DOMAIN_PLUGIN;

  duk_error(ctx, DUK_ERR_ERROR, "Unknown domain %s", domain);
}


/**
 *
 */
static int
es_kvstore_get_string(duk_context *ctx)
{
  rstr_t *r = kv_url_opt_get_rstr(duk_to_string(ctx, 0),
                                  es_kvstore_get_domain(ctx, 1),
                                  duk_to_string(ctx, 2));
  if(r == NULL)
    return 0;

  duk_push_string(ctx, rstr_get(r));
  rstr_release(r);
  return 1;
}


/**
 *
 */
static int
es_kvstore_get_int(duk_context *ctx)
{
  int64_t r = kv_url_opt_get_int64(duk_to_string(ctx, 0),
                                   es_kvstore_get_domain(ctx, 1),
                                   duk_to_string(ctx, 2),
                                   duk_is_number(ctx, 3) ?
                                   duk_get_number(ctx, 3) : 0);

  duk_push_number(ctx, r);
  return 1;
}


/**
 *
 */
static int
es_kvstore_get_bool(duk_context *ctx)
{
  int r = kv_url_opt_get_int(duk_to_string(ctx, 0),
                             es_kvstore_get_domain(ctx, 1),
                             duk_to_string(ctx, 2),
                             duk_to_boolean(ctx, 3));

  duk_push_boolean(ctx, r);
  return 1;
}


/**
 *
 */
static int
es_kvstore_set(duk_context *ctx)
{
  const char *url = duk_to_string(ctx, 0);
  int domain      = es_kvstore_get_domain(ctx, 1);
  const char *key = duk_to_string(ctx, 2);

  if(duk_is_boolean(ctx, 3)) {
    kv_url_opt_set(url, domain, key, KVSTORE_SET_INT,
                   (int)duk_get_boolean(ctx, 3));

  } else if(duk_is_number(ctx, 3)) {
    kv_url_opt_set(url, domain, key, KVSTORE_SET_INT64,
                   (int64_t)duk_get_number(ctx, 3));

  } else if(duk_is_object_coercible(ctx, 3)) {
    kv_url_opt_set(url, domain, key, KVSTORE_SET_STRING,
                   duk_get_string(ctx, 3));
  } else {
    kv_url_opt_set(url, domain, key, KVSTORE_SET_VOID);
  }
  return 0;
}


static const duk_function_list_entry fnlist_kvstore[] = {

  { "getString",      es_kvstore_get_string,   3},
  { "getInteger",     es_kvstore_get_int,      4},
  { "getBoolean",     es_kvstore_get_bool,     4},
  { "set",            es_kvstore_set,          4},
  { NULL, NULL, 0}
};

ES_MODULE("kvstore", fnlist_kvstore);
