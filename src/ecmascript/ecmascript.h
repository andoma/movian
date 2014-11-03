#pragma once

#include "ext/duktape/duktape.h"
#include "misc/queue.h"
#include "arch/threads.h"
#include "arch/atomic.h"
#include "compiler.h"

struct es_resource;
struct rstr;
struct prop;

#define ST_ERROR_SQLITE_BASE 0x10000


typedef enum {
  ES_NATIVE_PROP = 1,
  ES_NATIVE_RESOURCE,
  ES_NATIVE_HTSMSG,
  ES_NATIVE_HASH,

} es_native_type_t;



LIST_HEAD(es_resource_list, es_resource);
LIST_HEAD(es_context_list, es_context);


/**
 *
 */
typedef struct es_context {
  LIST_ENTRY(es_context) ec_link;
  char *ec_id;
  char *ec_path;
  char *ec_storage;

  int ec_linked;

  atomic_t ec_refcount;

  hts_mutex_t ec_mutex;
  duk_context *ec_duk;

  // Resource that will keep the duktape context alive
  // This include stuff such as page routes, service handles, etc
  struct es_resource_list ec_resources_permanent;

  // Resources to be automatically flushed when the context dies
  // This include stuff such as filedescriptors, database handles, etc
  struct es_resource_list ec_resources_volatile;

  size_t ec_mem_active;
  size_t ec_mem_peak;


  struct htsmsg *ec_manifest; // plugin.json

} es_context_t;


/**
 *
 */
typedef struct es_resource_class {
  const char *erc_name;
  size_t erc_size;

  void (*erc_destroy)(struct es_resource *er);

  void (*erc_info)(struct es_resource *er, char *buf, size_t buflen);

} es_resource_class_t;


/**
 *
 */
typedef struct es_resource {
  LIST_ENTRY(es_resource) er_link;
  const es_resource_class_t *er_class;
  es_context_t *er_ctx;
  atomic_t er_refcount;

} es_resource_t;


es_context_t *es_get(duk_context *ctx);

void es_dumpstack(duk_context *ctx);

void es_dump_err(duk_context *ctx);


void es_stprop_push(duk_context *ctx, struct prop *p);

/**
 * Native object wrapping
 */

void *es_get_native_obj(duk_context *ctx, int obj_idx, es_native_type_t type);

int es_push_native_obj(duk_context *ctx, es_native_type_t type, void *ptr);

/**
 * Resources
 */
static __inline void es_resource_retain(es_resource_t *er)
{
  atomic_inc(&er->er_refcount);
}

void *es_resource_alloc(const es_resource_class_t *erc);

void es_resource_release(es_resource_t *er);

void es_resource_destroy(es_resource_t *er);

void es_resource_link(es_resource_t *er, es_context_t *ec, int permanent);

void es_resource_unlink(es_resource_t *er);

void *es_resource_create(es_context_t *ec, const es_resource_class_t *erc,
                         int permanent);

void es_resource_push(duk_context *ctx, es_resource_t *er);

void *es_resource_get(duk_context *ctx, int obj_idx,
                      const es_resource_class_t *erc);

/**
 * Contexts
 */
static __inline es_context_t * attribute_unused_result
es_context_retain(es_context_t *ec)
{
  atomic_inc(&ec->ec_refcount);
  return ec;
}

void es_context_release(es_context_t *ec);

void es_context_begin(es_context_t *ec);

void es_context_end(es_context_t *ec);

es_context_t **ecmascript_get_all_contexts(void);

void ecmascript_release_context_vector(es_context_t **v);


/**
 * Plugin interface
 *
 * Version 1 is legacy plugin interface compatible with old spidermonkey
 * API
 *
 * Version 2 is the "new" API (until a even newer one is invented : 
 *
 */
int ecmascript_plugin_load(const char *id, const char *fullpath,
                           char *errbuf, size_t errlen,
                           int version, const char *manifest);

void ecmascript_plugin_unload(const char *id);


/**
 * Misc support
 */
int es_prop_is_true(duk_context *ctx, int obj_idx, const char *id);

int es_prop_to_int(duk_context *ctx, int obj_idx, const char *id, int def);

struct rstr *es_prop_to_rstr(duk_context *ctx, int obj_idx, const char *id);

struct prop *es_stprop_get(duk_context *ctx, int val_index);


/**
 * Rooted objects
 *
 * A root points to a value that we want to retain and also lookup
 * based on a pointer. They are saved in the global stash to avoid
 * being collected by GC.
 */
void es_root_register(duk_context *ctx, int obj_idx, void *ptr);

void es_root_unregister(duk_context *duk, void *ptr);

void es_push_root(duk_context *duk, void *ptr);


/**
 * Navigation/Backend interfaces
 */

int ecmascript_openuri(struct prop *page, const char *url, int sync);

void ecmascript_search(struct prop *model, const char *query,
                       struct prop *loading);

/**
 * Hooks
 */

int es_hook_invoke(const char *type,
                   int (*push_args)(duk_context *duk, void *opaque),
                   void *opaque);

/**
 * Crypto
 */
struct es_hash;
void es_hash_release(struct es_hash *);


/**
 * Function definitions
 */
extern const duk_function_list_entry fnlist_Showtime_service[];
extern const duk_function_list_entry fnlist_Showtime_route[];
extern const duk_function_list_entry fnlist_Showtime_hook[];
extern const duk_function_list_entry fnlist_Showtime_prop[];
extern const duk_function_list_entry fnlist_Showtime_io[];
extern const duk_function_list_entry fnlist_Showtime_fs[];
extern const duk_function_list_entry fnlist_Showtime_string[];
extern const duk_function_list_entry fnlist_Showtime_htsmsg[];
extern const duk_function_list_entry fnlist_Showtime_metadata[];
extern const duk_function_list_entry fnlist_Showtime_sqlite[];
extern const duk_function_list_entry fnlist_Showtime_misc[];
extern const duk_function_list_entry fnlist_Showtime_crypto[];

extern const duk_function_list_entry fnlist_Global_timer[];
