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
    kv_url_opt_set_deferred(url, domain, key, KVSTORE_SET_INT,
                            (int)duk_get_boolean(ctx, 3));

  } else if(duk_is_number(ctx, 3)) {
    kv_url_opt_set_deferred(url, domain, key, KVSTORE_SET_INT64,
                            (int64_t)duk_get_number(ctx, 3));

  } else if(duk_is_object_coercible(ctx, 3)) {
    kv_url_opt_set_deferred(url, domain, key, KVSTORE_SET_STRING,
                            duk_get_string(ctx, 3));
  } else {
    kv_url_opt_set_deferred(url, domain, key, KVSTORE_SET_VOID);
  }
  return 0;
}


/**
 * Showtime object exposed functions
 */
const duk_function_list_entry fnlist_Showtime_kvstore[] = {

  { "kvstoreGetString",      es_kvstore_get_string,   3},
  { "kvstoreGetInteger",     es_kvstore_get_int,      4},
  { "kvstoreGetBoolean",     es_kvstore_get_bool,     4},
  { "kvstoreSet",            es_kvstore_set,          4},
  { NULL, NULL, 0}
};
