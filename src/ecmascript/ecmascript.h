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
#include "main.h"
#include "ext/duktape/duktape.h"
#include "misc/queue.h"
#include "misc/lockmgr.h"
#include "arch/threads.h"
#include "arch/atomic.h"
#include "compiler.h"

struct es_resource;
struct rstr;
struct prop;
struct prop_vec;

#define ST_ERROR_PROP_ZOMBIE 0x8000
#define ST_ERROR_SQLITE_BASE 0x10000


LIST_HEAD(es_resource_list, es_resource);
LIST_HEAD(es_context_list, es_context);



/**
 * Native class
 */
typedef struct ecmascript_native_class {
  const char *name;
  int id;
  void (*release)(void *ptr);
} ecmascript_native_class_t;

void ecmascript_register_native_class(ecmascript_native_class_t *c);

#define ES_NATIVE_CLASS(nam, fn)                                  \
  ecmascript_native_class_t HTS_JOIN(es_native_, nam) = {   \
    .name = #nam,                                                  \
    .release = (void *)fn                                         \
  };                                                              \
  INITIALIZER(HTS_JOIN(esnativeclassdefinit, __LINE__))                \
  { ecmascript_register_native_class(&HTS_JOIN(es_native_, nam));}




/**
 *
 */
typedef struct es_context {
  LIST_ENTRY(es_context) ec_link;
  rstr_t *ec_id;
  char *ec_path;
  char *ec_storage;

  char ec_debug;
  char ec_bypass_file_acl_write;
  char ec_bypass_file_acl_read;

  int ec_flags;

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

  struct prop_vec *ec_prop_unload_destroy;

} es_context_t;


/**
 *
 */
typedef struct es_resource_class {
  const char *erc_name;
  size_t erc_size;

  void (*erc_destroy)(struct es_resource *er);

  void (*erc_info)(struct es_resource *er, char *buf, size_t buflen);

  void (*erc_finalizer)(struct es_resource *er);

} es_resource_class_t;


/**
 *
 */
typedef struct es_resource {
  LIST_ENTRY(es_resource) er_link;
  const es_resource_class_t *er_class;
  es_context_t *er_ctx;
  char er_zombie;
  atomic_t er_refcount;

} es_resource_t;


es_context_t *es_get(duk_context *ctx);

void es_dumpstack(duk_context *ctx);

void es_dump_err(duk_context *ctx);

int es_get_err_code(duk_context *ctx);


void es_stprop_push(duk_context *ctx, struct prop *p);

/**
 * Native object wrapping
 */

void *es_get_native_obj(duk_context *ctx, int obj_idx,
                        ecmascript_native_class_t *c);

void *es_get_native_obj_nothrow(duk_context *ctx, int obj_idx,
                                ecmascript_native_class_t *c);

int es_push_native_obj(duk_context *ctx, ecmascript_native_class_t *c,
                       void *ptr);

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

void es_context_end(es_context_t *ec, int do_gc);

es_context_t **ecmascript_get_all_contexts(void);

void ecmascript_release_context_vector(es_context_t **v);

int ecmascript_context_lockmgr(void *ptr, lockmgr_op_t op);


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
                           int version, const char *manifest,
                           int flags);

#define ECMASCRIPT_DEBUG                 0x1
#define ECMASCRIPT_FILE_BYPASS_ACL_READ  0x2
#define ECMASCRIPT_FILE_BYPASS_ACL_WRITE 0x4
#define ECMASCRIPT_PLUGIN                0x8

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
 * Create a new context, load and execute script given by url
 *
 * flags are ECMASCRIPT_ -flags
 */
void ecmascript_load(const char *ctxid, int flags, const char *url,
                     const char *storage);

/**
 *
 */
#define es_debug(ec, fmt, ...) do {                                     \
    if((ec)->ec_debug) {                                                \
      TRACE(TRACE_DEBUG, rstr_get((ec)->ec_id), fmt, ##__VA_ARGS__);   \
    }                                                                   \
  } while(0)


/**
 * Native modules
 */
typedef struct ecmascript_module {
  LIST_ENTRY(ecmascript_module) link;
  const char *name;
  const duk_function_list_entry *functions;
} ecmascript_module_t;

void ecmascript_register_module(ecmascript_module_t *m);

#define ES_MODULE(nam, fn)                                        \
  static ecmascript_module_t HTS_JOIN(esmoduledef, __LINE__) = {  \
    .name = nam,                                                  \
    .functions = fn                                               \
  };                                                              \
  INITIALIZER(HTS_JOIN(esmoduledefinit, __LINE__))                \
  { ecmascript_register_module(&HTS_JOIN(esmoduledef, __LINE__));}

/**
 * Global functions
 */
extern const duk_function_list_entry es_fnlist_timer[];
extern const duk_function_list_entry es_fnlist_console[];
