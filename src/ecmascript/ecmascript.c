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
#include "main.h"
#include "arch/arch.h"
#include "fileaccess/fileaccess.h"
#include "backend/backend.h"
#include "htsmsg/htsmsg.h"
#include "ecmascript.h"
#include "misc/minmax.h"

static int es_num_contexts;
static struct es_context_list es_contexts;
static HTS_MUTEX_DECL(es_context_mutex);


static LIST_HEAD(, ecmascript_module) modules;

ES_NATIVE_CLASS(resource, &es_resource_release);


/**
 *
 */
int
ecmascript_context_lockmgr(void *ptr, lockmgr_op_t op)
{
  es_context_t *ec = ptr;

  switch(op) {
  case LOCKMGR_UNLOCK:
    hts_mutex_unlock(&ec->ec_mutex);
    return 0;
  case LOCKMGR_LOCK:
    hts_mutex_lock(&ec->ec_mutex);
    return 0;
  case LOCKMGR_TRY:
    return hts_mutex_trylock(&ec->ec_mutex);

  case LOCKMGR_RETAIN:
    atomic_inc(&ec->ec_refcount);
    return 0;

  case LOCKMGR_RELEASE:
    es_context_release(ec);
    return 0;
  }
  abort();
}


/**
 *
 */
void
ecmascript_register_module(ecmascript_module_t *m)
{
  LIST_INSERT_HEAD(&modules, m, link);
}


/**
 *
 */
int
es_prop_is_true(duk_context *ctx, int obj_idx, const char *id)
{
  if(!duk_is_object(ctx, obj_idx))
    return 0;

  duk_get_prop_string(ctx, obj_idx, id);
  int r = duk_to_boolean(ctx, -1);
  duk_pop(ctx);
  return r;
}


/**
 *
 */
int
es_prop_to_int(duk_context *ctx, int obj_idx, const char *id, int def)
{
  if(!duk_is_object(ctx, obj_idx))
    return def;

  duk_get_prop_string(ctx, obj_idx, id);
  if(duk_is_number(ctx, -1))
    def = duk_to_int(ctx, -1);
  duk_pop(ctx);
  return def;
}


/**
 *
 */
rstr_t *
es_prop_to_rstr(duk_context *ctx, int obj_idx, const char *id)
{
  rstr_t *r = NULL;

  if(!duk_is_object(ctx, obj_idx))
    return NULL;

  duk_get_prop_string(ctx, obj_idx, id);
  const char *str = duk_get_string(ctx, -1);
  if(str != NULL)
    r = rstr_alloc(str);
  duk_pop(ctx);
  return r;
}


/**
 *
 */
void
es_dumpstack(duk_context *ctx)
{
  int size = duk_get_top(ctx);
  printf("STACKDUMP\n");
  for(int i = -1; i > -1 - size; i--) {
    duk_dup(ctx, i);
    printf("  [%5d]: %s\n", i, duk_safe_to_string(ctx, -1));
    duk_pop(ctx);
  }
}



/**
 *
 */
es_context_t *
es_get(duk_context *ctx)
{
  duk_push_global_stash(ctx);
  duk_get_prop_string(ctx, -1, "esctx");
  es_context_t *es = duk_get_pointer(ctx, -1);
  duk_pop_2(ctx);
  assert(es != NULL);
  return es;
}


/**
 *
 */
void *
es_resource_alloc(const es_resource_class_t *erc)
{
  es_resource_t *er = calloc(1, erc->erc_size);
  er->er_class = erc;
  return er;
}


/**
 *
 */
void
es_resource_link(es_resource_t *er, es_context_t *ec, int permanent)
{
  er->er_ctx = es_context_retain(ec);
  atomic_inc(&er->er_refcount);
  if(permanent)
    LIST_INSERT_HEAD(&ec->ec_resources_permanent, er, er_link);
  else
    LIST_INSERT_HEAD(&ec->ec_resources_volatile, er, er_link);
}


/**
 *
 */
void *
es_resource_create(es_context_t *ec, const es_resource_class_t *erc,
                   int permanent)
{
  void *r = es_resource_alloc(erc);
  es_resource_link(r, ec, permanent);
  return r;
}


/**
 *
 */
void
es_resource_release(es_resource_t *er)
{
  if(atomic_dec(&er->er_refcount))
    return;
  es_context_release(er->er_ctx);
  if(er->er_class->erc_finalizer != NULL)
    er->er_class->erc_finalizer(er);
  free(er);
}


/**
 *
 */
void
es_resource_destroy(es_resource_t *er)
{
  if(er->er_zombie)
    return;
  er->er_zombie = 1;
  er->er_class->erc_destroy(er);
}


/**
 *
 */
void
es_resource_unlink(es_resource_t *er)
{
  LIST_REMOVE(er, er_link);
  es_resource_release(er);
}


/**
 *
 */
void
es_resource_push(duk_context *ctx, es_resource_t *er)
{
  es_resource_retain(er);
  es_push_native_obj(ctx, &es_native_resource, er);
}


/**
 *
 */
void *
es_resource_get(duk_context *ctx, int obj_idx,
                const es_resource_class_t *erc)
{

  es_resource_t *er = es_get_native_obj(ctx, obj_idx, &es_native_resource);
  if(er->er_class != erc)
    duk_error(ctx, DUK_ERR_ERROR, "Invalid resource class %s expected %s",
              er->er_class->erc_name, erc->erc_name);
  return er;
}


/**
 *
 */
static int
es_resource_destroy_duk(duk_context *ctx)
{
  es_resource_t *er = es_get_native_obj(ctx, 0, &es_native_resource);
  es_resource_destroy(er);
  return 0;
}


/**
 *
 */
static int
es_compile(duk_context *ctx)
{
  const char *path = duk_require_string(ctx, 0);
  char errbuf[256];
  buf_t *buf = fa_load(path,
                       FA_LOAD_ERRBUF(errbuf, sizeof(errbuf)),
                       NULL);

  if(buf == NULL)
    duk_error(ctx, DUK_ERR_ERROR, "Unable to load %s -- %s", path, errbuf);

  duk_push_lstring(ctx, buf_cstr(buf), buf_len(buf));
  buf_release(buf);

  duk_push_string(ctx, path);

  duk_compile(ctx, 0);
  return 1;
}


static int
es_sleep(duk_context *ctx)
{
  int t = duk_get_number(ctx, 0) * 1000000.0;
  usleep(t);
  return 0;
}


static int
es_timestamp(duk_context *ctx)
{
  duk_push_number(ctx, (double)arch_get_ts());
  return 1;
}


static int
es_random_bytes(duk_context *ctx)
{
  int len = duk_get_int(ctx, 0);
  if(len > 65536)
    duk_error(ctx, DUK_ERR_ERROR, "Too many bytes requested");

  void *ptr = duk_push_fixed_buffer(ctx, len);
  arch_get_random_bytes(ptr, len);
  return 1;
}



static const duk_function_list_entry fnlist_core[] = {
  { "compile",                 es_compile,              1 },
  { "resourceDestroy",         es_resource_destroy_duk, 1 },
  { "sleep",                   es_sleep,                1 },
  { "timestamp",               es_timestamp,            0 },
  { "randomBytes",             es_random_bytes,         1 },
  { NULL, NULL, 0}
};



/**
 *
 */
static int
tryload(duk_context *ctx, const char *path, const char *id, es_context_t *ec)
{
  char errbuf[256];
  buf_t *buf = fa_load(path,
                       FA_LOAD_ERRBUF(errbuf, sizeof(errbuf)),
                       NULL);

  if(buf != NULL) {
    es_debug(ec, "Module %s loaded from %s", id, path);
    duk_push_lstring(ctx, buf_cstr(buf), buf_len(buf));
    buf_release(buf);
    return 1;
  }
  return 0;
}

/**
 *
 */
static int
es_modsearch(duk_context *ctx)
{
  char path[512];

  es_context_t *ec = es_get(ctx);
  const char *id = duk_require_string(ctx, 0);

  es_debug(ec, "Searching for module %s", id);

  const char *nativemod = mystrbegins(id, "native/");
  if(nativemod != NULL) {
    ecmascript_module_t *m;
    LIST_FOREACH(m, &modules, link) {
      if(!strcmp(m->name, nativemod))
        break;
    }

    if(m == NULL)
      duk_error(ctx, DUK_ERR_ERROR, "Can't find native module %s", id);

    for(int i = 0; m->functions[i].key != NULL; i++) {
      duk_push_c_lightfunc(ctx, m->functions[i].value,
                           m->functions[i].nargs, 0, 0);
      duk_put_prop_string(ctx, 2, m->functions[i].key);
    }
    return 0;
  }


  if(ec->ec_path != NULL) {
    fa_pathjoin(path, sizeof(path)-4, ec->ec_path, id);
    strcat(path, ".js");
    if(tryload(ctx, path, id, ec))
      return 1;
  }

  snprintf(path, sizeof(path),
           "dataroot://res/ecmascript/modules/%s.js", id);
  if(tryload(ctx, path, id, ec))
    return 1;

  duk_error(ctx, DUK_ERR_ERROR, "Can't find module %s", id);
}


/**
 *
 */
static void
es_create_env(es_context_t *ec, const char *loaddir, const char *storage)
{
  duk_context *ctx = ec->ec_duk;

  duk_push_global_stash(ctx);

  duk_push_pointer(ctx, ec);
  duk_put_prop_string(ctx, -2, "esctx");

  duk_push_object(ctx);
  duk_put_prop_string(ctx, -2, "roots");

  duk_pop(ctx); // global_stash

  duk_push_global_object(ctx);

  int obj_idx = duk_push_object(ctx);

  duk_push_int(ctx, app_get_version_int());
  duk_put_prop_string(ctx, obj_idx, "currentVersionInt");

  duk_push_string(ctx, htsversion);
  duk_put_prop_string(ctx, obj_idx, "currentVersionString");

  duk_push_string(ctx, gconf.device_id);
  duk_put_prop_string(ctx, obj_idx, "deviceId");

  if(loaddir != NULL) {
    duk_push_string(ctx, loaddir);
    duk_put_prop_string(ctx, obj_idx, "loadPath");
  }

  if(storage != NULL) {
    duk_push_string(ctx, storage);
    duk_put_prop_string(ctx, obj_idx, "storagePath");
  }

  duk_put_function_list(ctx, obj_idx, fnlist_core);
  duk_put_prop_string(ctx, -2, "Core");

  // Initialize modSearch helper

  duk_get_prop_string(ctx, -1, "Duktape");
  duk_push_c_function(ctx, es_modsearch, 4);
  duk_put_prop_string(ctx, -2, "modSearch");
  duk_pop(ctx);

  duk_put_function_list(ctx, -1, es_fnlist_timer);

  duk_push_object(ctx);
  duk_put_function_list(ctx, -1, es_fnlist_console);
  duk_put_prop_string(ctx, -2, "console");

  // Pop global object

  duk_pop(ctx);
}


/**
 *
 */
static void *
es_mem_alloc(void *udata, duk_size_t size)
{
  es_context_t *ec = udata;
  void *p = malloc(size);

  if(p != NULL) {
    ec->ec_mem_active += arch_malloc_size(p);
    ec->ec_mem_peak = MAX(ec->ec_mem_peak, ec->ec_mem_active);
  }
  return p;
}


/**
 *
 */
static void *
es_mem_realloc(void *udata, void *ptr, duk_size_t size)
{
  es_context_t *ec = udata;
  size_t prev = 0;

  if(udata != NULL)
    prev = arch_malloc_size(ptr);

  ptr = realloc(ptr, size);
  if(ptr != NULL) {
    ec->ec_mem_active -= prev;
    ec->ec_mem_active += arch_malloc_size(ptr);
    ec->ec_mem_peak = MAX(ec->ec_mem_peak, ec->ec_mem_active);
  }
  return ptr;
}


/**
 *
 */
static void
es_mem_free(void *udata, void *ptr)
{
  es_context_t *ec = udata;
  if(ptr == NULL)
    return;
  ec->ec_mem_active -= arch_malloc_size(ptr);
  free(ptr);
}


/**
 *
 */
static es_context_t *
es_context_create(const char *id, int flags, const char *url,
                  const char *storage)
{
  es_context_t *ec = calloc(1, sizeof(es_context_t));

  char normalize[PATH_MAX];

  if(!fa_normalize(url, normalize, sizeof(normalize)))
    url = normalize;

  char path[PATH_MAX];

  fa_stat_t st;

  if(!fa_parent(path, sizeof(path), url) &&
     !fa_stat(path, &st, NULL, 0)) {
    ec->ec_path = strdup(path);
  }


  if(ec->ec_path == NULL)
    TRACE(TRACE_ERROR, id,
          "Unable to get parent directory for %s -- No loadPath set",
          url);

  if(storage != NULL)
    ec->ec_storage = strdup(storage);

  ec->ec_flags = flags;
  ec->ec_debug  = !!(flags & ECMASCRIPT_DEBUG) ||
    gconf.enable_ecmascript_debug;
  ec->ec_bypass_file_acl_read  = !!(flags & ECMASCRIPT_FILE_BYPASS_ACL_READ);
  ec->ec_bypass_file_acl_write = !!(flags & ECMASCRIPT_FILE_BYPASS_ACL_WRITE);

  hts_mutex_init_recursive(&ec->ec_mutex);
  atomic_set(&ec->ec_refcount, 1);

  ec->ec_prop_unload_destroy = prop_vec_create(16);

  ec->ec_prop_dispatch_group = prop_dispatch_group_create();

  ec->ec_duk = duk_create_heap(es_mem_alloc, es_mem_realloc, es_mem_free,
                               ec, NULL);

  es_create_env(ec, ec->ec_path, ec->ec_storage);

  ec->ec_id = rstr_alloc(id);

  hts_mutex_lock(&es_context_mutex);
  es_num_contexts++;
  ec->ec_linked = 1;
  LIST_INSERT_HEAD(&es_contexts, ec, ec_link);
  hts_mutex_unlock(&es_context_mutex);

  return ec;
}


/**
 * Be careful here as it will sometimes will be called with prop_mutex
 * held (see es_prop_lockmgr() in es_prop.c). Thus any call that might
 * access the prop system might deadlock. You have been warned.
 */
void
es_context_release(es_context_t *ec)
{
  if(atomic_dec(&ec->ec_refcount))
    return;

  hts_mutex_destroy(&ec->ec_mutex);
  rstr_release(ec->ec_id);
  free(ec->ec_path);
  free(ec->ec_storage);

  if(ec->ec_linked) {
    hts_mutex_lock(&es_context_mutex);
    es_num_contexts--;
    LIST_REMOVE(ec, ec_link);
    hts_mutex_unlock(&es_context_mutex);
  }

  free(ec->ec_manifest);
  free(ec);
}


/**
 *
 */
void
es_context_begin(es_context_t *ec)
{
  atomic_inc(&ec->ec_refcount);
  hts_mutex_lock(&ec->ec_mutex);
}

/**
 *
 */
void
es_context_end(es_context_t *ec, int do_gc)
{
  if(ec->ec_duk != NULL) {

    duk_set_top(ec->ec_duk, 0);

    if(do_gc)
      duk_gc(ec->ec_duk, 0);

    if(LIST_FIRST(&ec->ec_resources_permanent) == NULL) {
      // No more permanent resources, attached. Terminate context

      es_resource_t *er;
      while((er = LIST_FIRST(&ec->ec_resources_volatile)) != NULL) {
        assert(er->er_zombie == 0);
        es_resource_destroy(er);
      }

      duk_destroy_heap(ec->ec_duk);
      ec->ec_duk = NULL;

      prop_vec_destroy_entries(ec->ec_prop_unload_destroy);
      prop_vec_release(ec->ec_prop_unload_destroy);

      prop_dispatch_group_destroy(ec->ec_prop_dispatch_group);

      TRACE(TRACE_DEBUG, rstr_get(ec->ec_id), "Unloaded");
    }
  }
  hts_mutex_unlock(&ec->ec_mutex);
  es_context_release(ec);
}


/**
 *
 */
void
es_dump_err(duk_context *ctx)
{
  es_context_t *ec = es_get(ctx);

  if(duk_is_string(ctx, -1)) {
    // Not a real exception
    TRACE(TRACE_ERROR, rstr_get(ec->ec_id), "%s",
          duk_to_string(ctx, -1));
    return;
  }

  duk_get_prop_string(ctx, -1, "name");
  const char *name = duk_get_string(ctx, -1);

  duk_get_prop_string(ctx, -2, "message");
  const char *message = duk_to_string(ctx, -1);

  duk_get_prop_string(ctx, -3, "fileName");
  const char *filename = duk_get_string(ctx, -1);

  duk_get_prop_string(ctx, -4, "lineNumber");
  int line_no = duk_get_int(ctx, -1);

  duk_get_prop_string(ctx, -5, "stack");
  const char *stack = duk_get_string(ctx, -1);

  TRACE(TRACE_ERROR, rstr_get(ec->ec_id), "%s (%s) at %s:%d",
        name, message, filename, line_no);

  TRACE(TRACE_ERROR, rstr_get(ec->ec_id), "STACK DUMP: %s", stack);
  duk_pop_n(ctx, 5);
}


/**
 *
 */
int
es_get_err_code(duk_context *ctx)
{
  duk_get_prop_string(ctx, -1, "message");
  int r = duk_to_int(ctx, -1);
  duk_pop(ctx);
  return r;
}


/**
 *
 */
static int
es_load_and_compile(es_context_t *ec, const char *path)
{
  char errbuf[256];
  buf_t *buf = fa_load(path,
                       FA_LOAD_ERRBUF(errbuf, sizeof(errbuf)),
                       NULL);

  if(buf == NULL) {
    TRACE(TRACE_ERROR, rstr_get(ec->ec_id), "Unable to load %s", path);
    return -1;
  }

  duk_context *ctx = ec->ec_duk;

  duk_push_lstring(ctx, buf_cstr(buf), buf_len(buf));
  buf_release(buf);
  duk_push_string(ctx, path);

  if(duk_pcompile(ctx, 0)) {

    TRACE(TRACE_ERROR, rstr_get(ec->ec_id), "Unable to compile %s -- %s",
          path, duk_safe_to_string(ctx, -1));
    duk_pop(ctx);
    return -1;
  }
  return 0;
}


/**
 *
 */
static int
es_exec(es_context_t *ec, const char *path)
{
  duk_context *ctx = ec->ec_duk;
  int rc;

  if(es_load_and_compile(ec, path))
    return -1;

  rc = duk_pcall(ctx, 0);
  if(rc != 0)
    es_dump_err(ctx);

  duk_pop(ctx);
  return 0;
}


/**
 *
 */
int
ecmascript_plugin_load(const char *id, const char *url,
                       char *errbuf, size_t errlen,
                       int version, const char *manifest,
                       int flags)
{
  char storage[PATH_MAX];

  snprintf(storage, sizeof(storage),
           "%s/plugins/%s", gconf.persistent_path, id);

  es_context_t *ec = es_context_create(id, flags | ECMASCRIPT_PLUGIN,
                                       url, storage);

  es_context_begin(ec);

  duk_context *ctx = ec->ec_duk;

  duk_push_global_object(ctx);

  int plugin_obj_idx = duk_push_object(ctx);

  duk_push_string(ctx, id);
  duk_put_prop_string(ctx, plugin_obj_idx, "id");

  duk_push_string(ctx, url);
  duk_put_prop_string(ctx, plugin_obj_idx, "url");

  duk_push_string(ctx, manifest);
  duk_put_prop_string(ctx, plugin_obj_idx, "manifest");

  duk_push_int(ctx, version);
  duk_put_prop_string(ctx, plugin_obj_idx, "apiversion");

  if(ec->ec_path) {
    duk_push_string(ctx, ec->ec_path);
    duk_put_prop_string(ctx, plugin_obj_idx, "path");
  }

  duk_put_prop_string(ctx, -2, "Plugin");
  duk_pop(ctx);

  if(version == 1) {

    int64_t ts0 = arch_get_ts();

    if(es_load_and_compile(ec, "dataroot://res/ecmascript/legacy/api-v1.js"))
      goto bad;

    int64_t ts1 = arch_get_ts();

    if(duk_pcall(ctx, 0)) {
      es_dump_err(ctx);
      goto bad;
    }

    int64_t ts2 = arch_get_ts();

    if(es_load_and_compile(ec, url)) {
      duk_pop(ctx);
      goto bad;
    }

    int64_t ts3 = arch_get_ts();

    duk_swap_top(ctx, 0);
    if(duk_pcall_method(ctx, 0))
      es_dump_err(ctx);

    int64_t ts4 = arch_get_ts();

    es_debug(ec, "API v1 emulation: Compile:%dms Exec:%dms",
             ((int)(ts1 - ts0)) / 1000,
             ((int)(ts2 - ts1)) / 1000);

    es_debug(ec, "Plugin main:      Compile:%dms Exec:%dms",
             ((int)(ts3 - ts2)) / 1000,
             ((int)(ts4 - ts3)) / 1000);

  } else {
    es_exec(ec, url);
  }

 bad:
  es_context_end(ec, 1);

  es_context_release(ec);

  return 0;
}


/**
 *
 */
es_context_t **
ecmascript_get_all_contexts(void)
{
  es_context_t **v = malloc(sizeof(es_context_t *) * (es_num_contexts + 1));
  es_context_t *ec;
  int i = 0;
  hts_mutex_lock(&es_context_mutex);
  LIST_FOREACH(ec, &es_contexts, ec_link)
    v[i++] = es_context_retain(ec);
  hts_mutex_unlock(&es_context_mutex);
  v[i] = NULL;
  return v;
}


/**
 *
 */
void
ecmascript_release_context_vector(es_context_t **v)
{
  es_context_t **x = v;
  while(*x) {
    es_context_release(*x);
    x++;
  }
  free(v);
}


/**
 *
 */
void
ecmascript_plugin_unload(const char *id)
{
  es_context_t *ec;
  es_resource_t *er;

  hts_mutex_lock(&es_context_mutex);
  LIST_FOREACH(ec, &es_contexts, ec_link) {
    if(!strcmp(id, rstr_get(ec->ec_id))) {
      assert(ec->ec_linked);
      es_num_contexts--;
      LIST_REMOVE(ec, ec_link);
      ec->ec_linked = 0;
      break;
    }
  }
  hts_mutex_unlock(&es_context_mutex);

  if(ec == NULL)
    return;

  es_context_begin(ec);

  while((er = LIST_FIRST(&ec->ec_resources_permanent)) != NULL)
    es_resource_destroy(er);

  es_context_end(ec, 1);
}


/**
 *
 */
static void
ecmascript_init(void)
{
  if(gconf.load_ecmascript == NULL)
    return;

  int flags =
    ECMASCRIPT_DEBUG |
    ECMASCRIPT_FILE_BYPASS_ACL_READ |
    ECMASCRIPT_FILE_BYPASS_ACL_WRITE;

  es_context_t *ec = es_context_create("cmdline", flags,
                                       gconf.load_ecmascript, "/tmp");
  es_context_begin(ec);

  es_exec(ec, gconf.load_ecmascript);

  es_context_end(ec, 1);
  es_context_release(ec);
}


/**
 *
 */
static void
ecmascript_fini(void)
{
  es_context_t *ec;
  es_resource_t *er;

  hts_mutex_lock(&es_context_mutex);
  while((ec = LIST_FIRST(&es_contexts)) != NULL) {

    assert(ec->ec_linked);
    es_num_contexts--;
    LIST_REMOVE(ec, ec_link);
    ec->ec_linked = 0;
    hts_mutex_unlock(&es_context_mutex);

    es_context_begin(ec);

    while((er = LIST_FIRST(&ec->ec_resources_permanent)) != NULL)
      es_resource_destroy(er);

    es_context_end(ec, 1);

    hts_mutex_lock(&es_context_mutex);
  }
  hts_mutex_unlock(&es_context_mutex);
}


INITME(INIT_GROUP_API, ecmascript_init, ecmascript_fini, 0);


/**
 *
 */
void
ecmascript_load(const char *ctxid, int flags, const char *url,
                const char *storage)
{
  es_context_t *ec = es_context_create(ctxid, flags, url, storage);
  es_context_begin(ec);
  es_exec(ec, url);
  es_context_end(ec, 1);
  es_context_release(ec);
}



/**
 *
 */
static backend_t be_ecmascript = {
  .be_flags  = BACKEND_OPEN_CHECKS_URI,
  .be_open   = ecmascript_openuri,
  .be_search = ecmascript_search,
};

BE_REGISTER(ecmascript);
