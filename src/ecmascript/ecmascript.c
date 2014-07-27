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


#include "showtime.h"
#include "fileaccess/fileaccess.h"

#include "ecmascript.h"


static int es_num_contexts;
static struct es_context_list es_contexts;
static HTS_MUTEX_DECL(es_context_mutex);

/**
 *
 */
int
es_prop_is_true(duk_context *ctx, int obj_idx, const char *id)
{
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
  duk_get_prop_string(ctx, obj_idx, id);
  const char *str = duk_to_string(ctx, -1);
  rstr_t *r = rstr_alloc(str);
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
  duk_push_global_object(ctx);
  duk_get_prop_string(ctx, -1, "esctx");
  es_context_t *es = duk_get_pointer(ctx, -1);
  duk_pop(ctx);
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
  atomic_set(&er->er_refcount, 1);
  er->er_class = erc;
  return er;
}


/**
 *
 */
void
es_resource_init(es_resource_t *er, es_context_t *ec)
{
  er->er_ctx = es_context_retain(ec);
  LIST_INSERT_HEAD(&ec->ec_resources, er, er_link);
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
  free(er);
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
static int
es_resource_destroy_duk(duk_context *ctx)
{
  es_resource_destroy(duk_require_pointer(ctx, 0));
  return 0;
}


/**
 *
 */
static int
es_resource_release_duk(duk_context *ctx)
{
  es_resource_t *er = duk_require_pointer(ctx, 0);
  es_resource_release(er);
  return 0;
}



/**
 *
 */
static int
es_load_script(duk_context *ctx)
{
  const char *path = duk_get_string(ctx, 0);
  char errbuf[256];
  buf_t *buf = fa_load(path,
                       FA_LOAD_ERRBUF(errbuf, sizeof(errbuf)),
                       NULL);

  if(buf == NULL)
    duk_error(ctx, DUK_ERR_ERROR, "Unable to load %s -- %s", path, errbuf);

  duk_push_thread_new_globalenv(ctx);
  duk_context *newctx = duk_require_context(ctx, -1);

  duk_push_lstring(newctx, buf_cstr(buf), buf_len(buf));
  buf_release(buf);

  duk_push_string(newctx, path);

  duk_compile(newctx, 0);

  int ret_obj = duk_push_object(ctx);
  duk_xmove(ctx, newctx, 1);
  duk_put_prop_string(ctx, ret_obj, "entry");

  duk_push_global_object(newctx);

  duk_xmove(ctx, newctx, 1);
  duk_put_prop_string(ctx, ret_obj, "globalobject");
  return 1;
}


static const duk_function_list_entry fnlist_Showtime[] = {
  { "load",                    es_load_script,          1 },

  { "resourceDestroy",         es_resource_destroy_duk, 1 },
  { "resourceRelease",         es_resource_release_duk, 1 },


  { NULL, NULL, 0}
};


/**
 *
 */
static void
es_create_env(es_context_t *ec)
{
  duk_context *ctx = ec->ec_duk;

  duk_push_global_object(ctx);

  duk_push_pointer(ctx, ec);
  duk_put_prop_string(ctx, -2, "esctx");

  int obj_idx = duk_push_object(ctx);

  duk_push_int(ctx, showtime_get_version_int());
  duk_put_prop_string(ctx, obj_idx, "currentVersionInt");

  duk_put_function_list(ctx, obj_idx, fnlist_Showtime);
  duk_put_function_list(ctx, obj_idx, fnlist_Showtime_service);
  duk_put_function_list(ctx, obj_idx, fnlist_Showtime_page);
  duk_put_function_list(ctx, obj_idx, fnlist_Showtime_prop);
  duk_put_function_list(ctx, obj_idx, fnlist_Showtime_io);
  duk_put_function_list(ctx, obj_idx, fnlist_Showtime_string);
  duk_put_function_list(ctx, obj_idx, fnlist_Showtime_htsmsg);
  duk_put_function_list(ctx, obj_idx, fnlist_Showtime_metadata);

  duk_put_prop_string(ctx, -2, "Showtime");

  duk_pop(ctx);
}


/**
 *
 */
static es_context_t *
es_context_create(void)
{
  es_context_t *ec = calloc(1, sizeof(es_context_t));
  hts_mutex_init(&ec->ec_mutex);
  atomic_set(&ec->ec_refcount, 1);

  ec->ec_duk = duk_create_heap_default();
  es_create_env(ec);

  return ec;
}


/**
 *
 */
void
es_context_release(es_context_t *ec)
{
  if(atomic_dec(&ec->ec_refcount))
    return;

  hts_mutex_destroy(&ec->ec_mutex);
  TRACE(TRACE_DEBUG, "ECMASCRIPT", "%s fully unloaded", ec->ec_id);
  free(ec->ec_id);
  free(ec);
}


/**
 *
 */
void
es_context_begin(es_context_t *ec)
{
  hts_mutex_lock(&ec->ec_mutex);
}


/**
 *
 */
static void
es_context_terminate(es_context_t *ec)
{
  duk_destroy_heap(ec->ec_duk);
  ec->ec_duk = NULL;
}


/**
 *
 */
void
es_context_end(es_context_t *ec)
{
  duk_gc(ec->ec_duk, 0);

  if(LIST_FIRST(&ec->ec_resources) == NULL)
    es_context_terminate(ec);

  hts_mutex_unlock(&ec->ec_mutex);
}


/**
 *
 */
void
es_dump_err(duk_context *ctx)
{
  duk_get_prop_string(ctx, -1, "name");
  const char *name = duk_get_string(ctx, -1);

  duk_get_prop_string(ctx, -2, "message");
  const char *message = duk_get_string(ctx, -1);

  duk_get_prop_string(ctx, -3, "fileName");
  const char *filename = duk_get_string(ctx, -1);

  duk_get_prop_string(ctx, -4, "lineNumber");
  int line_no = duk_get_int(ctx, -1);

  duk_get_prop_string(ctx, -5, "stack");
  const char *stack = duk_get_string(ctx, -1);

  TRACE(TRACE_ERROR, "ECMASCRIPT", "%s (%s) at %s:%d",
        name, message, filename, line_no);

  TRACE(TRACE_ERROR, "ECMASCRIPT", "STACK DUMP: %s", stack);
  duk_pop_n(ctx, 5);
}


/**
 *
 */
static int
es_exec(es_context_t *ec, const char *path)
{
  char errbuf[256];
  buf_t *buf = fa_load(path,
                       FA_LOAD_ERRBUF(errbuf, sizeof(errbuf)),
                       NULL);

  if(buf == NULL) {
    TRACE(TRACE_ERROR, "ECMASCRIPT", "Unable to load %s", path);
    return -1;
  }

  duk_context *ctx = ec->ec_duk;

  duk_push_lstring(ctx, buf_cstr(buf), buf_len(buf));
  duk_push_string(ctx, path);

  if(duk_pcompile(ctx, 0)) {

    TRACE(TRACE_ERROR, "ECMASCRIPT", "Unable to compile %s -- %s",
          path, duk_safe_to_string(ctx, -1));

  } else {

    int rc;

    rc = duk_pcall(ctx, 0);
    if(rc != 0)
      es_dump_err(ctx);
  }

  duk_pop(ctx);
  return 0;
}

#if 0
/**
 *
 */
static void
es_get_env(duk_context *ctx)
{
  duk_push_global_object(ctx);
  duk_get_prop_string(ctx, -1, "Showtime");
  duk_replace(ctx, -2);
}
#endif

/**
 *
 */
int
ecmascript_plugin_load(const char *id, const char *url,
                       char *errbuf, size_t errlen)
{
  es_context_t *ec = es_context_create();

  es_context_begin(ec);

  duk_context *ctx = ec->ec_duk;

  duk_push_global_object(ctx);

  int plugin_obj_idx = duk_push_object(ctx);

  duk_push_string(ctx, id);
  duk_put_prop_string(ctx, plugin_obj_idx, "id");

  duk_push_string(ctx, url);
  duk_put_prop_string(ctx, plugin_obj_idx, "url");

  char parent[PATH_MAX];
  if(!fa_parent(parent, sizeof(parent), url)) {
    duk_push_string(ctx, parent);
    duk_put_prop_string(ctx, plugin_obj_idx, "path");
  }

  duk_put_prop_string(ctx, -2, "Plugin");

  duk_pop(ctx);

  ec->ec_id = strdup(id);
  atomic_inc(&ec->ec_refcount);

  hts_mutex_lock(&es_context_mutex);
  es_num_contexts++;
  LIST_INSERT_HEAD(&es_contexts, ec, ec_link);
  hts_mutex_unlock(&es_context_mutex);

  es_exec(ec, "dataroot://resources/ecmascript/api-v1.js");

  es_context_end(ec);

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
    if(!strcmp(id, ec->ec_id)) {
      es_num_contexts--;
      LIST_REMOVE(ec, ec_link);
      break;
    }
  }
  hts_mutex_unlock(&es_context_mutex);

  if(ec == NULL)
    return;

  es_context_begin(ec);

  while((er = LIST_FIRST(&ec->ec_resources)) != NULL)
    es_resource_destroy(er);

  es_context_end(ec);
  es_context_release(ec);
}
